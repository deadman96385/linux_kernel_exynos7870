// SPDX-License-Identifier: GPL-2.0-only
/* Samsung S1402X on-chip audio mixer */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include <dt-bindings/sound/samsung,s1402x.h>

#include "s1402x.h"

#define S1402X_SYSCLK_48KHZ	24576100
#define S1402X_SYSCLK_192KHZ	49152100
#define S1402X_AUTOSUSPEND_MS	500

struct s1402x_priv {
	struct device *dev;
	struct regmap *regmap;
	struct regmap *pmu;
	struct clk_bulk_data clks[2];
	struct reset_control *reset;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default;
	struct pinctrl_state *pins_idle;
	struct pinctrl_state *pins_bt;
	struct pinctrl_state *pins_bt_idle;
	struct pinctrl_state *pins_fm;
	struct pinctrl_state *pins_fm_idle;
	struct mutex stream_lock; /* Protects stream and pinmux state. */
	unsigned int use_count[7];
	unsigned int active_streams;
	bool bt_fm_combo;
	bool bck4_mcko;
};

static const struct reg_default s1402x_reg_defaults[] = {
	{ 0x004, 0x0c }, { 0x008, 0x62 }, { 0x00c, 0x24 },
	{ 0x010, 0x0c }, { 0x014, 0x62 }, { 0x018, 0x24 },
	{ 0x01c, 0x0c }, { 0x020, 0x62 }, { 0x024, 0x24 },
	{ 0x028, 0x04 }, { 0x02c, 0x01 }, { 0x030, 0x02 },
	{ 0x034, 0x00 }, { 0x038, 0x01 }, { 0x03c, 0x00 },
	{ 0x040, 0x00 }, { 0x044, 0x00 },
	{ 0x058, 0x00 }, { 0x05c, 0x00 }, { 0x060, 0x2a },
	{ 0x064, 0x22 }, { 0x068, 0x22 },
	{ 0x1a0, 0x00 }, { 0x1a4, 0x80 }, { 0x1a8, 0x58 },
	{ 0x1ac, 0x1a }, { 0x1b0, 0x1a }, { 0x1b4, 0x12 },
	{ 0x1b8, 0x12 }, { 0x1bc, 0x12 }, { 0x1c0, 0x17 },
	{ 0x1c4, 0x6c }, { 0x1c8, 0x6c },
};

static bool s1402x_readable_reg(struct device *dev, unsigned int reg)
{
	if (reg % 4)
		return false;

	return (reg <= S1402X_DMIX2) ||
	       (reg >= S1402X_DOUTMX1 && reg <= S1402X_OUTCP1_CTL) ||
	       (reg >= S1402X_ALC_CTL && reg <= S1402X_ALC_SGR);
}

static bool s1402x_volatile_reg(struct device *dev, unsigned int reg)
{
	return reg == S1402X_SOFT_RSTB;
}

static const struct regmap_config s1402x_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = S1402X_MAX_REGISTER,
	.readable_reg = s1402x_readable_reg,
	.writeable_reg = s1402x_readable_reg,
	.volatile_reg = s1402x_volatile_reg,
	.reg_defaults = s1402x_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(s1402x_reg_defaults),
	.cache_type = REGCACHE_MAPLE,
};

static void s1402x_reset_data(struct s1402x_priv *s1402x)
{
	regmap_update_bits(s1402x->regmap, S1402X_SOFT_RSTB,
			   S1402X_DATA_RSTB, 0);
	regmap_update_bits(s1402x->regmap, S1402X_SOFT_RSTB,
			   S1402X_DATA_RSTB, S1402X_DATA_RSTB);
}

static int s1402x_reset(struct s1402x_priv *s1402x)
{
	int ret;

	ret = regmap_write(s1402x->regmap, S1402X_SOFT_RSTB, 0);
	if (ret)
		return ret;

	usleep_range(1000, 1100);

	ret = regmap_write(s1402x->regmap, S1402X_SOFT_RSTB,
			   S1402X_DATA_RSTB | S1402X_SYS_RSTB);
	if (ret)
		return ret;

	usleep_range(1000, 1100);
	return 0;
}

