// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung Exynos7870 KEPLER GNSS remote processor
 *
 * The register programming and boot commands are derived from Samsung's
 * downstream GNSS interface driver. The remoteproc lifecycle and mailbox
 * integration are native Linux implementations.
 */

#include <linux/bitfield.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mailbox_client.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/remoteproc.h>
#include <linux/sizes.h>

#include "remoteproc_internal.h"

#define EXYNOS7870_GNSS_CTRL_NS			0x0040
#define EXYNOS7870_GNSS_CTRL_S			0x0044
#define EXYNOS7870_GNSS2AP_MEM_CONFIG		0x0090
#define EXYNOS7870_GNSS2AP_MIF0_ACCESS		0x0094
#define EXYNOS7870_GNSS2AP_MIF1_ACCESS		0x0098
#define EXYNOS7870_GNSS2AP_PERI_ACCESS_WIN	0x00ac

#define EXYNOS7870_CENTRAL_SEQ_GNSS_CONFIGURATION	0x02c0
#define EXYNOS7870_RESET_AHEAD_GNSS_SYS_PWR_REG		0x1174
#define EXYNOS7870_TCXO_GATE_GNSS_SYS_PWR_REG		0x11c4
#define EXYNOS7870_RESET_ASB_GNSS_SYS_PWR_REG		0x11c8
#define EXYNOS7870_CLEANY_BUS_GNSS_SYS_PWR_REG		0x11e8
#define EXYNOS7870_LOGIC_RESET_GNSS_SYS_PWR_REG		0x11ec

#define EXYNOS7870_GNSS_PWRON		BIT(1)
#define EXYNOS7870_GNSS_RESET_SET	BIT(2)
#define EXYNOS7870_GNSS_START		BIT(3)
#define EXYNOS7870_GNSS_ACTIVE_REQ_CLR	BIT(6)
#define EXYNOS7870_GNSS_RESET_REQ_CLR	BIT(8)
#define EXYNOS7870_GNSS_WAKEUP_REQ_CLR	BIT(15)

#define EXYNOS7870_MEM_BASE_MASK		GENMASK(13, 0)
#define EXYNOS7870_MEM_SIZE_MASK		GENMASK(24, 16)

#define EXYNOS7870_MBOX_ISSR(index)	(0x80 + (index) * sizeof(u32))
#define EXYNOS7870_BCMD_CTRL0		0
#define EXYNOS7870_BCMD_CTRL1		1
#define EXYNOS7870_BCMD_CTRL2		2
#define EXYNOS7870_BCMD_CTRL3		3
#define EXYNOS7870_RX_IPC_MSG		4
#define EXYNOS7870_TX_IPC_MSG		5
#define EXYNOS7870_RX_HEAD		10
#define EXYNOS7870_RX_TAIL		11
#define EXYNOS7870_TX_HEAD		12
#define EXYNOS7870_TX_TAIL		13

#define EXYNOS7870_KEPLER_LOAD_ADDR	0x50000000
#define EXYNOS7870_KEPLER_LOAD_SIZE	0x20000
#define EXYNOS7870_KEPLER_START_ADDR	0x2000c1
#define EXYNOS7870_KEPLER_FW_MAX_SIZE	SZ_2M
#define EXYNOS7870_KEPLER_BCMD_TIMEOUT_MS	1000

struct exynos7870_kepler {
	struct device *dev;
	struct rproc *rproc;
	struct regmap *pmu;
	struct regmap *mbox;
	void __iomem *fw_mem;
	phys_addr_t mem_phys;
	size_t mem_size;

	struct mbox_client mbox_client;
	struct mbox_chan *bcmd_chan;
	struct completion bcmd_done;
	u32 doorbell;
};

static void exynos7870_kepler_mbox_rx(struct mbox_client *client, void *msg)
{
	struct exynos7870_kepler *kepler =
		container_of(client, struct exynos7870_kepler, mbox_client);

	complete(&kepler->bcmd_done);
}

static int exynos7870_kepler_ring(struct exynos7870_kepler *kepler)
{
	int ret;

	ret = mbox_send_message(kepler->bcmd_chan, &kepler->doorbell);
	if (ret < 0)
		return ret;

	/* A posted doorbell write is the transmit-completion point. */
	mbox_client_txdone(kepler->bcmd_chan, 0);

	return 0;
}

