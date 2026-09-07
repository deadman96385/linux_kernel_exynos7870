// SPDX-License-Identifier: GPL-2.0
/*
 * Battery Charger Driver for Samsung S2M series PMICs.
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd
 * Copyright (c) 2026 Kaustabh Chakraborty <kauschluss@disroot.org>
 * Copyright (c) 2026 Łukasz Lebiedziński <kernel@lvkasz.us>
 */

#include <linux/delay.h>
#include <linux/devm-helpers.h>
#include <linux/extcon.h>
#include <linux/interrupt.h>
#include <linux/limits.h>
#include <linux/mfd/samsung/core.h>
#include <linux/mfd/samsung/s2mu005.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>

#define S2MU005_INPUT_CURRENT_MIN_UA	100000
#define S2MU005_INPUT_CURRENT_MAX_UA	3250000
#define S2MU005_INPUT_CURRENT_STEP_UA	50000

#define S2MU005_FAST_CURRENT_MIN_UA	100000
#define S2MU005_FAST_CURRENT_MAX_UA	2600000
#define S2MU005_FAST_CURRENT_STEP_UA	50000
#define S2MU005_COOL_CURRENT_MAX_CODE	0x13

#define S2MU005_FLOAT_VOLTAGE_MIN_UV	3900000
#define S2MU005_FLOAT_VOLTAGE_MAX_UV	4400000
#define S2MU005_FLOAT_VOLTAGE_STEP_UV	10000

#define S2MU005_TOPOFF_CURRENT_MIN_UA	100000
#define S2MU005_TOPOFF_CURRENT_MAX_UA	475000
#define S2MU005_TOPOFF_CURRENT_STEP_UA	25000

/* The stock 460 mA SDP ceiling is conservatively encoded as 450 mA. */
#define S2MU005_SDP_INPUT_CURRENT_UA	450000
#define S2MU005_SDP_CHARGE_CURRENT_UA	450000
#define S2MU005_CDP_INPUT_CURRENT_UA	1000000
#define S2MU005_CDP_CHARGE_CURRENT_UA	1000000
#define S2MU005_DCP_INPUT_CURRENT_UA	900000
#define S2MU005_DCP_CHARGE_CURRENT_UA	1200000
#define S2MU005_EXTCON_DEBOUNCE_MS	20
#define S2MU005_SOURCE_RETRY_MS		1000
#define S2MU005_SOURCE_RETRY_MAX		10
#define S2MU005_MONITOR_INTERVAL_MS	10000
#define S2MU005_WATCHDOG_INTERVAL_MS	40000

enum s2mu005_thermal_state {
	S2MU005_THERMAL_UNKNOWN,
	S2MU005_THERMAL_NORMAL,
	S2MU005_THERMAL_COLD,
	S2MU005_THERMAL_HOT,
};

struct s2m_chgr {
	struct device *dev;
	struct regmap *regmap;
	struct power_supply *psy;
	struct extcon_dev *extcon;
	struct delayed_work extcon_work;
	struct delayed_work monitor_work;
	struct delayed_work watchdog_work;
	struct notifier_block extcon_nb;
	/* Serializes source, charge, and OTG mode transitions. */
	struct mutex lock;
	bool charge_enabled;
	int input_current_ua;
	int charge_current_ua;
	int charge_current_max_ua;
	int charge_voltage_uv;
	int charge_voltage_max_uv;
	int term_current_ua;
	int temp_stop_min_decic;
	int temp_stop_max_decic;
	int temp_resume_min_decic;
	int temp_resume_max_decic;
	int battery_temp_decic;
	enum s2mu005_thermal_state thermal_state;
	unsigned int source_retries;
	bool thermal_policy_dirty;
	bool thermal_limits_valid;
	bool watchdog_enabled;
	bool watchdog_work_ready;
	bool suspended;
	bool stopping;
};

static int s2mu005_chgr_get_usb_type(struct s2m_chgr *priv, int *value)
{
	int state;

	state = extcon_get_state(priv->extcon, EXTCON_CHG_USB_SLOW);
	if (state < 0)
		return state;
	if (state) {
		*value = POWER_SUPPLY_USB_TYPE_SDP;
		return 0;
	}

	state = extcon_get_state(priv->extcon, EXTCON_CHG_USB_CDP);
	if (state < 0)
		return state;
	if (state) {
		*value = POWER_SUPPLY_USB_TYPE_CDP;
		return 0;
	}

	state = extcon_get_state(priv->extcon, EXTCON_CHG_USB_SDP);
	if (state < 0)
		return state;
	if (state) {
		*value = POWER_SUPPLY_USB_TYPE_SDP;
		return 0;
	}

	state = extcon_get_state(priv->extcon, EXTCON_CHG_USB_DCP);
	if (state < 0)
		return state;
	if (state) {
		*value = POWER_SUPPLY_USB_TYPE_DCP;
		return 0;
	}

	*value = POWER_SUPPLY_USB_TYPE_UNKNOWN;

	return 0;
}

static int s2mu005_chgr_get_online(struct s2m_chgr *priv, int *value)
{
	int usb_type;
	int ret;

	ret = s2mu005_chgr_get_usb_type(priv, &usb_type);
	if (ret)
		return ret;

	*value = usb_type != POWER_SUPPLY_USB_TYPE_UNKNOWN;

	return 0;
}

static int s2mu005_chgr_get_status(struct s2m_chgr *priv, int *value)
{
	unsigned int status;
	unsigned int state;
	int online;
	int ret;

	ret = regmap_read(priv->regmap, S2MU005_REG_CHGR_STATUS0, &status);
	if (ret)
		return ret;

	state = FIELD_GET(S2MU005_CHGR_STAT, status);
	switch (state) {
	case S2MU005_CHGR_STAT_PRE_CHG:
	case S2MU005_CHGR_STAT_COOL_CHG:
	case S2MU005_CHGR_STAT_CC:
	case S2MU005_CHGR_STAT_CV:
	case S2MU005_CHGR_STAT_TOPOFF:
		*value = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case S2MU005_CHGR_STAT_DONE_FLAG:
	case S2MU005_CHGR_STAT_DONE:
		*value = POWER_SUPPLY_STATUS_FULL;
		break;
	case S2MU005_CHGR_STAT_INPUT_INVALID:
		*value = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case S2MU005_CHGR_STAT_OFF:
	default:
		ret = s2mu005_chgr_get_online(priv, &online);
		if (ret)
			return ret;
		*value = online ? POWER_SUPPLY_STATUS_NOT_CHARGING :
			POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	}

	return 0;
}

