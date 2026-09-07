// SPDX-License-Identifier: GPL-2.0
//
// Exynos Generic power domain support.
//
// Copyright (c) 2012 Samsung Electronics Co., Ltd.
//		http://www.samsung.com
//
// Implementation of Exynos specific power domain control which is used in
// conjunction with runtime-pm. Support for both device-tree and non-device-tree
// based power domain support is included.

#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/pm_domain.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/pm_runtime.h>

struct exynos_pm_domain_config {
	bool isp;
	/* Value for LOCAL_PWR_CFG and STATUS fields for each domain */
	u32 local_pwr_cfg;
};

/*
 * Exynos specific wrapper around the generic power domain
 */
struct exynos_pm_domain {
	struct regmap *isp_pmu;
	struct clk_bulk_data isp_clks[3];
	u32 failed_off, recovered_off;
	void __iomem *base;
	struct generic_pm_domain pd;
	u32 local_pwr_cfg;
	u32 completed_on, completed_off;
};

static int exynos_pd_power(struct generic_pm_domain *domain, bool power_on)
{
	struct exynos_pm_domain *pd;
	void __iomem *base;
	u32 timeout, pwr;
	int ret = 0, i;
	static const unsigned int isp_policy[] = {
		0x1448, 0x1488, 0x14c8, 0x1508, 0x1588,
	};
	char *op;

	pd = container_of(domain, struct exynos_pm_domain, pd);
	base = pd->base;

	if (pd->isp_pmu) {
		ret = clk_bulk_prepare_enable(ARRAY_SIZE(pd->isp_clks), pd->isp_clks);
		if (ret)
			return ret;
		/* ISP CAL policy from S5E7870-pmu.c, both transition directions. */
		for (i = 0; i < ARRAY_SIZE(isp_policy); i++) {
			ret = regmap_update_bits(pd->isp_pmu, isp_policy[i], BIT(0), 0);
			if (ret)
				goto out_clocks;
		}
		ret = regmap_update_bits(pd->isp_pmu, 0x4048, 0x3, 0x2);
		if (ret)
			goto out_clocks;
	}

	pwr = power_on ? pd->local_pwr_cfg : 0;
	writel_relaxed(pwr, base);

	/* Keep external clocks running through the PMU acknowledgement. */
	timeout = pd->isp_pmu ? 200 : 10;

	while ((readl_relaxed(base + 0x4) & pd->local_pwr_cfg) != pwr) {
		if (!timeout) {
			op = (power_on) ? "enable" : "disable";
			pr_err("Power domain %s %s failed: status=%x\n",
			       domain->name, op, readl_relaxed(base + 4));
			ret = -ETIMEDOUT;
			if (pd->isp_pmu && !power_on) {
				u32 status;
				int recovery;

				pd->failed_off++;
				writel(pd->local_pwr_cfg, base);
				recovery = readl_poll_timeout(base + 4, status,
					(status & pd->local_pwr_cfg) == pd->local_pwr_cfg,
					100, 20000);
				if (!recovery)
					pd->recovered_off++;
				pr_err("ISP off rollback: ret=%d status=%x\n",
				       recovery, status);
			}
			goto out_clocks;
		}
		timeout--;
		cpu_relax();
		usleep_range(80, 100);
	}

	if (pd->isp_pmu)
		pr_info("exynos7870 ISP: domain %s acknowledged\n",
			power_on ? "on" : "off");
	if (power_on)
		pd->completed_on++;
	else
		pd->completed_off++;
out_clocks:
	if (pd->isp_pmu)
		clk_bulk_disable_unprepare(ARRAY_SIZE(pd->isp_clks), pd->isp_clks);
	return ret;
}

static int exynos_pd_power_on(struct generic_pm_domain *domain)
{
	return exynos_pd_power(domain, true);
}

static int exynos_pd_power_off(struct generic_pm_domain *domain)
{
	return exynos_pd_power(domain, false);
}

static const struct exynos_pm_domain_config exynos4210_cfg = {
	.local_pwr_cfg		= 0x7,
};

static const struct exynos_pm_domain_config exynos5433_cfg = {
	.local_pwr_cfg		= 0xf,
};

static const struct exynos_pm_domain_config exynos7870_cfg = {
	.local_pwr_cfg		= 0xf,
};

static const struct exynos_pm_domain_config exynos7870_isp_cfg = {
	.local_pwr_cfg = 0xf,
	.isp = true,
};

