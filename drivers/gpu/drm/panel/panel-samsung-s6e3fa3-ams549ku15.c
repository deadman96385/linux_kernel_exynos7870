// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the Samsung AMS549KU15 panel with an S6E3FA3 controller.
 *
 * The command sequence and power timings are based on Samsung's j7y17lte
 * downstream driver.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#include "samsung-s6e3fa3-dimming/s6e3fa3_dimming.h"
#include "samsung-s6e3fa3-dimming/s6e3fa3_update.h"

#define S6E3FA3_MTP_DATE_LEN	47

struct s6e3fa3_ams549ku15 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[2];
	struct mutex lock;
	struct s6e3fa3_dimming dimming;
	u8 factory_elvss[S6E3FA3_ELVSS_PAYLOAD_LEN];
	bool calibrated;
	bool powered;
	bool adaptive_control;
	int temperature;
};

static inline struct s6e3fa3_ams549ku15 *
to_s6e3fa3_ams549ku15(struct drm_panel *panel)
{
	return container_of(panel, struct s6e3fa3_ams549ku15, panel);
}

static int s6e3fa3_ams549ku15_write(void *context, const u8 *data,
				    size_t length)
{
	struct s6e3fa3_ams549ku15 *ctx = context;
	ssize_t ret;

	ret = mipi_dsi_dcs_write_buffer(ctx->dsi, data, length);
	if (ret < 0)
		return ret;

	return ret == length ? 0 : -EIO;
}

static int s6e3fa3_ams549ku15_read(struct s6e3fa3_ams549ku15 *ctx,
				   u8 command, u8 *data, size_t length)
{
	unsigned int attempt;
	int error = -EIO;
	ssize_t ret;

	for (attempt = 0; attempt < 2; attempt++) {
		ret = mipi_dsi_set_maximum_return_packet_size(ctx->dsi,
							      length);
		if (ret < 0) {
			error = ret;
			continue;
		}

		ret = mipi_dsi_dcs_read(ctx->dsi, command, data, length);
		if (ret == length)
			return 0;

		error = ret < 0 ? ret : -EIO;
	}

	return error;
}

static bool s6e3fa3_ams549ku15_uniform(const u8 *data, size_t length,
				       u8 value)
{
	size_t i;

	for (i = 0; i < length; i++)
		if (data[i] != value)
			return false;

	return true;
}

/* The F0 and FC manufacturer keys must be enabled by the caller. */
static int s6e3fa3_ams549ku15_calibrate(struct s6e3fa3_ams549ku15 *ctx)
{
	u8 mtp_date[S6E3FA3_MTP_DATE_LEN];
	u8 hbm[S6E3FA3_HBM_MTP_LEN];
	int ret;

	if (!ctx->calibrated) {
		ctx->panel.backlight->props.max_brightness = 255;
		ret = s6e3fa3_ams549ku15_read(ctx, 0xc8, mtp_date,
					      sizeof(mtp_date));
		if (ret)
			return ret;
		ret = s6e3fa3_ams549ku15_read(ctx, 0xb6,
					     ctx->factory_elvss,
					     sizeof(ctx->factory_elvss));
		if (ret)
			return ret;
		if (s6e3fa3_ams549ku15_uniform(ctx->factory_elvss,
						      sizeof(ctx->factory_elvss),
						      0x00) ||
		    s6e3fa3_ams549ku15_uniform(ctx->factory_elvss,
						      sizeof(ctx->factory_elvss),
						      0xff))
			return -ENODATA;

		ret = s6e3fa3_dimming_init_live(&ctx->dimming, mtp_date);
		if (ret)
			return ret;
		ctx->calibrated = true;
	}

	if (ctx->dimming.hbm_valid)
		return 0;

	ret = s6e3fa3_ams549ku15_read(ctx, 0xb4, hbm, sizeof(hbm));
	if (!ret)
		ret = s6e3fa3_hbm_init_live(&ctx->dimming, hbm);
	if (ret) {
		dev_warn(&ctx->dsi->dev,
			 "HBM calibration unavailable (%d); limiting brightness to 255\n",
			 ret);
		return 0;
	}

	ctx->panel.backlight->props.max_brightness = S6E3FA3_MAX_BRIGHTNESS;
	return 0;
}

static int s6e3fa3_ams549ku15_update_locked(
	struct s6e3fa3_ams549ku15 *ctx, unsigned int brightness)
{
	struct s6e3fa3_update update;
	int ret;

	if (!ctx->calibrated)
		return -ENODATA;

	ret = s6e3fa3_update_build(&update, &ctx->dimming, brightness,
				      ctx->temperature, ctx->adaptive_control,
				      ctx->factory_elvss);
	if (ret)
		return ret;

	return s6e3fa3_update_emit(&update, s6e3fa3_ams549ku15_write, ctx);
}

