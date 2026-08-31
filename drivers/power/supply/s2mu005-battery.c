// SPDX-License-Identifier: GPL-2.0
/*
 * Battery Fuel Gauge Driver for Samsung S2MU005 PMIC.
 *
 * Copyright (C) 2015 Samsung Electronics
 * Copyright (C) 2023 Yassine Oudjana <y.oudjana@protonmail.com>
 * Copyright (C) 2025 Kaustabh Chakraborty <kauschluss@disroot.org>
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/limits.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/units.h>

#define S2MU005_FG_REG_STATUS		0x00
#define S2MU005_FG_REG_IRQ		0x02
#define S2MU005_FG_REG_RVBAT		0x04
#define S2MU005_FG_REG_RCURCC		0x06
#define S2MU005_FG_REG_RSOC		0x08
#define S2MU005_FG_REG_MONOUT		0x0a
#define S2MU005_FG_REG_MONOUTSEL	0x0c
#define S2MU005_FG_REG_RBATCAP		0x0e
#define S2MU005_FG_REG_RZADJ		0x12
#define S2MU005_FG_REG_RBATZ0		0x16
#define S2MU005_FG_REG_RBATZ1		0x18
#define S2MU005_FG_REG_IRQLVL		0x1a
#define S2MU005_FG_REG_START		0x1e
#define S2MU005_FG_REG_RESET		0x1f

#define S2MU005_FG_REG_REVISION		0x48
#define S2MU005_FG_RESET_POR		BIT(4)

#define S2MU005_FG_TABLE3_START		0x92
#define S2MU005_FG_TABLE4_START		0xea
#define S2MU005_FG_TABLE3_SIZE		88
#define S2MU005_FG_TABLE4_SIZE		22

#define S2MU005_FG_MONOUTSEL_IDLE		0x10
#define S2MU005_FG_MONOUTSEL_AVGCURRENT		0x26
#define S2MU005_FG_MONOUTSEL_AVGVOLTAGE		0x27
#define S2MU005_FG_MONOUTSEL_REV10_AVGCURRENT	0x17
#define S2MU005_FG_MONOUTSEL_REV10_AVGVOLTAGE	0x16
#define S2MU005_FG_TEMP_SAMPLES			5

struct s2mu005_fg {
	struct device *dev;
	struct i2c_client *client;
	struct regmap *regmap;
	struct power_supply *psy;
	struct mutex monout_mutex;
	/* Serializes profile recovery with all regmap and SMBus gauge I/O. */
	struct mutex init_mutex;
	const struct s2mu005_fg_profile *profile;
	struct iio_channel *battery_temp;
	u8 revision;
};

struct s2mu005_fg_temp_point {
	int adc;
	int temp_decic;
};

struct s2mu005_fg_profile {
	const char *battery_compatible;
	u8 battery_table3[S2MU005_FG_TABLE3_SIZE];
	u8 battery_table4[S2MU005_FG_TABLE4_SIZE];
	u8 battery_capacity[4];
	u8 accumulative_rate[2];
	const struct s2mu005_fg_temp_point *temp_table;
	size_t temp_table_size;
};

static const struct s2mu005_fg_temp_point s2mu005_eb_bj730abe_temp_table[] = {
	{ 325, 900 }, { 360, 850 }, { 436, 800 }, { 487, 750 },
	{ 570, 700 }, { 660, 650 }, { 768, 600 }, { 845, 570 },
	{ 899, 550 }, { 958, 530 }, { 1045, 500 }, { 1107, 480 },
	{ 1141, 470 }, { 1209, 450 }, { 1278, 430 }, { 1315, 420 },
	{ 1388, 400 }, { 1504, 370 }, { 1591, 350 }, { 1679, 330 },
	{ 1806, 300 }, { 1936, 270 }, { 2038, 250 }, { 2266, 200 },
	{ 2487, 150 }, { 2711, 100 }, { 2912, 50 }, { 2989, 30 },
	{ 3099, 0 }, { 3201, -30 }, { 3257, -50 }, { 3314, -70 },
	{ 3396, -100 }, { 3515, -150 }, { 3606, -200 },
};

