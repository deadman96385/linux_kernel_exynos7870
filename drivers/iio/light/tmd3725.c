// SPDX-License-Identifier: GPL-2.0-only
/*
 * ams TMD3725 ambient light, color, and proximity sensor
 *
 * Copyright (C) 2026 j7y17lte mainline contributors
 *
 * The register setup, autoranging, lux conversion, and proximity calibration
 * sequence are derived from Samsung's downstream TMD3725 driver. The external
 * ABI and lifetime management use the upstream IIO and regulator frameworks.
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/pm_wakeup.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/unaligned.h>

#include <linux/iio/events.h>
#include <linux/iio/iio.h>

#define TMD3725_REG_ENABLE		0x80
#define TMD3725_REG_ATIME		0x81
#define TMD3725_REG_PRATE		0x82
#define TMD3725_REG_WTIME		0x83
#define TMD3725_REG_PILT		0x88
#define TMD3725_REG_PIHT		0x8a
#define TMD3725_REG_PERS		0x8c
#define TMD3725_REG_PGCFG0		0x8e
#define TMD3725_REG_PGCFG1		0x8f
#define TMD3725_REG_CFG1		0x90
#define TMD3725_REG_ID			0x92
#define TMD3725_REG_STATUS		0x93
#define TMD3725_REG_CDATAL		0x94
#define TMD3725_REG_PDATA		0x9c
#define TMD3725_REG_CFG3		0xab
#define TMD3725_REG_AZ_CONFIG		0xd6
#define TMD3725_REG_CALIB		0xd7
#define TMD3725_REG_CALIBCFG		0xd9
#define TMD3725_REG_CALIBSTAT		0xdc
#define TMD3725_REG_INTENAB		0xdd

#define TMD3725_ENABLE_PON		BIT(0)
#define TMD3725_ENABLE_AEN		BIT(1)
#define TMD3725_ENABLE_PEN		BIT(2)
#define TMD3725_ENABLE_WEN		BIT(3)

#define TMD3725_STATUS_ZINT		BIT(2)
#define TMD3725_STATUS_CINT		BIT(3)
#define TMD3725_STATUS_PINT		BIT(5)

#define TMD3725_INTENAB_ZIEN		BIT(2)
#define TMD3725_INTENAB_PIEN		BIT(5)

#define TMD3725_CFG1_AGAIN_MASK		GENMASK(1, 0)
#define TMD3725_CFG1_AGAIN_1X		0
#define TMD3725_CFG1_AGAIN_16X		2

#define TMD3725_CFG3_INT_READ_CLEAR	BIT(7)
#define TMD3725_CALIBCFG_AUTO_OFFSET	BIT(3)
#define TMD3725_CALIBCFG_BINSRCH_MASK	GENMASK(7, 5)
#define TMD3725_CALIBCFG_BINSRCH_7	FIELD_PREP(TMD3725_CALIBCFG_BINSRCH_MASK, 3)
#define TMD3725_CALIB_START		BIT(0)
#define TMD3725_CALIBSTAT_FINISHED	BIT(0)

#define TMD3725_CHIP_ID			0xe4
#define TMD3725_ATIME_DEFAULT		0x11
#define TMD3725_ATIME_STEP_US		2800
#define TMD3725_ALS_TIME_US		((TMD3725_ATIME_DEFAULT + 1) * \
					 TMD3725_ATIME_STEP_US)
#define TMD3725_ALS_LOW_COUNTS		25
#define TMD3725_ALS_HIGH_COUNTS		15000
#define TMD3725_ALS_SAT_COUNTS		18500
#define TMD3725_LUX_SATURATION		150000
#define TMD3725_LUX_MULTIPLIER		10

#define TMD3725_PROX_NEAR_DEFAULT	55
#define TMD3725_PROX_FAR_DEFAULT	40
#define TMD3725_PROX_MAX		255

enum tmd3725_rgbc_index {
	TMD3725_CLEAR,
	TMD3725_RED,
	TMD3725_GREEN,
	TMD3725_BLUE,
	TMD3725_NUM_RGBC_CHANNELS,
};

struct tmd3725_data {
	struct device *dev;
	struct regmap *regmap;
	struct regulator *vled;
	/* Serializes configuration, power state, and direct IIO reads. */
	struct mutex lock;
	s32 lux_coeff[TMD3725_NUM_RGBC_CHANNELS];
	u32 device_factor;
	u32 cct_coefficient;
	u32 cct_offset;
	u8 als_gain;
	u8 prox_low;
	u8 prox_high;
	int irq;
	bool vled_enabled;
	bool prox_event_enabled;
	bool event_suspended;
	bool irq_wake_enabled;
};