static int exynos7870_kepler_bcmd(struct exynos7870_kepler *kepler,
				  u16 command, u16 flags, u32 param1,
				  u32 param2, bool power_on)
{
	unsigned long timeout;
	u32 result;
	int ret;

	reinit_completion(&kepler->bcmd_done);

	ret = regmap_write(kepler->mbox,
			   EXYNOS7870_MBOX_ISSR(EXYNOS7870_BCMD_CTRL0),
			   (u32)flags << 16 | command);
	if (ret)
		return ret;
	ret = regmap_write(kepler->mbox,
			   EXYNOS7870_MBOX_ISSR(EXYNOS7870_BCMD_CTRL1), param1);
	if (ret)
		return ret;
	ret = regmap_write(kepler->mbox,
			   EXYNOS7870_MBOX_ISSR(EXYNOS7870_BCMD_CTRL2), param2);
	if (ret)
		return ret;
	ret = regmap_write(kepler->mbox,
			   EXYNOS7870_MBOX_ISSR(EXYNOS7870_BCMD_CTRL3), 0xff);
	if (ret)
		return ret;

	ret = exynos7870_kepler_ring(kepler);
	if (ret)
		return ret;

	/* The boot ROM consumes the pending load command as power is applied. */
	if (power_on) {
		ret = regmap_update_bits(kepler->pmu, EXYNOS7870_GNSS_CTRL_NS,
					 EXYNOS7870_GNSS_PWRON,
					 EXYNOS7870_GNSS_PWRON);
		if (ret)
			return ret;

		ret = regmap_update_bits(kepler->pmu, EXYNOS7870_GNSS_CTRL_S,
					 EXYNOS7870_GNSS_START,
					 EXYNOS7870_GNSS_START);
		if (ret)
			return ret;
	}

	timeout = msecs_to_jiffies(EXYNOS7870_KEPLER_BCMD_TIMEOUT_MS);
	timeout = wait_for_completion_timeout(&kepler->bcmd_done, timeout);
	if (!timeout)
		return -ETIMEDOUT;

	ret = regmap_read(kepler->mbox,
			  EXYNOS7870_MBOX_ISSR(EXYNOS7870_BCMD_CTRL3),
			  &result);
	if (ret)
		return ret;

	dev_dbg(kepler->dev, "BCMD %#x returned %#x\n", command, result);

	return 0;
}

static int
exynos7870_kepler_powerdown_config(struct exynos7870_kepler *kepler)
{
	static const unsigned int offsets[] = {
		EXYNOS7870_CENTRAL_SEQ_GNSS_CONFIGURATION,
		EXYNOS7870_RESET_AHEAD_GNSS_SYS_PWR_REG,
		EXYNOS7870_CLEANY_BUS_GNSS_SYS_PWR_REG,
		EXYNOS7870_LOGIC_RESET_GNSS_SYS_PWR_REG,
		EXYNOS7870_TCXO_GATE_GNSS_SYS_PWR_REG,
		EXYNOS7870_RESET_ASB_GNSS_SYS_PWR_REG,
	};
	int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(offsets); i++) {
		ret = regmap_write(kepler->pmu, offsets[i], 0);

		if (ret)
			return ret;
	}

	return 0;
}

static int exynos7870_kepler_start(struct rproc *rproc)
{
	struct exynos7870_kepler *kepler = rproc->priv;
	u32 mem_config;
	int ret;

	mem_config = FIELD_PREP(EXYNOS7870_MEM_BASE_MASK,
				kepler->mem_phys >> 22) |
		     FIELD_PREP(EXYNOS7870_MEM_SIZE_MASK,
				kepler->mem_size >> 22);
	ret = regmap_update_bits(kepler->pmu,
				 EXYNOS7870_GNSS2AP_MEM_CONFIG,
				 EXYNOS7870_MEM_BASE_MASK |
				 EXYNOS7870_MEM_SIZE_MASK,
				 mem_config);
	if (ret)
		return ret;

	ret = regmap_write(kepler->pmu, EXYNOS7870_GNSS2AP_MIF0_ACCESS, 0);
	if (ret)
		return ret;
	ret = regmap_write(kepler->pmu, EXYNOS7870_GNSS2AP_MIF1_ACCESS, 0);
	if (ret)
		return ret;
	ret = regmap_write(kepler->pmu,
			   EXYNOS7870_GNSS2AP_PERI_ACCESS_WIN, 0);
	if (ret)
		return ret;

	ret = regmap_write(kepler->mbox,
			   EXYNOS7870_MBOX_ISSR(EXYNOS7870_RX_IPC_MSG), 0);
	if (ret)
		return ret;
	ret = regmap_write(kepler->mbox,
			   EXYNOS7870_MBOX_ISSR(EXYNOS7870_TX_IPC_MSG), 0);
	if (ret)
		return ret;
	ret = regmap_write(kepler->mbox,
			   EXYNOS7870_MBOX_ISSR(EXYNOS7870_RX_HEAD), 0);
	if (ret)
		return ret;
	ret = regmap_write(kepler->mbox,
			   EXYNOS7870_MBOX_ISSR(EXYNOS7870_RX_TAIL), 0);
	if (ret)
		return ret;
	ret = regmap_write(kepler->mbox,
			   EXYNOS7870_MBOX_ISSR(EXYNOS7870_TX_HEAD), 0);
	if (ret)
		return ret;
	ret = regmap_write(kepler->mbox,
			   EXYNOS7870_MBOX_ISSR(EXYNOS7870_TX_TAIL), 0);
	if (ret)
		return ret;

	ret = exynos7870_kepler_bcmd(kepler, 1, 0,
				     EXYNOS7870_KEPLER_LOAD_ADDR,
				     EXYNOS7870_KEPLER_LOAD_SIZE, true);
	if (ret)
		goto err_power_off;

	ret = exynos7870_kepler_bcmd(kepler, 2, 0,
				     EXYNOS7870_KEPLER_START_ADDR, 0, false);
	if (ret)
		goto err_power_off;

	return 0;

err_power_off:
	regmap_update_bits(kepler->pmu, EXYNOS7870_GNSS_CTRL_NS,
			   EXYNOS7870_GNSS_PWRON, 0);
	return ret;
}