static int s1402x_hw_init(struct s1402x_priv *s1402x)
{
	unsigned int hq;
	int ret;

	ret = regmap_write(s1402x->regmap, S1402X_IN1_CTL2,
			   S1402X_I2S_XFS_32FS << S1402X_I2S_XFS_SHIFT |
			   S1402X_I2S_DF_I2S << S1402X_I2S_DF_SHIFT |
			   S1402X_I2S_DL_16BIT << S1402X_I2S_DL_SHIFT);
	if (ret)
		return ret;

	ret = regmap_write(s1402x->regmap, S1402X_IN2_CTL2,
			   S1402X_I2S_XFS_32FS << S1402X_I2S_XFS_SHIFT |
			   S1402X_I2S_DF_I2S << S1402X_I2S_DF_SHIFT |
			   S1402X_I2S_DL_16BIT << S1402X_I2S_DL_SHIFT);
	if (ret)
		return ret;

	ret = regmap_write(s1402x->regmap, S1402X_IN3_CTL1,
			   S1402X_MPCM_SLOT_32BCK << S1402X_MPCM_SLOT_SHIFT);
	if (ret)
		return ret;

	ret = regmap_write(s1402x->regmap, S1402X_IN3_CTL2,
			   S1402X_I2S_XFS_32FS << S1402X_I2S_XFS_SHIFT |
			   S1402X_I2S_DF_I2S << S1402X_I2S_DF_SHIFT |
			   S1402X_I2S_DL_16BIT << S1402X_I2S_DL_SHIFT);
	if (ret)
		return ret;

	ret = regmap_write(s1402x->regmap, S1402X_IN3_CTL3,
			   S1402X_PCM_DAD_0BCK << S1402X_PCM_DAD_SHIFT |
			   S1402X_PCM_DF_SHORT << S1402X_PCM_DF_SHIFT);
	if (ret)
		return ret;

	regmap_write(s1402x->regmap, S1402X_SLOT_L, S1402X_SLOT_1);
	regmap_write(s1402x->regmap, S1402X_SLOT_R, S1402X_SLOT_2);
	regmap_write(s1402x->regmap, S1402X_TSLOT, S1402X_TSLOT_2);

	regmap_write(s1402x->regmap, S1402X_INAMP_CTL,
		     S1402X_INAMP_RESERVED_2 |
		     S1402X_I2S_XFS_64FS << S1402X_INAMP_XFS_SHIFT |
		     S1402X_AMP_DL_24BIT << S1402X_INAMP_DL_SHIFT);
	regmap_write(s1402x->regmap, S1402X_OUTAP1_CTL,
		     S1402X_AMP_DL_16BIT << S1402X_OUT_DL_SHIFT |
		     S1402X_I2S_XFS_32FS << S1402X_OUT_XFS_SHIFT);
	regmap_write(s1402x->regmap, S1402X_OUTCP1_CTL,
		     S1402X_AMP_DL_16BIT << S1402X_OUT_DL_SHIFT |
		     S1402X_I2S_XFS_32FS << S1402X_OUT_XFS_SHIFT);

	hq = S1402X_MCKO_EN;
	if (s1402x->bck4_mcko)
		hq |= S1402X_BCK4_MODE;
	regmap_write(s1402x->regmap, S1402X_HQ_CTL, hq);
	regmap_write(s1402x->regmap, S1402X_DIG_EN,
		     BIT(S1402X_DIG_MIX_EN_SHIFT));
	s1402x_reset_data(s1402x);

	return 0;
}

static struct pinctrl_state *s1402x_lookup_state(struct s1402x_priv *s1402x,
						 const char *name)
{
	struct pinctrl_state *state;

	if (!s1402x->pinctrl)
		return NULL;

	state = pinctrl_lookup_state(s1402x->pinctrl, name);
	return IS_ERR(state) ? NULL : state;
}

static void s1402x_select_state(struct s1402x_priv *s1402x,
				struct pinctrl_state *state)
{
	int ret;

	if (!state)
		return;

	ret = pinctrl_select_state(s1402x->pinctrl, state);
	if (ret)
		dev_warn(s1402x->dev, "failed to select pin state: %d\n", ret);
}

static int s1402x_runtime_resume(struct device *dev)
{
	struct s1402x_priv *s1402x = dev_get_drvdata(dev);
	int ret;

	ret = clk_bulk_prepare_enable(ARRAY_SIZE(s1402x->clks), s1402x->clks);
	if (ret)
		return ret;

	ret = reset_control_deassert(s1402x->reset);
	if (ret)
		goto err_clks;

	regcache_cache_only(s1402x->regmap, false);
	ret = s1402x_reset(s1402x);
	if (ret)
		goto err_reset;

	ret = regcache_sync(s1402x->regmap);
	if (ret)
		goto err_reset;

	s1402x_select_state(s1402x, s1402x->pins_default);
	return 0;

err_reset:
	regcache_cache_only(s1402x->regmap, true);
	reset_control_assert(s1402x->reset);
err_clks:
	clk_bulk_disable_unprepare(ARRAY_SIZE(s1402x->clks), s1402x->clks);
	return ret;
}

static int s1402x_runtime_suspend(struct device *dev)
{
	struct s1402x_priv *s1402x = dev_get_drvdata(dev);

	regcache_cache_only(s1402x->regmap, true);
	regcache_mark_dirty(s1402x->regmap);
	s1402x_select_state(s1402x, s1402x->pins_idle);
	reset_control_assert(s1402x->reset);
	clk_bulk_disable_unprepare(ARRAY_SIZE(s1402x->clks), s1402x->clks);

	return 0;
}

static int s1402x_set_bclk_ratio(struct snd_soc_dai *dai, unsigned int ratio)
{
	struct s1402x_priv *s1402x = snd_soc_component_get_drvdata(dai->component);
	unsigned int reg, shift, mask, xfs;

	switch (ratio) {
	case 32:
		xfs = S1402X_I2S_XFS_32FS;
		break;
	case 48:
		xfs = S1402X_I2S_XFS_48FS;
		break;
	case 64:
		xfs = S1402X_I2S_XFS_64FS;
		break;
	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case S1402X_DAI_AP0:
		reg = S1402X_IN1_CTL2;
		shift = S1402X_I2S_XFS_SHIFT;
		mask = S1402X_I2S_XFS_MASK;
		break;
	case S1402X_DAI_CP0:
		reg = S1402X_IN2_CTL2;
		shift = S1402X_I2S_XFS_SHIFT;
		mask = S1402X_I2S_XFS_MASK;
		break;
	case S1402X_DAI_BT:
	case S1402X_DAI_FM:
		reg = S1402X_IN3_CTL2;
		shift = S1402X_I2S_XFS_SHIFT;
		mask = S1402X_I2S_XFS_MASK;
		break;
	case S1402X_DAI_AMP:
		reg = S1402X_INAMP_CTL;
		shift = S1402X_INAMP_XFS_SHIFT;
		mask = S1402X_INAMP_XFS_MASK;
		break;
	case S1402X_DAI_AP1:
		reg = S1402X_OUTAP1_CTL;
		shift = S1402X_OUT_XFS_SHIFT;
		mask = S1402X_OUT_XFS_MASK;
		break;
	case S1402X_DAI_CP1:
		reg = S1402X_OUTCP1_CTL;
		shift = S1402X_OUT_XFS_SHIFT;
		mask = S1402X_OUT_XFS_MASK;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(s1402x->regmap, reg, mask, xfs << shift);
}

static int s1402x_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct s1402x_priv *s1402x = snd_soc_component_get_drvdata(dai->component);
	unsigned int mode, frame;
	int ret;

	if (dai->id != S1402X_DAI_BT && dai->id != S1402X_DAI_FM)
		return (fmt & SND_SOC_DAIFMT_FORMAT_MASK) == SND_SOC_DAIFMT_I2S ?
			0 : -EINVAL;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		mode = 0;
		frame = S1402X_PCM_DF_SHORT;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		mode = S1402X_PCM_MODE;
		frame = S1402X_PCM_DF_LONG;
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_update_bits(s1402x->regmap, S1402X_IN3_CTL1,
				 S1402X_PCM_MODE, mode);
	if (ret)
		return ret;

	ret = regmap_update_bits(s1402x->regmap, S1402X_IN3_CTL3,
				 S1402X_PCM_DF_MASK,
				 frame << S1402X_PCM_DF_SHIFT);
	if (ret)
		return ret;

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBP_CFP:
		mode = S1402X_MASTER;
		break;
	case SND_SOC_DAIFMT_CBC_CFC:
		mode = 0;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(s1402x->regmap, S1402X_IN3_CTL1,
				  S1402X_MASTER, mode);
}

