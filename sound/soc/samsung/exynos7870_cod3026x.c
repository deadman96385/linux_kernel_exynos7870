// SPDX-License-Identifier: GPL-2.0-only
/* Samsung Exynos7870 machine driver for the COD3026X audio codec */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <sound/jack.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <dt-bindings/sound/samsung,s1402x.h>

#include "i2s.h"

#define EXYNOS7870_CODEC_INDEX	1

enum exynos7870_link_id {
	EXYNOS7870_LINK_PRIMARY,
	EXYNOS7870_LINK_SECONDARY,
	EXYNOS7870_LINK_AMP,
	EXYNOS7870_LINK_VOICE,
	EXYNOS7870_LINK_BLUETOOTH,
	EXYNOS7870_LINK_FM,
	EXYNOS7870_LINK_CP_AMP,
	EXYNOS7870_NUM_LINKS,
};

struct exynos7870_audio {
	struct snd_soc_card card;
	struct snd_soc_dai_link links[EXYNOS7870_NUM_LINKS];
	struct snd_soc_dai_link_component cpus[EXYNOS7870_NUM_LINKS];
	struct snd_soc_dai_link_component platforms[EXYNOS7870_NUM_LINKS];
	struct snd_soc_dai_link_component primary_codecs[2];
	struct snd_soc_dai_link_component secondary_codecs[2];
	struct snd_soc_dai_link_component amp_codecs[1];
	struct snd_soc_dai_link_component voice_codecs[2];
	struct snd_soc_dai_link_component bluetooth_codecs[1];
	struct snd_soc_dai_link_component fm_codecs[2];
	struct snd_soc_dai_link_component cp_amp_codecs[1];
	struct snd_soc_jack headset_jack;
};

static struct snd_soc_jack_pin exynos7870_headset_pins[] = {
	{
		.pin = "Headphone",
		.mask = SND_JACK_HEADPHONE,
	}, {
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},
};

static const struct snd_soc_dapm_widget exynos7870_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_SPK("Earpiece", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_MIC("Main Mic", NULL),
	SND_SOC_DAPM_MIC("Sub Mic", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_soc_dapm_widget exynos7870_ext_widgets[] = {
	SND_SOC_DAPM_INPUT("Modem Downlink"),
	SND_SOC_DAPM_OUTPUT("Modem Uplink"),
	SND_SOC_DAPM_INPUT("Bluetooth RX"),
	SND_SOC_DAPM_OUTPUT("Bluetooth TX"),
	SND_SOC_DAPM_INPUT("FM Radio"),
};

static const struct snd_soc_dapm_route exynos7870_ext_routes[] = {
	{ "Voice Call Playback", NULL, "Modem Downlink" },
	{ "Modem Uplink", NULL, "Voice Call Capture" },
	{ "Bluetooth Playback", NULL, "Bluetooth RX" },
	{ "Bluetooth TX", NULL, "Bluetooth Capture" },
	{ "FM Playback", NULL, "FM Radio" },
};

static const struct snd_soc_component_driver exynos7870_component = {
	.name = "exynos7870-audio",
	.dapm_widgets = exynos7870_ext_widgets,
	.num_dapm_widgets = ARRAY_SIZE(exynos7870_ext_widgets),
	.dapm_routes = exynos7870_ext_routes,
	.num_dapm_routes = ARRAY_SIZE(exynos7870_ext_routes),
};

static const struct snd_kcontrol_new exynos7870_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Earpiece"),
	SOC_DAPM_PIN_SWITCH("Speaker"),
	SOC_DAPM_PIN_SWITCH("Main Mic"),
	SOC_DAPM_PIN_SWITCH("Sub Mic"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Modem Downlink"),
	SOC_DAPM_PIN_SWITCH("Modem Uplink"),
	SOC_DAPM_PIN_SWITCH("Bluetooth RX"),
	SOC_DAPM_PIN_SWITCH("Bluetooth TX"),
	SOC_DAPM_PIN_SWITCH("FM Radio"),
};

#define EXYNOS7870_C2C_PARAMS(_name, _format, _rate_min, _rate_max) { \
	.stream_name = (_name), \
	.formats = (_format), \
	.rate_min = (_rate_min), \
	.rate_max = (_rate_max), \
	.channels_min = 2, \
	.channels_max = 2, \
}

static const struct snd_soc_pcm_stream exynos7870_voice_params[] = {
	EXYNOS7870_C2C_PARAMS("8 kHz S16", SNDRV_PCM_FMTBIT_S16_LE,
			      8000, 8000),
	EXYNOS7870_C2C_PARAMS("8 kHz S24", SNDRV_PCM_FMTBIT_S24_LE,
			      8000, 8000),
	EXYNOS7870_C2C_PARAMS("16 kHz S16", SNDRV_PCM_FMTBIT_S16_LE,
			      16000, 16000),
	EXYNOS7870_C2C_PARAMS("16 kHz S24", SNDRV_PCM_FMTBIT_S24_LE,
			      16000, 16000),
	EXYNOS7870_C2C_PARAMS("48 kHz S16", SNDRV_PCM_FMTBIT_S16_LE,
			      48000, 48000),
	EXYNOS7870_C2C_PARAMS("48 kHz S24", SNDRV_PCM_FMTBIT_S24_LE,
			      48000, 48000),
};

static const struct snd_soc_pcm_stream exynos7870_bluetooth_params[] = {
	EXYNOS7870_C2C_PARAMS("8 kHz S16", SNDRV_PCM_FMTBIT_S16_LE,
			      8000, 8000),
	EXYNOS7870_C2C_PARAMS("8 kHz S24", SNDRV_PCM_FMTBIT_S24_LE,
			      8000, 8000),
	EXYNOS7870_C2C_PARAMS("16 kHz S16", SNDRV_PCM_FMTBIT_S16_LE,
			      16000, 16000),
	EXYNOS7870_C2C_PARAMS("16 kHz S24", SNDRV_PCM_FMTBIT_S24_LE,
			      16000, 16000),
};

static const struct snd_soc_pcm_stream exynos7870_fm_params[] = {
	EXYNOS7870_C2C_PARAMS("48 kHz S16", SNDRV_PCM_FMTBIT_S16_LE,
			      48000, 48000),
	EXYNOS7870_C2C_PARAMS("48 kHz S24", SNDRV_PCM_FMTBIT_S24_LE,
			      48000, 48000),
};

#define EXYNOS7870_EXT_STREAM(_name, _rates) { \
	.stream_name = (_name), \
	.channels_min = 2, \
	.channels_max = 2, \
	.rates = (_rates), \
	.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE, \
}