static int exynos7870_kepler_stop(struct rproc *rproc)
{
	struct exynos7870_kepler *kepler = rproc->priv;
	int ret;

	ret = exynos7870_kepler_powerdown_config(kepler);
	if (ret)
		return ret;

	ret = regmap_update_bits(kepler->pmu, EXYNOS7870_GNSS_CTRL_NS,
				 EXYNOS7870_GNSS_RESET_SET,
				 EXYNOS7870_GNSS_RESET_SET);
	if (ret)
		return ret;

	usleep_range(80, 100);

	return regmap_update_bits(kepler->pmu, EXYNOS7870_GNSS_CTRL_NS,
				  EXYNOS7870_GNSS_PWRON, 0);
}

static int exynos7870_kepler_sanity_check(struct rproc *rproc,
					  const struct firmware *fw)
{
	if (!fw->size || fw->size > EXYNOS7870_KEPLER_FW_MAX_SIZE)
		return -EINVAL;

	return 0;
}

static int exynos7870_kepler_load(struct rproc *rproc,
				  const struct firmware *fw)
{
	struct exynos7870_kepler *kepler = rproc->priv;

	memset_io(kepler->fw_mem, 0, EXYNOS7870_KEPLER_FW_MAX_SIZE);
	memcpy_toio(kepler->fw_mem, fw->data, fw->size);

	return 0;
}

static const struct rproc_ops exynos7870_kepler_ops = {
	.start = exynos7870_kepler_start,
	.stop = exynos7870_kepler_stop,
	.load = exynos7870_kepler_load,
	.sanity_check = exynos7870_kepler_sanity_check,
};

static irqreturn_t exynos7870_kepler_active_irq(int irq, void *data)
{
	struct exynos7870_kepler *kepler = data;

	regmap_update_bits(kepler->pmu, EXYNOS7870_GNSS_CTRL_NS,
			   EXYNOS7870_GNSS_ACTIVE_REQ_CLR,
			   EXYNOS7870_GNSS_ACTIVE_REQ_CLR);

	return IRQ_HANDLED;
}

static irqreturn_t exynos7870_kepler_watchdog_irq(int irq, void *data)
{
	struct exynos7870_kepler *kepler = data;

	regmap_update_bits(kepler->pmu, EXYNOS7870_GNSS_CTRL_NS,
			   EXYNOS7870_GNSS_RESET_REQ_CLR,
			   EXYNOS7870_GNSS_RESET_REQ_CLR);
	if (READ_ONCE(kepler->rproc->state) == RPROC_RUNNING)
		rproc_report_crash(kepler->rproc, RPROC_WATCHDOG);

	return IRQ_HANDLED;
}

static irqreturn_t exynos7870_kepler_wakeup_irq(int irq, void *data)
{
	struct exynos7870_kepler *kepler = data;

	regmap_update_bits(kepler->pmu, EXYNOS7870_GNSS_CTRL_NS,
			   EXYNOS7870_GNSS_WAKEUP_REQ_CLR,
			   EXYNOS7870_GNSS_WAKEUP_REQ_CLR);
	pm_wakeup_event(kepler->dev, 0);

	return IRQ_HANDLED;
}