static const struct reg_sequence tmd3725_init_regs[] = {
	{ TMD3725_REG_ENABLE, TMD3725_ENABLE_PON },
	{ TMD3725_REG_INTENAB, 0 },
	{ TMD3725_REG_CFG3, TMD3725_CFG3_INT_READ_CLEAR },
	{ TMD3725_REG_AZ_CONFIG, 0x7f },
	{ TMD3725_REG_CFG1, TMD3725_CFG1_AGAIN_16X },
	{ TMD3725_REG_ATIME, TMD3725_ATIME_DEFAULT },
	{ TMD3725_REG_PERS, 0x30 },
	{ TMD3725_REG_PGCFG0, 0x96 },
	{ TMD3725_REG_PRATE, 0x38 },
	{ TMD3725_REG_WTIME, 0x00 },
	{ TMD3725_REG_PGCFG1, 0x08 },
};

static const struct iio_event_spec tmd3725_prox_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
};

#define TMD3725_INTENSITY_CHANNEL(_modifier, _index) { \
	.type = IIO_INTENSITY, \
	.modified = 1, \
	.channel2 = (_modifier), \
	.address = (_index), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) | \
				    BIT(IIO_CHAN_INFO_INT_TIME), \
}

static const struct iio_chan_spec tmd3725_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
	},
	TMD3725_INTENSITY_CHANNEL(IIO_MOD_LIGHT_CLEAR, TMD3725_CLEAR),
	TMD3725_INTENSITY_CHANNEL(IIO_MOD_LIGHT_RED, TMD3725_RED),
	TMD3725_INTENSITY_CHANNEL(IIO_MOD_LIGHT_GREEN, TMD3725_GREEN),
	TMD3725_INTENSITY_CHANNEL(IIO_MOD_LIGHT_BLUE, TMD3725_BLUE),
	{
		.type = IIO_CCT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
	},
	{
		.type = IIO_PROXIMITY,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.event_spec = tmd3725_prox_events,
		.num_event_specs = ARRAY_SIZE(tmd3725_prox_events),
	},
};

static int tmd3725_set_enable(struct tmd3725_data *data, bool als, bool prox)
{
	u8 val = TMD3725_ENABLE_PON;

	if (als)
		val |= TMD3725_ENABLE_AEN;
	if (prox)
		val |= TMD3725_ENABLE_PEN | TMD3725_ENABLE_WEN;

	return regmap_write(data->regmap, TMD3725_REG_ENABLE, val);
}

static int tmd3725_vled_enable(struct tmd3725_data *data)
{
	int ret;

	if (!data->vled || data->vled_enabled)
		return 0;

	ret = regulator_enable(data->vled);
	if (ret)
		return ret;

	data->vled_enabled = true;
	usleep_range(10000, 11000);

	return 0;
}

static int tmd3725_vled_disable(struct tmd3725_data *data)
{
	int ret;

	if (!data->vled || !data->vled_enabled)
		return 0;

	ret = regulator_disable(data->vled);
	if (!ret)
		data->vled_enabled = false;

	return ret;
}

static int tmd3725_read_rgbc(struct tmd3725_data *data, u16 rgbc[])
{
	u8 buf[TMD3725_NUM_RGBC_CHANNELS * sizeof(u16)];
	u8 gain = data->als_gain;
	bool prox = data->prox_event_enabled && !data->event_suspended;
	int ret, restore_ret;
	unsigned int i;

	for (i = 0; i < 2; i++) {
		ret = regmap_update_bits(data->regmap, TMD3725_REG_CFG1,
					 TMD3725_CFG1_AGAIN_MASK, gain);
		if (ret)
			return ret;

		ret = tmd3725_set_enable(data, true, prox);
		if (ret)
			return ret;

		usleep_range(TMD3725_ALS_TIME_US + 1000,
			     TMD3725_ALS_TIME_US + 3000);

		ret = regmap_bulk_read(data->regmap, TMD3725_REG_CDATAL,
				       buf, sizeof(buf));
		restore_ret = tmd3725_set_enable(data, false, prox);
		if (ret)
			return ret;
		if (restore_ret)
			return restore_ret;

		for (unsigned int j = 0; j < ARRAY_SIZE(data->lux_coeff); j++)
			rgbc[j] = get_unaligned_le16(&buf[j * sizeof(u16)]);

		if (gain == TMD3725_CFG1_AGAIN_1X &&
		    rgbc[TMD3725_CLEAR] < TMD3725_ALS_LOW_COUNTS) {
			gain = TMD3725_CFG1_AGAIN_16X;
			continue;
		}

		if (gain == TMD3725_CFG1_AGAIN_16X &&
		    rgbc[TMD3725_CLEAR] > TMD3725_ALS_HIGH_COUNTS) {
			gain = TMD3725_CFG1_AGAIN_1X;
			continue;
		}

		break;
	}

	data->als_gain = gain;

	return 0;
}