static int s6e3fa3_ams549ku15_set_brightness_locked(
	struct s6e3fa3_ams549ku15 *ctx, unsigned int brightness)
{
	static const u8 key_f0_on[] = { 0xf0, 0x5a, 0x5a };
	static const u8 key_fc_on[] = { 0xfc, 0x5a, 0x5a };
	static const u8 key_f0_off[] = { 0xf0, 0xa5, 0xa5 };
	static const u8 key_fc_off[] = { 0xfc, 0xa5, 0xa5 };
	int first_error;
	int ret;

	first_error = s6e3fa3_ams549ku15_write(ctx, key_f0_on,
					      sizeof(key_f0_on));
	if (!first_error)
		first_error = s6e3fa3_ams549ku15_write(ctx, key_fc_on,
							  sizeof(key_fc_on));
	if (!first_error)
		first_error = s6e3fa3_ams549ku15_update_locked(ctx, brightness);

	ret = s6e3fa3_ams549ku15_write(ctx, key_f0_off, sizeof(key_f0_off));
	if (!first_error)
		first_error = ret;
	ret = s6e3fa3_ams549ku15_write(ctx, key_fc_off, sizeof(key_fc_off));
	if (!first_error)
		first_error = ret;

	return first_error;
}

static int s6e3fa3_ams549ku15_bl_update_status(
	struct backlight_device *backlight)
{
	struct s6e3fa3_ams549ku15 *ctx = bl_get_data(backlight);
	int ret = 0;

	mutex_lock(&ctx->lock);
	if (ctx->panel.enabled && !backlight_is_blank(backlight))
		ret = s6e3fa3_ams549ku15_set_brightness_locked(
			ctx, backlight_get_brightness(backlight));
	mutex_unlock(&ctx->lock);

	return ret;
}

static int s6e3fa3_ams549ku15_bl_get_brightness(
	struct backlight_device *backlight)
{
	return backlight->props.brightness;
}

static const struct backlight_ops s6e3fa3_ams549ku15_bl_ops = {
	.update_status = s6e3fa3_ams549ku15_bl_update_status,
	.get_brightness = s6e3fa3_ams549ku15_bl_get_brightness,
};

static int s6e3fa3_ams549ku15_on(struct s6e3fa3_ams549ku15 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	static const u8 key_f0_off[] = { 0xf0, 0xa5, 0xa5 };
	static const u8 key_fc_off[] = { 0xfc, 0xa5, 0xa5 };
	u8 id[3];
	int first_error;
	int ret;

	mipi_dsi_msleep(&dsi_ctx, 5);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);

	/* Temporary ELVSS settings required immediately after sleep-out. */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb6, 0xac, 0x29);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf5, 0x84, 0x33);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_msleep(&dsi_ctx, 40);

	if (dsi_ctx.accum_err)
		return dsi_ctx.accum_err;

	ret = s6e3fa3_ams549ku15_read(ctx, MIPI_DCS_GET_DISPLAY_ID,
				      id, sizeof(id));
	if (!ret)
		dev_info(&ctx->dsi->dev, "panel ID: %*ph\n", (int)sizeof(id), id);
	else
		dev_warn(&ctx->dsi->dev, "failed to read panel ID: %d\n", ret);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfc, 0x5a, 0x5a);
	/* Match the downstream J7 sequence: enable TE before common setup. */
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcc, 0x5c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfd, 0xb2);
	mipi_dsi_msleep(&dsi_ctx, 50);
	first_error = dsi_ctx.accum_err;
	if (first_error)
		goto keys_off;

	ret = s6e3fa3_ams549ku15_calibrate(ctx);
	if (ret) {
		dev_warn(&ctx->dsi->dev,
			 "panel calibration unavailable (%d); retaining fixed defaults\n",
			 ret);
	} else {
		ret = s6e3fa3_ams549ku15_update_locked(
			ctx, ctx->panel.backlight->props.brightness);
		if (ret)
			first_error = ret;
	}

keys_off:
	ret = s6e3fa3_ams549ku15_write(ctx, key_f0_off,
					     sizeof(key_f0_off));
	if (!first_error)
		first_error = ret;
	ret = s6e3fa3_ams549ku15_write(ctx, key_fc_off,
					     sizeof(key_fc_off));
	if (!first_error)
		first_error = ret;
	if (first_error)
		return first_error;

	return mipi_dsi_dcs_set_display_on(ctx->dsi);
}

static int s6e3fa3_ams549ku15_prepare(struct drm_panel *panel)
{
	struct s6e3fa3_ams549ku15 *ctx = to_s6e3fa3_ams549ku15(panel);
	int ret;

	mutex_lock(&ctx->lock);
	if (ctx->powered) {
		ret = 0;
		goto out;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret)
		goto err_unlock;
	ctx->powered = true;

	usleep_range(5000, 6000);

	/* Physical reset sequence: high, low, high. */
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);

