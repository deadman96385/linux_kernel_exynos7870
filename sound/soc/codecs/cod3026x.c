// SPDX-License-Identifier: GPL-2.0-only
/* Samsung COD3026X audio codec */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/iio/consumer.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeirq.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <sound/pcm_params.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "cod3026x.h"

#define COD3026X_AUTOSUSPEND_MS	500
#define COD3026X_RATE_48KHZ	48000
#define COD3026X_RATE_192KHZ	192000
#define COD3026X_BUTTON_COUNT	4
#define COD3026X_ADC_SAMPLES	5

struct cod3026x_button_zone {
	u32 code;
	u32 low;
	u32 high;
};

struct cod3026x_priv {
	struct device *dev;
	struct regmap *regmap;
	struct regulator *vdd;
	struct device_link *mixer_link;
	struct iio_channel *jack_adc;
	struct snd_soc_component *component;
	struct snd_soc_jack *jack;
	struct delayed_work adc_unmute_work;
	struct delayed_work jack_work;
	struct delayed_work button_work;
	struct mutex lock; /* Protects jack state and ADC mute transitions. */
	struct cod3026x_button_zone buttons[COD3026X_BUTTON_COUNT];
	u8 otp[COD3026X_OTP_COUNT];
	u8 vol_hpl;
	u8 vol_hpr;
	u32 mic_threshold;
	u32 mic_delay_ms;
	u32 button_release;
	u32 button_delay_ms;
	u32 micbias1;
	u32 micbias2;
	u32 micbias_ldo;
	unsigned int num_buttons;
	unsigned int aifrate;
	unsigned int button_mask;
	bool jack_present;
	bool mic_present;
	bool initialized;
};

static bool cod3026x_readable_reg(struct device *dev, unsigned int reg)
{
	return (reg >= 0x01 && reg <= 0x0d) ||
	       (reg >= 0x10 && reg <= 0x1c) ||
	       (reg >= 0x20 && reg <= 0x26) ||
	       (reg >= 0x30 && reg <= 0x38) ||
	       (reg >= 0x40 && reg <= 0x44) ||
	       (reg >= 0x50 && reg <= 0x62) ||
	       (reg >= 0x70 && reg <= 0x7a) ||
	       (reg >= 0x80 && reg <= 0x8b) ||
	       (reg >= COD3026X_OTP_BASE && reg <= COD3026X_MAX_REGISTER);
}

static bool cod3026x_writeable_reg(struct device *dev, unsigned int reg)
{
	if (reg >= COD3026X_IRQ1PEND && reg <= COD3026X_IRQ5PEND)
		return false;
	if (reg >= COD3026X_STATUS1 && reg <= COD3026X_STATUS3)
		return false;
	if (reg == 0x61 || reg == 0x62)
		return false;

	return cod3026x_readable_reg(dev, reg);
}

static bool cod3026x_volatile_reg(struct device *dev, unsigned int reg)
{
	return (reg >= COD3026X_IRQ1PEND && reg <= COD3026X_IRQ5PEND) ||
	       (reg >= COD3026X_STATUS1 && reg <= COD3026X_STATUS3) ||
	       reg == 0x62 ||
	       (reg >= COD3026X_OTP_BASE && reg <= COD3026X_MAX_REGISTER);
}

static const struct regmap_config cod3026x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = COD3026X_MAX_REGISTER,
	.readable_reg = cod3026x_readable_reg,
	.writeable_reg = cod3026x_writeable_reg,
	.volatile_reg = cod3026x_volatile_reg,
	.use_single_read = true,
	.use_single_write = true,
	.cache_type = REGCACHE_MAPLE,
};

static unsigned int cod3026x_bias_code(unsigned int uv)
{
	switch (uv) {
	case 2800000:
		return 1;
	case 2600000:
		return 2;
	case 3000000:
	default:
		return 3;
	}
}

static unsigned int cod3026x_ldo_code(unsigned int uv)
{
	switch (uv) {
	case 2500000:
		return 0;
	case 2800000:
		return 1;
	case 3000000:
		return 2;
	case 3300000:
	default:
		return 3;
	}
}

static int cod3026x_save_otp(struct cod3026x_priv *cod3026x)
{
	unsigned int val;
	int i, ret;

	for (i = 0; i < COD3026X_OTP_COUNT; i++) {
		ret = regmap_read(cod3026x->regmap, COD3026X_OTP_BASE + i, &val);
		if (ret)
			return ret;
		cod3026x->otp[i] = val;
	}

	return 0;
}