static const struct of_device_id exynos_pm_domain_of_match[] = {
	{
		.compatible = "samsung,exynos7870-isp-pd",
		.data = &exynos7870_isp_cfg,
	},
	{
		.compatible = "samsung,exynos4210-pd",
		.data = &exynos4210_cfg,
	}, {
		.compatible = "samsung,exynos5433-pd",
		.data = &exynos5433_cfg,
	}, {
		.compatible = "samsung,exynos7870-pd",
		.data = &exynos7870_cfg,
	},
	{ },
};

static const char *exynos_get_domain_name(struct device *dev,
					  struct device_node *node)
{
	const char *name;

	if (of_property_read_string(node, "label", &name) < 0)
		name = kbasename(node->full_name);
	return devm_kstrdup_const(dev, name, GFP_KERNEL);
}

static ssize_t hw_state_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct exynos_pm_domain *pd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "config=%x status=%x on=%u off=%u failed_off=%u recovered_off=%u\n",
		readl(pd->base) & pd->local_pwr_cfg,
		readl(pd->base + 4) & pd->local_pwr_cfg,
		READ_ONCE(pd->completed_on), READ_ONCE(pd->completed_off),
		READ_ONCE(pd->failed_off), READ_ONCE(pd->recovered_off));
}
static DEVICE_ATTR_RO(hw_state);
static struct attribute *exynos_pd_attrs[] = { &dev_attr_hw_state.attr, NULL };
ATTRIBUTE_GROUPS(exynos_pd);

static int exynos_pd_probe(struct platform_device *pdev)
{
	const struct exynos_pm_domain_config *pm_domain_cfg;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct of_phandle_args child, parent;
	struct exynos_pm_domain *pd;
	int on, ret;

	pm_domain_cfg = of_device_get_match_data(dev);
	pd = devm_kzalloc(dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	platform_set_drvdata(pdev, pd);
	pd->pd.name = exynos_get_domain_name(dev, np);
	if (!pd->pd.name)
		return -ENOMEM;

	if (pm_domain_cfg->isp) {
		pd->isp_clks[0].id = "cam";
		pd->isp_clks[1].id = "isp";
		pd->isp_clks[2].id = "vra";
		ret = devm_clk_bulk_get(dev, ARRAY_SIZE(pd->isp_clks), pd->isp_clks);
		if (ret)
			return dev_err_probe(dev, ret, "ISP transition clocks\n");
		pd->isp_pmu = syscon_regmap_lookup_by_phandle(np, "samsung,pmu");
		if (IS_ERR(pd->isp_pmu))
			return dev_err_probe(dev, PTR_ERR(pd->isp_pmu), "ISP PMU\n");
	}

	pd->base = of_iomap(np, 0);
	if (!pd->base)
		return -ENODEV;

	pd->pd.power_off = exynos_pd_power_off;
	pd->pd.power_on = exynos_pd_power_on;
	pd->local_pwr_cfg = pm_domain_cfg->local_pwr_cfg;

	/*
	 * Some Samsung platforms with bootloaders turning on the splash-screen
	 * and handing it over to the kernel, requires the power-domains to be
	 * reset during boot.
	 */
	if (IS_ENABLED(CONFIG_ARM) &&
	    of_device_is_compatible(np, "samsung,exynos4210-pd"))
		exynos_pd_power_off(&pd->pd);

	if (pm_domain_cfg->isp) {
		ret = exynos_pd_power_on(&pd->pd);
		if (ret) {
			iounmap(pd->base);
			return ret;
		}
	}

	on = readl_relaxed(pd->base + 0x4) & pd->local_pwr_cfg;

	pm_genpd_init(&pd->pd, NULL, !on);
	ret = of_genpd_add_provider_simple(np, &pd->pd);

	if (ret == 0 && of_parse_phandle_with_args(np, "power-domains",
				      "#power-domain-cells", 0, &parent) == 0) {
		child.np = np;
		child.args_count = 0;

		if (of_genpd_add_subdomain(&parent, &child))
			pr_warn("%pOF failed to add subdomain: %pOF\n",
				parent.np, child.np);
		else
			pr_info("%pOF has as child subdomain: %pOF.\n",
				parent.np, child.np);
	}

	pm_runtime_enable(dev);
	return ret;
}

static struct platform_driver exynos_pd_driver = {
	.probe	= exynos_pd_probe,
	.driver	= {
		.name		= "exynos-pd",
		.dev_groups	= exynos_pd_groups,
		.of_match_table	= exynos_pm_domain_of_match,
		.suppress_bind_attrs = true,
	}
};

static __init int exynos4_pm_init_power_domain(void)
{
	return platform_driver_register(&exynos_pd_driver);
}
core_initcall(exynos4_pm_init_power_domain);