out:
	mutex_unlock(&ctx->lock);
	return ret;

err_unlock:
	dev_err_probe(&ctx->dsi->dev, ret,
		      "failed to enable panel supplies\n");
	goto out;
}

static int s6e3fa3_ams549ku15_enable(struct drm_panel *panel)
{
	struct s6e3fa3_ams549ku15 *ctx = to_s6e3fa3_ams549ku15(panel);
	int ret;

	mutex_lock(&ctx->lock);
	if (!ctx->powered)
		ret = -EPERM;
	else
		ret = s6e3fa3_ams549ku15_on(ctx);
	mutex_unlock(&ctx->lock);

	return ret;
}

static int s6e3fa3_ams549ku15_disable(struct drm_panel *panel)
{
	struct s6e3fa3_ams549ku15 *ctx = to_s6e3fa3_ams549ku15(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mutex_lock(&ctx->lock);
	if (!ctx->powered)
		goto out;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 10);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 150);

out:
	mutex_unlock(&ctx->lock);
	return dsi_ctx.accum_err;
}

static int s6e3fa3_ams549ku15_unprepare(struct drm_panel *panel)
{
	struct s6e3fa3_ams549ku15 *ctx = to_s6e3fa3_ams549ku15(panel);
	int ret = 0;

	mutex_lock(&ctx->lock);
	if (!ctx->powered)
		goto out;

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	ret = regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	ctx->powered = false;

out:
	mutex_unlock(&ctx->lock);
	return ret;
}

static const struct drm_display_mode s6e3fa3_ams549ku15_mode = {
	.clock = (1080 + 1 + 1 + 1) * (1920 + 3 + 1 + 10) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 1,
	.hsync_end = 1080 + 1 + 1,
	.htotal = 1080 + 1 + 1 + 1,
	.vdisplay = 1920,
	.vsync_start = 1920 + 3,
	.vsync_end = 1920 + 3 + 1,
	.vtotal = 1920 + 3 + 1 + 10,
	.width_mm = 68,
	.height_mm = 121,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int s6e3fa3_ams549ku15_get_modes(struct drm_panel *panel,
				       struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector,
					    &s6e3fa3_ams549ku15_mode);
}

static const struct drm_panel_funcs s6e3fa3_ams549ku15_panel_funcs = {
	.prepare = s6e3fa3_ams549ku15_prepare,
	.enable = s6e3fa3_ams549ku15_enable,
	.disable = s6e3fa3_ams549ku15_disable,
	.unprepare = s6e3fa3_ams549ku15_unprepare,
	.get_modes = s6e3fa3_ams549ku15_get_modes,
};

static int s6e3fa3_ams549ku15_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct backlight_properties backlight_props = { };
	struct s6e3fa3_ams549ku15 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct s6e3fa3_ams549ku15, panel,
				   &s6e3fa3_ams549ku15_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->dsi = dsi;
	ctx->temperature = 25;
	ctx->adaptive_control = true;
	mutex_init(&ctx->lock);
	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->supplies[0].supply = "vci";
	ctx->supplies[1].supply = "vdd3";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get panel supplies\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "failed to get reset GPIO\n");

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	backlight_props.type = BACKLIGHT_RAW;
	backlight_props.max_brightness = S6E3FA3_MAX_BRIGHTNESS;
	backlight_props.brightness = 140;
	ctx->panel.backlight = devm_backlight_device_register(
		dev, dev_name(dev), dev, ctx, &s6e3fa3_ams549ku15_bl_ops,
		&backlight_props);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "failed to register backlight\n");

	ctx->panel.prepare_prev_first = true;
	drm_panel_add(&ctx->panel);

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "failed to attach to DSI host\n");
	}

	return 0;
}

static void s6e3fa3_ams549ku15_remove(struct mipi_dsi_device *dsi)
{
	struct s6e3fa3_ams549ku15 *ctx = mipi_dsi_get_drvdata(dsi);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id s6e3fa3_ams549ku15_of_match[] = {
	{ .compatible = "samsung,s6e3fa3-ams549ku15" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s6e3fa3_ams549ku15_of_match);

static struct mipi_dsi_driver s6e3fa3_ams549ku15_driver = {
	.probe = s6e3fa3_ams549ku15_probe,
	.remove = s6e3fa3_ams549ku15_remove,
	.driver = {
		.name = "panel-samsung-s6e3fa3-ams549ku15",
		.of_match_table = s6e3fa3_ams549ku15_of_match,
	},
};
module_mipi_dsi_driver(s6e3fa3_ams549ku15_driver);

MODULE_AUTHOR("j7y17lte mainline contributors");
MODULE_DESCRIPTION("Samsung AMS549KU15 AMOLED panel driver");
MODULE_LICENSE("GPL");