static int s1402x_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct s1402x_priv *s1402x = snd_soc_component_get_drvdata(dai->component);
	unsigned int bfs, dl, rate = params_rate(params);
	int ret = 0;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		dl = S1402X_I2S_DL_16BIT;
		bfs = 32;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		dl = S1402X_I2S_DL_24BIT;
		bfs = 48;
		break;
	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case S1402X_DAI_AP0:
		ret = regmap_update_bits(s1402x->regmap, S1402X_IN1_CTL2,
					 S1402X_I2S_DL_MASK,
					 dl << S1402X_I2S_DL_SHIFT);
		if (ret)
			return ret;
		if (rate == 192000) {
			ret = clk_set_rate(s1402x->clks[0].clk,
					   S1402X_SYSCLK_192KHZ);
			if (!ret)
				ret = regmap_update_bits(s1402x->regmap,
							 S1402X_HQ_CTL,
							 S1402X_HQ_EN,
							 S1402X_HQ_EN);
		} else if (rate == 48000) {
			ret = clk_set_rate(s1402x->clks[0].clk,
					   S1402X_SYSCLK_48KHZ);
			if (!ret)
				ret = regmap_update_bits(s1402x->regmap,
							 S1402X_HQ_CTL,
							 S1402X_HQ_EN, 0);
		} else {
			ret = -EINVAL;
		}
		break;
	case S1402X_DAI_CP0:
		ret = clk_set_rate(s1402x->clks[0].clk, S1402X_SYSCLK_48KHZ);
		if (!ret)
			ret = regmap_update_bits(s1402x->regmap, S1402X_IN2_CTL2,
						 S1402X_I2S_DL_MASK,
						 dl << S1402X_I2S_DL_SHIFT);
		break;
	case S1402X_DAI_BT:
	case S1402X_DAI_FM:
		ret = regmap_update_bits(s1402x->regmap, S1402X_IN3_CTL2,
					 S1402X_I2S_DL_MASK,
					 dl << S1402X_I2S_DL_SHIFT);
		if (ret)
			return ret;
		if (rate == 8000 || rate == 16000)
			ret = regmap_update_bits(s1402x->regmap, S1402X_IN3_CTL1,
						 S1402X_MPCM_SRATE_MASK,
						 (rate == 8000 ? 0 : 1) <<
						 S1402X_MPCM_SRATE_SHIFT);
		if (!ret)
			ret = clk_set_rate(s1402x->clks[0].clk,
					   S1402X_SYSCLK_48KHZ);
		break;
	case S1402X_DAI_AMP:
		dl = dl == S1402X_I2S_DL_24BIT ? S1402X_AMP_DL_24BIT :
			S1402X_AMP_DL_16BIT;
		ret = regmap_update_bits(s1402x->regmap, S1402X_INAMP_CTL,
					 S1402X_INAMP_DL_MASK,
					 dl << S1402X_INAMP_DL_SHIFT);
		break;
	case S1402X_DAI_AP1:
	case S1402X_DAI_CP1:
		dl = dl == S1402X_I2S_DL_24BIT ? S1402X_AMP_DL_24BIT :
			S1402X_AMP_DL_16BIT;
		ret = regmap_update_bits(s1402x->regmap,
					 dai->id == S1402X_DAI_AP1 ?
					 S1402X_OUTAP1_CTL : S1402X_OUTCP1_CTL,
					 S1402X_OUT_DL_MASK,
					 dl << S1402X_OUT_DL_SHIFT);
		break;
	default:
		return -EINVAL;
	}

	if (!ret && dai->id != S1402X_DAI_AP0)
		ret = s1402x_set_bclk_ratio(dai, bfs);

	if (!ret && s1402x->active_streams == 1)
		s1402x_reset_data(s1402x);

	return ret;
}