static int s2mu005_chgr_get_health(struct s2m_chgr *priv, int *value)
{
	unsigned int status1;
	unsigned int status3;
	int ret;

	ret = regmap_read(priv->regmap, S2MU005_REG_CHGR_STATUS1, &status1);
	if (ret)
		return ret;

	ret = regmap_read(priv->regmap, S2MU005_REG_CHGR_STATUS3, &status3);
	if (ret)
		return ret;

	if (FIELD_GET(S2MU005_CHGR_EVT, status3) ==
	    S2MU005_CHGR_EVT_THERM_SHUTDOWN)
		*value = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (FIELD_GET(S2MU005_CHGR_VBUS_OVP, status1) ==
		 S2MU005_CHGR_VBUS_OVP_OVERVOLT)
		*value = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	else if (priv->thermal_state == S2MU005_THERMAL_COLD)
		*value = POWER_SUPPLY_HEALTH_COLD;
	else if (priv->thermal_state == S2MU005_THERMAL_HOT)
		*value = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (priv->thermal_state == S2MU005_THERMAL_UNKNOWN)
		*value = POWER_SUPPLY_HEALTH_UNKNOWN;
	else
		*value = POWER_SUPPLY_HEALTH_GOOD;

	return 0;
}

static int s2mu005_chgr_get_present(struct s2m_chgr *priv, int *value)
{
	unsigned int status;
	int ret;

	ret = regmap_read(priv->regmap, S2MU005_REG_CHGR_STATUS1, &status);
	if (ret)
		return ret;

	*value = !!(status & S2MU005_CHGR_DETBAT);

	return 0;
}

static int s2mu005_chgr_get_charge_type(struct s2m_chgr *priv, int *value)
{
	unsigned int status;
	unsigned int state;
	int ret;

	ret = regmap_read(priv->regmap, S2MU005_REG_CHGR_STATUS0, &status);
	if (ret)
		return ret;

	state = FIELD_GET(S2MU005_CHGR_STAT, status);
	switch (state) {
	case S2MU005_CHGR_STAT_PRE_CHG:
		*value = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
		break;
	case S2MU005_CHGR_STAT_COOL_CHG:
	case S2MU005_CHGR_STAT_CC:
	case S2MU005_CHGR_STAT_CV:
	case S2MU005_CHGR_STAT_TOPOFF:
		*value = POWER_SUPPLY_CHARGE_TYPE_FAST;
		break;
	case S2MU005_CHGR_STAT_DONE_FLAG:
	case S2MU005_CHGR_STAT_DONE:
		*value = POWER_SUPPLY_CHARGE_TYPE_NONE;
		break;
	default:
		*value = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
		break;
	}

	return 0;
}

static int s2mu005_chgr_read_field(struct s2m_chgr *priv, unsigned int reg,
				   unsigned int mask, unsigned int *value)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(priv->regmap, reg, &regval);
	if (ret)
		return ret;

	*value = FIELD_GET(mask, regval);

	return 0;
}

static int s2mu005_chgr_set_input_current(struct s2m_chgr *priv, int ua)
{
	unsigned int code;
	int ret;

	if (ua < S2MU005_INPUT_CURRENT_MIN_UA ||
	    ua > S2MU005_INPUT_CURRENT_MAX_UA)
		return -EINVAL;

	code = (ua - S2MU005_INPUT_CURRENT_MIN_UA) /
		S2MU005_INPUT_CURRENT_STEP_UA;
	ret = regmap_update_bits(priv->regmap, S2MU005_REG_CHGR_CTRL2,
				 S2MU005_CHGR_IN_CURR_LIM,
				 FIELD_PREP(S2MU005_CHGR_IN_CURR_LIM, code));
	if (!ret)
		priv->input_current_ua = S2MU005_INPUT_CURRENT_MIN_UA +
			code * S2MU005_INPUT_CURRENT_STEP_UA;

	return ret;
}

static int s2mu005_chgr_get_input_current(struct s2m_chgr *priv, int *ua)
{
	unsigned int code;
	int ret;

	ret = s2mu005_chgr_read_field(priv, S2MU005_REG_CHGR_CTRL2,
				      S2MU005_CHGR_IN_CURR_LIM, &code);
	if (!ret)
		*ua = S2MU005_INPUT_CURRENT_MIN_UA +
			code * S2MU005_INPUT_CURRENT_STEP_UA;

	return ret;
}

static int s2mu005_chgr_set_charge_current(struct s2m_chgr *priv, int ua)
{
	unsigned int cool_code;
	unsigned int old_fast_code;
	unsigned int code;
	int rollback_ret;
	int ret;

	if (ua < S2MU005_FAST_CURRENT_MIN_UA ||
	    ua > S2MU005_FAST_CURRENT_MAX_UA)
		return -EINVAL;

	code = (ua - S2MU005_FAST_CURRENT_MIN_UA) /
		S2MU005_FAST_CURRENT_STEP_UA + 1;
	ret = s2mu005_chgr_read_field(priv, S2MU005_REG_CHGR_CTRL7,
				      S2MU005_CHGR_FAST_CHG_CURR,
				      &old_fast_code);
	if (ret)
		return ret;

	ret = regmap_update_bits(priv->regmap, S2MU005_REG_CHGR_CTRL7,
				 S2MU005_CHGR_FAST_CHG_CURR,
				 FIELD_PREP(S2MU005_CHGR_FAST_CHG_CURR, code));
	if (ret)
		return ret;

	/* Samsung limits the cool-charge path to 1 A for boot stability. */
	cool_code = min(code, S2MU005_COOL_CURRENT_MAX_CODE);
	ret = regmap_update_bits(priv->regmap, S2MU005_REG_CHGR_CTRL6,
				 S2MU005_CHGR_COOL_CHG_CURR,
				 FIELD_PREP(S2MU005_CHGR_COOL_CHG_CURR, cool_code));
	if (ret) {
		rollback_ret = regmap_update_bits(priv->regmap,
						  S2MU005_REG_CHGR_CTRL7,
						  S2MU005_CHGR_FAST_CHG_CURR,
						  FIELD_PREP(S2MU005_CHGR_FAST_CHG_CURR,
							     old_fast_code));
		if (rollback_ret)
			dev_warn(priv->dev,
				 "failed to restore fast-charge current (%d)\n",
				 rollback_ret);
		return ret;
	}

	priv->charge_current_ua = S2MU005_FAST_CURRENT_MIN_UA +
		(code - 1) * S2MU005_FAST_CURRENT_STEP_UA;

	return 0;
}