static int tmd3725_calculate_lux(struct tmd3725_data *data,
				 const u16 rgbc[])
{
	s64 weighted;
	u32 gain;

	if (data->als_gain == TMD3725_CFG1_AGAIN_1X &&
	    rgbc[TMD3725_CLEAR] >= TMD3725_ALS_SAT_COUNTS)
		return TMD3725_LUX_SATURATION;

	weighted = (s64)rgbc[TMD3725_RED] * data->lux_coeff[TMD3725_RED] +
		   (s64)rgbc[TMD3725_GREEN] * data->lux_coeff[TMD3725_GREEN] +
		   (s64)rgbc[TMD3725_BLUE] * data->lux_coeff[TMD3725_BLUE] +
		   (s64)rgbc[TMD3725_CLEAR] * data->lux_coeff[TMD3725_CLEAR];
	weighted = div_s64(weighted, 1000);
	if (weighted <= 0)
		return 0;

	gain = data->als_gain == TMD3725_CFG1_AGAIN_16X ? 16 : 1;
	weighted *= data->device_factor * TMD3725_LUX_MULTIPLIER;
	weighted = div_s64(weighted,
			   (TMD3725_ATIME_DEFAULT + 1) * 28 * gain);

	return min_t(s64, weighted, INT_MAX);
}

static int tmd3725_calculate_cct(struct tmd3725_data *data,
				 const u16 rgbc[])
{
	u64 cct;

	if (!rgbc[TMD3725_RED])
		return -ERANGE;

	cct = (u64)data->cct_coefficient * rgbc[TMD3725_BLUE];
	cct = div_u64(cct, rgbc[TMD3725_RED]);
	cct += data->cct_offset;

	return min_t(u64, cct, INT_MAX);
}

static int tmd3725_calibrate_proximity(struct tmd3725_data *data)
{
	unsigned int status;
	int ret;

	ret = tmd3725_set_enable(data, false, false);
	if (ret)
		return ret;

	/* Poll CINT directly so the IRQ thread cannot consume it first. */
	ret = regmap_write(data->regmap, TMD3725_REG_INTENAB, 0);
	if (ret)
		return ret;

	ret = regmap_update_bits(data->regmap, TMD3725_REG_CALIBCFG,
				 TMD3725_CALIBCFG_BINSRCH_MASK |
				 TMD3725_CALIBCFG_AUTO_OFFSET,
				 TMD3725_CALIBCFG_BINSRCH_7 |
				 TMD3725_CALIBCFG_AUTO_OFFSET);
	if (ret)
		return ret;

	ret = regmap_write(data->regmap, TMD3725_REG_CALIB,
			   TMD3725_CALIB_START);
	if (ret)
		return ret;

	ret = regmap_read_poll_timeout(data->regmap, TMD3725_REG_STATUS,
				       status, status & TMD3725_STATUS_CINT,
				       1000, 100000);
	if (ret)
		return ret;

	return regmap_update_bits(data->regmap, TMD3725_REG_CALIBSTAT,
				  TMD3725_CALIBSTAT_FINISHED,
				  TMD3725_CALIBSTAT_FINISHED);
}

static int tmd3725_apply_proximity_events(struct tmd3725_data *data)
{
	int ret;

	ret = regmap_write(data->regmap, TMD3725_REG_PILT, data->prox_low);
	if (ret)
		return ret;

	ret = regmap_write(data->regmap, TMD3725_REG_PIHT, data->prox_high);
	if (ret)
		return ret;

	ret = regmap_write(data->regmap, TMD3725_REG_INTENAB,
			   TMD3725_INTENAB_PIEN | TMD3725_INTENAB_ZIEN);
	if (ret)
		return ret;

	return tmd3725_set_enable(data, false, true);
}