static int s1402x_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct s1402x_priv *s1402x = snd_soc_component_get_drvdata(dai->component);
	struct pinctrl_state *state = NULL;
	unsigned int mux = 0;
	int ret;

	ret = pm_runtime_resume_and_get(s1402x->dev);
	if (ret < 0)
		return ret;

	mutex_lock(&s1402x->stream_lock);
	if ((dai->id == S1402X_DAI_BT &&
	     s1402x->use_count[S1402X_DAI_FM]) ||
	    (dai->id == S1402X_DAI_FM &&
	     s1402x->use_count[S1402X_DAI_BT])) {
		ret = -EBUSY;
		goto err_unlock;
	}

	if (dai->id == S1402X_DAI_BT) {
		state = s1402x->pins_bt;
		mux = 0;
	} else if (dai->id == S1402X_DAI_FM) {
		state = s1402x->bt_fm_combo ? s1402x->pins_bt : s1402x->pins_fm;
		mux = s1402x->bt_fm_combo ? 0 : S1402X_PMU_BT_MUX;
	}

	if (!s1402x->use_count[dai->id]++) {
		s1402x_select_state(s1402x, state);
		if (dai->id == S1402X_DAI_BT || dai->id == S1402X_DAI_FM)
			regmap_update_bits(s1402x->pmu, S1402X_PMU_AUD_PATH_CFG,
					   S1402X_PMU_BT_MUX, mux);
	}
	s1402x->active_streams++;
	mutex_unlock(&s1402x->stream_lock);

	return 0;

err_unlock:
	mutex_unlock(&s1402x->stream_lock);
	pm_runtime_mark_last_busy(s1402x->dev);
	pm_runtime_put_autosuspend(s1402x->dev);
	return ret;
}

static void s1402x_shutdown(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct s1402x_priv *s1402x = snd_soc_component_get_drvdata(dai->component);
	struct pinctrl_state *state = NULL;

	mutex_lock(&s1402x->stream_lock);
	if (s1402x->use_count[dai->id])
		s1402x->use_count[dai->id]--;
	if (s1402x->active_streams)
		s1402x->active_streams--;

	if (!s1402x->use_count[dai->id]) {
		if (dai->id == S1402X_DAI_BT)
			state = s1402x->pins_bt_idle;
		else if (dai->id == S1402X_DAI_FM)
			state = s1402x->bt_fm_combo ? s1402x->pins_bt_idle :
				s1402x->pins_fm_idle;
		s1402x_select_state(s1402x, state);
	}
	mutex_unlock(&s1402x->stream_lock);

	pm_runtime_mark_last_busy(s1402x->dev);
	pm_runtime_put_autosuspend(s1402x->dev);
}

static const struct snd_soc_dai_ops s1402x_dai_ops = {
	.startup = s1402x_startup,
	.shutdown = s1402x_shutdown,
	.hw_params = s1402x_hw_params,
	.set_bclk_ratio = s1402x_set_bclk_ratio,
	.set_fmt = s1402x_set_fmt,
};

#define S1402X_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE)
#define S1402X_STREAM(_name, _rates) { \
	.stream_name = _name, \
	.channels_min = 2, \
	.channels_max = 2, \
	.rates = (_rates), \
	.formats = S1402X_FORMATS, \
}

static struct snd_soc_dai_driver s1402x_dais[] = {
	{
		.name = "AP0", .id = S1402X_DAI_AP0, .ops = &s1402x_dai_ops,
		.playback = S1402X_STREAM("AP0 Playback",
			SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_192000),
		.capture = S1402X_STREAM("AP0 Capture",
			SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_192000),
		.symmetric_rate = 1, .symmetric_channels = 1,
		.symmetric_sample_bits = 1,
	}, {
		.name = "CP0", .id = S1402X_DAI_CP0, .ops = &s1402x_dai_ops,
		.playback = S1402X_STREAM("CP0 Playback",
			SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
			SNDRV_PCM_RATE_48000),
		.capture = S1402X_STREAM("CP0 Capture",
			SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
			SNDRV_PCM_RATE_48000),
		.symmetric_channels = 1, .symmetric_sample_bits = 1,
	}, {
		.name = "BT", .id = S1402X_DAI_BT, .ops = &s1402x_dai_ops,
		.playback = S1402X_STREAM("BT Playback",
			SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
			SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_KNOT),
		.capture = S1402X_STREAM("BT Capture",
			SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
			SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_KNOT),
		.symmetric_rate = 1, .symmetric_channels = 1,
		.symmetric_sample_bits = 1,
	}, {
		.name = "FM", .id = S1402X_DAI_FM, .ops = &s1402x_dai_ops,
		.playback = S1402X_STREAM("FM Playback",
			SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
			SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_KNOT),
		.capture = S1402X_STREAM("FM Capture",
			SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
			SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_KNOT),
		.symmetric_rate = 1, .symmetric_channels = 1,
		.symmetric_sample_bits = 1,
	}, {
		.name = "AMP", .id = S1402X_DAI_AMP, .ops = &s1402x_dai_ops,
		.capture = S1402X_STREAM("AMP Capture", SNDRV_PCM_RATE_48000),
	}, {
		.name = "AP1", .id = S1402X_DAI_AP1, .ops = &s1402x_dai_ops,
		.capture = S1402X_STREAM("AP1 Capture", SNDRV_PCM_RATE_48000),
	}, {
		.name = "CP1", .id = S1402X_DAI_CP1, .ops = &s1402x_dai_ops,
		.capture = S1402X_STREAM("CP1 Capture",
			SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
			SNDRV_PCM_RATE_48000),
	},
};

static const unsigned int s1402x_mix_tlv[] = {
	TLV_DB_RANGE_HEAD(4),
	0x0, 0x1, TLV_DB_SCALE_ITEM(0, 287, 0),
	0x2, 0x3, TLV_DB_SCALE_ITEM(602, 326, 0),
	0x4, 0x5, TLV_DB_SCALE_ITEM(1204, 250, 0),
	0x6, 0x7, TLV_DB_SCALE_ITEM(1806, 250, 0),
};

static const DECLARE_TLV_DB_SCALE(s1402x_alc_ng_hys_tlv, 300, 300, 0);
static const DECLARE_TLV_DB_SCALE(s1402x_alc_max_gain_tlv, 0, 50, 0);
static const DECLARE_TLV_DB_SCALE(s1402x_alc_min_gain_tlv, -5400, 50, 0);
static const DECLARE_TLV_DB_SCALE(s1402x_alc_lvl_tlv, -4800, 150, 0);
static const DECLARE_TLV_DB_SCALE(s1402x_alc_ng_th_tlv, -7650, 150, 0);

