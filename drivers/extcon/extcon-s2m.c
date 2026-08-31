// SPDX-License-Identifier: GPL-2.0
/*
 * Extcon Driver for Samsung S2M series PMICs.
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd
 * Copyright (C) 2026 Kaustabh Chakraborty <kauschluss@disroot.org>
 */

#include <linux/extcon-provider.h>
#include <linux/interrupt.h>
#include <linux/mfd/samsung/core.h>
#include <linux/mfd/samsung/s2mu005.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/usb/role.h>

struct s2m_muic;

struct s2m_muic_irq_data {
	const char *name;
	int (*const handler)(struct s2m_muic *);
	bool call_on_init;
	bool call_on_cleanup;
	int irq;
};

struct s2m_muic {
	struct device *dev;
	struct regmap *regmap;
	struct extcon_dev *extcon;
	struct usb_role_switch *role_sw;
	/* Serializes accessory classification and USB role transitions. */
	struct mutex lock;
	struct s2m_muic_irq_data *irq_data;
	const unsigned int *extcon_cable;
};

static int s2m_muic_set_role(struct s2m_muic *priv, enum usb_role role)
{
	int ret;

	ret = usb_role_switch_set_role(priv->role_sw, role);
	if (ret)
		dev_err(priv->dev, "failed to set USB role to %s (%d)\n",
			usb_role_string(role), ret);

	return ret;
}

static int s2m_muic_clear_cables(struct s2m_muic *priv)
{
	int first_error = 0;
	int ret;

	for (int i = 0; priv->extcon_cable[i]; i++) {
		unsigned int cable = priv->extcon_cable[i];

		ret = extcon_get_state(priv->extcon, cable);
		if (ret < 0) {
			if (!first_error)
				first_error = ret;
			continue;
		}

		if (!ret)
			continue;

		ret = extcon_set_state_sync(priv->extcon, cable, false);
		if (ret && !first_error)
			first_error = ret;
	}

	return first_error;
}

static int s2mu005_muic_detach_locked(struct s2m_muic *priv)
{
	int first_error = 0;
	int ret;

	ret = regmap_set_bits(priv->regmap, S2MU005_REG_MUIC_CTRL1,
			      S2MU005_MUIC_MAN_SW);
	if (ret) {
		dev_err(priv->dev, "failed to disable manual switching\n");
		first_error = ret;
	}

	ret = regmap_set_bits(priv->regmap, S2MU005_REG_MUIC_CTRL3,
			      S2MU005_MUIC_ONESHOT_ADC);
	if (ret) {
		dev_err(priv->dev, "failed to enable ADC oneshot mode\n");
		if (!first_error)
			first_error = ret;
	}

	ret = regmap_write(priv->regmap, S2MU005_REG_MUIC_SWCTRL, 0);
	if (ret) {
		dev_err(priv->dev, "failed to clear switch control register\n");
		if (!first_error)
			first_error = ret;
	}

	ret = s2m_muic_clear_cables(priv);
	if (ret && !first_error)
		first_error = ret;

	ret = s2m_muic_set_role(priv, USB_ROLE_NONE);
	if (ret && !first_error)
		first_error = ret;

	return first_error;
}

static int s2mu005_muic_detach(struct s2m_muic *priv)
{
	int ret;

	mutex_lock(&priv->lock);
	ret = s2mu005_muic_detach_locked(priv);
	mutex_unlock(&priv->lock);

	return ret;
}

static int s2mu005_muic_set_data_path(struct s2m_muic *priv)
{
	int ret;

	ret = regmap_update_bits(priv->regmap, S2MU005_REG_MUIC_SWCTRL,
				 S2MU005_MUIC_DM_DP,
				 FIELD_PREP(S2MU005_MUIC_DM_DP,
					    S2MU005_MUIC_DM_DP_USB));
	if (ret)
		dev_err(priv->dev, "failed to configure DM/DP pins\n");

	return ret;
}

static int s2mu005_muic_set_otg_detection(struct s2m_muic *priv)
{
	int ret;

	ret = regmap_clear_bits(priv->regmap, S2MU005_REG_MUIC_CTRL1,
				S2MU005_MUIC_MAN_SW);
	if (ret) {
		dev_err(priv->dev, "failed to enable manual switching\n");
		return ret;
	}

	ret = regmap_clear_bits(priv->regmap, S2MU005_REG_MUIC_CTRL3,
				S2MU005_MUIC_ONESHOT_ADC);
	if (ret)
		dev_err(priv->dev, "failed to disable ADC oneshot mode\n");

	return ret;
}

static int s2mu005_muic_set_cable(struct s2m_muic *priv, unsigned int cable,
				  enum usb_role role)
{
	int ret;

	ret = extcon_set_state_sync(priv->extcon, cable, true);
	if (ret)
		return ret;

	return s2m_muic_set_role(priv, role);
}