static int cod3026x_restore_otp(struct cod3026x_priv *cod3026x)
{
	int i, ret;

	for (i = 0; i < COD3026X_OTP_COUNT; i++) {
		ret = regmap_write(cod3026x->regmap, COD3026X_OTP_BASE + i,
				   cod3026x->otp[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int cod3026x_configure_bias(struct cod3026x_priv *cod3026x)
{
	int ret;

	ret = regmap_update_bits(cod3026x->regmap, COD3026X_CTRL_REF,
				 COD3026X_MICBIAS1_MASK,
				 cod3026x_bias_code(cod3026x->micbias1) <<
				 COD3026X_MICBIAS1_SHIFT);
	if (ret)
		return ret;

	return regmap_update_bits(cod3026x->regmap, COD3026X_MIC_BIAS,
				  COD3026X_MICBIAS2_MASK |
				  COD3026X_MICBIAS_LDO_MASK,
				  cod3026x_bias_code(cod3026x->micbias2) <<
				  COD3026X_MICBIAS2_SHIFT |
				  cod3026x_ldo_code(cod3026x->micbias_ldo) <<
				  COD3026X_MICBIAS_LDO_SHIFT);
}

static int cod3026x_hw_init(struct cod3026x_priv *cod3026x)
{
	int ret;

	regmap_write(cod3026x->regmap, COD3026X_IRQ1M, 0x80);
	regmap_write(cod3026x->regmap, COD3026X_IRQ2M, 0xc0);
	regmap_write(cod3026x->regmap, COD3026X_IRQ3M, 0xc0);
	regmap_write(cod3026x->regmap, COD3026X_DET_ON, 0x42);
	usleep_range(1000, 1100);
	regmap_write(cod3026x->regmap, COD3026X_DET_ON, 0x43);
	regmap_write(cod3026x->regmap, COD3026X_MIC_DET, 0xff);
	regmap_write(cod3026x->regmap, COD3026X_DET_TIME, 0xff);
	regmap_write(cod3026x->regmap, COD3026X_LDO_DIG, 0x0f);
	regmap_write(cod3026x->regmap, COD3026X_KEY_TIME, 0xf2);
	regmap_write(cod3026x->regmap, COD3026X_MICBIAS2, 0x0b);
	regmap_write(cod3026x->regmap, COD3026X_JACK_DET2, 0x0d);
	regmap_write(cod3026x->regmap, COD3026X_ADC1,
		     COD3026X_ADC_HPF_100HZ);
	regmap_write(cod3026x->regmap, COD3026X_DNC7, 0x1a);
	regmap_write(cod3026x->regmap, COD3026X_CTRL_CP, 0x02);
	regmap_write(cod3026x->regmap, COD3026X_CTRL_SPKS1, 0x82);
	regmap_write(cod3026x->regmap, COD3026X_AD_MIC3N_TRIM1, 0x11);
	regmap_write(cod3026x->regmap, COD3026X_MIC_ON, 0x01);
	regmap_update_bits(cod3026x->regmap, COD3026X_CHOP_DA,
			   COD3026X_CHOP_HP | COD3026X_CHOP_EP |
			   COD3026X_CHOP_SPK_PGA, 0);
	regmap_update_bits(cod3026x->regmap, COD3026X_CLK1_AD,
			   GENMASK(5, 4), 0);
	regmap_update_bits(cod3026x->regmap, COD3026X_FORMAT,
			   COD3026X_WORD_LEN_MASK | GENMASK(5, 4) |
			   COD3026X_FMT_I2S | COD3026X_FMT_LRJ,
			   COD3026X_WORD_LEN_24 << COD3026X_WORD_LEN_SHIFT |
			   COD3026X_BCLK_64FS | COD3026X_FMT_I2S);

	ret = cod3026x_configure_bias(cod3026x);
	if (ret)
		return ret;

	return 0;
}

static int cod3026x_runtime_resume(struct device *dev)
{
	struct cod3026x_priv *cod3026x = dev_get_drvdata(dev);
	unsigned int val;
	int reg, ret;

	ret = regulator_enable(cod3026x->vdd);
	if (ret)
		return ret;

	pinctrl_pm_select_default_state(dev);
	regcache_cache_only(cod3026x->regmap, false);
	usleep_range(15000, 16000);

	if (!cod3026x->initialized) {
		for (reg = 0; reg <= COD3026X_MAX_REGISTER; reg++) {
			if (cod3026x_readable_reg(dev, reg) &&
			    !cod3026x_volatile_reg(dev, reg))
				regmap_read(cod3026x->regmap, reg, &val);
		}

		ret = cod3026x_save_otp(cod3026x);
		if (!ret)
			ret = cod3026x_hw_init(cod3026x);
		if (!ret)
			cod3026x->initialized = true;
	} else {
		ret = regcache_sync(cod3026x->regmap);
		if (!ret)
			ret = cod3026x_restore_otp(cod3026x);
	}

	if (!ret)
		return 0;

	regcache_cache_only(cod3026x->regmap, true);
	regulator_disable(cod3026x->vdd);
	return ret;
}

static int cod3026x_runtime_suspend(struct device *dev)
{
	struct cod3026x_priv *cod3026x = dev_get_drvdata(dev);

	regcache_cache_only(cod3026x->regmap, true);
	regcache_mark_dirty(cod3026x->regmap);
	pinctrl_pm_select_idle_state(dev);
	regulator_disable(cod3026x->vdd);

	return 0;
}

static int cod3026x_dai_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct cod3026x_priv *cod3026x =
		snd_soc_component_get_drvdata(dai->component);

	return pm_runtime_resume_and_get(cod3026x->dev);
}

static void cod3026x_dai_shutdown(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct cod3026x_priv *cod3026x =
		snd_soc_component_get_drvdata(dai->component);

	pm_runtime_mark_last_busy(cod3026x->dev);
	pm_runtime_put_autosuspend(cod3026x->dev);
}

static int cod3026x_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct cod3026x_priv *cod3026x =
		snd_soc_component_get_drvdata(dai->component);
	unsigned int format, polarity = 0;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_LEFT_J:
		format = 0;
		break;
	case SND_SOC_DAIFMT_I2S:
		format = COD3026X_FMT_I2S;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		polarity = COD3026X_BCLK_POL | COD3026X_LRCLK_POL;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		polarity = COD3026X_BCLK_POL;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		polarity = COD3026X_LRCLK_POL;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(cod3026x->regmap, COD3026X_FORMAT,
				  COD3026X_FMT_I2S | COD3026X_FMT_LRJ |
				  COD3026X_BCLK_POL | COD3026X_LRCLK_POL,
				  format | polarity);
}

static int cod3026x_dai_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct cod3026x_priv *cod3026x =
		snd_soc_component_get_drvdata(dai->component);
	unsigned int rate = params_rate(params), dnc;
	int ret;

	if (cod3026x->aifrate != rate) {
		ret = regmap_read(cod3026x->regmap, COD3026X_DNC1, &dnc);
		if (ret)
			return ret;
		regmap_write(cod3026x->regmap, COD3026X_DNC1, 0);

		if (rate == COD3026X_RATE_192KHZ) {
			regmap_update_bits(cod3026x->regmap, COD3026X_MQS,
					   COD3026X_MQS_MODE |
					   COD3026X_MQS_GAIN_MASK,
					   COD3026X_MQS_MODE |
					   3 << COD3026X_MQS_GAIN_SHIFT);
		} else if (cod3026x->aifrate == COD3026X_RATE_192KHZ) {
			regmap_update_bits(cod3026x->regmap, COD3026X_MQS,
					   COD3026X_MQS_MODE |
					   COD3026X_MQS_GAIN_MASK, 0);
			regmap_update_bits(cod3026x->regmap,
					   COD3026X_DIGITAL_POWER,
					   COD3026X_DIG_PDB_DAC |
					   COD3026X_DIG_RST_DAC |
					   COD3026X_DIG_RESERVED, 0);
			regmap_update_bits(cod3026x->regmap,
					   COD3026X_DIGITAL_POWER,
					   COD3026X_DIG_PDB_DAC |
					   COD3026X_DIG_RST_DAC |
					   COD3026X_DIG_RESERVED,
					   COD3026X_DIG_PDB_DAC |
					   COD3026X_DIG_RST_DAC |
					   COD3026X_DIG_RESERVED);
		}

		regmap_write(cod3026x->regmap, COD3026X_DNC1, dnc);
		cod3026x->aifrate = rate;
	}

	return regmap_update_bits(cod3026x->regmap, COD3026X_FORMAT,
				  COD3026X_WORD_LEN_MASK | GENMASK(5, 4),
				  COD3026X_WORD_LEN_24 <<
				  COD3026X_WORD_LEN_SHIFT |
				  COD3026X_BCLK_64FS);
}

static const struct snd_soc_dai_ops cod3026x_dai_ops = {
	.startup = cod3026x_dai_startup,
	.shutdown = cod3026x_dai_shutdown,
	.set_fmt = cod3026x_dai_set_fmt,
	.hw_params = cod3026x_dai_hw_params,
};

#define COD3026X_RATES	SNDRV_PCM_RATE_8000_192000
#define COD3026X_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | \
				 SNDRV_PCM_FMTBIT_S20_3LE | \
				 SNDRV_PCM_FMTBIT_S24_LE | \
				 SNDRV_PCM_FMTBIT_S32_LE)
#define COD3026X_STREAM(_name, _channels) { \
	.stream_name = _name, \
	.channels_min = 1, \
	.channels_max = _channels, \
	.rates = COD3026X_RATES, \
	.formats = COD3026X_FORMATS, \
}

static struct snd_soc_dai_driver cod3026x_dais[] = {
	{
		.name = "cod3026x-aif",
		.id = 0,
		.playback = COD3026X_STREAM("AIF Playback", 8),
		.capture = COD3026X_STREAM("AIF Capture", 8),
		.ops = &cod3026x_dai_ops,
		.symmetric_rate = 1,
	}, {
		.name = "cod3026x-aif2",
		.id = 1,
		.playback = COD3026X_STREAM("AIF2 Playback", 2),
		.capture = COD3026X_STREAM("AIF2 Capture", 2),
		.ops = &cod3026x_dai_ops,
		.symmetric_rate = 1,
	},
};

static const unsigned int cod3026x_mic_boost_tlv[] = {
	TLV_DB_RANGE_HEAD(2),
	0, 1, TLV_DB_SCALE_ITEM(0, 1200, 0),
	2, 2, TLV_DB_SCALE_ITEM(2000, 0, 0),
};

static const DECLARE_TLV_DB_SCALE(cod3026x_mic_tlv, -1650, 150, 0);
static const DECLARE_TLV_DB_SCALE(cod3026x_hp_tlv, -5700, 100, 0);
static const DECLARE_TLV_DB_SCALE(cod3026x_ep_tlv, 0, 100, 0);
static const DECLARE_TLV_DB_SCALE(cod3026x_spk_tlv, -600, 100, 0);
static const unsigned int cod3026x_dvol_tlv[] = {
	TLV_DB_RANGE_HEAD(4),
	0x00, 0x01, TLV_DB_SCALE_ITEM(-8400, 600, 0),
	0x02, 0x04, TLV_DB_SCALE_ITEM(-7200, 200, 0),
	0x05, 0x0f, TLV_DB_SCALE_ITEM(-6600, 100, 0),
	0x10, 0x96, TLV_DB_SCALE_ITEM(-5500, 50, 0),
};

static const DECLARE_TLV_DB_SCALE(cod3026x_dnc_min_tlv, -600, 100, 0);
static const DECLARE_TLV_DB_SCALE(cod3026x_dnc_max_tlv, 0, 100, 0);
static const DECLARE_TLV_DB_SCALE(cod3026x_dnc_level_tlv, -1050, 150, 0);

static const char * const cod3026x_monomix_text[] = {
	"Disable", "R", "L", "LR-Invert", "(L+R)/2", "L+R"
};

static SOC_ENUM_SINGLE_DECL(cod3026x_monomix, COD3026X_DAC1,
			    COD3026X_DAC_MONOMIX_SHIFT,
			    cod3026x_monomix_text);
static const char * const cod3026x_chargepump_text[] = {
	"VDD", "HALF-VDD", "CLASS-G-D", "CLASS-G-A"
};

static SOC_ENUM_SINGLE_DECL(cod3026x_chargepump, COD3026X_CTRL_EP, 4,
			    cod3026x_chargepump_text);
static const char * const cod3026x_off_on_text[] = { "Off", "On" };
static SOC_ENUM_SINGLE_DECL(cod3026x_dnc_zcd, COD3026X_DNC9, 7,
			    cod3026x_off_on_text);