static struct snd_soc_dai_driver exynos7870_ext_dais[] = {
	{
		.name = "Voice Call",
		.playback = EXYNOS7870_EXT_STREAM("Voice Call Playback",
			SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
			SNDRV_PCM_RATE_48000),
		.capture = EXYNOS7870_EXT_STREAM("Voice Call Capture",
			SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
			SNDRV_PCM_RATE_48000),
		.symmetric_rate = 1,
		.symmetric_channels = 1,
		.symmetric_sample_bits = 1,
	}, {
		.name = "Bluetooth",
		.playback = EXYNOS7870_EXT_STREAM("Bluetooth Playback",
			SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000),
		.capture = EXYNOS7870_EXT_STREAM("Bluetooth Capture",
			SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000),
		.symmetric_rate = 1,
		.symmetric_channels = 1,
		.symmetric_sample_bits = 1,
	}, {
		.name = "FM",
		.playback = EXYNOS7870_EXT_STREAM("FM Playback",
			SNDRV_PCM_RATE_48000),
	},
};

static int exynos7870_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *mixer_dai = snd_soc_rtd_to_codec(rtd, 0);
	unsigned int rate = params_rate(params);
	unsigned int bfs, rfs;
	int ret;

	switch (rate) {
	case 48000:
		rfs = 512;
		bfs = params_width(params) > 16 ? 64 : 32;
		break;
	case 192000:
		rfs = 128;
		bfs = 64;
		break;
	default:
		return -EINVAL;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_CDCLK, rfs,
				     SND_SOC_CLOCK_OUT);
	if (ret)
		return ret;

	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_OPCLK, 0,
				     SAMSUNG_I2S_OPCLK_PCLK);
	if (ret)
		return ret;

	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_RCLKSRC_1, 0,
				     SND_SOC_CLOCK_IN);
	if (ret)
		return ret;

	ret = snd_soc_dai_set_clkdiv(cpu_dai, SAMSUNG_I2S_DIV_BCLK, bfs);
	if (ret)
		return ret;

	return snd_soc_dai_set_bclk_ratio(mixer_dai, bfs);
}

static const struct snd_soc_ops exynos7870_ops = {
	.hw_params = exynos7870_hw_params,
};