static const char * const s1402x_mpcm_rate_text[] = {
	"8KHz", "16KHz", "24KHz", "32KHz"
};

static const char * const s1402x_mpcm_slot_text[] = {
	"1 slot", "2 slots", "3 slots", "4 slots"
};

static const char * const s1402x_polarity_text[] = { "Normal", "Inverted" };
static const char * const s1402x_audio_mode_text[] = { "I2S", "PCM" };
static const char * const s1402x_xfs_text[] = {
	"32fs", "48fs", "64fs", "64fs"
};

static const char * const s1402x_i2s_format_text[] = {
	"I2S", "Left-Justified", "Right-Justified", "Invalid"
};

static const char * const s1402x_data_length_text[] = {
	"16-bit", "18-bit", "20-bit", "24-bit"
};

static const char * const s1402x_pcm_delay_text[] = {
	"1 bck", "0 bck", "2 bck", "", "3 bck", "", "4 bck", ""
};

static const char * const s1402x_pcm_format_text[] = {
	"", "", "", "", "Short Frame", "", "", "",
	"", "", "", "", "Long Frame"
};

static const char * const s1402x_bck4_text[] = { "Normal BCK", "MCKO" };
static const char * const s1402x_dout1_text[] = {
	"DMIX_OUT", "AIF4IN", "RMIX_OUT"
};

static const char * const s1402x_dout2_text[] = {
	"DMIX_OUT", "AIF4IN", "AIF3IN"
};

static const char * const s1402x_dout3_text[] = {
	"DMIX_OUT", "AIF4IN", "AIF2IN"
};

static const char * const s1402x_off_on_text[] = { "Off", "On" };
static const char * const s1402x_alc_window_text[] = {
	"fs600", "fs1200", "fs2400", "fs300"
};

static const char * const s1402x_alc_mode_text[] = {
	"Stereo", "Right", "Left", "Independent"
};

static const char * const s1402x_alc_path_text[] = { "ADC", "Mixer" };

#define S1402X_ENUM_DECL(_name, _reg, _shift, _texts) \
	SOC_ENUM_SINGLE_DECL(_name, _reg, _shift, _texts)

static S1402X_ENUM_DECL(s1402x_mpcm_rate1, S1402X_IN1_CTL1,
			 S1402X_MPCM_SRATE_SHIFT, s1402x_mpcm_rate_text);
static S1402X_ENUM_DECL(s1402x_mpcm_rate2, S1402X_IN2_CTL1,
			 S1402X_MPCM_SRATE_SHIFT, s1402x_mpcm_rate_text);
static S1402X_ENUM_DECL(s1402x_mpcm_rate3, S1402X_IN3_CTL1,
			 S1402X_MPCM_SRATE_SHIFT, s1402x_mpcm_rate_text);
static S1402X_ENUM_DECL(s1402x_mpcm_slot1, S1402X_IN1_CTL1,
			 S1402X_MPCM_SLOT_SHIFT, s1402x_mpcm_slot_text);
static S1402X_ENUM_DECL(s1402x_mpcm_slot2, S1402X_IN2_CTL1,
			 S1402X_MPCM_SLOT_SHIFT, s1402x_mpcm_slot_text);
static S1402X_ENUM_DECL(s1402x_mpcm_slot3, S1402X_IN3_CTL1,
			 S1402X_MPCM_SLOT_SHIFT, s1402x_mpcm_slot_text);
static S1402X_ENUM_DECL(s1402x_bck_pol1, S1402X_IN1_CTL1, 1,
			 s1402x_polarity_text);
static S1402X_ENUM_DECL(s1402x_bck_pol2, S1402X_IN2_CTL1, 1,
			 s1402x_polarity_text);
static S1402X_ENUM_DECL(s1402x_bck_pol3, S1402X_IN3_CTL1, 1,
			 s1402x_polarity_text);
static S1402X_ENUM_DECL(s1402x_lrck_pol1, S1402X_IN1_CTL2, 4,
			 s1402x_polarity_text);
static S1402X_ENUM_DECL(s1402x_lrck_pol2, S1402X_IN2_CTL2, 4,
			 s1402x_polarity_text);
static S1402X_ENUM_DECL(s1402x_lrck_pol3, S1402X_IN3_CTL2, 4,
			 s1402x_polarity_text);
static S1402X_ENUM_DECL(s1402x_audio_mode1, S1402X_IN1_CTL1, 0,
			 s1402x_audio_mode_text);
static S1402X_ENUM_DECL(s1402x_audio_mode2, S1402X_IN2_CTL1, 0,
			 s1402x_audio_mode_text);
static S1402X_ENUM_DECL(s1402x_audio_mode3, S1402X_IN3_CTL1, 0,
			 s1402x_audio_mode_text);
static S1402X_ENUM_DECL(s1402x_xfs1, S1402X_IN1_CTL2,
			 S1402X_I2S_XFS_SHIFT, s1402x_xfs_text);
static S1402X_ENUM_DECL(s1402x_xfs2, S1402X_IN2_CTL2,
			 S1402X_I2S_XFS_SHIFT, s1402x_xfs_text);
static S1402X_ENUM_DECL(s1402x_xfs3, S1402X_IN3_CTL2,
			 S1402X_I2S_XFS_SHIFT, s1402x_xfs_text);
static S1402X_ENUM_DECL(s1402x_i2s_format1, S1402X_IN1_CTL2,
			 S1402X_I2S_DF_SHIFT, s1402x_i2s_format_text);