/* First-life (4.35 V) data from Samsung's j7y17lte battery profile. */
static const struct s2mu005_fg_profile s2mu005_eb_bj730abe_profile = {
	.battery_compatible = "samsung,eb-bj730abe",
	.battery_table3 = {
		197, 11, 63, 11, 186, 10, 53, 10, 186, 9,
		67, 9, 209, 8, 102, 8, 3, 8, 173, 7,
		75, 7, 233, 6, 174, 6, 130, 6, 95, 6,
		66, 6, 32, 6, 244, 5, 188, 5, 138, 5,
		240, 4, 146, 1, 237, 8, 126, 8, 15, 8,
		160, 7, 49, 7, 195, 6, 84, 6, 229, 5,
		118, 5, 7, 5, 153, 4, 42, 4, 187, 3,
		76, 3, 222, 2, 111, 2, 0, 2, 145, 1,
		34, 1, 180, 0, 69, 0, 214, 15,
	},
	.battery_table4 = {
		61, 61, 61, 61, 61, 61, 61, 61, 60, 60, 60,
		61, 62, 63, 63, 64, 65, 67, 70, 76, 96, 174,
	},
	.battery_capacity = { 0x37, 0x28, 0x0d, 0xca },
	.accumulative_rate = { 0xad, 0x07 },
	.temp_table = s2mu005_eb_bj730abe_temp_table,
	.temp_table_size = ARRAY_SIZE(s2mu005_eb_bj730abe_temp_table),
};

static const struct regmap_config s2mu005_fg_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.max_register = 0xff,
};

static int s2mu005_fg_read_byte(struct s2mu005_fg *priv, u8 reg, u8 *value)
{
	int ret;

	ret = i2c_smbus_read_byte_data(priv->client, reg);
	if (ret < 0)
		return ret;

	*value = ret;

	return 0;
}

static int s2mu005_fg_write_byte(struct s2mu005_fg *priv, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(priv->client, reg, value);
}

static int s2mu005_fg_update_byte(struct s2mu005_fg *priv, u8 reg,
				  u8 mask, u8 value)
{
	u8 regval;
	int ret;

	ret = s2mu005_fg_read_byte(priv, reg, &regval);
	if (ret)
		return ret;

	regval &= ~mask;
	regval |= value & mask;

	return s2mu005_fg_write_byte(priv, reg, regval);
}

static const struct s2mu005_fg_profile *
s2mu005_fg_get_profile(struct device *dev)
{
	struct device_node *battery;
	const struct s2mu005_fg_profile *profile = NULL;

	battery = of_parse_phandle(dev->of_node, "monitored-battery", 0);
	if (!battery)
		return NULL;

	if (of_device_is_compatible(battery,
				    s2mu005_eb_bj730abe_profile.battery_compatible))
		profile = &s2mu005_eb_bj730abe_profile;

	of_node_put(battery);

	return profile;
}