static const struct snd_kcontrol_new cod3026x_controls[] = {
	SOC_SINGLE_TLV("MIC1 Boost Volume", COD3026X_VOL_AD1, 5, 3, 0,
		       cod3026x_mic_boost_tlv),
	SOC_SINGLE_TLV("MIC1 Volume", COD3026X_VOL_AD1, 0, 31, 1,
		       cod3026x_mic_tlv),
	SOC_SINGLE_TLV("MIC2 Boost Volume", COD3026X_VOL_AD2, 5, 3, 0,
		       cod3026x_mic_boost_tlv),
	SOC_SINGLE_TLV("MIC2 Volume", COD3026X_VOL_AD2, 0, 31, 1,
		       cod3026x_mic_tlv),
	SOC_SINGLE_TLV("MIC3 Boost Volume", COD3026X_VOL_AD3, 5, 3, 0,
		       cod3026x_mic_boost_tlv),
	SOC_SINGLE_TLV("MIC3 Volume", COD3026X_VOL_AD3, 0, 31, 1,
		       cod3026x_mic_tlv),
	SOC_DOUBLE_R_TLV("Headphone Volume", COD3026X_VOL_HPL,
			 COD3026X_VOL_HPR, 0, 63, 1, cod3026x_hp_tlv),
	SOC_SINGLE_TLV("Earphone Volume", COD3026X_VOL_EP_SPK, 4, 12, 0,
		       cod3026x_ep_tlv),
	SOC_SINGLE_TLV("Speaker Volume", COD3026X_VOL_EP_SPK, 0, 9, 0,
		       cod3026x_spk_tlv),
	SOC_SINGLE_TLV("ADC Left Gain", COD3026X_ADC_L_VOL, 0,
		       COD3026X_DVOL_MAX, 1, cod3026x_dvol_tlv),
	SOC_SINGLE_TLV("ADC Right Gain", COD3026X_ADC_R_VOL, 0,
		       COD3026X_DVOL_MAX, 1, cod3026x_dvol_tlv),
	SOC_DOUBLE_R_TLV("DAC Gain", COD3026X_DAC_L_VOL,
			 COD3026X_DAC_R_VOL, 0, COD3026X_DVOL_MAX, 1,
			 cod3026x_dvol_tlv),
	SOC_SINGLE_TLV("DNC Min Gain", COD3026X_DNC2, 5, 6, 0,
		       cod3026x_dnc_min_tlv),
	SOC_SINGLE_TLV("DNC Max Gain", COD3026X_DNC2, 0, 24, 0,
		       cod3026x_dnc_max_tlv),
	SOC_SINGLE_TLV("DNC Level Left", COD3026X_DNC3, 4, 7, 0,
		       cod3026x_dnc_level_tlv),
	SOC_SINGLE_TLV("DNC Level Right", COD3026X_DNC3, 0, 7, 0,
		       cod3026x_dnc_level_tlv),
	SOC_SINGLE("DNC ZCD Timeout", COD3026X_DNC9, 0, 0x7f, 0),
	SOC_ENUM("DNC ZCD Enable", cod3026x_dnc_zcd),
	SOC_ENUM("MonoMix Mode", cod3026x_monomix),
	SOC_ENUM("Chargepump Mode", cod3026x_chargepump),
	SOC_SINGLE("DAC Soft Mute", COD3026X_DAC1, 1, 1, 1),
};

static void cod3026x_adc_unmute_work(struct work_struct *work)
{
	struct cod3026x_priv *cod3026x =
		container_of(to_delayed_work(work), struct cod3026x_priv,
			     adc_unmute_work);

	mutex_lock(&cod3026x->lock);
	regmap_update_bits(cod3026x->regmap, COD3026X_ADC1,
			   COD3026X_ADC_MUTE, 0);
	mutex_unlock(&cod3026x->lock);
}

static int cod3026x_dac_event(struct snd_soc_dapm_widget *widget,
			      struct snd_kcontrol *control, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(widget->dapm);
	struct cod3026x_priv *cod3026x =
		snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_update_bits(cod3026x->regmap, COD3026X_DIGITAL_POWER,
				   COD3026X_DIG_PDB_DAC,
				   COD3026X_DIG_PDB_DAC);
		regmap_update_bits(cod3026x->regmap, COD3026X_DIGITAL_POWER,
				   COD3026X_DIG_RST_DAC, 0);
		regmap_update_bits(cod3026x->regmap, COD3026X_DIGITAL_POWER,
				   COD3026X_DIG_RST_DAC,
				   COD3026X_DIG_RST_DAC);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_update_bits(cod3026x->regmap, COD3026X_DIGITAL_POWER,
				   COD3026X_DIG_PDB_DAC |
				   COD3026X_DIG_RST_DAC, 0);
		break;
	default:
		break;
	}

	return 0;
}

static int cod3026x_vmid_event(struct snd_soc_dapm_widget *widget,
			       struct snd_kcontrol *control, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(widget->dapm);
	struct cod3026x_priv *cod3026x =
		snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_update_bits(cod3026x->regmap, COD3026X_ADC1,
				   COD3026X_ADC_MUTE, COD3026X_ADC_MUTE);
		regmap_update_bits(cod3026x->regmap, COD3026X_DIGITAL_POWER,
				   COD3026X_DIG_PDB_ADC,
				   COD3026X_DIG_PDB_ADC);
		regmap_update_bits(cod3026x->regmap, COD3026X_DIGITAL_POWER,
				   COD3026X_DIG_RST_ADC, 0);
		regmap_update_bits(cod3026x->regmap, COD3026X_DIGITAL_POWER,
				   COD3026X_DIG_RST_ADC,
				   COD3026X_DIG_RST_ADC);
		regmap_update_bits(cod3026x->regmap, COD3026X_PD_REF,
				   COD3026X_PDB_VMID, COD3026X_PDB_VMID);
		regmap_update_bits(cod3026x->regmap, COD3026X_CTRL_REF,
				   COD3026X_VMID_MASK,
				   COD3026X_VMID_5K << COD3026X_VMID_SHIFT);
		regmap_update_bits(cod3026x->regmap, COD3026X_PD_AD1,
				   COD3026X_EN_DSMR_PREQ |
				   COD3026X_EN_DSML_PREQ,
				   COD3026X_EN_DSMR_PREQ |
				   COD3026X_EN_DSML_PREQ);
		msleep(140);
		regmap_update_bits(cod3026x->regmap, COD3026X_CTRL_REF,
				   COD3026X_VMID_MASK,
				   COD3026X_VMID_50K << COD3026X_VMID_SHIFT);
		regmap_update_bits(cod3026x->regmap, COD3026X_PD_REF,
				   COD3026X_PDB_IGEN, COD3026X_PDB_IGEN);
		usleep_range(100, 200);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		cancel_delayed_work_sync(&cod3026x->adc_unmute_work);
		regmap_update_bits(cod3026x->regmap, COD3026X_PD_REF,
				   COD3026X_PDB_IGEN | COD3026X_PDB_VMID, 0);
		regmap_update_bits(cod3026x->regmap, COD3026X_DIGITAL_POWER,
				   COD3026X_DIG_PDB_ADC |
				   COD3026X_DIG_RST_ADC, 0);
		regmap_update_bits(cod3026x->regmap, COD3026X_ADC1,
				   COD3026X_ADC_MUTE, 0);
		break;
	default:
		break;
	}

	return 0;
}

enum cod3026x_micbias {
	COD3026X_MICBIAS_1,
	COD3026X_MICBIAS_2,
};

static int cod3026x_micbias_event(struct snd_soc_dapm_widget *widget,
				  struct snd_kcontrol *control, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(widget->dapm);
	struct cod3026x_priv *cod3026x =
		snd_soc_component_get_drvdata(component);
	unsigned int other_bias, bias, mask, value;
	int ret;

	if (widget->shift == COD3026X_MICBIAS_1) {
		bias = COD3026X_PDB_MCB1;
		other_bias = COD3026X_PDB_MCB2;
	} else {
		bias = COD3026X_PDB_MCB2;
		other_bias = COD3026X_PDB_MCB1;
	}

	ret = regmap_read(cod3026x->regmap, COD3026X_PD_REF, &value);
	if (ret)
		return ret;

	mask = bias;
	if (!(value & other_bias))
		mask |= COD3026X_PDB_MCB_LDO;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		ret = regmap_update_bits(cod3026x->regmap, COD3026X_PD_REF,
					 mask, mask);
		if (!ret && widget->shift == COD3026X_MICBIAS_2)
			ret = regmap_update_bits(cod3026x->regmap,
						 COD3026X_CTRL_REF,
						 COD3026X_MCB2_MANUAL,
						 COD3026X_MCB2_MANUAL);
		return ret;
	case SND_SOC_DAPM_POST_PMD:
		ret = regmap_update_bits(cod3026x->regmap, COD3026X_PD_REF,
					 mask, 0);
		if (!ret && widget->shift == COD3026X_MICBIAS_2)
			ret = regmap_update_bits(cod3026x->regmap,
						 COD3026X_CTRL_REF,
						 COD3026X_MCB2_MANUAL, 0);
		return ret;
	default:
		return 0;
	}
}