static int s2mu005_chgr_get_charge_current(struct s2m_chgr *priv, int *ua)
{
	unsigned int code;
	int ret;

	ret = s2mu005_chgr_read_field(priv, S2MU005_REG_CHGR_CTRL7,
				      S2MU005_CHGR_FAST_CHG_CURR, &code);
	if (ret)
		return ret;

	if (!code)
		*ua = 0;
	else
		*ua = S2MU005_FAST_CURRENT_MIN_UA +
			(code - 1) * S2MU005_FAST_CURRENT_STEP_UA;

	return 0;
}

static int s2mu005_chgr_set_charge_voltage(struct s2m_chgr *priv, int uv)
{
	unsigned int code;
	int ret;

	if (uv < S2MU005_FLOAT_VOLTAGE_MIN_UV ||
	    uv > S2MU005_FLOAT_VOLTAGE_MAX_UV)
		return -EINVAL;

	code = (uv - S2MU005_FLOAT_VOLTAGE_MIN_UV) /
		S2MU005_FLOAT_VOLTAGE_STEP_UV;
	ret = regmap_update_bits(priv->regmap, S2MU005_REG_CHGR_CTRL8,
				 S2MU005_CHGR_VF_VBAT,
				 FIELD_PREP(S2MU005_CHGR_VF_VBAT, code));
	if (!ret)
		priv->charge_voltage_uv = S2MU005_FLOAT_VOLTAGE_MIN_UV +
			code * S2MU005_FLOAT_VOLTAGE_STEP_UV;

	return ret;
}

static int s2mu005_chgr_get_charge_voltage(struct s2m_chgr *priv, int *uv)
{
	unsigned int code;
	int ret;

	ret = s2mu005_chgr_read_field(priv, S2MU005_REG_CHGR_CTRL8,
				      S2MU005_CHGR_VF_VBAT, &code);
	if (!ret)
		*uv = S2MU005_FLOAT_VOLTAGE_MIN_UV +
			code * S2MU005_FLOAT_VOLTAGE_STEP_UV;

	return ret;
}

static int s2mu005_chgr_set_term_current(struct s2m_chgr *priv, int ua)
{
	unsigned int code;
	int ret;

	if (ua < S2MU005_TOPOFF_CURRENT_MIN_UA ||
	    ua > S2MU005_TOPOFF_CURRENT_MAX_UA)
		return -EINVAL;

	code = (ua - S2MU005_TOPOFF_CURRENT_MIN_UA) /
		S2MU005_TOPOFF_CURRENT_STEP_UA;
	ret = regmap_update_bits(priv->regmap, S2MU005_REG_CHGR_CTRL10,
				 S2MU005_CHGR_TOPOFF_CURR(0),
				 FIELD_PREP(S2MU005_CHGR_TOPOFF_CURR(0), code));
	if (!ret)
		priv->term_current_ua = S2MU005_TOPOFF_CURRENT_MIN_UA +
			code * S2MU005_TOPOFF_CURRENT_STEP_UA;

	return ret;
}

static int s2mu005_chgr_get_term_current(struct s2m_chgr *priv, int *ua)
{
	unsigned int code;
	int ret;

	ret = s2mu005_chgr_read_field(priv, S2MU005_REG_CHGR_CTRL10,
				      S2MU005_CHGR_TOPOFF_CURR(0), &code);
	if (!ret)
		*ua = S2MU005_TOPOFF_CURRENT_MIN_UA +
			code * S2MU005_TOPOFF_CURRENT_STEP_UA;

	return ret;
}

static int s2mu005_chgr_set_watchdog(struct s2m_chgr *priv, bool enable)
{
	int disable_ret;
	int ret;

	if (!enable) {
		ret = regmap_update_bits(priv->regmap, S2MU005_REG_CHGR_CTRL12,
					 S2MU005_CHGR_WDT,
					 FIELD_PREP(S2MU005_CHGR_WDT,
						    S2MU005_CHGR_WDT_OFF));
		priv->watchdog_enabled = false;
		return ret;
	}

	if (!priv->watchdog_work_ready)
		return -EAGAIN;

	ret = regmap_update_bits(priv->regmap, S2MU005_REG_CHGR_CTRL12,
				 S2MU005_CHGR_WDT,
				 FIELD_PREP(S2MU005_CHGR_WDT,
					    S2MU005_CHGR_WDT_ON));
	if (ret)
		return ret;

	ret = regmap_set_bits(priv->regmap, S2MU005_REG_CHGR_CTRL13,
			      S2MU005_CHGR_WDT_CLEAR);
	if (ret) {
		disable_ret = regmap_update_bits(priv->regmap,
						 S2MU005_REG_CHGR_CTRL12,
						 S2MU005_CHGR_WDT,
						 FIELD_PREP(S2MU005_CHGR_WDT,
							    S2MU005_CHGR_WDT_OFF));
		if (disable_ret)
			dev_warn(priv->dev, "failed to disable watchdog (%d)\n",
				 disable_ret);
		return ret;
	}

	priv->watchdog_enabled = true;
	mod_delayed_work(system_wq, &priv->watchdog_work,
			 msecs_to_jiffies(S2MU005_WATCHDOG_INTERVAL_MS));

	return 0;
}