static int s2mu005_fg_program_profile(struct s2mu005_fg *priv)
{
	const struct s2mu005_fg_profile *profile = priv->profile;
	u8 revision;
	int ret;

	if (!profile)
		return -ENODEV;

	ret = s2mu005_fg_read_byte(priv, S2MU005_FG_REG_REVISION, &revision);
	if (ret)
		return ret;
	priv->revision = revision >> 4;

	ret = s2mu005_fg_write_byte(priv, S2MU005_FG_REG_RESET, 0x40);
	if (ret)
		return ret;
	msleep(50);

	ret = s2mu005_fg_write_byte(priv, S2MU005_FG_REG_RESET, 0x01);
	if (ret)
		return ret;
	msleep(50);

	ret = s2mu005_fg_write_byte(priv, 0x0f,
				    profile->battery_capacity[0]);
	if (ret)
		return ret;
	ret = s2mu005_fg_write_byte(priv, 0x0e,
				    profile->battery_capacity[1]);
	if (ret)
		return ret;
	ret = s2mu005_fg_write_byte(priv, 0x11,
				    profile->battery_capacity[2]);
	if (ret)
		return ret;
	ret = s2mu005_fg_write_byte(priv, 0x10,
				    profile->battery_capacity[3]);
	if (ret)
		return ret;

	if (priv->revision >= 0x0a) {
		ret = s2mu005_fg_update_byte(priv, S2MU005_FG_REG_MONOUTSEL,
					     BIT(6), BIT(6));
		if (ret)
			return ret;
	}

	for (int i = 0; i < S2MU005_FG_TABLE3_SIZE; i++) {
		ret = s2mu005_fg_write_byte(priv, S2MU005_FG_TABLE3_START + i,
					    profile->battery_table3[i]);
		if (ret)
			return ret;
	}

	for (int i = 0; i < S2MU005_FG_TABLE4_SIZE; i++) {
		ret = s2mu005_fg_write_byte(priv, S2MU005_FG_TABLE4_START + i,
					    profile->battery_table4[i]);
		if (ret)
			return ret;
	}

	ret = s2mu005_fg_write_byte(priv, 0x21, 0x13);
	if (ret)
		return ret;
	ret = s2mu005_fg_write_byte(priv, 0x14, 0x40);
	if (ret)
		return ret;

	ret = s2mu005_fg_update_byte(priv, 0x45, 0x0f,
				     profile->accumulative_rate[1]);
	if (ret)
		return ret;
	ret = s2mu005_fg_write_byte(priv, 0x44,
				    profile->accumulative_rate[0]);
	if (ret)
		return ret;

	ret = s2mu005_fg_update_byte(priv, 0x27, BIT(4), BIT(4));
	if (ret)
		return ret;

	if (priv->revision >= 2) {
		ret = s2mu005_fg_write_byte(priv, 0x4b, 0x0b);
		if (ret)
			return ret;
		ret = s2mu005_fg_write_byte(priv, 0x4a, 0x10);
		if (ret)
			return ret;
		ret = s2mu005_fg_update_byte(priv, 0x03, BIT(6), BIT(6));
		if (ret)
			return ret;
	}

	ret = s2mu005_fg_write_byte(priv, 0x40, 0x08);
	if (ret)
		return ret;
	ret = s2mu005_fg_write_byte(priv, 0x41, 0x04);
	if (ret)
		return ret;
	ret = s2mu005_fg_write_byte(priv, 0x26, 0xf6);
	if (ret)
		return ret;
	ret = s2mu005_fg_update_byte(priv, 0x27, 0x0f, 0x0f);
	if (ret)
		return ret;

	ret = s2mu005_fg_write_byte(priv, S2MU005_FG_REG_START, 0x0f);
	if (ret)
		return ret;

	msleep(300);

	return 0;
}

static int s2mu005_fg_initialize_if_needed_locked(struct s2mu005_fg *priv)
{
	union power_supply_propval behaviour = {
		.intval = POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO,
	};
	union power_supply_propval inhibit = {
		.intval = POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE,
	};
	struct power_supply *charger;
	u8 reset;
	int restore_ret;
	int ret;

	ret = s2mu005_fg_read_byte(priv, S2MU005_FG_REG_RESET, &reset);
	if (ret || !(reset & S2MU005_FG_RESET_POR))
		return ret;

	if (!priv->profile) {
		return dev_err_probe(priv->dev, -ENODEV,
				     "fuel gauge reset with no matching battery profile\n");
	}

	charger = power_supply_get_by_name("s2mu005-charger");
	if (!charger)
		return -EPROBE_DEFER;

	ret = power_supply_get_property(charger,
					POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
					&behaviour);
	if (ret)
		goto out_put;

	ret = power_supply_set_property(charger,
					POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
					&inhibit);
	if (ret)
		goto out_put;

	dev_info(priv->dev, "restoring lost %s fuel-gauge profile\n",
		 priv->profile->battery_compatible);
	ret = s2mu005_fg_program_profile(priv);

	restore_ret = power_supply_set_property(charger,
						POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
						&behaviour);
	if (!ret)
		ret = restore_ret;

out_put:
	power_supply_put(charger);

	return ret;
}