static int tmd3725_read_proximity(struct tmd3725_data *data, int *val)
{
	unsigned int raw;
	int ret, restore_ret, vled_ret;

	if (data->prox_event_enabled && !data->event_suspended) {
		ret = regmap_read(data->regmap, TMD3725_REG_PDATA, &raw);
		if (!ret)
			*val = raw;
		return ret;
	}

	ret = tmd3725_vled_enable(data);
	if (ret)
		return ret;

	ret = tmd3725_calibrate_proximity(data);
	if (ret)
		goto out_vled;

	ret = tmd3725_set_enable(data, false, true);
	if (ret)
		goto out_vled;

	usleep_range(10000, 12000);
	ret = regmap_read(data->regmap, TMD3725_REG_PDATA, &raw);
	restore_ret = tmd3725_set_enable(data, false, false);
	if (!ret)
		ret = restore_ret;
	if (!ret)
		*val = raw;

out_vled:
	vled_ret = tmd3725_vled_disable(data);
	if (!ret)
		ret = vled_ret;

	return ret;
}

static int tmd3725_read_raw(struct iio_dev *indio_dev,
			    const struct iio_chan_spec *chan,
			    int *val, int *val2, long mask)
{
	struct tmd3725_data *data = iio_priv(indio_dev);
	u16 rgbc[TMD3725_NUM_RGBC_CHANNELS];
	int ret;

	mutex_lock(&data->lock);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type == IIO_INTENSITY) {
			ret = tmd3725_read_rgbc(data, rgbc);
			if (!ret) {
				*val = rgbc[chan->address];
				ret = IIO_VAL_INT;
			}
		} else if (chan->type == IIO_PROXIMITY) {
			ret = tmd3725_read_proximity(data, val);
			if (!ret)
				ret = IIO_VAL_INT;
		} else {
			ret = -EINVAL;
		}
		break;

	case IIO_CHAN_INFO_PROCESSED:
		if (chan->type != IIO_LIGHT && chan->type != IIO_CCT) {
			ret = -EINVAL;
			break;
		}

		ret = tmd3725_read_rgbc(data, rgbc);
		if (ret)
			break;

		if (chan->type == IIO_LIGHT)
			ret = tmd3725_calculate_lux(data, rgbc);
		else
			ret = tmd3725_calculate_cct(data, rgbc);
		if (ret >= 0) {
			*val = ret;
			ret = IIO_VAL_INT;
		}
		break;

	case IIO_CHAN_INFO_SCALE:
		if (chan->type != IIO_INTENSITY) {
			ret = -EINVAL;
			break;
		}
		*val = 1;
		*val2 = data->als_gain == TMD3725_CFG1_AGAIN_16X ? 16 : 1;
		ret = IIO_VAL_FRACTIONAL;
		break;

	case IIO_CHAN_INFO_INT_TIME:
		if (chan->type != IIO_INTENSITY) {
			ret = -EINVAL;
			break;
		}
		*val = 0;
		*val2 = TMD3725_ALS_TIME_US;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&data->lock);

	return ret;
}

static int tmd3725_read_event_value(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    enum iio_event_info info,
				    int *val, int *val2)
{
	struct tmd3725_data *data = iio_priv(indio_dev);

	if (chan->type != IIO_PROXIMITY || type != IIO_EV_TYPE_THRESH ||
	    info != IIO_EV_INFO_VALUE)
		return -EINVAL;

	mutex_lock(&data->lock);
	if (dir == IIO_EV_DIR_RISING)
		*val = data->prox_high;
	else if (dir == IIO_EV_DIR_FALLING)
		*val = data->prox_low;
	else
		*val = -EINVAL;
	mutex_unlock(&data->lock);

	if (*val < 0)
		return *val;

	return IIO_VAL_INT;
}