static int s2mu005_muic_attach(struct s2m_muic *priv)
{
	unsigned int type;
	int cleanup_ret;
	int ret;

	mutex_lock(&priv->lock);

	ret = regmap_read(priv->regmap, S2MU005_REG_MUIC_DEV1, &type);
	if (ret) {
		dev_err(priv->dev, "failed to read DEV1 register\n");
		goto out;
	}

	/* Reset the old path before applying an attach-to-attach reclassification. */
	ret = s2mu005_muic_detach_locked(priv);
	if (ret)
		goto out;

	if (type & (S2MU005_MUIC_OTG | S2MU005_MUIC_CDP | S2MU005_MUIC_SDP)) {
		ret = s2mu005_muic_set_data_path(priv);
		if (ret)
			goto out;
	}

	if (type & S2MU005_MUIC_OTG) {
		ret = s2mu005_muic_set_otg_detection(priv);
		if (ret)
			goto out;
	}

	switch (type) {
	case S2MU005_MUIC_OTG:
		dev_dbg(priv->dev, "USB OTG connection detected\n");
		ret = s2mu005_muic_set_cable(priv, EXTCON_USB_HOST,
					     USB_ROLE_HOST);
		break;
	case S2MU005_MUIC_CDP:
		dev_dbg(priv->dev, "USB CDP connection detected\n");
		ret = extcon_set_state_sync(priv->extcon, EXTCON_CHG_USB_CDP,
					    true);
		if (!ret)
			ret = s2mu005_muic_set_cable(priv, EXTCON_USB,
						     USB_ROLE_DEVICE);
		break;
	case S2MU005_MUIC_SDP:
		dev_dbg(priv->dev, "USB SDP connection detected\n");
		ret = extcon_set_state_sync(priv->extcon, EXTCON_CHG_USB_SDP,
					    true);
		if (!ret)
			ret = s2mu005_muic_set_cable(priv, EXTCON_USB,
						     USB_ROLE_DEVICE);
		break;
	case S2MU005_MUIC_DCP:
		dev_dbg(priv->dev, "USB DCP connection detected\n");
		ret = extcon_set_state_sync(priv->extcon, EXTCON_CHG_USB_DCP,
					    true);
		break;
	case S2MU005_MUIC_UART:
		dev_dbg(priv->dev, "UART connection detected\n");
		ret = extcon_set_state_sync(priv->extcon, EXTCON_JIG, true);
		break;
	case 0: /* OPEN */
		ret = 0;
		break;
	default:
		dev_warn(priv->dev,
			 "failed to recognize attached device (DEV1=0x%x)\n", type);
		ret = 0;
	}

out:
	if (ret) {
		cleanup_ret = s2mu005_muic_detach_locked(priv);
		if (cleanup_ret)
			dev_err(priv->dev,
				"failed to restore detached state (%d)\n",
				cleanup_ret);
	}
	mutex_unlock(&priv->lock);

	return ret;
}

static int s2mu005_muic_init(struct s2m_muic *priv)
{
	int ret;

	ret = regmap_update_bits(priv->regmap, S2MU005_REG_MUIC_LDOADC_L,
				 S2MU005_MUIC_VSET,
				 FIELD_PREP(S2MU005_MUIC_VSET,
					    S2MU005_MUIC_VSET_3P0V));
	if (ret)
		return dev_err_probe(priv->dev, ret,
				     "failed to set low ADC regulator\n");

	ret = regmap_update_bits(priv->regmap, S2MU005_REG_MUIC_LDOADC_H,
				 S2MU005_MUIC_VSET,
				 FIELD_PREP(S2MU005_MUIC_VSET,
					    S2MU005_MUIC_VSET_3P0V));
	if (ret)
		return dev_err_probe(priv->dev, ret,
				     "failed to set high ADC regulator\n");

	for (int i = 0; priv->irq_data[i].handler; i++) {
		if (!priv->irq_data[i].call_on_init)
			continue;

		ret = priv->irq_data[i].handler(priv);
		if (ret)
			return ret;
	}

	ret = regmap_clear_bits(priv->regmap, S2MU005_REG_MUIC_CTRL1,
				S2MU005_MUIC_IRQ);
	if (ret)
		dev_err(priv->dev, "failed to unmask MUIC interrupts\n");

	return ret;
}

static int s2mu005_muic_cleanup(struct s2m_muic *priv)
{
	int first_error = 0;
	int ret;

	ret = regmap_set_bits(priv->regmap, S2MU005_REG_MUIC_CTRL1,
			      S2MU005_MUIC_IRQ);
	if (ret) {
		dev_err(priv->dev, "failed to mask MUIC interrupts\n");
		first_error = ret;
	}

	for (int i = 0; priv->irq_data[i].handler; i++) {
		if (!priv->irq_data[i].call_on_cleanup)
			continue;

		ret = priv->irq_data[i].handler(priv);
		if (ret && !first_error)
			first_error = ret;
	}

	return first_error;
}

static const unsigned int s2mu005_muic_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_CHG_USB_SDP,
	EXTCON_CHG_USB_DCP,
	EXTCON_CHG_USB_CDP,
	EXTCON_JIG,
	EXTCON_NONE,
};