static irqreturn_t s2mu005_handle_irq(int irq, void *data)
{
	struct s2mu005_fg *priv = data;

	msleep(100);
	power_supply_changed(priv->psy);

	return IRQ_HANDLED;
}

static int s2mu005_fg_get_voltage_now(struct s2mu005_fg *priv, int *value)
{
	struct regmap *regmap = priv->regmap;
	u32 val;
	int ret;

	ret = regmap_read(regmap, S2MU005_FG_REG_RVBAT, &val);
	if (ret < 0) {
		dev_err(priv->dev, "failed to read voltage register (%d)\n", ret);
		return ret;
	}

	*value = (val * MICRO) >> 13;

	return 0;
}

static int s2mu005_fg_get_voltage_avg(struct s2mu005_fg *priv, int *value)
{
	struct regmap *regmap = priv->regmap;
	u8 selector = priv->revision >= 0x0a ?
		S2MU005_FG_MONOUTSEL_REV10_AVGVOLTAGE :
		S2MU005_FG_MONOUTSEL_AVGVOLTAGE;
	u32 val;
	int restore_ret;
	int ret;

	mutex_lock(&priv->monout_mutex);

	ret = s2mu005_fg_write_byte(priv, S2MU005_FG_REG_MONOUTSEL, selector);
	if (ret < 0) {
		dev_err(priv->dev, "failed to enable average voltage monitoring (%d)\n",
			ret);
		goto unlock;
	}

	msleep(50);

	ret = regmap_read(regmap, S2MU005_FG_REG_MONOUT, &val);
	if (ret < 0) {
		dev_err(priv->dev, "failed to read average voltage register (%d)\n",
			ret);
	} else {
		*value = (val * MICRO) >> 12;
	}

	restore_ret = s2mu005_fg_write_byte(priv, S2MU005_FG_REG_MONOUTSEL,
					    S2MU005_FG_MONOUTSEL_IDLE);
	if (!ret)
		ret = restore_ret;

unlock:
	mutex_unlock(&priv->monout_mutex);

	return ret;
}

static int s2mu005_fg_get_current_now(struct s2mu005_fg *priv, int *value)
{
	struct regmap *regmap = priv->regmap;
	u32 val;
	int ret;

	ret = regmap_read(regmap, S2MU005_FG_REG_RCURCC, &val);
	if (ret < 0) {
		dev_err(priv->dev, "failed to read current register (%d)\n", ret);
		return ret;
	}

	*value = -((s16)val * MICRO) >> 12;

	return 0;
}

static int s2mu005_fg_get_current_avg(struct s2mu005_fg *priv, int *value)
{
	struct regmap *regmap = priv->regmap;
	u8 selector = priv->revision >= 0x0a ?
		S2MU005_FG_MONOUTSEL_REV10_AVGCURRENT :
		S2MU005_FG_MONOUTSEL_AVGCURRENT;
	u32 val;
	int restore_ret;
	int ret;

	mutex_lock(&priv->monout_mutex);

	ret = s2mu005_fg_write_byte(priv, S2MU005_FG_REG_MONOUTSEL, selector);
	if (ret < 0) {
		dev_err(priv->dev, "failed to enable average current monitoring (%d)\n",
			ret);
		goto unlock;
	}

	ret = regmap_read(regmap, S2MU005_FG_REG_MONOUT, &val);
	if (ret < 0) {
		dev_err(priv->dev, "failed to read average current register (%d)\n",
			ret);
	} else {
		*value = -((s16)val * MICRO) >> 12;
	}

	restore_ret = s2mu005_fg_write_byte(priv, S2MU005_FG_REG_MONOUTSEL,
					    S2MU005_FG_MONOUTSEL_IDLE);
	if (!ret)
		ret = restore_ret;

unlock:
	mutex_unlock(&priv->monout_mutex);

	return ret;
}