static int cod3026x_adc_event(struct snd_soc_dapm_widget *widget,
			      struct snd_kcontrol *control, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(widget->dapm);
	struct cod3026x_priv *cod3026x =
		snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		mod_delayed_work(system_power_efficient_wq,
				 &cod3026x->adc_unmute_work,
				 msecs_to_jiffies(220));
		break;
	case SND_SOC_DAPM_PRE_PMD:
		cancel_delayed_work_sync(&cod3026x->adc_unmute_work);
		regmap_update_bits(cod3026x->regmap, COD3026X_ADC1,
				   COD3026X_ADC_MUTE, COD3026X_ADC_MUTE);
		break;
	default:
		break;
	}

	return 0;
}

static int cod3026x_mic_event(struct snd_soc_dapm_widget *widget,
			      struct snd_kcontrol *control, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(widget->dapm);
	struct cod3026x_priv *cod3026x =
		snd_soc_component_get_drvdata(component);
	unsigned int analog_mask, active = 0, mic_bit;

	switch (widget->shift) {
	case 0:
		analog_mask = COD3026X_PDB_MIC_BST1 |
			      COD3026X_PDB_MIC_PGA1;
		mic_bit = COD3026X_MIC1;
		break;
	case 1:
		analog_mask = COD3026X_PDB_MIC_BST2 |
			      COD3026X_PDB_MIC_PGA2;
		mic_bit = COD3026X_MIC2;
		break;
	case 2:
		analog_mask = COD3026X_PDB_MIC_BST3 |
			      COD3026X_PDB_MIC_PGA3;
		mic_bit = COD3026X_MIC3;
		break;
	default:
		analog_mask = COD3026X_PDB_LNL | COD3026X_PDB_LNR;
		mic_bit = COD3026X_MIC_LINEIN;
		break;
	}

	if (event == SND_SOC_DAPM_PRE_PMU) {
		regmap_update_bits(cod3026x->regmap, COD3026X_PD_REF,
				   COD3026X_PDB_IGEN_AD,
				   COD3026X_PDB_IGEN_AD);
		regmap_update_bits(cod3026x->regmap, COD3026X_PD_AD2,
				   analog_mask, analog_mask);
		regmap_update_bits(cod3026x->regmap, COD3026X_PD_AD1,
				   COD3026X_PDB_MIXL | COD3026X_PDB_MIXR |
				   COD3026X_PDB_DSML | COD3026X_PDB_DSMR,
				   COD3026X_PDB_MIXL | COD3026X_PDB_MIXR |
				   COD3026X_PDB_DSML | COD3026X_PDB_DSMR);
		regmap_update_bits(cod3026x->regmap, COD3026X_PD_AD1,
				   COD3026X_EN_DSML_PREQ |
				   COD3026X_EN_DSMR_PREQ, 0);
		regmap_update_bits(cod3026x->regmap, COD3026X_PD_AD1,
				   COD3026X_RESETB_DSML |
				   COD3026X_RESETB_DSMR,
				   COD3026X_RESETB_DSML |
				   COD3026X_RESETB_DSMR);
	} else if (event == SND_SOC_DAPM_PRE_PMD) {
		regmap_update_bits(cod3026x->regmap, COD3026X_PD_AD2,
				   analog_mask, 0);
		regmap_read(cod3026x->regmap, COD3026X_MIC_ON, &active);
		active &= COD3026X_MIC_LINEIN | COD3026X_MIC1 |
			  COD3026X_MIC2 | COD3026X_MIC3;
		active &= ~mic_bit;
		if (!active) {
			regmap_update_bits(cod3026x->regmap, COD3026X_PD_AD1,
					   COD3026X_RESETB_DSML |
					   COD3026X_RESETB_DSMR |
					   COD3026X_PDB_DSML |
					   COD3026X_PDB_DSMR |
					   COD3026X_PDB_MIXL |
					   COD3026X_PDB_MIXR, 0);
			regmap_update_bits(cod3026x->regmap, COD3026X_PD_REF,
					   COD3026X_PDB_IGEN_AD, 0);
		}
	}

	return 0;
}

static void cod3026x_update_playback_otp(struct cod3026x_priv *cod3026x,
					 unsigned int chop)
{
	bool hp = chop & COD3026X_CHOP_HP;
	bool ep = chop & COD3026X_CHOP_EP;
	bool spk = chop & COD3026X_CHOP_SPK_PGA;

	if ((hp && !ep) || (!hp && ep && !spk)) {
		regmap_write(cod3026x->regmap, COD3026X_OFFSET_DAL,
			     (!hp && ep) ?
			     cod3026x->otp[COD3026X_CTRL_EP_OTP -
					     COD3026X_OTP_BASE] :
			     cod3026x->otp[COD3026X_OFFSET_DAL -
					     COD3026X_OTP_BASE]);
		if (hp)
			regmap_write(cod3026x->regmap, COD3026X_OFFSET_DAR,
				     cod3026x->otp[COD3026X_OFFSET_DAR -
						     COD3026X_OTP_BASE]);
	} else {
		regmap_write(cod3026x->regmap, COD3026X_OFFSET_DAL, 0);
		regmap_write(cod3026x->regmap, COD3026X_OFFSET_DAR, 0);
	}

	if (!(hp && !spk && !ep)) {
		regmap_update_bits(cod3026x->regmap, COD3026X_DNC1,
				   COD3026X_DNC_ENABLE, 0);
		usleep_range(100, 200);
	}
}

enum cod3026x_output {
	COD3026X_OUTPUT_HP,
	COD3026X_OUTPUT_EP,
	COD3026X_OUTPUT_SPK,
};

