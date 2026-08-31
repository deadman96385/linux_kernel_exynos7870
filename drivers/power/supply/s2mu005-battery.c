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
	const struct s2mu005_fg_profile *profiles;
	size_t profile_count;
	const struct s2mu005_fg_profile *profile;
	struct iio_channel *battery_temp;
	int cycle_count;
	u8 revision;
};

struct s2mu005_fg_temp_point {
	int adc;
	int temp_decic;
};

struct s2mu005_fg_profile {
	const char *battery_compatible;
	unsigned int cycle_count;
	unsigned int charge_voltage_uv;
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

/* Five cycle-age steps from Samsung's j7y17lte EB-BJ730ABE profile. */
static const struct s2mu005_fg_profile s2mu005_eb_bj730abe_profiles[] = {
	{
	.battery_compatible = "samsung,eb-bj730abe",
	.cycle_count = 0,
	.charge_voltage_uv = 4350000,
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
	}, {
	.battery_compatible = "samsung,eb-bj730abe",
	.cycle_count = 200,
	.charge_voltage_uv = 4330000,
	.battery_table3 = {
		82, 11, 213, 10, 87, 10, 220, 9, 108, 9,
		0, 9, 157, 8, 55, 8, 204, 7, 141, 7,
		44, 7, 218, 6, 166, 6, 127, 6, 94, 6,
		66, 6, 31, 6, 248, 5, 192, 5, 142, 5,
		82, 5, 151, 1, 216, 8, 105, 8, 251, 7,
		140, 7, 30, 7, 175, 6, 65, 6, 211, 5,
		100, 5, 246, 4, 135, 4, 25, 4, 170, 3,
		60, 3, 205, 2, 95, 2, 240, 1, 130, 1,
		19, 1, 165, 0, 54, 0, 200, 15,
	},
	.battery_table4 = {
		55, 55, 55, 56, 55, 55, 55, 55, 55, 54, 54,
		54, 55, 55, 55, 56, 57, 58, 61, 64, 71, 154,
	},
	.battery_capacity = { 0x33, 0x90, 0x0c, 0xe4 },
	.accumulative_rate = { 0xad, 0x07 },
	.temp_table = s2mu005_eb_bj730abe_temp_table,
	.temp_table_size = ARRAY_SIZE(s2mu005_eb_bj730abe_temp_table),
	}, {
	.battery_compatible = "samsung,eb-bj730abe",
	.cycle_count = 250,
	.charge_voltage_uv = 4310000,
	.battery_table3 = {
		34, 11, 168, 10, 46, 10, 182, 9, 74, 9,
		227, 8, 131, 8, 12, 8, 188, 7, 120, 7,
		23, 7, 207, 6, 159, 6, 121, 6, 90, 6,
		63, 6, 29, 6, 245, 5, 189, 5, 144, 5,
		62, 5, 124, 1, 216, 8, 105, 8, 251, 7,
		140, 7, 30, 7, 176, 6, 65, 6, 211, 5,
		100, 5, 246, 4, 135, 4, 25, 4, 170, 3,
		60, 3, 205, 2, 95, 2, 241, 1, 130, 1,
		20, 1, 165, 0, 55, 0, 200, 15,
	},
	.battery_table4 = {
		60, 60, 60, 60, 59, 59, 58, 59, 58, 58, 58,
		58, 59, 59, 60, 61, 62, 62, 62, 65, 73, 154,
	},
	.battery_capacity = { 0x33, 0x40, 0x0c, 0xd0 },
	.accumulative_rate = { 0xad, 0x07 },
	.temp_table = s2mu005_eb_bj730abe_temp_table,
	.temp_table_size = ARRAY_SIZE(s2mu005_eb_bj730abe_temp_table),
	}, {
	.battery_compatible = "samsung,eb-bj730abe",
	.cycle_count = 300,
	.charge_voltage_uv = 4290000,
	.battery_table3 = {
		243, 10, 126, 10, 9, 10, 149, 9, 44, 9,
		201, 8, 111, 8, 240, 7, 175, 7, 97, 7,
		2, 7, 197, 6, 152, 6, 116, 6, 87, 6,
		59, 6, 22, 6, 239, 5, 183, 5, 140, 5,
		68, 5, 154, 1, 181, 8, 72, 8, 219, 7,
		111, 7, 2, 7, 149, 6, 40, 6, 187, 5,
		78, 5, 225, 4, 116, 4, 8, 4, 155, 3,
		46, 3, 193, 2, 84, 2, 231, 1, 122, 1,
		14, 1, 161, 0, 52, 0, 199, 15,
	},
	.battery_table4 = {
		55, 55, 55, 55, 55, 56, 55, 56, 56, 55, 55,
		55, 56, 57, 57, 58, 59, 60, 63, 67, 73, 154,
	},
	.battery_capacity = { 0x32, 0xa0, 0x0c, 0xa0 },
	.accumulative_rate = { 0xad, 0x07 },
	.temp_table = s2mu005_eb_bj730abe_temp_table,
	.temp_table_size = ARRAY_SIZE(s2mu005_eb_bj730abe_temp_table),
	}, {
	.battery_compatible = "samsung,eb-bj730abe",
	.cycle_count = 1000,
	.charge_voltage_uv = 4240000,
	.battery_table3 = {
		126, 10, 18, 10, 166, 9, 59, 9, 220, 8,
		136, 8, 10, 8, 192, 7, 134, 7, 38, 7,
		221, 6, 173, 6, 135, 6, 104, 6, 77, 6,
		49, 6, 14, 6, 228, 5, 173, 5, 137, 5,
		238, 4, 70, 1, 181, 8, 72, 8, 220, 7,
		111, 7, 3, 7, 150, 6, 42, 6, 189, 5,
		80, 5, 228, 4, 119, 4, 11, 4, 158, 3,
		50, 3, 197, 2, 88, 2, 236, 1, 127, 1,
		19, 1, 166, 0, 58, 0, 205, 15,
	},
	.battery_table4 = {
		56, 56, 56, 56, 56, 56, 57, 57, 56, 55, 56,
		56, 57, 58, 59, 60, 60, 62, 65, 70, 77, 154,
	},
	.battery_capacity = { 0x30, 0xe8, 0x0c, 0x3a },
	.accumulative_rate = { 0xad, 0x07 },
	.temp_table = s2mu005_eb_bj730abe_temp_table,
	.temp_table_size = ARRAY_SIZE(s2mu005_eb_bj730abe_temp_table),
	},
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
s2mu005_fg_get_profiles(struct device *dev, size_t *profile_count)
{
	struct device_node *battery;
	const struct s2mu005_fg_profile *profiles = NULL;

	*profile_count = 0;

	battery = of_parse_phandle(dev->of_node, "monitored-battery", 0);
	if (!battery)
		return NULL;

	if (of_device_is_compatible(battery,
				    s2mu005_eb_bj730abe_profiles[0].battery_compatible)) {
		profiles = s2mu005_eb_bj730abe_profiles;
		*profile_count = ARRAY_SIZE(s2mu005_eb_bj730abe_profiles);
	}

	of_node_put(battery);

	return profiles;
}

static const struct s2mu005_fg_profile *
s2mu005_fg_profile_for_cycle(struct s2mu005_fg *priv, unsigned int cycle_count)
{
	const struct s2mu005_fg_profile *profile;

	if (!priv->profiles || !priv->profile_count)
		return NULL;

	profile = &priv->profiles[0];
	for (size_t i = 1; i < priv->profile_count; i++) {
		if (cycle_count < priv->profiles[i].cycle_count)
			break;
		profile = &priv->profiles[i];
	}

	return profile;
}

static int s2mu005_fg_program_profile(struct s2mu005_fg *priv,
				      const struct s2mu005_fg_profile *profile)
{
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
	ret = s2mu005_fg_program_profile(priv, priv->profile);

	restore_ret = power_supply_set_property(charger,
						POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
						&behaviour);
	if (!ret)
		ret = restore_ret;

out_put:
	power_supply_put(charger);

	return ret;
}

static int s2mu005_fg_set_cycle_count(struct s2mu005_fg *priv,
				      unsigned int cycle_count)
{
	union power_supply_propval behaviour;
	union power_supply_propval inhibit = {
		.intval = POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE,
	};
	union power_supply_propval old_voltage;
	union power_supply_propval new_voltage;
	const struct s2mu005_fg_profile *new_profile;
	const struct s2mu005_fg_profile *old_profile = priv->profile;
	struct power_supply *charger;
	int rollback_ret;
	int restore_ret;
	bool safe_to_restore = false;
	int ret;

	if (priv->cycle_count >= 0 && cycle_count < priv->cycle_count)
		return -EINVAL;

	new_profile = s2mu005_fg_profile_for_cycle(priv, cycle_count);
	if (!new_profile)
		return -ENODEV;

	if (new_profile == old_profile) {
		priv->cycle_count = cycle_count;
		return 0;
	}

	charger = power_supply_get_by_name("s2mu005-charger");
	if (!charger)
		return -EPROBE_DEFER;

	ret = power_supply_get_property(charger,
					POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
					&behaviour);
	if (ret)
		goto out_put;

	ret = power_supply_get_property(charger,
					POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
					&old_voltage);
	if (ret)
		goto out_put;

	ret = power_supply_set_property(charger,
					POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
					&inhibit);
	if (ret)
		goto out_put;

	new_voltage.intval = min_t(int, old_voltage.intval,
				   new_profile->charge_voltage_uv);
	ret = power_supply_set_property(charger,
					POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
					&new_voltage);
	if (ret)
		goto restore_old_voltage;

	ret = s2mu005_fg_program_profile(priv, new_profile);
	if (ret) {
		rollback_ret = s2mu005_fg_program_profile(priv, old_profile);
		if (rollback_ret) {
			dev_err(priv->dev,
				"failed to restore prior fuel-gauge profile (%d); charging remains inhibited\n",
				rollback_ret);
			goto out_put;
		}
		goto restore_old_voltage;
	}

	priv->profile = new_profile;
	priv->cycle_count = cycle_count;
	safe_to_restore = true;
	dev_info(priv->dev,
		 "selected battery age step %zu at %u cycles (%u uV ceiling)\n",
		 (size_t)(new_profile - priv->profiles), cycle_count,
		 new_profile->charge_voltage_uv);
	goto restore_behaviour;

restore_old_voltage:
	rollback_ret = power_supply_set_property(charger,
						 POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
						 &old_voltage);
	if (rollback_ret) {
		dev_err(priv->dev,
			"failed to restore charge voltage (%d); charging remains inhibited\n",
			rollback_ret);
		goto out_put;
	}
	safe_to_restore = true;

restore_behaviour:
	if (safe_to_restore) {
		restore_ret = power_supply_set_property(charger,
							POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
							&behaviour);
		if (restore_ret)
			dev_err(priv->dev,
				"failed to restore charge behaviour (%d)\n",
				restore_ret);
		if (!ret)
			ret = restore_ret;
	}

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
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_STATUS,
};

static const enum power_supply_property s2mu005_fg_temp_properties[] = {
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
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
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		if (priv->cycle_count < 0) {
			ret = -ENODATA;
		} else {
			val->intval = priv->cycle_count;
			ret = 0;
		}
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

static int s2mu005_fg_set_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   const union power_supply_propval *val)
{
	struct s2mu005_fg *priv = power_supply_get_drvdata(psy);
	int ret;

	mutex_lock(&priv->init_mutex);

	ret = s2mu005_fg_initialize_if_needed_locked(priv);
	if (ret)
		goto out_unlock;

	switch (psp) {
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		if (val->intval < 0)
			ret = -EINVAL;
		else
			ret = s2mu005_fg_set_cycle_count(priv, val->intval);
		break;
	default:
		ret = -EINVAL;
		break;
	}

out_unlock:
	mutex_unlock(&priv->init_mutex);
	if (!ret)
		power_supply_changed(priv->psy);

	return ret;
}

static int s2mu005_fg_property_is_writeable(struct power_supply *psy,
					    enum power_supply_property psp)
{
	return psp == POWER_SUPPLY_PROP_CYCLE_COUNT;
}

static const struct power_supply_desc s2mu005_fg_desc = {
	.name = "s2mu005-fuel-gauge",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = s2mu005_fg_properties,
	.num_properties = ARRAY_SIZE(s2mu005_fg_properties),
	.get_property = s2mu005_fg_get_property,
	.set_property = s2mu005_fg_set_property,
	.property_is_writeable = s2mu005_fg_property_is_writeable,
};

static const struct power_supply_desc s2mu005_fg_temp_desc = {
	.name = "s2mu005-fuel-gauge",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = s2mu005_fg_temp_properties,
	.num_properties = ARRAY_SIZE(s2mu005_fg_temp_properties),
	.get_property = s2mu005_fg_get_property,
	.set_property = s2mu005_fg_set_property,
	.property_is_writeable = s2mu005_fg_property_is_writeable,
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
	priv->profiles = s2mu005_fg_get_profiles(dev, &priv->profile_count);
	priv->profile = s2mu005_fg_profile_for_cycle(priv, 0);
	priv->cycle_count = -1;
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