static int s2mu005_chgr_mode_unset(struct s2m_chgr *priv)
{
	int first_error = 0;
	int ret;

	ret = regmap_clear_bits(priv->regmap, S2MU005_REG_CHGR_CTRL15,
				S2MU005_CHGR_OTG_EN);
	if (ret)
		first_error = ret;

	ret = regmap_clear_bits(priv->regmap, S2MU005_REG_CHGR_CTRL0,
				S2MU005_CHGR_OP_MODE);
	if (ret && !first_error)
		first_error = ret;

	ret = s2mu005_chgr_set_watchdog(priv, false);
	if (ret && !first_error)
		first_error = ret;

	return first_error;
}

static int s2mu005_chgr_mode_set_host(struct s2m_chgr *priv)
{
	unsigned int ctrl0;
	unsigned int status0;
	int ret;

	ret = regmap_read(priv->regmap, S2MU005_REG_CHGR_CTRL0, &ctrl0);
	if (ret)
		return ret;

	if (FIELD_GET(S2MU005_CHGR_OP_MODE, ctrl0) !=
	    S2MU005_CHGR_OP_MODE_OTG) {
		ret = regmap_read(priv->regmap, S2MU005_REG_CHGR_STATUS0,
				  &status0);
		if (ret)
			return ret;
		if (status0 & S2MU005_CHGR_VBUS) {
			dev_warn(priv->dev,
				 "refusing OTG while external VBUS is present\n");
			return -EBUSY;
		}
	}

	ret = s2mu005_chgr_mode_unset(priv);
	if (ret)
		return ret;

	ret = regmap_update_bits(priv->regmap, S2MU005_REG_CHGR_CTRL0,
				 S2MU005_CHGR_OP_MODE,
				 FIELD_PREP(S2MU005_CHGR_OP_MODE,
					    S2MU005_CHGR_OP_MODE_OTG));
	if (ret)
		return ret;

	ret = regmap_update_bits(priv->regmap, S2MU005_REG_CHGR_CTRL11,
				 S2MU005_CHGR_OSC_BOOST,
				 FIELD_PREP(S2MU005_CHGR_OSC_BOOST,
					    S2MU005_CHGR_OSC_BOOST_2MHZ));
	if (ret)
		goto err_disable;

	ret = regmap_update_bits(priv->regmap, S2MU005_REG_CHGR_CTRL4,
				 S2MU005_CHGR_OTG_OCP,
				 FIELD_PREP(S2MU005_CHGR_OTG_OCP,
					    S2MU005_CHGR_OTG_OCP_1P5A));
	if (ret)
		goto err_disable;

	ret = regmap_set_bits(priv->regmap, S2MU005_REG_CHGR_CTRL4,
			      S2MU005_CHGR_OTG_OCP_OFF);
	if (ret)
		goto err_disable;

	ret = regmap_update_bits(priv->regmap, S2MU005_REG_CHGR_CTRL5,
				 S2MU005_CHGR_VMID_BOOST,
				 FIELD_PREP(S2MU005_CHGR_VMID_BOOST,
					    S2MU005_CHGR_VMID_BOOST_5P1V));
	if (ret)
		goto err_disable;

	ret = regmap_update_bits(priv->regmap, S2MU005_REG_CHGR_CTRL15,
				 S2MU005_CHGR_OTG_EN,
				 FIELD_PREP(S2MU005_CHGR_OTG_EN,
					    S2MU005_CHGR_OTG_EN_ON));
	if (ret)
		goto err_disable;

	return 0;

err_disable:
	s2mu005_chgr_mode_unset(priv);
	return ret;
}

static int s2mu005_chgr_mode_set_charger(struct s2m_chgr *priv)
{
	int ret;

	ret = s2mu005_chgr_mode_unset(priv);
	if (ret)
		return ret;

	/* Samsung's downstream sequence requires the power path to settle. */
	msleep(50);

	ret = regmap_set_bits(priv->regmap, S2MU005_REG_CHGR_CTRL0,
			      S2MU005_CHGR_CHG_EN);
	if (ret)
		return ret;

	ret = regmap_update_bits(priv->regmap, S2MU005_REG_CHGR_CTRL0,
				 S2MU005_CHGR_OP_MODE,
				 FIELD_PREP(S2MU005_CHGR_OP_MODE,
					    S2MU005_CHGR_OP_MODE_CHG));
	if (ret)
		return ret;

	ret = s2mu005_chgr_set_watchdog(priv, true);
	if (!ret)
		return 0;

	s2mu005_chgr_mode_unset(priv);
	return ret;
}

static void s2mu005_chgr_source_limits(int usb_type, int *input_ua,
				       int *charge_ua)
{
	switch (usb_type) {
	case POWER_SUPPLY_USB_TYPE_SDP:
		*input_ua = S2MU005_SDP_INPUT_CURRENT_UA;
		*charge_ua = S2MU005_SDP_CHARGE_CURRENT_UA;
		break;
	case POWER_SUPPLY_USB_TYPE_CDP:
		*input_ua = S2MU005_CDP_INPUT_CURRENT_UA;
		*charge_ua = S2MU005_CDP_CHARGE_CURRENT_UA;
		break;
	case POWER_SUPPLY_USB_TYPE_DCP:
		*input_ua = S2MU005_DCP_INPUT_CURRENT_UA;
		*charge_ua = S2MU005_DCP_CHARGE_CURRENT_UA;
		break;
	default:
		*input_ua = 0;
		*charge_ua = 0;
		break;
	}
}