static int tmd3725_write_event_value(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir,
				     enum iio_event_info info,
				     int val, int val2)
{
	struct tmd3725_data *data = iio_priv(indio_dev);
	u8 reg;
	int ret = 0;

	if (chan->type != IIO_PROXIMITY || type != IIO_EV_TYPE_THRESH ||
	    info != IIO_EV_INFO_VALUE || val2 || val < 0 ||
	    val > TMD3725_PROX_MAX)
		return -EINVAL;

	mutex_lock(&data->lock);

	if (dir == IIO_EV_DIR_RISING) {
		if (val <= data->prox_low) {
			ret = -EINVAL;
			goto out_unlock;
		}
		data->prox_high = val;
		reg = TMD3725_REG_PIHT;
	} else if (dir == IIO_EV_DIR_FALLING) {
		if (val >= data->prox_high) {
			ret = -EINVAL;
			goto out_unlock;
		}
		data->prox_low = val;
		reg = TMD3725_REG_PILT;
	} else {
		ret = -EINVAL;
		goto out_unlock;
	}

	if (data->prox_event_enabled && !data->event_suspended)
		ret = regmap_write(data->regmap, reg, val);

out_unlock:
	mutex_unlock(&data->lock);

	return ret;
}

static int tmd3725_read_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir)
{
	struct tmd3725_data *data = iio_priv(indio_dev);
	int enabled;

	if (chan->type != IIO_PROXIMITY || type != IIO_EV_TYPE_THRESH ||
	    dir != IIO_EV_DIR_EITHER)
		return -EINVAL;

	mutex_lock(&data->lock);
	enabled = data->prox_event_enabled;
	mutex_unlock(&data->lock);

	return enabled;
}

static int tmd3725_write_event_config(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      enum iio_event_type type,
				      enum iio_event_direction dir,
				      bool state)
{
	struct tmd3725_data *data = iio_priv(indio_dev);
	int ret, vled_ret;

	if (chan->type != IIO_PROXIMITY || type != IIO_EV_TYPE_THRESH ||
	    dir != IIO_EV_DIR_EITHER)
		return -EINVAL;
	if (data->irq <= 0)
		return -ENXIO;

	mutex_lock(&data->lock);
	if (state == data->prox_event_enabled) {
		ret = 0;
		goto out_unlock;
	}

	if (state) {
		ret = tmd3725_vled_enable(data);
		if (ret)
			goto out_unlock;

		ret = tmd3725_calibrate_proximity(data);
		if (ret)
			goto out_disable_vled;

		ret = tmd3725_apply_proximity_events(data);
		if (ret)
			goto out_disable_vled;

		data->prox_event_enabled = true;
		goto out_unlock;
	}

	ret = regmap_write(data->regmap, TMD3725_REG_INTENAB, 0);
	if (!ret)
		ret = tmd3725_set_enable(data, false, false);
	if (ret)
		goto out_unlock;

	data->prox_event_enabled = false;
	ret = tmd3725_vled_disable(data);
	goto out_unlock;

out_disable_vled:
	regmap_write(data->regmap, TMD3725_REG_INTENAB, 0);
	tmd3725_set_enable(data, false, false);
	vled_ret = tmd3725_vled_disable(data);
	if (!ret)
		ret = vled_ret;
out_unlock:
	mutex_unlock(&data->lock);

	return ret;
}

static const struct iio_info tmd3725_info = {
	.read_raw = tmd3725_read_raw,
	.read_event_value = tmd3725_read_event_value,
	.write_event_value = tmd3725_write_event_value,
	.read_event_config = tmd3725_read_event_config,
	.write_event_config = tmd3725_write_event_config,
};

static irqreturn_t tmd3725_irq_thread(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct tmd3725_data *data = iio_priv(indio_dev);
	unsigned int raw, status;
	enum iio_event_direction dir;
	u64 event;
	int ret;

	mutex_lock(&data->lock);

	ret = regmap_read(data->regmap, TMD3725_REG_STATUS, &status);
	if (ret) {
		dev_err(data->dev, "failed to read interrupt status: %d\n", ret);
		goto out_unlock;
	}

	if (status & TMD3725_STATUS_ZINT) {
		ret = tmd3725_calibrate_proximity(data);
		if (!ret)
			ret = tmd3725_apply_proximity_events(data);
		if (ret) {
			dev_err(data->dev,
				"failed to recalibrate proximity sensor: %d\n", ret);
			goto out_unlock;
		}
	}

	if (!(status & TMD3725_STATUS_PINT))
		goto out_unlock;

	ret = regmap_read(data->regmap, TMD3725_REG_PDATA, &raw);
	if (ret) {
		dev_err(data->dev, "failed to read proximity sample: %d\n", ret);
		goto out_unlock;
	}

	if (raw >= data->prox_high)
		dir = IIO_EV_DIR_RISING;
	else if (raw <= data->prox_low)
		dir = IIO_EV_DIR_FALLING;
	else
		goto out_unlock;

	event = IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, 0,
				     IIO_EV_TYPE_THRESH, dir);
	iio_push_event(indio_dev, event, iio_get_time_ns(indio_dev));