static int exynos7870_link_init(struct snd_soc_pcm_runtime *rtd)
{
	struct exynos7870_audio *audio = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_component *codec =
		snd_soc_rtd_to_codec(rtd, EXYNOS7870_CODEC_INDEX)->component;
	int ret;

	ret = snd_soc_card_jack_new_pins(rtd->card, "Headset Jack",
					 SND_JACK_HEADSET |
					 SND_JACK_BTN_0 | SND_JACK_BTN_1 |
					 SND_JACK_BTN_2 | SND_JACK_BTN_3,
					 &audio->headset_jack,
					 exynos7870_headset_pins,
					 ARRAY_SIZE(exynos7870_headset_pins));
	if (ret)
		return ret;

	return snd_soc_component_set_jack(codec, &audio->headset_jack, NULL);
}

static void exynos7870_link_exit(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *codec =
		snd_soc_rtd_to_codec(rtd, EXYNOS7870_CODEC_INDEX)->component;

	snd_soc_component_set_jack(codec, NULL, NULL);
}

static int exynos7870_parse_dai(struct device *dev, const char *name,
				struct snd_soc_dai_link_component *dlc)
{
	struct device_node *child;
	struct of_phandle_args args;
	int ret;

	child = of_get_child_by_name(dev->of_node, name);
	if (!child)
		return dev_err_probe(dev, -EINVAL, "missing %s node\n", name);

	ret = of_parse_phandle_with_args(child, "sound-dai",
					 "#sound-dai-cells", 0, &args);
	of_node_put(child);
	if (ret)
		return dev_err_probe(dev, ret, "invalid %s sound-dai\n", name);

	ret = snd_soc_get_dlc(&args, dlc);
	if (ret) {
		of_node_put(args.np);
		return dev_err_probe(dev, ret, "failed to resolve %s DAI\n",
				     name);
	}

	return 0;
}

static int exynos7870_derive_dai(struct device *dev,
				 const struct snd_soc_dai_link_component *parent,
		unsigned int id, struct snd_soc_dai_link_component *dlc,
		const char *name)
{
	struct of_phandle_args args = {
		.np = parent->of_node,
		.args_count = 1,
		.args[0] = id,
	};
	int ret;

	ret = snd_soc_get_dlc(&args, dlc);
	if (ret)
		return dev_err_probe(dev, ret, "failed to resolve %s DAI\n",
				     name);

	return 0;
}

static void exynos7870_put_dai_nodes(void *data)
{
	struct exynos7870_audio *audio = data;

	of_node_put(audio->cpus[EXYNOS7870_LINK_PRIMARY].of_node);
	of_node_put(audio->cpus[EXYNOS7870_LINK_SECONDARY].of_node);
	of_node_put(audio->cpus[EXYNOS7870_LINK_AMP].of_node);
	of_node_put(audio->primary_codecs[0].of_node);
	of_node_put(audio->primary_codecs[1].of_node);
}

static void exynos7870_init_link(struct snd_soc_dai_link *link,
				 const char *name,
				 struct snd_soc_dai_link_component *cpu,
				 struct snd_soc_dai_link_component *platform,
				 struct snd_soc_dai_link_component *codecs,
				 unsigned int num_codecs)
{
	link->name = name;
	link->stream_name = name;
	link->cpus = cpu;
	link->num_cpus = 1;
	link->platforms = platform;
	link->num_platforms = 1;
	link->codecs = codecs;
	link->num_codecs = num_codecs;
	link->dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC;
	link->ops = &exynos7870_ops;
}

static void exynos7870_init_c2c_link(struct snd_soc_dai_link *link,
				     const char *name,
		struct snd_soc_dai_link_component *cpu,
		struct snd_soc_dai_link_component *codecs,
		unsigned int num_codecs,
		const struct snd_soc_pcm_stream *params,
		unsigned int num_params)
{
	link->name = name;
	link->stream_name = name;
	link->cpus = cpu;
	link->num_cpus = 1;
	link->codecs = codecs;
	link->num_codecs = num_codecs;
	link->dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC;
	link->c2c_params = params;
	link->num_c2c_params = num_params;
	link->ignore_suspend = 1;
}