static int s2mu005_chgr_apply_source(struct s2m_chgr *priv)
{
	int old_charge_ua = priv->charge_current_ua;
	int old_input_ua = priv->input_current_ua;
	int charge_ua;
	int input_ua;
	int usb_type;
	int rollback_ret;
	int state;
	int ret;

	/* Suspend has already forced the hardware off; preserve only user intent. */
	if (priv->suspended)
		return 0;

	state = extcon_get_state(priv->extcon, EXTCON_USB_HOST);
	if (state < 0)
		return state;
	if (state)
		return s2mu005_chgr_mode_set_host(priv);

	ret = s2mu005_chgr_get_usb_type(priv, &usb_type);
	if (ret)
		return ret;

	s2mu005_chgr_source_limits(usb_type, &input_ua, &charge_ua);
	if (!input_ua)
		return s2mu005_chgr_mode_unset(priv);

	ret = s2mu005_chgr_set_input_current(priv, input_ua);
	if (ret)
		return ret;

	charge_ua = min(charge_ua, priv->charge_current_max_ua);
	ret = s2mu005_chgr_set_charge_current(priv, charge_ua);
	if (ret) {
		rollback_ret = s2mu005_chgr_set_input_current(priv, old_input_ua);
		if (rollback_ret)
			dev_warn(priv->dev,
				 "failed to restore input-current limit (%d)\n",
				 rollback_ret);
		return ret;
	}

	if (!priv->charge_enabled ||
	    priv->thermal_state != S2MU005_THERMAL_NORMAL)
		return s2mu005_chgr_mode_unset(priv);

	ret = s2mu005_chgr_mode_set_charger(priv);
	if (!ret)
		return 0;

	rollback_ret = s2mu005_chgr_set_charge_current(priv, old_charge_ua);
	if (rollback_ret)
		dev_warn(priv->dev,
			 "failed to restore fast-charge current (%d)\n",
			 rollback_ret);
	rollback_ret = s2mu005_chgr_set_input_current(priv, old_input_ua);
	if (rollback_ret)
		dev_warn(priv->dev,
			 "failed to restore input-current limit (%d)\n",
			 rollback_ret);

	return ret;
}

static int s2mu005_chgr_get_property(struct power_supply *psy,
				     enum power_supply_property psp,
				     union power_supply_propval *val)
{
	struct s2m_chgr *priv = power_supply_get_drvdata(psy);
	int ret;