static const struct s2m_muic_irq_data s2mu005_muic_irq_data[] = {
	{
		.name = "attach",
		.handler = s2mu005_muic_attach,
		.call_on_init = true,
	}, {
		.name = "detach",
		.handler = s2mu005_muic_detach,
		.call_on_cleanup = true,
	}, {
		/* sentinel */
	}
};

static irqreturn_t s2m_muic_irq_func(int virq, void *data)
{
	struct s2m_muic *priv = data;

	for (int i = 0; priv->irq_data[i].handler; i++) {
		int ret;

		if (virq != priv->irq_data[i].irq)
			continue;

		ret = priv->irq_data[i].handler(priv);
		if (ret)
			dev_err(priv->dev, "failed to handle %s interrupt (%d)\n",
				priv->irq_data[i].name, ret);
		break;
	}

	return IRQ_HANDLED;
}

static void s2m_muic_role_switch_put(void *data)
{
	usb_role_switch_put(data);
}

static int s2m_muic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sec_pmic_dev *pmic_drvdata = dev_get_drvdata(dev->parent);
	struct s2m_muic *priv;
	int (*variant_init)(struct s2m_muic *) = NULL;
	int (*variant_cleanup)(struct s2m_muic *) = NULL;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);
	priv->dev = dev;
	priv->regmap = pmic_drvdata->regmap_pmic;

	ret = devm_mutex_init(dev, &priv->lock);
	if (ret)
		return dev_err_probe(dev, ret, "failed to initialize mutex lock\n");

	switch (platform_get_device_id(pdev)->driver_data) {
	case S2MU005:
		variant_init = s2mu005_muic_init;
		variant_cleanup = s2mu005_muic_cleanup;
		priv->extcon_cable = s2mu005_muic_extcon_cable;
		priv->irq_data = devm_kmemdup(dev, s2mu005_muic_irq_data,
					      sizeof(s2mu005_muic_irq_data),
					      GFP_KERNEL);
		if (!priv->irq_data)
			return -ENOMEM;
		break;
	default:
		return dev_err_probe(dev, -ENODEV,
				     "device type not supported by driver\n");
	}

	priv->extcon = devm_extcon_dev_allocate(dev, priv->extcon_cable);
	if (IS_ERR(priv->extcon))
		return dev_err_probe(dev, PTR_ERR(priv->extcon),
				     "failed to allocate extcon device\n");

	ret = devm_extcon_dev_register(dev, priv->extcon);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register extcon device\n");

	priv->role_sw = usb_role_switch_get(dev);
	if (IS_ERR(priv->role_sw))
		return dev_err_probe(dev, PTR_ERR(priv->role_sw),
				     "failed to get USB role switch\n");

	ret = devm_add_action_or_reset(dev, s2m_muic_role_switch_put,
				       priv->role_sw);
	if (ret)
		return ret;

	for (int i = 0; priv->irq_data[i].handler; i++) {
		ret = platform_get_irq_byname_optional(pdev, priv->irq_data[i].name);
		if (ret == -ENXIO)
			continue;
		if (ret < 0)
			return dev_err_probe(dev, ret, "failed to get IRQ %s\n",
					     priv->irq_data[i].name);

		priv->irq_data[i].irq = ret;
		ret = devm_request_threaded_irq(dev, ret, NULL, s2m_muic_irq_func,
						IRQF_ONESHOT,
						priv->irq_data[i].name, priv);
		if (ret)
			return dev_err_probe(dev, ret, "failed to request IRQ %s\n",
					     priv->irq_data[i].name);
	}

	if (variant_init)
		ret = variant_init(priv);
	if (ret) {
		ret = dev_err_probe(dev, ret, "failed to initialize MUIC\n");
		if (variant_cleanup)
			variant_cleanup(priv);
		return ret;
	}

	return 0;
}

static void s2m_muic_remove(struct platform_device *pdev)
{
	struct s2m_muic *priv = platform_get_drvdata(pdev);

	switch (platform_get_device_id(pdev)->driver_data) {
	case S2MU005:
		s2mu005_muic_cleanup(priv);
		break;
	default:
		unreachable();
	}
}

static const struct platform_device_id s2m_muic_id_table[] = {
	{ .name = "s2mu005-muic", .driver_data = S2MU005 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, s2m_muic_id_table);

/* The MFD core performs device matching through platform_device_id. */
static const struct of_device_id s2m_muic_of_match_table[] = {
	{
		.compatible = "samsung,s2mu005-muic",
		.data = (void *)S2MU005,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, s2m_muic_of_match_table);

static struct platform_driver s2m_muic_driver = {
	.driver = {
		.name = "s2m-muic",
	},
	.probe = s2m_muic_probe,
	.remove = s2m_muic_remove,
	.id_table = s2m_muic_id_table,
};
module_platform_driver(s2m_muic_driver);

MODULE_DESCRIPTION("Extcon Driver For Samsung S2M Series PMICs");
MODULE_AUTHOR("Kaustabh Chakraborty <kauschluss@disroot.org>");
MODULE_LICENSE("GPL");