static int exynos7870_audio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct exynos7870_audio *audio;
	struct device_node *amp_node;
	struct snd_soc_card *card;
	bool has_amp;
	int ret;

	audio = devm_kzalloc(dev, sizeof(*audio), GFP_KERNEL);
	if (!audio)
		return -ENOMEM;

	card = &audio->card;
	card->driver_name = "exynos7870";

	ret = exynos7870_parse_dai(dev, "cpu",
				   &audio->cpus[EXYNOS7870_LINK_PRIMARY]);
	if (ret)
		return ret;

	ret = exynos7870_parse_dai(dev, "cpu-secondary",
				   &audio->cpus[EXYNOS7870_LINK_SECONDARY]);
	if (ret)
		goto err_put_cpu;

	amp_node = of_get_child_by_name(dev->of_node, "cpu-amp");
	has_amp = !!amp_node;
	of_node_put(amp_node);
	if (has_amp) {
		ret = exynos7870_parse_dai(dev, "cpu-amp",
					   &audio->cpus[EXYNOS7870_LINK_AMP]);
		if (ret)
			goto err_put_secondary;
	}

	ret = exynos7870_parse_dai(dev, "mixer",
				   &audio->primary_codecs[0]);
	if (ret)
		goto err_put_amp;

	ret = exynos7870_parse_dai(dev, "codec",
				   &audio->primary_codecs[1]);
	if (ret)
		goto err_put_mixer;

	audio->secondary_codecs[0] = audio->primary_codecs[0];
	audio->secondary_codecs[1] = audio->primary_codecs[1];

	if (has_amp) {
		ret = exynos7870_derive_dai(dev, &audio->primary_codecs[0],
					    S1402X_DAI_AP1,
					    &audio->amp_codecs[0], "mixer AP1");
		if (ret)
			goto err_put_codec;
	}

	ret = exynos7870_derive_dai(dev, &audio->primary_codecs[0],
				    S1402X_DAI_CP0,
				    &audio->voice_codecs[0], "mixer CP0");
	if (ret)
		goto err_put_codec;

	ret = exynos7870_derive_dai(dev, &audio->primary_codecs[1], 1,
				    &audio->voice_codecs[1], "codec AIF2");
	if (ret)
		goto err_put_codec;

	ret = exynos7870_derive_dai(dev, &audio->primary_codecs[0],
				    S1402X_DAI_BT,
				    &audio->bluetooth_codecs[0], "mixer BT");
	if (ret)
		goto err_put_codec;

	ret = exynos7870_derive_dai(dev, &audio->primary_codecs[0],
				    S1402X_DAI_FM,
				    &audio->fm_codecs[0], "mixer FM");
	if (ret)
		goto err_put_codec;
	audio->fm_codecs[1] = audio->primary_codecs[1];

	ret = exynos7870_derive_dai(dev, &audio->primary_codecs[0],
				    S1402X_DAI_CP1,
				    &audio->cp_amp_codecs[0], "mixer CP1");
	if (ret)
		goto err_put_codec;

	ret = devm_add_action_or_reset(dev, exynos7870_put_dai_nodes, audio);
	if (ret)
		return ret;

	audio->platforms[EXYNOS7870_LINK_PRIMARY].of_node =
		audio->cpus[EXYNOS7870_LINK_PRIMARY].of_node;
	audio->platforms[EXYNOS7870_LINK_SECONDARY].of_node =
		audio->cpus[EXYNOS7870_LINK_SECONDARY].of_node;
	if (has_amp)
		audio->platforms[EXYNOS7870_LINK_AMP].of_node =
			audio->cpus[EXYNOS7870_LINK_AMP].of_node;
	audio->cpus[EXYNOS7870_LINK_VOICE].of_node = dev->of_node;
	audio->cpus[EXYNOS7870_LINK_VOICE].dai_name = "Voice Call";
	audio->cpus[EXYNOS7870_LINK_BLUETOOTH].of_node = dev->of_node;
	audio->cpus[EXYNOS7870_LINK_BLUETOOTH].dai_name = "Bluetooth";
	audio->cpus[EXYNOS7870_LINK_FM].of_node = dev->of_node;
	audio->cpus[EXYNOS7870_LINK_FM].dai_name = "FM";
	audio->cpus[EXYNOS7870_LINK_CP_AMP] =
		audio->cpus[EXYNOS7870_LINK_VOICE];

	exynos7870_init_link(&audio->links[EXYNOS7870_LINK_PRIMARY],
			     "Primary",
			     &audio->cpus[EXYNOS7870_LINK_PRIMARY],
			     &audio->platforms[EXYNOS7870_LINK_PRIMARY],
			     audio->primary_codecs,
			     ARRAY_SIZE(audio->primary_codecs));
	audio->links[EXYNOS7870_LINK_PRIMARY].init = exynos7870_link_init;
	audio->links[EXYNOS7870_LINK_PRIMARY].exit = exynos7870_link_exit;

	exynos7870_init_link(&audio->links[EXYNOS7870_LINK_SECONDARY],
			     "Secondary",
			     &audio->cpus[EXYNOS7870_LINK_SECONDARY],
			     &audio->platforms[EXYNOS7870_LINK_SECONDARY],
			     audio->secondary_codecs,
			     ARRAY_SIZE(audio->secondary_codecs));
	if (has_amp) {
		exynos7870_init_link(&audio->links[EXYNOS7870_LINK_AMP],
				     "Amplifier",
				     &audio->cpus[EXYNOS7870_LINK_AMP],
				     &audio->platforms[EXYNOS7870_LINK_AMP],
				     audio->amp_codecs,
				     ARRAY_SIZE(audio->amp_codecs));
	} else {
		audio->links[EXYNOS7870_LINK_AMP].ignore = true;
	}

	exynos7870_init_c2c_link(&audio->links[EXYNOS7870_LINK_VOICE],
				 "Voice Call",
				 &audio->cpus[EXYNOS7870_LINK_VOICE],
				 audio->voice_codecs,
				 ARRAY_SIZE(audio->voice_codecs),
				 exynos7870_voice_params,
				 ARRAY_SIZE(exynos7870_voice_params));
	exynos7870_init_c2c_link(&audio->links[EXYNOS7870_LINK_BLUETOOTH],
				 "Bluetooth",
				 &audio->cpus[EXYNOS7870_LINK_BLUETOOTH],
				 audio->bluetooth_codecs,
				 ARRAY_SIZE(audio->bluetooth_codecs),
				 exynos7870_bluetooth_params,
				 ARRAY_SIZE(exynos7870_bluetooth_params));
	exynos7870_init_c2c_link(&audio->links[EXYNOS7870_LINK_FM], "FM",
				 &audio->cpus[EXYNOS7870_LINK_FM],
				 audio->fm_codecs,
				 ARRAY_SIZE(audio->fm_codecs),
				 exynos7870_fm_params,
				 ARRAY_SIZE(exynos7870_fm_params));
	exynos7870_init_c2c_link(&audio->links[EXYNOS7870_LINK_CP_AMP],
				 "Voice Amplifier",
				 &audio->cpus[EXYNOS7870_LINK_CP_AMP],
				 audio->cp_amp_codecs,
				 ARRAY_SIZE(audio->cp_amp_codecs),
				 exynos7870_voice_params,
				 ARRAY_SIZE(exynos7870_voice_params));

	card->owner = THIS_MODULE;
	card->dev = dev;
	card->dai_link = audio->links;
	card->num_links = ARRAY_SIZE(audio->links);
	card->controls = exynos7870_controls;
	card->num_controls = ARRAY_SIZE(exynos7870_controls);
	card->dapm_widgets = exynos7870_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(exynos7870_widgets);

	ret = snd_soc_of_parse_card_name(card, "model");
	if (ret)
		return ret;

	ret = snd_soc_of_parse_audio_routing(card, "audio-routing");
	if (ret)
		return ret;

	snd_soc_card_set_drvdata(card, audio);
	platform_set_drvdata(pdev, card);

	ret = devm_snd_soc_register_component(dev, &exynos7870_component,
					      exynos7870_ext_dais,
					      ARRAY_SIZE(exynos7870_ext_dais));
	if (ret)
		return ret;

	return devm_snd_soc_register_card(dev, card);
err_put_codec:
	of_node_put(audio->primary_codecs[1].of_node);
err_put_mixer:
	of_node_put(audio->primary_codecs[0].of_node);
err_put_amp:
	of_node_put(audio->cpus[EXYNOS7870_LINK_AMP].of_node);
err_put_secondary:
	of_node_put(audio->cpus[EXYNOS7870_LINK_SECONDARY].of_node);
err_put_cpu:
	of_node_put(audio->cpus[EXYNOS7870_LINK_PRIMARY].of_node);
	return ret;
}

static const struct of_device_id exynos7870_audio_of_match[] = {
	{ .compatible = "samsung,exynos7870-cod3026x" },
	{ }
};
MODULE_DEVICE_TABLE(of, exynos7870_audio_of_match);

static struct platform_driver exynos7870_audio_driver = {
	.probe = exynos7870_audio_probe,
	.driver = {
		.name = "exynos7870-cod3026x",
		.of_match_table = exynos7870_audio_of_match,
	},
};
module_platform_driver(exynos7870_audio_driver);

MODULE_DESCRIPTION("Samsung Exynos7870 COD3026X machine driver");
MODULE_AUTHOR("Sean Hoyt <seanhoyt963@gmail.com>");
MODULE_LICENSE("GPL");