static S1402X_ENUM_DECL(s1402x_i2s_format2, S1402X_IN2_CTL2,
			 S1402X_I2S_DF_SHIFT, s1402x_i2s_format_text);
static S1402X_ENUM_DECL(s1402x_i2s_format3, S1402X_IN3_CTL2,
			 S1402X_I2S_DF_SHIFT, s1402x_i2s_format_text);
static S1402X_ENUM_DECL(s1402x_data_length1, S1402X_IN1_CTL2,
			 S1402X_I2S_DL_SHIFT, s1402x_data_length_text);
static S1402X_ENUM_DECL(s1402x_data_length2, S1402X_IN2_CTL2,
			 S1402X_I2S_DL_SHIFT, s1402x_data_length_text);
static S1402X_ENUM_DECL(s1402x_data_length3, S1402X_IN3_CTL2,
			 S1402X_I2S_DL_SHIFT, s1402x_data_length_text);
static S1402X_ENUM_DECL(s1402x_pcm_delay1, S1402X_IN1_CTL3,
			 S1402X_PCM_DAD_SHIFT, s1402x_pcm_delay_text);
static S1402X_ENUM_DECL(s1402x_pcm_delay2, S1402X_IN2_CTL3,
			 S1402X_PCM_DAD_SHIFT, s1402x_pcm_delay_text);
static S1402X_ENUM_DECL(s1402x_pcm_delay3, S1402X_IN3_CTL3,
			 S1402X_PCM_DAD_SHIFT, s1402x_pcm_delay_text);
static S1402X_ENUM_DECL(s1402x_pcm_format1, S1402X_IN1_CTL3,
			 S1402X_PCM_DF_SHIFT, s1402x_pcm_format_text);
static S1402X_ENUM_DECL(s1402x_pcm_format2, S1402X_IN2_CTL3,
			 S1402X_PCM_DF_SHIFT, s1402x_pcm_format_text);
static S1402X_ENUM_DECL(s1402x_pcm_format3, S1402X_IN3_CTL3,
			 S1402X_PCM_DF_SHIFT, s1402x_pcm_format_text);
static S1402X_ENUM_DECL(s1402x_ch3_record, S1402X_HQ_CTL, 3,
			 s1402x_off_on_text);
static S1402X_ENUM_DECL(s1402x_mcko, S1402X_HQ_CTL, 2,
			 s1402x_off_on_text);
static S1402X_ENUM_DECL(s1402x_bck4, S1402X_HQ_CTL, 1, s1402x_bck4_text);
static S1402X_ENUM_DECL(s1402x_hq, S1402X_HQ_CTL, 0, s1402x_off_on_text);
static S1402X_ENUM_DECL(s1402x_rmix1, S1402X_RMIX_CTL, 3,
			 s1402x_off_on_text);
static S1402X_ENUM_DECL(s1402x_rmix2, S1402X_RMIX_CTL, 7,
			 s1402x_off_on_text);
static S1402X_ENUM_DECL(s1402x_dout1, S1402X_DOUTMX1, 0,
			 s1402x_dout1_text);
static S1402X_ENUM_DECL(s1402x_dout2, S1402X_DOUTMX1, 3,
			 s1402x_dout2_text);
static S1402X_ENUM_DECL(s1402x_dout3, S1402X_DOUTMX2, 0,
			 s1402x_dout3_text);
static S1402X_ENUM_DECL(s1402x_mix1, S1402X_DMIX1, 3,
			 s1402x_off_on_text);
static S1402X_ENUM_DECL(s1402x_mix2, S1402X_DMIX1, 7,
			 s1402x_off_on_text);
static S1402X_ENUM_DECL(s1402x_mix3, S1402X_DMIX2, 3,
			 s1402x_off_on_text);
static S1402X_ENUM_DECL(s1402x_mix4, S1402X_DMIX2, 7,
			 s1402x_off_on_text);
static S1402X_ENUM_DECL(s1402x_mixer, S1402X_DIG_EN, 3,
			 s1402x_off_on_text);
static S1402X_ENUM_DECL(s1402x_ap0, S1402X_DIG_EN, 0,
			 s1402x_off_on_text);
static S1402X_ENUM_DECL(s1402x_src2, S1402X_DIG_EN, 1,
			 s1402x_off_on_text);
static S1402X_ENUM_DECL(s1402x_src3, S1402X_DIG_EN, 2,
			 s1402x_off_on_text);
static S1402X_ENUM_DECL(s1402x_ap1, S1402X_DIG_EN, 4,
			 s1402x_off_on_text);
static S1402X_ENUM_DECL(s1402x_cp1, S1402X_DIG_EN, 5,
			 s1402x_off_on_text);
static S1402X_ENUM_DECL(s1402x_alc_window, S1402X_ALC_CTL, 4,
			 s1402x_alc_window_text);
static S1402X_ENUM_DECL(s1402x_alc_mode, S1402X_ALC_CTL, 0,
			 s1402x_alc_mode_text);
static S1402X_ENUM_DECL(s1402x_alc_enable, S1402X_ALC_CTL, 3,
			 s1402x_off_on_text);
static S1402X_ENUM_DECL(s1402x_alc_limiter, S1402X_ALC_CTL, 2,
			 s1402x_off_on_text);
static S1402X_ENUM_DECL(s1402x_alc_start_gain, S1402X_ALC_HLD, 6,
			 s1402x_off_on_text);
static S1402X_ENUM_DECL(s1402x_alc_path, S1402X_ALC_HLD, 7,
			 s1402x_alc_path_text);
static S1402X_ENUM_DECL(s1402x_noise_gate, S1402X_ALC_NG, 7,
			 s1402x_off_on_text);