static int s2mu005_fg_get_capacity(struct s2mu005_fg *priv, int *value)
{
	struct regmap *regmap = priv->regmap;
	s16 raw_soc;
	u32 val;
	int ret;

	ret = regmap_read(regmap, S2MU005_FG_REG_RSOC, &val);
	if (ret < 0) {
		dev_err(priv->dev, "failed to read capacity register (%d)\n", ret);
		return ret;
	}

	raw_soc = val;
	*value = clamp_t(int, (raw_soc * CENTI) >> 14, 0, 100);

	return 0;
}

static int s2mu005_fg_get_temperature(struct s2mu005_fg *priv, int *value)
{
	const struct s2mu005_fg_temp_point *table = priv->profile->temp_table;
	size_t high = priv->profile->temp_table_size - 1;
	size_t low = 0;
	int adc_max = INT_MIN;
	int adc_min = INT_MAX;
	int adc_total = 0;
	int adc;
	int ret;

	for (int i = 0; i < S2MU005_FG_TEMP_SAMPLES; i++) {
		ret = iio_read_channel_raw(priv->battery_temp, &adc);
		if (ret < 0)
			return ret;

		adc_max = max(adc_max, adc);
		adc_min = min(adc_min, adc);
		adc_total += adc;
	}

	adc = (adc_total - adc_max - adc_min) /
		(S2MU005_FG_TEMP_SAMPLES - 2);

	if (adc <= table[0].adc) {
		*value = table[0].temp_decic;
		return 0;
	}

	if (adc >= table[high].adc) {
		*value = table[high].temp_decic;
		return 0;
	}

	while (low <= high) {
		size_t mid = (low + high) / 2;

		if (table[mid].adc > adc) {
			high = mid - 1;
		} else if (table[mid].adc < adc) {
			low = mid + 1;
		} else {
			*value = table[mid].temp_decic;
			return 0;
		}
	}

	*value = table[high].temp_decic;
	*value += (table[low].temp_decic - table[high].temp_decic) *
		(adc - table[high].adc) /
		(table[low].adc - table[high].adc);

	return 0;
}

static int s2mu005_fg_get_status(struct s2mu005_fg *priv, int *value)
{
	int current_now, current_avg, capacity;
	int ret;

	ret = s2mu005_fg_get_current_now(priv, &current_now);
	if (ret < 0)
		return ret;

	ret = s2mu005_fg_get_current_avg(priv, &current_avg);
	if (ret < 0)
		return ret;

	/*
	 * Verify both current values reported to reduce inaccuracies due to
	 * internal hysteresis.
	 */
	if (current_now < 0 && current_avg < 0) {
		*value = POWER_SUPPLY_STATUS_DISCHARGING;
	} else if (current_now == 0) {
		*value = POWER_SUPPLY_STATUS_NOT_CHARGING;
	} else {
		*value = POWER_SUPPLY_STATUS_CHARGING;

		ret = s2mu005_fg_get_capacity(priv, &capacity);
		if (!ret && capacity > 98)
			*value = POWER_SUPPLY_STATUS_FULL;
		return ret;
	}

	return 0;
}

static const enum power_supply_property s2mu005_fg_properties[] = {
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_STATUS,
};

static const enum power_supply_property s2mu005_fg_temp_properties[] = {
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TEMP,
};