out_unlock:
	mutex_unlock(&data->lock);

	return IRQ_HANDLED;
}

static bool tmd3725_volatile_reg(struct device *dev, unsigned int reg)
{
	return reg == TMD3725_REG_STATUS ||
	       (reg >= TMD3725_REG_CDATAL && reg <= TMD3725_REG_PDATA) ||
	       reg == TMD3725_REG_CALIBSTAT;
}

static const struct regmap_config tmd3725_regmap_config = {
	.name = "tmd3725",
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xe7,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = tmd3725_volatile_reg,
};

static void tmd3725_cleanup(void *private)
{
	struct tmd3725_data *data = private;

	mutex_lock(&data->lock);
	if (data->irq_wake_enabled)
		disable_irq_wake(data->irq);
	regmap_write(data->regmap, TMD3725_REG_INTENAB, 0);
	regmap_write(data->regmap, TMD3725_REG_ENABLE, 0);
	tmd3725_vled_disable(data);
	mutex_unlock(&data->lock);
}

static int tmd3725_parse_properties(struct tmd3725_data *data)
{
	u32 coeff[TMD3725_NUM_RGBC_CHANNELS];
	u32 val;
	int ret;

	data->lux_coeff[TMD3725_RED] = -220;
	data->lux_coeff[TMD3725_GREEN] = 110;
	data->lux_coeff[TMD3725_BLUE] = -1120;
	data->lux_coeff[TMD3725_CLEAR] = 1000;
	data->device_factor = 831;
	data->cct_coefficient = 4091;
	data->cct_offset = 227;
	data->prox_high = TMD3725_PROX_NEAR_DEFAULT;
	data->prox_low = TMD3725_PROX_FAR_DEFAULT;

	ret = device_property_read_u32_array(data->dev, "ams,lux-coefficients",
					     coeff, ARRAY_SIZE(coeff));
	if (!ret) {
		/* The binding defines these cells as signed two's-complement. */
		data->lux_coeff[TMD3725_RED] = (s32)coeff[0];
		data->lux_coeff[TMD3725_GREEN] = (s32)coeff[1];
		data->lux_coeff[TMD3725_BLUE] = (s32)coeff[2];
		data->lux_coeff[TMD3725_CLEAR] = (s32)coeff[3];
	} else if (ret != -EINVAL && ret != -ENODATA && ret != -ENOENT) {
		return ret;
	}

	device_property_read_u32(data->dev, "ams,device-factor",
				 &data->device_factor);
	device_property_read_u32(data->dev, "ams,cct-coefficient",
				 &data->cct_coefficient);
	device_property_read_u32(data->dev, "ams,cct-offset",
				 &data->cct_offset);

	if (!data->device_factor)
		return -EINVAL;

	if (!device_property_read_u32(data->dev, "proximity-near-level", &val)) {
		if (!val || val > TMD3725_PROX_MAX)
			return -EINVAL;
		data->prox_high = val;
		data->prox_low = val > 15 ? val - 15 : 0;
	}

	return 0;
}