static const struct snd_kcontrol_new s1402x_controls[] = {
	SOC_SINGLE_TLV("RMIX1_LVL", S1402X_RMIX_CTL, 4, 7, 0,
		       s1402x_mix_tlv),
	SOC_SINGLE_TLV("RMIX2_LVL", S1402X_RMIX_CTL, 0, 7, 0,
		       s1402x_mix_tlv),
	SOC_SINGLE_TLV("MIX1_LVL", S1402X_DMIX1, 0, 7, 0, s1402x_mix_tlv),
	SOC_SINGLE_TLV("MIX2_LVL", S1402X_DMIX1, 4, 7, 0, s1402x_mix_tlv),
	SOC_SINGLE_TLV("MIX3_LVL", S1402X_DMIX2, 0, 7, 0, s1402x_mix_tlv),
	SOC_SINGLE_TLV("MIX4_LVL", S1402X_DMIX2, 4, 7, 0, s1402x_mix_tlv),
	SOC_SINGLE_TLV("ALC NG HYS", S1402X_ALC_CTL, 6, 3, 0,
		       s1402x_alc_ng_hys_tlv),
	SOC_SINGLE_RANGE_TLV("ALC Max Gain", S1402X_ALC_GA1, 0,
			     0x6c, 0x9c, 0, s1402x_alc_max_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC Min Gain", S1402X_ALC_GA2, 0,
			     0, 0x6c, 0, s1402x_alc_min_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC Start Gain Left", S1402X_ALC_SGL, 0,
			     0x6c, 0x9c, 0, s1402x_alc_max_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC Start Gain Right", S1402X_ALC_SGR, 0,
			     0x6c, 0x9c, 0, s1402x_alc_max_gain_tlv),
	SOC_SINGLE_TLV("ALC LVL Left", S1402X_ALC_LVL, 0, 0x1f, 0,
		       s1402x_alc_lvl_tlv),
	SOC_SINGLE_TLV("ALC LVL Right", S1402X_ALC_LVR, 0, 0x1f, 0,
		       s1402x_alc_lvl_tlv),
	SOC_SINGLE_TLV("ALC Noise Gate Threshold", S1402X_ALC_NG, 0,
		       0x1f, 0, s1402x_alc_ng_th_tlv),

	SOC_ENUM("CH1 Master PCM Sample Rate", s1402x_mpcm_rate1),
	SOC_ENUM("CH2 Master PCM Sample Rate", s1402x_mpcm_rate2),
	SOC_ENUM("CH3 Master PCM Sample Rate", s1402x_mpcm_rate3),
	SOC_ENUM("CH1 Master PCM Slot", s1402x_mpcm_slot1),
	SOC_ENUM("CH2 Master PCM Slot", s1402x_mpcm_slot2),
	SOC_ENUM("CH3 Master PCM Slot", s1402x_mpcm_slot3),
	SOC_ENUM("CH1 BCLK Polarity", s1402x_bck_pol1),
	SOC_ENUM("CH2 BCLK Polarity", s1402x_bck_pol2),
	SOC_ENUM("CH3 BCLK Polarity", s1402x_bck_pol3),
	SOC_ENUM("CH1 LRCLK Polarity", s1402x_lrck_pol1),
	SOC_ENUM("CH2 LRCLK Polarity", s1402x_lrck_pol2),
	SOC_ENUM("CH3 LRCLK Polarity", s1402x_lrck_pol3),
	SOC_ENUM("CH1 Input Audio Mode", s1402x_audio_mode1),
	SOC_ENUM("CH2 Input Audio Mode", s1402x_audio_mode2),
	SOC_ENUM("CH3 Input Audio Mode", s1402x_audio_mode3),
	SOC_ENUM("CH1 XFS", s1402x_xfs1),
	SOC_ENUM("CH2 XFS", s1402x_xfs2),
	SOC_ENUM("CH3 XFS", s1402x_xfs3),
	SOC_ENUM("CH1 I2S Format", s1402x_i2s_format1),
	SOC_ENUM("CH2 I2S Format", s1402x_i2s_format2),
	SOC_ENUM("CH3 I2S Format", s1402x_i2s_format3),
	SOC_ENUM("CH1 I2S Data Length", s1402x_data_length1),
	SOC_ENUM("CH2 I2S Data Length", s1402x_data_length2),
	SOC_ENUM("CH3 I2S Data Length", s1402x_data_length3),
	SOC_ENUM("CH1 PCM DAD", s1402x_pcm_delay1),
	SOC_ENUM("CH2 PCM DAD", s1402x_pcm_delay2),
	SOC_ENUM("CH3 PCM DAD", s1402x_pcm_delay3),
	SOC_ENUM("CH1 PCM Data Format", s1402x_pcm_format1),
	SOC_ENUM("CH2 PCM Data Format", s1402x_pcm_format2),
	SOC_ENUM("CH3 PCM Data Format", s1402x_pcm_format3),
	SOC_ENUM("CH3 Rec En", s1402x_ch3_record),
	SOC_ENUM("MCKO En", s1402x_mcko),
	SOC_ENUM("RMIX1 En", s1402x_rmix1),
	SOC_ENUM("RMIX2 En", s1402x_rmix2),
	SOC_ENUM("BCK4 Output Selection", s1402x_bck4),
	SOC_ENUM("HQ En", s1402x_hq),
	SOC_ENUM("CH1 DOUT Select", s1402x_dout1),
	SOC_ENUM("CH2 DOUT Select", s1402x_dout2),
	SOC_ENUM("CH3 DOUT Select", s1402x_dout3),
	SOC_ENUM("CH1 Mixer En", s1402x_mix1),
	SOC_ENUM("CH2 Mixer En", s1402x_mix2),
	SOC_ENUM("CH3 Mixer En", s1402x_mix3),
	SOC_ENUM("CH4 Mixer En", s1402x_mix4),
	SOC_ENUM("Mixer En", s1402x_mixer),
	SOC_ENUM("AP0 En", s1402x_ap0),
	SOC_ENUM("AP1 En", s1402x_ap1),
	SOC_ENUM("CP1 En", s1402x_cp1),
	SOC_ENUM("SRC2 En", s1402x_src2),
	SOC_ENUM("SRC3 En", s1402x_src3),
	SOC_ENUM("ALC Window Length", s1402x_alc_window),
	SOC_ENUM("ALC Mode", s1402x_alc_mode),
	SOC_ENUM("ALC En", s1402x_alc_enable),
	SOC_ENUM("ALC Limiter Mode En", s1402x_alc_limiter),
	SOC_ENUM("ALC Start Gain En", s1402x_alc_start_gain),
	SOC_SINGLE("ALC Hold Time", S1402X_ALC_HLD, 0, 0x1f, 0),
	SOC_SINGLE("ALC Attack Time", S1402X_ALC_ATK, 0, 0x1f, 0),
	SOC_SINGLE("ALC Decay Time", S1402X_ALC_DCY, 0, 0x1f, 0),
	SOC_ENUM("ALC Path", s1402x_alc_path),
	SOC_ENUM("ALC Noise Gate En", s1402x_noise_gate),
};