static int s2mu005_fg_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct s2mu005_fg *priv = power_supply_get_drvdata(psy);
	int ret;

	mutex_lock(&priv->init_mutex);

	ret = s2mu005_fg_initialize_if_needed_locked(priv);
	if (ret)
		goto out_unlock;

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = s2mu005_fg_get_voltage_now(priv, &val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		ret = s2mu005_fg_get_voltage_avg(priv, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = s2mu005_fg_get_current_now(priv, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		ret = s2mu005_fg_get_current_avg(priv, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = s2mu005_fg_get_capacity(priv, &val->intval);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		ret = s2mu005_fg_get_status(priv, &val->intval);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = s2mu005_fg_get_temperature(priv, &val->intval);
		break;
	default:
		ret = -EINVAL;
		break;
	}

out_unlock:
	mutex_unlock(&priv->init_mutex);

	return ret;
}

static const struct power_supply_desc s2mu005_fg_desc = {
	.name = "s2mu005-fuel-gauge",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = s2mu005_fg_properties,
	.num_properties = ARRAY_SIZE(s2mu005_fg_properties),
	.get_property = s2mu005_fg_get_property,
};

static const struct power_supply_desc s2mu005_fg_temp_desc = {
	.name = "s2mu005-fuel-gauge",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = s2mu005_fg_temp_properties,
	.num_properties = ARRAY_SIZE(s2mu005_fg_temp_properties),
	.get_property = s2mu005_fg_get_property,
};

static int s2mu005_fg_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct s2mu005_fg *priv;
	struct power_supply_config psy_cfg = {};
	const struct power_supply_desc *psy_desc;
	u8 revision;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);
	priv->dev = dev;
	priv->client = client;
	priv->profile = s2mu005_fg_get_profile(dev);
	priv->battery_temp = devm_iio_channel_get(dev, "battery-temperature");
	if (IS_ERR(priv->battery_temp)) {
		ret = PTR_ERR(priv->battery_temp);
		if (ret != -ENODEV)
			return dev_err_probe(dev, ret,
					     "failed to get battery-temperature channel\n");

		priv->battery_temp = NULL;
	}

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_I2C | I2C_FUNC_SMBUS_BYTE_DATA))
		return dev_err_probe(dev, -EOPNOTSUPP,
				     "adapter lacks required I2C operations\n");

	priv->regmap = devm_regmap_init_i2c(client, &s2mu005_fg_regmap_config);
	if (IS_ERR(priv->regmap))
		return dev_err_probe(dev, PTR_ERR(priv->regmap),
				     "failed to initialize regmap\n");

	ret = devm_mutex_init(dev, &priv->monout_mutex);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to initialize MONOUT mutex\n");

	ret = devm_mutex_init(dev, &priv->init_mutex);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to initialize profile mutex\n");

	ret = s2mu005_fg_read_byte(priv, S2MU005_FG_REG_REVISION, &revision);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to read fuel-gauge revision\n");
	priv->revision = revision >> 4;

	mutex_lock(&priv->init_mutex);
	ret = s2mu005_fg_initialize_if_needed_locked(priv);
	mutex_unlock(&priv->init_mutex);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to initialize fuel gauge\n");

	psy_desc = device_get_match_data(dev);
	if (priv->battery_temp && priv->profile && priv->profile->temp_table &&
	    priv->profile->temp_table_size)
		psy_desc = &s2mu005_fg_temp_desc;

	psy_cfg.drv_data = priv;
	psy_cfg.fwnode = dev_fwnode(dev);
	priv->psy = devm_power_supply_register(priv->dev, psy_desc, &psy_cfg);
	if (IS_ERR(priv->psy))
		return dev_err_probe(dev, PTR_ERR(priv->psy),
				     "failed to register power supply subsystem\n");

	if (client->irq > 0) {
		ret = devm_request_threaded_irq(priv->dev, client->irq, NULL,
						s2mu005_handle_irq, IRQF_ONESHOT,
						psy_desc->name, priv);
		if (ret)
			return dev_err_probe(dev, ret, "failed to request IRQ\n");
	}

	return 0;
}

static const struct of_device_id s2mu005_fg_of_match_table[] = {
	{
		.compatible = "samsung,s2mu005-fuel-gauge",
		.data = &s2mu005_fg_desc,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, s2mu005_fg_of_match_table);

static struct i2c_driver s2mu005_fg_i2c_driver = {
	.probe = s2mu005_fg_i2c_probe,
	.driver = {
		.name = "s2mu005-fuel-gauge",
		.of_match_table = s2mu005_fg_of_match_table,
	},
};
module_i2c_driver(s2mu005_fg_i2c_driver);

MODULE_DESCRIPTION("Samsung S2MU005 PMIC Battery Fuel Gauge Driver");
MODULE_AUTHOR("Yassine Oudjana <y.oudjana@protonmail.com>");
MODULE_AUTHOR("Kaustabh Chakraborty <kauschluss@disroot.org>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_CONSUMER");