	mutex_lock(&priv->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ret = s2mu005_chgr_get_online(priv, &val->intval);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		ret = s2mu005_chgr_get_status(priv, &val->intval);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		ret = s2mu005_chgr_get_health(priv, &val->intval);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		ret = s2mu005_chgr_get_present(priv, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		ret = s2mu005_chgr_get_charge_type(priv, &val->intval);
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		ret = s2mu005_chgr_get_usb_type(priv, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = s2mu005_chgr_get_input_current(priv, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = s2mu005_chgr_get_charge_current(priv, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = priv->charge_current_max_ua;
		ret = 0;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = s2mu005_chgr_get_charge_voltage(priv, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		val->intval = priv->charge_voltage_max_uv;
		ret = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		ret = s2mu005_chgr_get_term_current(priv, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		val->intval = priv->charge_enabled ?
			POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO :
			POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE;
		ret = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&priv->lock);

	return ret;
}

static int s2mu005_chgr_set_property(struct power_supply *psy,
				     enum power_supply_property psp,
				     const union power_supply_propval *val)
{
	struct s2m_chgr *priv = power_supply_get_drvdata(psy);
	bool old_charge_enabled;
	int rollback_ret;
	int ret;

	mutex_lock(&priv->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = s2mu005_chgr_set_input_current(priv, val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		if (val->intval > priv->charge_current_max_ua)
			ret = -EINVAL;
		else
			ret = s2mu005_chgr_set_charge_current(priv, val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		if (val->intval > priv->charge_voltage_max_uv)
			ret = -EINVAL;
		else
			ret = s2mu005_chgr_set_charge_voltage(priv, val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		ret = s2mu005_chgr_set_term_current(priv, val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		old_charge_enabled = priv->charge_enabled;
		if (val->intval == POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO) {
			priv->charge_enabled = true;
		} else if (val->intval ==
			   POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE) {
			priv->charge_enabled = false;
		} else {
			ret = -EINVAL;
			break;
		}
		ret = s2mu005_chgr_apply_source(priv);
		if (ret) {
			priv->charge_enabled = old_charge_enabled;
			rollback_ret = s2mu005_chgr_apply_source(priv);
			if (rollback_ret)
				dev_warn(priv->dev,
					 "failed to restore charge behaviour (%d)\n",
					 rollback_ret);
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&priv->lock);
	if (!ret)
		power_supply_changed(priv->psy);

	return ret;
}

static int s2mu005_chgr_property_is_writeable(struct power_supply *psy,
					      enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		return 1;
	default:
		return 0;
	}
}

static const enum power_supply_property s2mu005_chgr_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
};

static const struct power_supply_desc s2mu005_chgr_psy_desc = {
	.name = "s2mu005-charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = s2mu005_chgr_properties,
	.num_properties = ARRAY_SIZE(s2mu005_chgr_properties),
	.get_property = s2mu005_chgr_get_property,
	.set_property = s2mu005_chgr_set_property,
	.property_is_writeable = s2mu005_chgr_property_is_writeable,
	.charge_behaviours = BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO) |
			     BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE),
	.usb_types = BIT(POWER_SUPPLY_USB_TYPE_CDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_SDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_DCP) |
		     BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN),
};

static void s2mu005_chgr_extcon_work(struct work_struct *work)
{
	struct s2m_chgr *priv = container_of(to_delayed_work(work),
					    struct s2m_chgr, extcon_work);
	unsigned int retries;
	bool retry = false;
	int ret;

	if (READ_ONCE(priv->stopping))
		return;

	mutex_lock(&priv->lock);
	if (priv->stopping || priv->suspended) {
		mutex_unlock(&priv->lock);
		return;
	}

	ret = s2mu005_chgr_apply_source(priv);
	if (!ret) {
		priv->source_retries = 0;
		priv->thermal_policy_dirty = false;
	} else if (priv->source_retries < S2MU005_SOURCE_RETRY_MAX) {
		priv->source_retries++;
		retry = true;
	}
	retries = priv->source_retries;
	if (retry)
		mod_delayed_work(system_wq, &priv->extcon_work,
				 msecs_to_jiffies(S2MU005_SOURCE_RETRY_MS));
	mutex_unlock(&priv->lock);

	if (ret)
		dev_warn_ratelimited(priv->dev,
				     "cable reconciliation failed (%u/%u retries used): %d\n",
				     retries, S2MU005_SOURCE_RETRY_MAX, ret);

	power_supply_changed(priv->psy);
}

static int s2m_chgr_extcon_notifier(struct notifier_block *nb,
				    unsigned long event, void *param)
{
	struct s2m_chgr *priv = container_of(nb, struct s2m_chgr, extcon_nb);

	mutex_lock(&priv->lock);
	if (!priv->stopping && !priv->suspended) {
		priv->source_retries = 0;
		mod_delayed_work(system_wq, &priv->extcon_work,
				 msecs_to_jiffies(S2MU005_EXTCON_DEBOUNCE_MS));
	}
	mutex_unlock(&priv->lock);

	return NOTIFY_OK;
}

static int s2mu005_chgr_get_battery_temp(struct s2m_chgr *priv, int *temp)
{
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	int ret;

	fuel_gauge = power_supply_get_by_name("s2mu005-fuel-gauge");
	if (!fuel_gauge)
		return -EPROBE_DEFER;

	ret = power_supply_get_property(fuel_gauge, POWER_SUPPLY_PROP_TEMP, &val);
	power_supply_put(fuel_gauge);
	if (ret)
		return ret;

	*temp = val.intval;

	return 0;
}

static enum s2mu005_thermal_state
s2mu005_chgr_update_thermal_state(struct s2m_chgr *priv, int temp)
{
	switch (priv->thermal_state) {
	case S2MU005_THERMAL_COLD:
		if (temp < priv->temp_resume_min_decic)
			return S2MU005_THERMAL_COLD;
		break;
	case S2MU005_THERMAL_HOT:
		if (temp > priv->temp_resume_max_decic)
			return S2MU005_THERMAL_HOT;
		break;
	default:
		break;
	}

	if (temp <= priv->temp_stop_min_decic)
		return S2MU005_THERMAL_COLD;
	if (temp >= priv->temp_stop_max_decic)
		return S2MU005_THERMAL_HOT;

	return S2MU005_THERMAL_NORMAL;
}

static void s2mu005_chgr_monitor_work(struct work_struct *work)
{
	struct s2m_chgr *priv = container_of(to_delayed_work(work),
					    struct s2m_chgr, monitor_work);
	enum s2mu005_thermal_state old_thermal;
	bool changed = false;
	int temp = 0;
	int ret;

	if (READ_ONCE(priv->stopping) || READ_ONCE(priv->suspended))
		return;

	if (priv->thermal_limits_valid)
		ret = s2mu005_chgr_get_battery_temp(priv, &temp);
	else
		ret = -EINVAL;

	mutex_lock(&priv->lock);
	if (priv->stopping || priv->suspended) {
		mutex_unlock(&priv->lock);
		return;
	}

	old_thermal = priv->thermal_state;
	if (ret) {
		priv->thermal_state = S2MU005_THERMAL_UNKNOWN;
	} else {
		priv->battery_temp_decic = temp;
		priv->thermal_state =
			s2mu005_chgr_update_thermal_state(priv, temp);
	}

	if (old_thermal != priv->thermal_state) {
		changed = true;
		priv->thermal_policy_dirty = true;
		switch (priv->thermal_state) {
		case S2MU005_THERMAL_NORMAL:
			dev_info(priv->dev, "battery temperature safe at %d.%d C\n",
				 temp / 10, abs(temp % 10));
			break;
		case S2MU005_THERMAL_COLD:
			dev_warn(priv->dev,
				 "charging stopped: battery cold at %d.%d C\n",
				 temp / 10, abs(temp % 10));
			break;
		case S2MU005_THERMAL_HOT:
			dev_warn(priv->dev,
				 "charging stopped: battery hot at %d.%d C\n",
				 temp / 10, abs(temp % 10));
			break;
		case S2MU005_THERMAL_UNKNOWN:
			dev_warn(priv->dev,
				 "charging stopped: battery temperature unavailable (%d)\n",
				 ret);
			break;
		}
	}

	if (priv->thermal_policy_dirty) {
		ret = s2mu005_chgr_apply_source(priv);
		if (!ret)
			priv->thermal_policy_dirty = false;
	} else {
		ret = 0;
	}
	mutex_unlock(&priv->lock);
	if (ret)
		dev_err_ratelimited(priv->dev,
				    "failed to apply temperature policy (%d)\n", ret);

	if (changed)
		power_supply_changed(priv->psy);

	mutex_lock(&priv->lock);
	if (!priv->stopping && !priv->suspended)
		mod_delayed_work(system_freezable_wq, &priv->monitor_work,
				 msecs_to_jiffies(S2MU005_MONITOR_INTERVAL_MS));
	mutex_unlock(&priv->lock);
}

static void s2mu005_chgr_watchdog_work(struct work_struct *work)
{
	struct s2m_chgr *priv = container_of(to_delayed_work(work),
					    struct s2m_chgr, watchdog_work);
	unsigned int event;
	unsigned int status3;
	bool retry = false;
	int disable_ret;
	int ret;

	if (READ_ONCE(priv->stopping) || READ_ONCE(priv->suspended))
		return;

	mutex_lock(&priv->lock);
	if (priv->stopping || priv->suspended || !priv->watchdog_enabled) {
		mutex_unlock(&priv->lock);
		return;
	}

	ret = regmap_read(priv->regmap, S2MU005_REG_CHGR_STATUS3, &status3);
	if (ret)
		goto err_disable;

	ret = regmap_set_bits(priv->regmap, S2MU005_REG_CHGR_CTRL13,
			      S2MU005_CHGR_WDT_CLEAR);
	if (ret)
		goto err_disable;

	event = FIELD_GET(S2MU005_CHGR_EVT, status3);
	if (event == S2MU005_CHGR_EVT_WDT_SUSP ||
	    event == S2MU005_CHGR_EVT_WDT_RST) {
		dev_warn(priv->dev, "recovering from charger watchdog event %u\n",
			 event);
		ret = s2mu005_chgr_apply_source(priv);
		if (ret)
			goto err_disable;
	}

	if (priv->watchdog_enabled)
		mod_delayed_work(system_wq, &priv->watchdog_work,
				 msecs_to_jiffies(S2MU005_WATCHDOG_INTERVAL_MS));
	mutex_unlock(&priv->lock);

	return;

err_disable:
	disable_ret = s2mu005_chgr_mode_unset(priv);
	if (disable_ret)
		dev_warn(priv->dev,
			 "failed to establish safe state after watchdog error (%d)\n",
			 disable_ret);
	if (!priv->stopping && !priv->suspended) {
		priv->source_retries = 0;
		retry = true;
	}
	mutex_unlock(&priv->lock);

	dev_err_ratelimited(priv->dev, "charger watchdog service failed: %d\n",
			    ret);
	if (retry)
		mod_delayed_work(system_wq, &priv->extcon_work,
				 msecs_to_jiffies(S2MU005_SOURCE_RETRY_MS));
	power_supply_changed(priv->psy);
}

static irqreturn_t s2m_chgr_irq(int irq, void *data)
{
	struct s2m_chgr *priv = data;

	if (READ_ONCE(priv->watchdog_enabled) &&
	    !READ_ONCE(priv->stopping) && !READ_ONCE(priv->suspended))
		mod_delayed_work(system_wq, &priv->watchdog_work, 0);

	power_supply_changed(priv->psy);

	return IRQ_HANDLED;
}

static int s2mu005_chgr_init_limits(struct s2m_chgr *priv)
{
	struct power_supply_battery_info *info;
	int ret;

	priv->charge_current_max_ua = S2MU005_FAST_CURRENT_MAX_UA;
	priv->charge_voltage_max_uv = S2MU005_FLOAT_VOLTAGE_MAX_UV;
	priv->thermal_state = S2MU005_THERMAL_UNKNOWN;

	ret = s2mu005_chgr_get_input_current(priv, &priv->input_current_ua);
	if (ret)
		return ret;

	/* Exact downstream J7 policy: 90-minute top-off and 80-second watchdog. */
	ret = regmap_update_bits(priv->regmap, S2MU005_REG_CHGR_CTRL18,
				 S2MU005_CHGR_TIMER_CONFIG,
				 FIELD_PREP(S2MU005_CHGR_TIMER_CONFIG,
					    S2MU005_CHGR_TIMER_90M_WDT_80S));
	if (ret)
		return ret;

	ret = power_supply_get_battery_info(priv->psy, &info);
	if (ret) {
		dev_warn(priv->dev,
			 "battery data unavailable; preserving bootloader limits (%d)\n",
			 ret);
		ret = s2mu005_chgr_get_charge_current(priv,
						      &priv->charge_current_ua);
		if (ret)
			return ret;
		ret = s2mu005_chgr_get_charge_voltage(priv,
						      &priv->charge_voltage_uv);
		if (ret)
			return ret;
		return s2mu005_chgr_get_term_current(priv,
						  &priv->term_current_ua);
	}

	if (info->constant_charge_current_max_ua > 0) {
		priv->charge_current_max_ua = min(info->constant_charge_current_max_ua,
						  S2MU005_FAST_CURRENT_MAX_UA);
		ret = s2mu005_chgr_set_charge_current(priv,
						      priv->charge_current_max_ua);
		if (ret)
			goto out;
		priv->charge_current_max_ua = priv->charge_current_ua;
	}

	if (info->constant_charge_voltage_max_uv > 0) {
		priv->charge_voltage_max_uv = min(info->constant_charge_voltage_max_uv,
						  S2MU005_FLOAT_VOLTAGE_MAX_UV);
		ret = s2mu005_chgr_set_charge_voltage(priv,
						      priv->charge_voltage_max_uv);
		if (ret)
			goto out;
		priv->charge_voltage_max_uv = priv->charge_voltage_uv;
	}

	if (info->temp_min != INT_MIN && info->temp_max != INT_MAX &&
	    info->temp_alert_min != INT_MIN &&
	    info->temp_alert_max != INT_MAX &&
	    info->temp_alert_min > info->temp_min &&
	    info->temp_alert_max < info->temp_max) {
		priv->temp_stop_min_decic = info->temp_min * 10;
		priv->temp_stop_max_decic = info->temp_max * 10;
		priv->temp_resume_min_decic = info->temp_alert_min * 10;
		priv->temp_resume_max_decic = info->temp_alert_max * 10;
		priv->thermal_limits_valid = true;
	} else {
		dev_warn(priv->dev,
			 "battery temperature limits unavailable; charging remains inhibited\n");
	}

	if (info->charge_term_current_ua > 0)
		ret = s2mu005_chgr_set_term_current(priv,
						    info->charge_term_current_ua);
	else
		ret = s2mu005_chgr_get_term_current(priv,
						    &priv->term_current_ua);

out:
	power_supply_put_battery_info(priv->psy, info);
	return ret;
}

static void s2m_chgr_disable(void *data)
{
	struct s2m_chgr *priv = data;

	mutex_lock(&priv->lock);
	s2mu005_chgr_mode_unset(priv);
	mutex_unlock(&priv->lock);
}

static int s2m_chgr_probe(struct platform_device *pdev)
{
	static const char * const irq_names[] = {
		"det-bat", "bat", "ivr", "event",
		"chg", "vmid", "wcin", "vbus",
	};
	struct device *dev = &pdev->dev;
	struct sec_pmic_dev *pmic_drvdata = dev_get_drvdata(dev->parent);
	struct device_node *extcon_node __free(device_node) = NULL;
	struct power_supply_config psy_cfg = {};
	struct s2m_chgr *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);
	priv->dev = dev;
	priv->regmap = pmic_drvdata->regmap_pmic;
	priv->charge_enabled = true;

	ret = devm_mutex_init(dev, &priv->lock);
	if (ret)
		return dev_err_probe(dev, ret, "failed to initialize mutex\n");

	if (platform_get_device_id(pdev)->driver_data != S2MU005)
		return dev_err_probe(dev, -ENODEV,
				     "device type not supported by driver\n");

	extcon_node = of_get_child_by_name(dev->parent->of_node, "muic");
	if (!extcon_node)
		return dev_err_probe(dev, -ENODEV,
				     "MUIC node required but not found\n");

	priv->extcon = extcon_find_edev_by_node(extcon_node);
	if (IS_ERR(priv->extcon))
		return dev_err_probe(dev, PTR_ERR(priv->extcon),
				     "failed to get MUIC extcon\n");

	psy_cfg.drv_data = priv;
	psy_cfg.fwnode = dev_fwnode(dev->parent);
	priv->psy = devm_power_supply_register(dev, &s2mu005_chgr_psy_desc,
					       &psy_cfg);
	if (IS_ERR(priv->psy))
		return dev_err_probe(dev, PTR_ERR(priv->psy),
				     "failed to register power supply\n");

	ret = devm_add_action_or_reset(dev, s2m_chgr_disable, priv);
	if (ret)
		return ret;

	ret = s2mu005_chgr_init_limits(priv);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to initialize charging limits\n");

	/* Do not inherit an unsafe bootloader charging state before thermal I/O. */
	mutex_lock(&priv->lock);
	ret = s2mu005_chgr_mode_unset(priv);
	mutex_unlock(&priv->lock);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to establish a safe charger state\n");

	ret = devm_delayed_work_autocancel(dev, &priv->extcon_work,
					   s2mu005_chgr_extcon_work);
	if (ret)
		return dev_err_probe(dev, ret, "failed to initialize extcon work\n");
	ret = devm_delayed_work_autocancel(dev, &priv->monitor_work,
						   s2mu005_chgr_monitor_work);
	if (ret)
		return dev_err_probe(dev, ret,
					     "failed to initialize temperature work\n");
	ret = devm_delayed_work_autocancel(dev, &priv->watchdog_work,
					   s2mu005_chgr_watchdog_work);
	if (ret)
		return dev_err_probe(dev, ret,
					     "failed to initialize watchdog work\n");
	priv->watchdog_work_ready = true;

	priv->extcon_nb.notifier_call = s2m_chgr_extcon_notifier;
	ret = devm_extcon_register_notifier_all(dev, priv->extcon,
						&priv->extcon_nb);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register extcon notifier\n");

	for (int i = 0; i < ARRAY_SIZE(irq_names); i++) {
		int irq = platform_get_irq_byname_optional(pdev, irq_names[i]);

		if (irq == -ENXIO)
			continue;
		if (irq < 0)
			return dev_err_probe(dev, irq, "failed to get %s IRQ\n",
					     irq_names[i]);

		ret = devm_request_threaded_irq(dev, irq, NULL, s2m_chgr_irq,
						IRQF_ONESHOT, irq_names[i], priv);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to request %s IRQ\n",
					     irq_names[i]);
	}

	/* Establish temperature safety before reconciling an attached cable. */
	mod_delayed_work(system_freezable_wq, &priv->monitor_work, 0);
	/* Preserve a cable that was attached before this driver probed. */
	mod_delayed_work(system_wq, &priv->extcon_work, 0);

	return 0;
}

static void s2m_chgr_shutdown(struct platform_device *pdev)
{
	struct s2m_chgr *priv = platform_get_drvdata(pdev);

	mutex_lock(&priv->lock);
	priv->stopping = true;
	mutex_unlock(&priv->lock);
	cancel_delayed_work_sync(&priv->extcon_work);
	cancel_delayed_work_sync(&priv->monitor_work);
	cancel_delayed_work_sync(&priv->watchdog_work);
	s2m_chgr_disable(priv);
}

static int s2m_chgr_suspend(struct device *dev)
{
	struct s2m_chgr *priv = dev_get_drvdata(dev);
	enum s2mu005_thermal_state old_thermal;
	int ret;

	mutex_lock(&priv->lock);
	if (priv->stopping) {
		mutex_unlock(&priv->lock);
		return 0;
	}

	old_thermal = priv->thermal_state;
	priv->suspended = true;
	priv->thermal_state = S2MU005_THERMAL_UNKNOWN;
	priv->thermal_policy_dirty = true;
	ret = s2mu005_chgr_mode_unset(priv);
	if (ret) {
		priv->suspended = false;
		priv->thermal_state = old_thermal;
	}
	mutex_unlock(&priv->lock);

	if (ret) {
		mod_delayed_work(system_freezable_wq, &priv->monitor_work, 0);
		mod_delayed_work(system_wq, &priv->extcon_work, 0);
		return dev_err_probe(dev, ret,
				     "failed to inhibit charging for suspend\n");
	}

	/* A frozen monitor must never leave charging active on a stale sample. */
	cancel_delayed_work_sync(&priv->monitor_work);
	cancel_delayed_work_sync(&priv->watchdog_work);
	power_supply_changed(priv->psy);

	return 0;
}

static int s2m_chgr_resume(struct device *dev)
{
	struct s2m_chgr *priv = dev_get_drvdata(dev);

	mutex_lock(&priv->lock);
	if (priv->stopping) {
		mutex_unlock(&priv->lock);
		return 0;
	}

	priv->suspended = false;
	priv->thermal_state = S2MU005_THERMAL_UNKNOWN;
	priv->thermal_policy_dirty = true;
	priv->source_retries = 0;
	mutex_unlock(&priv->lock);

	/* UNKNOWN keeps charging off until the fresh temperature sample succeeds. */
	mod_delayed_work(system_freezable_wq, &priv->monitor_work, 0);
	mod_delayed_work(system_wq, &priv->extcon_work, 0);
	power_supply_changed(priv->psy);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(s2m_chgr_pm_ops, s2m_chgr_suspend,
				s2m_chgr_resume);

static void s2m_chgr_remove(struct platform_device *pdev)
{
	struct s2m_chgr *priv = platform_get_drvdata(pdev);

	mutex_lock(&priv->lock);
	priv->stopping = true;
	mutex_unlock(&priv->lock);
	cancel_delayed_work_sync(&priv->extcon_work);
	cancel_delayed_work_sync(&priv->monitor_work);
	cancel_delayed_work_sync(&priv->watchdog_work);
}

static const struct platform_device_id s2m_chgr_id_table[] = {
	{ .name = "s2mu005-charger", .driver_data = S2MU005 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, s2m_chgr_id_table);

static struct platform_driver s2m_chgr_driver = {
	.driver = {
		.name = "s2m-charger",
		.pm = pm_sleep_ptr(&s2m_chgr_pm_ops),
	},
	.probe = s2m_chgr_probe,
	.remove = s2m_chgr_remove,
	.shutdown = s2m_chgr_shutdown,
	.id_table = s2m_chgr_id_table,
};
module_platform_driver(s2m_chgr_driver);

MODULE_DESCRIPTION("Battery Charger Driver For Samsung S2M Series PMICs");
MODULE_AUTHOR("Kaustabh Chakraborty <kauschluss@disroot.org>");
MODULE_AUTHOR("Łukasz Lebiedziński <kernel@lvkasz.us>");
MODULE_LICENSE("GPL");