static const struct snd_soc_component_driver s1402x_component = {
	.name = "s1402x",
	.controls = s1402x_controls,
	.num_controls = ARRAY_SIZE(s1402x_controls),
	.endianness = 1,
};

static int s1402x_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct s1402x_priv *s1402x;
	void __iomem *base;
	int ret;

	s1402x = devm_kzalloc(dev, sizeof(*s1402x), GFP_KERNEL);
	if (!s1402x)
		return -ENOMEM;

	s1402x->dev = dev;
	s1402x->clks[0].id = "dout";
	s1402x->clks[1].id = "mixer";
	mutex_init(&s1402x->stream_lock);
	platform_set_drvdata(pdev, s1402x);

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	s1402x->regmap = devm_regmap_init_mmio(dev, base,
					       &s1402x_regmap_config);
	if (IS_ERR(s1402x->regmap))
		return dev_err_probe(dev, PTR_ERR(s1402x->regmap),
				     "failed to create regmap\n");

	ret = devm_clk_bulk_get(dev, ARRAY_SIZE(s1402x->clks), s1402x->clks);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get clocks\n");

	s1402x->reset = devm_reset_control_get_exclusive(dev, "mixer");
	if (IS_ERR(s1402x->reset))
		return dev_err_probe(dev, PTR_ERR(s1402x->reset),
				     "failed to get reset\n");

	s1402x->pmu = syscon_regmap_lookup_by_phandle(dev->of_node,
						      "samsung,pmu-syscon");
	if (IS_ERR(s1402x->pmu))
		return dev_err_probe(dev, PTR_ERR(s1402x->pmu),
				     "failed to get PMU syscon\n");

	s1402x->bt_fm_combo = device_property_read_bool(dev,
							"samsung,bt-fm-combo");
	s1402x->bck4_mcko = device_property_read_bool(dev,
						      "samsung,bck4-mcko");

	s1402x->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(s1402x->pinctrl)) {
		ret = PTR_ERR(s1402x->pinctrl);
		if (ret != -ENODEV)
			return dev_err_probe(dev, ret, "failed to get pinctrl\n");
		s1402x->pinctrl = NULL;
	}
	s1402x->pins_default = s1402x_lookup_state(s1402x, PINCTRL_STATE_DEFAULT);
	s1402x->pins_idle = s1402x_lookup_state(s1402x, PINCTRL_STATE_IDLE);
	s1402x->pins_bt = s1402x_lookup_state(s1402x, "bt");
	s1402x->pins_bt_idle = s1402x_lookup_state(s1402x, "bt-idle");
	s1402x->pins_fm = s1402x_lookup_state(s1402x, "fm");
	s1402x->pins_fm_idle = s1402x_lookup_state(s1402x, "fm-idle");

	regcache_cache_only(s1402x->regmap, true);
	ret = reset_control_assert(s1402x->reset);
	if (ret)
		return dev_err_probe(dev, ret, "failed to assert reset\n");

	pm_runtime_set_autosuspend_delay(dev, S1402X_AUTOSUSPEND_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_enable(dev);

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		goto err_pm;

	ret = s1402x_hw_init(s1402x);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
	if (ret)
		goto err_pm;

	ret = devm_snd_soc_register_component(dev, &s1402x_component,
					      s1402x_dais,
					      ARRAY_SIZE(s1402x_dais));
	if (ret)
		goto err_pm;

	return 0;

err_pm:
	pm_runtime_disable(dev);
	if (!pm_runtime_status_suspended(dev))
		s1402x_runtime_suspend(dev);
	return ret;
}

static void s1402x_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	pm_runtime_disable(dev);
	if (!pm_runtime_status_suspended(dev))
		s1402x_runtime_suspend(dev);
}

static const struct of_device_id s1402x_of_match[] = {
	{ .compatible = "samsung,s1402x" },
	{ }
};
MODULE_DEVICE_TABLE(of, s1402x_of_match);

static const struct dev_pm_ops s1402x_pm_ops = {
	SET_RUNTIME_PM_OPS(s1402x_runtime_suspend, s1402x_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
};

static struct platform_driver s1402x_driver = {
	.probe = s1402x_probe,
	.remove = s1402x_remove,
	.driver = {
		.name = "s1402x",
		.of_match_table = s1402x_of_match,
		.pm = &s1402x_pm_ops,
	},
};
module_platform_driver(s1402x_driver);

MODULE_DESCRIPTION("Samsung S1402X audio mixer driver");
MODULE_AUTHOR("Sean Hoyt <seanhoyt963@gmail.com>");
MODULE_LICENSE("GPL");