static int cod3026x_output_event(struct snd_soc_dapm_widget *widget,
				 struct snd_kcontrol *control, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(widget->dapm);
	struct cod3026x_priv *cod3026x =
		snd_soc_component_get_drvdata(component);
	unsigned int chop = 0, gain = 0, mix = 0;
	bool hp, ep, spk;

	regmap_read(cod3026x->regmap, COD3026X_CHOP_DA, &chop);
	hp = chop & COD3026X_CHOP_HP;
	ep = chop & COD3026X_CHOP_EP;
	spk = chop & COD3026X_CHOP_SPK_PGA;

	switch (widget->shift) {
	case COD3026X_OUTPUT_HP:
		if (event == SND_SOC_DAPM_PRE_PMU) {
			regmap_read(cod3026x->regmap, COD3026X_VOL_HPL,
				    &gain);
			cod3026x->vol_hpl = gain;
			regmap_read(cod3026x->regmap, COD3026X_VOL_HPR,
				    &gain);
			cod3026x->vol_hpr = gain;
			regmap_update_bits(cod3026x->regmap, COD3026X_DNC1,
					   COD3026X_DNC_START_GAIN,
					   COD3026X_DNC_START_GAIN);
			regmap_write(cod3026x->regmap, COD3026X_DNC7, 0x1a);
			regmap_update_bits(cod3026x->regmap, COD3026X_DNC1,
					   COD3026X_DNC_ENABLE,
					   COD3026X_DNC_ENABLE);
			usleep_range(100, 200);
			regmap_update_bits(cod3026x->regmap, COD3026X_DNC1,
					   COD3026X_DNC_ENABLE, 0);
			regmap_write(cod3026x->regmap, COD3026X_VOL_HPL, 0x1a);
			regmap_write(cod3026x->regmap, COD3026X_VOL_HPR, 0x1a);
			regmap_update_bits(cod3026x->regmap, COD3026X_SV_HP,
					   COD3026X_SKIP_HP_SV,
					   COD3026X_SKIP_HP_SV);
			cod3026x_update_playback_otp(cod3026x, chop);
			regmap_update_bits(cod3026x->regmap, COD3026X_DNC4,
					   COD3026X_DNC_WINDOW_MASK,
					   COD3026X_DNC_WINDOW_20HZ);
		} else if (event == SND_SOC_DAPM_POST_PMU) {
			regmap_update_bits(cod3026x->regmap, COD3026X_PWAUTO_DA,
					   COD3026X_PW_AUTO_DA |
					   COD3026X_APW_HP,
					   COD3026X_PW_AUTO_DA |
					   COD3026X_APW_HP);
			regmap_update_bits(cod3026x->regmap, COD3026X_MIX_DA1,
					   COD3026X_HP_MIXL_DACL |
					   COD3026X_HP_MIXR_DACR,
					   COD3026X_HP_MIXL_DACL |
					   COD3026X_HP_MIXR_DACR);
			msleep(180);
			regmap_update_bits(cod3026x->regmap, COD3026X_SV_HP,
					   COD3026X_SKIP_HP_SV, 0);
			if (!spk && !ep) {
				regmap_update_bits(cod3026x->regmap,
						   COD3026X_DNC1,
						   COD3026X_DNC_ENABLE,
						   COD3026X_DNC_ENABLE);
			} else {
				regmap_write(cod3026x->regmap, COD3026X_VOL_HPL,
					     cod3026x->vol_hpl);
				regmap_write(cod3026x->regmap, COD3026X_VOL_HPR,
					     cod3026x->vol_hpr);
			}
		} else if (event == SND_SOC_DAPM_PRE_PMD) {
			regmap_update_bits(cod3026x->regmap, COD3026X_DNC1,
					   COD3026X_DNC_ENABLE, 0);
			regmap_update_bits(cod3026x->regmap, COD3026X_SV_HP,
					   COD3026X_SKIP_HP_SV,
					   COD3026X_SKIP_HP_SV);
			regmap_update_bits(cod3026x->regmap, COD3026X_PWAUTO_DA,
					   COD3026X_APW_HP |
					   (spk || ep ? 0 :
					    COD3026X_PW_AUTO_DA), 0);
			regmap_update_bits(cod3026x->regmap, COD3026X_MIX_DA1,
					   COD3026X_HP_MIXL_DACL |
					   COD3026X_HP_MIXR_DACR, 0);
			msleep(40);
			regmap_update_bits(cod3026x->regmap, COD3026X_SV_HP,
					   COD3026X_SKIP_HP_SV, 0);
		}
		break;
	case COD3026X_OUTPUT_EP:
		if (event == SND_SOC_DAPM_PRE_PMU) {
			cod3026x_update_playback_otp(cod3026x, chop);
			regmap_update_bits(cod3026x->regmap, COD3026X_PWAUTO_DA,
					   COD3026X_APW_EP |
					   COD3026X_PW_AUTO_DA,
					   COD3026X_APW_EP |
					   COD3026X_PW_AUTO_DA);
			regmap_update_bits(cod3026x->regmap, COD3026X_MIX_DA2,
					   COD3026X_EP_MIX_DACL,
					   COD3026X_EP_MIX_DACL);
			msleep(136);
		} else if (event == SND_SOC_DAPM_PRE_PMD) {
			regmap_update_bits(cod3026x->regmap, COD3026X_PWAUTO_DA,
					   COD3026X_APW_EP |
					   (spk || hp ? 0 :
					    COD3026X_PW_AUTO_DA), 0);
			usleep_range(100, 200);
			regmap_update_bits(cod3026x->regmap, COD3026X_MIX_DA2,
					   COD3026X_EP_MIX_DACL, 0);
		}
		break;
	case COD3026X_OUTPUT_SPK:
		if (event == SND_SOC_DAPM_PRE_PMU) {
			cod3026x_update_playback_otp(cod3026x, chop);
			regmap_write(cod3026x->regmap, COD3026X_CTRL_SPKS1,
				     0x82);
			regmap_read(cod3026x->regmap, COD3026X_MIX_DA2, &mix);
			mix &= COD3026X_SPK_MIX_DACL |
			       COD3026X_SPK_MIX_DACR |
			       COD3026X_SPK_MIX_ADCL |
			       COD3026X_SPK_MIX_ADCR;
			mix |= COD3026X_SPK_MIX_DACL |
			       COD3026X_SPK_MIX_DACR;
			regmap_read(cod3026x->regmap, COD3026X_VOL_EP_SPK,
				    &gain);
			regmap_update_bits(cod3026x->regmap,
					   COD3026X_VOL_EP_SPK, GENMASK(3, 0), 0);
			regmap_update_bits(cod3026x->regmap, COD3026X_PWAUTO_DA,
					   COD3026X_PW_AUTO_DA |
					   COD3026X_APW_SPK,
					   COD3026X_PW_AUTO_DA |
					   COD3026X_APW_SPK);
			regmap_update_bits(cod3026x->regmap, COD3026X_MIX_DA2,
					   COD3026X_SPK_MIX_DACL |
					   COD3026X_SPK_MIX_DACR |
					   COD3026X_SPK_MIX_ADCL |
					   COD3026X_SPK_MIX_ADCR, mix);
			msleep(135);
			regmap_update_bits(cod3026x->regmap,
					   COD3026X_VOL_EP_SPK, GENMASK(3, 0),
					   gain & GENMASK(3, 0));
		} else if (event == SND_SOC_DAPM_PRE_PMD) {
			regmap_update_bits(cod3026x->regmap,
					   COD3026X_VOL_EP_SPK, GENMASK(3, 0), 6);
			regmap_update_bits(cod3026x->regmap, COD3026X_PWAUTO_DA,
					   COD3026X_APW_SPK |
					   (hp || ep ? 0 :
					    COD3026X_PW_AUTO_DA), 0);
			usleep_range(200, 300);
			regmap_update_bits(cod3026x->regmap, COD3026X_MIX_DA2,
					   COD3026X_SPK_MIX_DACL |
					   COD3026X_SPK_MIX_DACR, 0);
		}
		break;
	}

	return 0;
}

static const struct snd_kcontrol_new cod3026x_adcl_mix[] = {
	SOC_DAPM_SINGLE("MIC1L Switch", COD3026X_MIX_AD1, 5, 1, 0),
	SOC_DAPM_SINGLE("MIC2L Switch", COD3026X_MIX_AD1, 3, 1, 0),
	SOC_DAPM_SINGLE("MIC3L Switch", COD3026X_MIX_AD1, 1, 1, 0),
	SOC_DAPM_SINGLE("LINELL Switch", COD3026X_MIX_AD1, 7, 1, 0),
	SOC_DAPM_SINGLE("LINERL Switch", COD3026X_MIX_AD2, 6, 1, 0),
	SOC_DAPM_SINGLE("MIC1L Bypass", COD3026X_MIX_AD2, 5, 1, 0),
	SOC_DAPM_SINGLE("MIC2L Bypass", COD3026X_MIX_AD2, 3, 1, 0),
	SOC_DAPM_SINGLE("MIC3L Bypass", COD3026X_MIX_AD2, 1, 1, 0),
};

static const struct snd_kcontrol_new cod3026x_adcr_mix[] = {
	SOC_DAPM_SINGLE("MIC1R Switch", COD3026X_MIX_AD1, 4, 1, 0),
	SOC_DAPM_SINGLE("MIC2R Switch", COD3026X_MIX_AD1, 2, 1, 0),
	SOC_DAPM_SINGLE("MIC3R Switch", COD3026X_MIX_AD1, 0, 1, 0),
	SOC_DAPM_SINGLE("LINELR Switch", COD3026X_MIX_AD2, 7, 1, 0),
	SOC_DAPM_SINGLE("LINERR Switch", COD3026X_MIX_AD1, 6, 1, 0),
	SOC_DAPM_SINGLE("MIC1R Bypass", COD3026X_MIX_AD2, 4, 1, 0),
	SOC_DAPM_SINGLE("MIC2R Bypass", COD3026X_MIX_AD2, 2, 1, 0),
	SOC_DAPM_SINGLE("MIC3R Bypass", COD3026X_MIX_AD2, 0, 1, 0),
};

static const struct snd_kcontrol_new cod3026x_hpl_mix[] = {
	SOC_DAPM_SINGLE("DACL Switch", COD3026X_MIX_DA1, 7, 1, 0),
	SOC_DAPM_SINGLE("DACR Switch", COD3026X_MIX_DA1, 6, 1, 0),
	SOC_DAPM_SINGLE("ADCL Switch", COD3026X_MIX_DA1, 5, 1, 0),
	SOC_DAPM_SINGLE("ADCR Switch", COD3026X_MIX_DA1, 4, 1, 0),
};

static const struct snd_kcontrol_new cod3026x_hpr_mix[] = {
	SOC_DAPM_SINGLE("DACL Switch", COD3026X_MIX_DA1, 3, 1, 0),
	SOC_DAPM_SINGLE("DACR Switch", COD3026X_MIX_DA1, 2, 1, 0),
	SOC_DAPM_SINGLE("ADCL Switch", COD3026X_MIX_DA1, 1, 1, 0),
	SOC_DAPM_SINGLE("ADCR Switch", COD3026X_MIX_DA1, 0, 1, 0),
};

static const struct snd_kcontrol_new cod3026x_ep_mix[] = {
	SOC_DAPM_SINGLE("DACL Switch", COD3026X_MIX_DA2, 7, 1, 0),
	SOC_DAPM_SINGLE("DACR Switch", COD3026X_MIX_DA2, 6, 1, 0),
	SOC_DAPM_SINGLE("ADCL Switch", COD3026X_MIX_DA2, 5, 1, 0),
	SOC_DAPM_SINGLE("ADCR Switch", COD3026X_MIX_DA2, 4, 1, 0),
};

static const struct snd_kcontrol_new cod3026x_spk_mix[] = {
	SOC_DAPM_SINGLE("DACL Switch", COD3026X_MIX_DA2, 3, 1, 0),
	SOC_DAPM_SINGLE("DACR Switch", COD3026X_MIX_DA2, 2, 1, 0),
	SOC_DAPM_SINGLE("ADCL Switch", COD3026X_MIX_DA2, 1, 1, 0),
	SOC_DAPM_SINGLE("ADCR Switch", COD3026X_MIX_DA2, 0, 1, 0),
};

static const struct snd_kcontrol_new cod3026x_spk_on[] = {
	SOC_DAPM_SINGLE("Switch", COD3026X_CHOP_DA, 2, 1, 0),
};