static int tmd3725_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct tmd3725_data *data;
	struct iio_dev *indio_dev;
	unsigned int chip_id;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->dev = dev;
	data->irq = client->irq;
	data->als_gain = TMD3725_CFG1_AGAIN_16X;

	ret = devm_mutex_init(dev, &data->lock);
	if (ret)
		return ret;

	ret = devm_regulator_get_enable_optional(dev, "vdd");
	if (ret && ret != -ENODEV)
		return dev_err_probe(dev, ret, "failed to enable core supply\n");

	data->vled = devm_regulator_get_optional(dev, "vled");
	if (IS_ERR(data->vled)) {
		ret = PTR_ERR(data->vled);
		if (ret == -ENODEV)
			data->vled = NULL;
		else
			return dev_err_probe(dev, ret, "failed to get VLED supply\n");
	}

	data->regmap = devm_regmap_init_i2c(client, &tmd3725_regmap_config);
	if (IS_ERR(data->regmap))
		return dev_err_probe(dev, PTR_ERR(data->regmap),
				     "failed to initialize regmap\n");

	usleep_range(3000, 5000);

	ret = regmap_read(data->regmap, TMD3725_REG_ID, &chip_id);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read chip ID\n");
	if (chip_id != TMD3725_CHIP_ID)
		return dev_err_probe(dev, -ENODEV,
				     "unexpected chip ID 0x%02x\n", chip_id);

	ret = tmd3725_parse_properties(data);
	if (ret)
		return dev_err_probe(dev, ret, "invalid sensor properties\n");

	ret = regmap_multi_reg_write(data->regmap, tmd3725_init_regs,
				     ARRAY_SIZE(tmd3725_init_regs));
	if (ret)
		return dev_err_probe(dev, ret, "failed to initialize sensor\n");

	ret = regmap_update_bits(data->regmap, TMD3725_REG_CALIBCFG,
				 TMD3725_CALIBCFG_AUTO_OFFSET,
				 TMD3725_CALIBCFG_AUTO_OFFSET);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to enable automatic offset adjustment\n");

	ret = devm_add_action_or_reset(dev, tmd3725_cleanup, data);
	if (ret)
		return ret;

	if (client->irq > 0) {
		ret = devm_request_threaded_irq(dev, client->irq, NULL,
						tmd3725_irq_thread, IRQF_ONESHOT,
						dev_name(dev), indio_dev);
		if (ret)
			return dev_err_probe(dev, ret, "failed to request IRQ\n");

		if (device_property_read_bool(dev, "wakeup-source")) {
			ret = devm_device_init_wakeup(dev);
			if (ret)
				return ret;
		}
	}

	indio_dev->name = "tmd3725";
	indio_dev->info = &tmd3725_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = tmd3725_channels;
	indio_dev->num_channels = ARRAY_SIZE(tmd3725_channels);

	i2c_set_clientdata(client, indio_dev);

	return devm_iio_device_register(dev, indio_dev);
}

static int tmd3725_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct tmd3725_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->lock);
	if (!data->prox_event_enabled) {
		ret = 0;
		goto out_unlock;
	}

	if (device_may_wakeup(dev)) {
		ret = enable_irq_wake(data->irq);
		if (!ret)
			data->irq_wake_enabled = true;
		goto out_unlock;
	}

	ret = regmap_write(data->regmap, TMD3725_REG_INTENAB, 0);
	if (!ret)
		ret = tmd3725_set_enable(data, false, false);
	if (!ret)
		ret = tmd3725_vled_disable(data);
	if (!ret)
		data->event_suspended = true;

out_unlock:
	mutex_unlock(&data->lock);

	return ret;
}

static int tmd3725_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct tmd3725_data *data = iio_priv(indio_dev);
	int ret = 0;

	mutex_lock(&data->lock);

	if (data->irq_wake_enabled) {
		ret = disable_irq_wake(data->irq);
		if (!ret)
			data->irq_wake_enabled = false;
		goto out_unlock;
	}

	if (!data->event_suspended)
		goto out_unlock;

	ret = tmd3725_vled_enable(data);
	if (ret)
		goto out_unlock;

	ret = tmd3725_calibrate_proximity(data);
	if (!ret)
		ret = tmd3725_apply_proximity_events(data);
	if (ret) {
		tmd3725_vled_disable(data);
		goto out_unlock;
	}

	data->event_suspended = false;

out_unlock:
	mutex_unlock(&data->lock);

	return ret;
}

static DEFINE_SIMPLE_DEV_PM_OPS(tmd3725_pm_ops, tmd3725_suspend,
				tmd3725_resume);

static const struct of_device_id tmd3725_of_match[] = {
	{ .compatible = "ams,tmd3725" },
	{ }
};
MODULE_DEVICE_TABLE(of, tmd3725_of_match);

static const struct i2c_device_id tmd3725_id[] = {
	{ "tmd3725" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tmd3725_id);

static struct i2c_driver tmd3725_driver = {
	.driver = {
		.name = "tmd3725",
		.of_match_table = tmd3725_of_match,
		.pm = pm_sleep_ptr(&tmd3725_pm_ops),
	},
	.probe = tmd3725_probe,
	.id_table = tmd3725_id,
};
module_i2c_driver(tmd3725_driver);

MODULE_AUTHOR("j7y17lte mainline contributors");
MODULE_DESCRIPTION("ams TMD3725 ambient light, color, and proximity sensor");
MODULE_LICENSE("GPL");