static int exynos7870_kepler_request_irq(struct platform_device *pdev,
					 const char *name,
					 irq_handler_t handler, void *data)
{
	int irq;

	irq = platform_get_irq_byname(pdev, name);
	if (irq < 0)
		return irq;

	return devm_request_irq(&pdev->dev, irq, handler, 0, name, data);
}

static void exynos7870_kepler_free_mbox(void *data)
{
	mbox_free_channel(data);
}

static int exynos7870_kepler_probe(struct platform_device *pdev)
{
	struct device_node *memory_node;
	struct reserved_mem *rmem;
	struct exynos7870_kepler *kepler;
	const char *firmware = NULL;
	struct rproc *rproc;
	int ret;

	ret = rproc_of_parse_firmware(&pdev->dev, 0, &firmware);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to parse firmware-name\n");

	rproc = devm_rproc_alloc(&pdev->dev, "exynos7870-kepler",
				 &exynos7870_kepler_ops, firmware,
				 sizeof(*kepler));
	if (!rproc)
		return -ENOMEM;

	kepler = rproc->priv;
	kepler->dev = &pdev->dev;
	kepler->rproc = rproc;
	init_completion(&kepler->bcmd_done);

	kepler->pmu = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						      "samsung,pmu-syscon");
	if (IS_ERR(kepler->pmu))
		return dev_err_probe(&pdev->dev, PTR_ERR(kepler->pmu),
				     "Failed to get PMU syscon\n");

	kepler->mbox = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						       "samsung,mailbox-syscon");
	if (IS_ERR(kepler->mbox))
		return dev_err_probe(&pdev->dev, PTR_ERR(kepler->mbox),
				     "Failed to get mailbox syscon\n");

	memory_node = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!memory_node)
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "Missing memory-region\n");

	rmem = of_reserved_mem_lookup(memory_node);
	of_node_put(memory_node);
	if (!rmem || rmem->size < EXYNOS7870_KEPLER_FW_MAX_SIZE)
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "Invalid memory-region\n");

	kepler->mem_phys = rmem->base;
	kepler->mem_size = rmem->size;
	kepler->fw_mem = devm_ioremap_wc(&pdev->dev, rmem->base,
					 EXYNOS7870_KEPLER_FW_MAX_SIZE);
	if (!kepler->fw_mem)
		return -ENOMEM;

	kepler->mbox_client.dev = &pdev->dev;
	kepler->mbox_client.rx_callback = exynos7870_kepler_mbox_rx;
	kepler->mbox_client.knows_txdone = true;
	kepler->bcmd_chan = mbox_request_channel_byname(&kepler->mbox_client,
							"bcmd");
	if (IS_ERR(kepler->bcmd_chan))
		return dev_err_probe(&pdev->dev, PTR_ERR(kepler->bcmd_chan),
				     "Failed to request BCMD mailbox\n");

	ret = devm_add_action_or_reset(&pdev->dev,
				       exynos7870_kepler_free_mbox,
				       kepler->bcmd_chan);
	if (ret)
		return ret;

	ret = exynos7870_kepler_request_irq(pdev, "active",
					    exynos7870_kepler_active_irq,
					    kepler);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to request active IRQ\n");

	ret = exynos7870_kepler_request_irq(pdev, "watchdog",
					    exynos7870_kepler_watchdog_irq,
					    kepler);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to request watchdog IRQ\n");

	ret = exynos7870_kepler_request_irq(pdev, "wakeup",
					    exynos7870_kepler_wakeup_irq,
					    kepler);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to request wakeup IRQ\n");

	device_init_wakeup(&pdev->dev, true);
	rproc->auto_boot = false;
	rproc->has_iommu = false;
	platform_set_drvdata(pdev, rproc);

	return devm_rproc_add(&pdev->dev, rproc);
}

static const struct of_device_id exynos7870_kepler_of_match[] = {
	{ .compatible = "samsung,exynos7870-kepler" },
	{ }
};
MODULE_DEVICE_TABLE(of, exynos7870_kepler_of_match);

static struct platform_driver exynos7870_kepler_driver = {
	.probe = exynos7870_kepler_probe,
	.driver = {
		.name = "exynos7870-kepler",
		.of_match_table = exynos7870_kepler_of_match,
	},
};
module_platform_driver(exynos7870_kepler_driver);

MODULE_AUTHOR("postmarketOS Exynos7870 maintainers");
MODULE_DESCRIPTION("Samsung Exynos7870 KEPLER remote processor");
MODULE_LICENSE("GPL");