static const struct snd_kcontrol_new cod3026x_hp_on[] = {
	SOC_DAPM_SINGLE("Switch", COD3026X_CHOP_DA, 4, 1, 0),
};

static const struct snd_kcontrol_new cod3026x_ep_on[] = {
	SOC_DAPM_SINGLE("Switch", COD3026X_CHOP_DA, 3, 1, 0),
};

static const struct snd_kcontrol_new cod3026x_mic1_on[] = {
	SOC_DAPM_SINGLE("Switch", COD3026X_MIC_ON, 4, 1, 0),
};

static const struct snd_kcontrol_new cod3026x_mic2_on[] = {
	SOC_DAPM_SINGLE("Switch", COD3026X_MIC_ON, 5, 1, 0),
};

static const struct snd_kcontrol_new cod3026x_mic3_on[] = {
	SOC_DAPM_SINGLE("Switch", COD3026X_MIC_ON, 6, 1, 0),
};

static const struct snd_kcontrol_new cod3026x_linein_on[] = {
	SOC_DAPM_SINGLE("Switch", COD3026X_MIC_ON, 7, 1, 0),
};

static const struct snd_soc_dapm_widget cod3026x_widgets[] = {
	SND_SOC_DAPM_SUPPLY("MICBIAS1", SND_SOC_NOPM,
			    COD3026X_MICBIAS_1, 0,
			    cod3026x_micbias_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("MICBIAS2", SND_SOC_NOPM,
			    COD3026X_MICBIAS_2, 0,
			    cod3026x_micbias_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("VMID", SND_SOC_NOPM, 0, 0,
			    cod3026x_vmid_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_DAC_E("DAC", NULL, SND_SOC_NOPM, 0, 0,
			   cod3026x_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_ADC_E("ADC", NULL, SND_SOC_NOPM, 0, 0,
			   cod3026x_adc_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_PGA_E("MIC1 PGA", SND_SOC_NOPM, 0, 0, NULL, 0,
			   cod3026x_mic_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_PGA_E("MIC2 PGA", SND_SOC_NOPM, 1, 0, NULL, 0,
			   cod3026x_mic_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_PGA_E("MIC3 PGA", SND_SOC_NOPM, 2, 0, NULL, 0,
			   cod3026x_mic_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_PGA_E("LINEIN PGA", SND_SOC_NOPM, 3, 0, NULL, 0,
			   cod3026x_mic_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SWITCH("MIC1", SND_SOC_NOPM, 0, 0,
			    cod3026x_mic1_on),
	SND_SOC_DAPM_SWITCH("MIC2", SND_SOC_NOPM, 0, 0,
			    cod3026x_mic2_on),
	SND_SOC_DAPM_SWITCH("MIC3", SND_SOC_NOPM, 0, 0,
			    cod3026x_mic3_on),
	SND_SOC_DAPM_SWITCH("LINEIN", SND_SOC_NOPM, 0, 0,
			    cod3026x_linein_on),
	SND_SOC_DAPM_MIXER("ADCL Mixer", SND_SOC_NOPM, 0, 0,
			   cod3026x_adcl_mix, ARRAY_SIZE(cod3026x_adcl_mix)),
	SND_SOC_DAPM_MIXER("ADCR Mixer", SND_SOC_NOPM, 0, 0,
			   cod3026x_adcr_mix, ARRAY_SIZE(cod3026x_adcr_mix)),
	SND_SOC_DAPM_MIXER("HPL Mixer", SND_SOC_NOPM, 0, 0,
			   cod3026x_hpl_mix, ARRAY_SIZE(cod3026x_hpl_mix)),
	SND_SOC_DAPM_MIXER("HPR Mixer", SND_SOC_NOPM, 0, 0,
			   cod3026x_hpr_mix, ARRAY_SIZE(cod3026x_hpr_mix)),
	SND_SOC_DAPM_MIXER("EP Mixer", SND_SOC_NOPM, 0, 0,
			   cod3026x_ep_mix, ARRAY_SIZE(cod3026x_ep_mix)),
	SND_SOC_DAPM_MIXER("SPK Mixer", SND_SOC_NOPM, 0, 0,
			   cod3026x_spk_mix, ARRAY_SIZE(cod3026x_spk_mix)),
	SND_SOC_DAPM_OUT_DRV_E("HP Driver", SND_SOC_NOPM,
			       COD3026X_OUTPUT_HP, 0, NULL, 0,
			       cod3026x_output_event,
			       SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			       SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUT_DRV_E("EP Driver", SND_SOC_NOPM,
			       COD3026X_OUTPUT_EP, 0, NULL, 0,
			       cod3026x_output_event,
			       SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUT_DRV_E("SPK Driver", SND_SOC_NOPM,
			       COD3026X_OUTPUT_SPK, 0, NULL, 0,
			       cod3026x_output_event,
			       SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SWITCH("HP", SND_SOC_NOPM, 0, 0, cod3026x_hp_on),
	SND_SOC_DAPM_SWITCH("EP", SND_SOC_NOPM, 0, 0, cod3026x_ep_on),
	SND_SOC_DAPM_SWITCH("SPK", SND_SOC_NOPM, 0, 0, cod3026x_spk_on),
	SND_SOC_DAPM_INPUT("IN1L"),
	SND_SOC_DAPM_INPUT("IN2L"),
	SND_SOC_DAPM_INPUT("IN3L"),
	SND_SOC_DAPM_INPUT("IN4L"),
	SND_SOC_DAPM_OUTPUT("HPOUTLN"),
	SND_SOC_DAPM_OUTPUT("EPOUTN"),
	SND_SOC_DAPM_OUTPUT("SPKOUTLN"),
};

static const struct snd_soc_dapm_route cod3026x_routes[] = {
	{ "DAC", NULL, "AIF Playback" },
	{ "DAC", NULL, "AIF2 Playback" },
	{ "HPL Mixer", "DACL Switch", "DAC" },
	{ "HPL Mixer", "DACR Switch", "DAC" },
	{ "HPL Mixer", "ADCL Switch", "ADCL Mixer" },
	{ "HPL Mixer", "ADCR Switch", "ADCR Mixer" },
	{ "HPR Mixer", "DACL Switch", "DAC" },
	{ "HPR Mixer", "DACR Switch", "DAC" },
	{ "HPR Mixer", "ADCL Switch", "ADCL Mixer" },
	{ "HPR Mixer", "ADCR Switch", "ADCR Mixer" },
	{ "HP Driver", NULL, "HPL Mixer" },
	{ "HP Driver", NULL, "HPR Mixer" },
	{ "HP", "Switch", "HP Driver" },
	{ "HPOUTLN", NULL, "HP" },
	{ "EP Mixer", "DACL Switch", "DAC" },
	{ "EP Mixer", "DACR Switch", "DAC" },
	{ "EP Mixer", "ADCL Switch", "ADCL Mixer" },
	{ "EP Mixer", "ADCR Switch", "ADCR Mixer" },
	{ "EP Driver", NULL, "EP Mixer" },
	{ "EP", "Switch", "EP Driver" },
	{ "EPOUTN", NULL, "EP" },
	{ "SPK Mixer", "DACL Switch", "DAC" },
	{ "SPK Mixer", "DACR Switch", "DAC" },
	{ "SPK Mixer", "ADCL Switch", "ADCL Mixer" },
	{ "SPK Mixer", "ADCR Switch", "ADCR Mixer" },
	{ "SPK Driver", NULL, "SPK Mixer" },
	{ "SPK", "Switch", "SPK Driver" },
	{ "SPKOUTLN", NULL, "SPK" },
	{ "MIC1 PGA", NULL, "IN1L" },
	{ "MIC1 PGA", NULL, "VMID" },
	{ "MIC1", "Switch", "MIC1 PGA" },
	{ "ADCL Mixer", "MIC1L Switch", "MIC1" },
	{ "ADCR Mixer", "MIC1R Switch", "MIC1" },
	{ "ADCL Mixer", "MIC1L Bypass", "MIC1" },
	{ "ADCR Mixer", "MIC1R Bypass", "MIC1" },
	{ "MIC2 PGA", NULL, "IN2L" },
	{ "MIC2 PGA", NULL, "VMID" },
	{ "MIC2", "Switch", "MIC2 PGA" },
	{ "ADCL Mixer", "MIC2L Switch", "MIC2" },
	{ "ADCR Mixer", "MIC2R Switch", "MIC2" },
	{ "ADCL Mixer", "MIC2L Bypass", "MIC2" },
	{ "ADCR Mixer", "MIC2R Bypass", "MIC2" },
	{ "MIC3 PGA", NULL, "IN3L" },
	{ "MIC3 PGA", NULL, "VMID" },
	{ "MIC3", "Switch", "MIC3 PGA" },
	{ "ADCL Mixer", "MIC3L Switch", "MIC3" },
	{ "ADCR Mixer", "MIC3R Switch", "MIC3" },
	{ "ADCL Mixer", "MIC3L Bypass", "MIC3" },
	{ "ADCR Mixer", "MIC3R Bypass", "MIC3" },
	{ "LINEIN PGA", NULL, "IN4L" },
	{ "LINEIN PGA", NULL, "VMID" },
	{ "LINEIN", "Switch", "LINEIN PGA" },
	{ "ADCL Mixer", "LINELL Switch", "LINEIN" },
	{ "ADCL Mixer", "LINERL Switch", "LINEIN" },
	{ "ADCR Mixer", "LINELR Switch", "LINEIN" },
	{ "ADCR Mixer", "LINERR Switch", "LINEIN" },
	{ "ADC", NULL, "ADCL Mixer" },
	{ "ADC", NULL, "ADCR Mixer" },
	{ "AIF Capture", NULL, "ADC" },
	{ "AIF2 Capture", NULL, "ADC" },
};

#define COD3026X_JACK_REPORT_MASK	(SND_JACK_HEADSET | \
					 SND_JACK_BTN_0 | SND_JACK_BTN_1 | \
					 SND_JACK_BTN_2 | SND_JACK_BTN_3)
#define COD3026X_BUTTON_REPORT_MASK	(SND_JACK_BTN_0 | SND_JACK_BTN_1 | \
					 SND_JACK_BTN_2 | SND_JACK_BTN_3)

static int cod3026x_read_jack_adc(struct cod3026x_priv *cod3026x)
{
	int sample, total = 0, minimum = INT_MAX, maximum = 0;
	int i, retry, ret;

	if (!cod3026x->jack_adc)
		return -ENODEV;

	for (i = 0; i < COD3026X_ADC_SAMPLES; i++) {
		for (retry = 0; retry < 10; retry++) {
			ret = iio_read_channel_raw(cod3026x->jack_adc, &sample);
			if (!ret && sample >= 0)
				break;
			usleep_range(1000, 1500);
		}
		if (retry == 10)
			return ret ?: -EIO;

		minimum = min(minimum, sample);
		maximum = max(maximum, sample);
		total += sample;
	}

	return (total - minimum - maximum) / (COD3026X_ADC_SAMPLES - 2);
}

static void cod3026x_jack_work(struct work_struct *work)
{
	struct cod3026x_priv *cod3026x =
		container_of(to_delayed_work(work), struct cod3026x_priv,
			     jack_work);
	unsigned int status = 0, report = 0;
	bool jack_present, mic_present;
	int adc = -ENODEV, ret;

	ret = pm_runtime_resume_and_get(cod3026x->dev);
	if (ret < 0)
		return;

	mutex_lock(&cod3026x->lock);
	ret = regmap_read(cod3026x->regmap, COD3026X_STATUS1, &status);
	if (ret)
		goto out_unlock;

	jack_present = status & COD3026X_STATUS_JACK;
	mic_present = status & COD3026X_STATUS_MIC;
	if (jack_present && cod3026x->jack_adc) {
		adc = cod3026x_read_jack_adc(cod3026x);
		if (adc >= 0)
			mic_present = adc > cod3026x->mic_threshold;
	}

	cod3026x->jack_present = jack_present;
	cod3026x->mic_present = jack_present && mic_present;

	if (cod3026x->jack_present && cod3026x->mic_present) {
		regmap_write(cod3026x->regmap, COD3026X_MIC_DET, 0x02);
		regmap_write(cod3026x->regmap, COD3026X_DET_PDB, 0x00);
		report = SND_JACK_HEADSET;
	} else if (cod3026x->jack_present) {
		regmap_write(cod3026x->regmap, COD3026X_MIC_DET, 0x02);
		regmap_write(cod3026x->regmap, COD3026X_DET_PDB, 0x01);
		report = SND_JACK_HEADPHONE;
	} else {
		regmap_write(cod3026x->regmap, COD3026X_MIC_DET, 0xff);
		regmap_write(cod3026x->regmap, COD3026X_DET_PDB, 0x00);
		regmap_update_bits(cod3026x->regmap, COD3026X_PD_AD2,
				   COD3026X_PDB_MIC_BST3, 0);
		regmap_update_bits(cod3026x->regmap, COD3026X_ADC1,
				   COD3026X_ADC_MUTE, 0);
		cod3026x->button_mask = 0;
	}

	if (cod3026x->jack)
		snd_soc_jack_report(cod3026x->jack, report,
				    COD3026X_JACK_REPORT_MASK);

	dev_dbg(cod3026x->dev, "jack %s, microphone %s, ADC %d\n",
		cod3026x->jack_present ? "inserted" : "removed",
		cod3026x->mic_present ? "present" : "absent", adc);

out_unlock:
	mutex_unlock(&cod3026x->lock);
	pm_runtime_mark_last_busy(cod3026x->dev);
	pm_runtime_put_autosuspend(cod3026x->dev);
}

static void cod3026x_button_work(struct work_struct *work)
{
	struct cod3026x_priv *cod3026x =
		container_of(to_delayed_work(work), struct cod3026x_priv,
			     button_work);
	unsigned int new_mask = 0;
	int adc, i, ret;

	ret = pm_runtime_resume_and_get(cod3026x->dev);
	if (ret < 0)
		return;

	mutex_lock(&cod3026x->lock);
	if (!cod3026x->jack_present || !cod3026x->mic_present ||
	    !cod3026x->jack_adc)
		goto release;

	adc = cod3026x_read_jack_adc(cod3026x);
	if (adc < 0)
		goto out_unlock;

	if (adc > cod3026x->button_release)
		goto release;

	for (i = 0; i < cod3026x->num_buttons; i++) {
		if (adc >= cod3026x->buttons[i].low &&
		    adc <= cod3026x->buttons[i].high) {
			new_mask = SND_JACK_BTN_0 << i;
			break;
		}
	}

	if (!new_mask)
		goto release;

	regmap_update_bits(cod3026x->regmap, COD3026X_ADC1,
			   COD3026X_ADC_MUTE, COD3026X_ADC_MUTE);
	cod3026x->button_mask = new_mask;
	if (cod3026x->jack)
		snd_soc_jack_report(cod3026x->jack, new_mask,
				    COD3026X_BUTTON_REPORT_MASK);
	goto out_unlock;

release:
	if (cod3026x->button_mask) {
		regmap_update_bits(cod3026x->regmap, COD3026X_PD_AD2,
				   COD3026X_PDB_MIC_BST3, 0);
		msleep(40);
		regmap_update_bits(cod3026x->regmap, COD3026X_PD_AD2,
				   COD3026X_PDB_MIC_BST3,
				   COD3026X_PDB_MIC_BST3);
		regmap_update_bits(cod3026x->regmap, COD3026X_ADC1,
				   COD3026X_ADC_MUTE, 0);
		cod3026x->button_mask = 0;
		if (cod3026x->jack)
			snd_soc_jack_report(cod3026x->jack, 0,
					    COD3026X_BUTTON_REPORT_MASK);
	}

out_unlock:
	mutex_unlock(&cod3026x->lock);
	pm_runtime_mark_last_busy(cod3026x->dev);
	pm_runtime_put_autosuspend(cod3026x->dev);
}

static irqreturn_t cod3026x_irq_thread(int irq, void *data)
{
	struct cod3026x_priv *cod3026x = data;
	u8 pending[5];
	bool jack_event, button_event;
	int ret;

	ret = pm_runtime_resume_and_get(cod3026x->dev);
	if (ret < 0)
		return IRQ_HANDLED;

	ret = regmap_bulk_read(cod3026x->regmap, COD3026X_IRQ1PEND,
			       pending, ARRAY_SIZE(pending));
	if (ret)
		goto out_pm;

	jack_event = (pending[1] | pending[2]) &
		     (COD3026X_IRQ_JACK_RISE | COD3026X_IRQ_MIC_RISE);
	button_event = pending[0] & COD3026X_IRQ_KEY_PRESS;
	button_event |= (pending[1] | pending[2]) &
			COD3026X_IRQ_KEY_RELEASE;

	if (jack_event) {
		cancel_delayed_work(&cod3026x->button_work);
		mod_delayed_work(system_power_efficient_wq,
				 &cod3026x->jack_work,
				 msecs_to_jiffies(cod3026x->jack_present ? 0 :
						    cod3026x->mic_delay_ms));
	} else if (button_event) {
		mod_delayed_work(system_power_efficient_wq,
				 &cod3026x->button_work,
				 msecs_to_jiffies(cod3026x->button_delay_ms));
	}

out_pm:
	pm_runtime_mark_last_busy(cod3026x->dev);
	pm_runtime_put_autosuspend(cod3026x->dev);
	return IRQ_HANDLED;
}

static int cod3026x_set_jack(struct snd_soc_component *component,
			     struct snd_soc_jack *jack, void *data)
{
	struct cod3026x_priv *cod3026x =
		snd_soc_component_get_drvdata(component);
	int i, ret;

	if (jack) {
		for (i = 0; i < cod3026x->num_buttons; i++) {
			ret = snd_jack_set_key(jack->jack, SND_JACK_BTN_0 << i,
					       cod3026x->buttons[i].code);
			if (ret)
				return ret;
		}
	}

	mutex_lock(&cod3026x->lock);
	cod3026x->jack = jack;
	mutex_unlock(&cod3026x->lock);

	if (jack)
		mod_delayed_work(system_power_efficient_wq,
				 &cod3026x->jack_work, 0);

	return 0;
}

static int cod3026x_component_probe(struct snd_soc_component *component)
{
	struct cod3026x_priv *cod3026x =
		snd_soc_component_get_drvdata(component);

	cod3026x->component = component;
	return 0;
}

static const struct snd_soc_component_driver cod3026x_component = {
	.probe = cod3026x_component_probe,
	.set_jack = cod3026x_set_jack,
	.controls = cod3026x_controls,
	.num_controls = ARRAY_SIZE(cod3026x_controls),
	.dapm_widgets = cod3026x_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cod3026x_widgets),
	.dapm_routes = cod3026x_routes,
	.num_dapm_routes = ARRAY_SIZE(cod3026x_routes),
	.endianness = 1,
};

static int cod3026x_parse_properties(struct cod3026x_priv *cod3026x)
{
	struct device *dev = cod3026x->dev;
	u32 zones[COD3026X_BUTTON_COUNT][3];
	int count, i, ret;

	cod3026x->micbias1 = 3000000;
	cod3026x->micbias2 = 3000000;
	cod3026x->micbias_ldo = 3300000;
	cod3026x->mic_threshold = 1120;
	cod3026x->mic_delay_ms = 550;
	cod3026x->button_release = 1100;
	cod3026x->button_delay_ms = 30;

	device_property_read_u32(dev, "samsung,mic-bias1-microvolt",
				 &cod3026x->micbias1);
	device_property_read_u32(dev, "samsung,mic-bias2-microvolt",
				 &cod3026x->micbias2);
	device_property_read_u32(dev, "samsung,mic-bias-ldo-microvolt",
				 &cod3026x->micbias_ldo);
	device_property_read_u32(dev, "samsung,mic-detect-threshold",
				 &cod3026x->mic_threshold);
	device_property_read_u32(dev, "samsung,mic-detect-delay-ms",
				 &cod3026x->mic_delay_ms);
	device_property_read_u32(dev, "samsung,button-release-threshold",
				 &cod3026x->button_release);
	device_property_read_u32(dev, "samsung,button-press-delay-ms",
				 &cod3026x->button_delay_ms);

	count = device_property_count_u32(dev, "samsung,headset-button-zones");
	if (count < 0)
		return count == -EINVAL ? 0 : count;
	if (!count || count % 3 || count > ARRAY_SIZE(zones) * 3)
		return -EINVAL;

	ret = device_property_read_u32_array(dev,
					     "samsung,headset-button-zones",
					     &zones[0][0], count);
	if (ret)
		return ret;

	cod3026x->num_buttons = count / 3;
	for (i = 0; i < cod3026x->num_buttons; i++) {
		if (zones[i][1] > zones[i][2])
			return -EINVAL;
		cod3026x->buttons[i].code = zones[i][0];
		cod3026x->buttons[i].low = zones[i][1];
		cod3026x->buttons[i].high = zones[i][2];
	}

	return 0;
}

static int cod3026x_add_mixer_link(struct cod3026x_priv *cod3026x)
{
	struct device_node *mixer_np;
	struct platform_device *mixer_pdev;
	u32 flags = DL_FLAG_PM_RUNTIME | DL_FLAG_AUTOREMOVE_CONSUMER;

	mixer_np = of_parse_phandle(cod3026x->dev->of_node,
				    "samsung,audio-mixer", 0);
	if (!mixer_np)
		return -EINVAL;

	mixer_pdev = of_find_device_by_node(mixer_np);
	of_node_put(mixer_np);
	if (!mixer_pdev)
		return -EPROBE_DEFER;

	cod3026x->mixer_link =
		device_link_add(cod3026x->dev, &mixer_pdev->dev, flags);
	put_device(&mixer_pdev->dev);
	if (!cod3026x->mixer_link)
		return -ENOMEM;

	return 0;
}

static int cod3026x_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct cod3026x_priv *cod3026x;
	int ret;

	cod3026x = devm_kzalloc(dev, sizeof(*cod3026x), GFP_KERNEL);
	if (!cod3026x)
		return -ENOMEM;

	cod3026x->dev = dev;
	cod3026x->aifrate = COD3026X_RATE_48KHZ;
	mutex_init(&cod3026x->lock);
	INIT_DELAYED_WORK(&cod3026x->adc_unmute_work,
			  cod3026x_adc_unmute_work);
	INIT_DELAYED_WORK(&cod3026x->jack_work, cod3026x_jack_work);
	INIT_DELAYED_WORK(&cod3026x->button_work, cod3026x_button_work);
	i2c_set_clientdata(client, cod3026x);

	ret = cod3026x_parse_properties(cod3026x);
	if (ret)
		return dev_err_probe(dev, ret, "invalid device properties\n");

	ret = cod3026x_add_mixer_link(cod3026x);
	if (ret)
		return dev_err_probe(dev, ret, "failed to link audio mixer\n");

	cod3026x->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(cod3026x->vdd))
		return dev_err_probe(dev, PTR_ERR(cod3026x->vdd),
				     "failed to get vdd supply\n");

	cod3026x->regmap =
		devm_regmap_init_i2c(client, &cod3026x_regmap_config);
	if (IS_ERR(cod3026x->regmap))
		return dev_err_probe(dev, PTR_ERR(cod3026x->regmap),
				     "failed to create regmap\n");

	cod3026x->jack_adc = devm_iio_channel_get(dev, "jack");
	if (IS_ERR(cod3026x->jack_adc)) {
		ret = PTR_ERR(cod3026x->jack_adc);
		if (ret == -ENODEV || ret == -ENXIO)
			cod3026x->jack_adc = NULL;
		else
			return dev_err_probe(dev, ret,
					     "failed to get jack ADC\n");
	}

	regcache_cache_only(cod3026x->regmap, true);
	pm_runtime_set_autosuspend_delay(dev, COD3026X_AUTOSUSPEND_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_enable(dev);

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		goto err_pm;

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	if (client->irq <= 0) {
		ret = -EINVAL;
		dev_err(dev, "invalid interrupt\n");
		goto err_pm;
	}

	ret = devm_request_threaded_irq(dev, client->irq, NULL,
					cod3026x_irq_thread, IRQF_ONESHOT,
					dev_name(dev), cod3026x);
	if (ret)
		goto err_pm;

	ret = device_init_wakeup(dev, true);
	if (ret)
		goto err_pm;

	ret = devm_pm_set_wake_irq(dev, client->irq);
	if (ret)
		goto err_wakeup;

	ret = devm_snd_soc_register_component(dev, &cod3026x_component,
					      cod3026x_dais,
					      ARRAY_SIZE(cod3026x_dais));
	if (ret)
		goto err_wakeup;

	return 0;

err_wakeup:
	device_init_wakeup(dev, false);
err_pm:
	pm_runtime_disable(dev);
	if (!pm_runtime_status_suspended(dev))
		cod3026x_runtime_suspend(dev);
	return ret;
}

static void cod3026x_i2c_remove(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct cod3026x_priv *cod3026x = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&cod3026x->adc_unmute_work);
	cancel_delayed_work_sync(&cod3026x->jack_work);
	cancel_delayed_work_sync(&cod3026x->button_work);
	device_init_wakeup(dev, false);
	pm_runtime_disable(dev);
	if (!pm_runtime_status_suspended(dev))
		cod3026x_runtime_suspend(dev);
}

static const struct of_device_id cod3026x_of_match[] = {
	{ .compatible = "samsung,cod3026x" },
	{ }
};
MODULE_DEVICE_TABLE(of, cod3026x_of_match);

static const struct i2c_device_id cod3026x_i2c_id[] = {
	{ "cod3026x" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cod3026x_i2c_id);

static const struct dev_pm_ops cod3026x_pm_ops = {
	SET_RUNTIME_PM_OPS(cod3026x_runtime_suspend,
			   cod3026x_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static struct i2c_driver cod3026x_i2c_driver = {
	.probe = cod3026x_i2c_probe,
	.remove = cod3026x_i2c_remove,
	.id_table = cod3026x_i2c_id,
	.driver = {
		.name = "cod3026x",
		.of_match_table = cod3026x_of_match,
		.pm = &cod3026x_pm_ops,
	},
};
module_i2c_driver(cod3026x_i2c_driver);

MODULE_DESCRIPTION("Samsung COD3026X audio codec driver");
MODULE_AUTHOR("Sean Hoyt <seanhoyt963@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_CONSUMER");
