// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2020 Samsung Electronics Co., Ltd.
 * Copyright 2020 Google LLC.
 * Copyright 2024 Linaro Ltd.
 */

#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/mailbox_controller.h>
#include <linux/mailbox/exynos-message.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define EXYNOS_MBOX_INTMR0		0x28	/* Interrupt Mask Register 0 */
#define EXYNOS_MBOX_INTGR1		0x40	/* Interrupt Generation Register 1 */

#define EXYNOS_MBOX_INTMR0_MASK		GENMASK(15, 0)
#define EXYNOS_MBOX_INTGR1_MASK		GENMASK(15, 0)

#define EXYNOS_MBOX_CHAN_COUNT		HWEIGHT32(EXYNOS_MBOX_INTGR1_MASK)

#define EXYNOS850_MBOX_INTGR0		0x8	/* Interrupt Generation Register 0	*/
#define EXYNOS850_MBOX_INTMR1		0x24	/* Interrupt Mask Register 1		*/

#define EXYNOS850_MBOX_INTMR1_MASK	GENMASK(15, 0)
#define EXYNOS850_MBOX_INTGR0_MASK	GENMASK(31, 16)

#define EXYNOS850_MBOX_CHAN_COUNT	HWEIGHT32(EXYNOS850_MBOX_INTGR0_MASK)

#define EXYNOS7870_MBOX_INTCR0		0xc
#define EXYNOS7870_MBOX_INTMR0		0x10
#define EXYNOS7870_MBOX_INTSR0		0x14
#define EXYNOS7870_MBOX_INTGR1		0x1c

#define EXYNOS7870_MBOX_RX_MASK		GENMASK(31, 16)
#define EXYNOS7870_MBOX_TX_MASK		GENMASK(15, 0)
#define EXYNOS7870_MBOX_CHAN_COUNT	HWEIGHT32(EXYNOS7870_MBOX_TX_MASK)

/**
 * struct exynos_mbox_driver_data - platform-specific mailbox configuration.
 * @intgr:		offset to the IRQ generation register, doorbell
 *			to APM co-processor.
 * @intgr_shift:	shift to apply to the value written to IRQ generation
 *			register.
 * @intmr:		offset to the IRQ mask register.
 * @intmr_mask:		value to write to the mask register to mask out all
 *			interrupts.
 * @intsr:		offset to the incoming interrupt status register.
 * @intcr:		offset to the incoming interrupt clear register.
 * @rx_mask:		mask of incoming interrupt bits.
 * @rx_shift:		bit offset of the first incoming interrupt.
 * @num_chans:		number of channels the mailbox can support (hardware
 *			capability).
 * @fixed_chans:	whether the channel number is selected by #mbox-cells.
 * @has_clock:		whether the controller requires a pclk.
 */
struct exynos_mbox_driver_data {
	u32 intgr;
	u32 intgr_shift;
	u32 intmr;
	u32 intmr_mask;
	u32 intsr;
	u32 intcr;
	u32 rx_mask;
	u32 rx_shift;
	int num_chans;
	bool fixed_chans;
	bool has_clock;
};

/**
 * struct exynos_mbox - driver's private data.
 * @regs:	mailbox registers base address.
 * @mbox:	pointer to the mailbox controller.
 * @data:	pointer to driver platform-specific data.
 * @lock:	serializes interrupt-mask register updates.
 */
struct exynos_mbox {
	void __iomem *regs;
	struct mbox_controller *mbox;
	const struct exynos_mbox_driver_data *data;
	spinlock_t lock; /* protects the interrupt mask register */
};

struct exynos_mbox_chan {
	unsigned int id;
};

static const struct exynos_mbox_driver_data exynos850_mbox_data = {
	.intgr = EXYNOS850_MBOX_INTGR0,
	.intgr_shift = 16,
	.intmr = EXYNOS850_MBOX_INTMR1,
	.intmr_mask = EXYNOS850_MBOX_INTMR1_MASK,
	.num_chans = EXYNOS850_MBOX_CHAN_COUNT,
	.has_clock = true,
};

static const struct exynos_mbox_driver_data exynos_gs101_mbox_data = {
	.intgr = EXYNOS_MBOX_INTGR1,
	.intgr_shift = 0,
	.intmr = EXYNOS_MBOX_INTMR0,
	.intmr_mask = EXYNOS_MBOX_INTMR0_MASK,
	.num_chans = EXYNOS_MBOX_CHAN_COUNT,
	.has_clock = true,
};

static const struct exynos_mbox_driver_data exynos7870_mbox_data = {
	.intgr = EXYNOS7870_MBOX_INTGR1,
	.intgr_shift = 0,
	.intmr = EXYNOS7870_MBOX_INTMR0,
	.intmr_mask = EXYNOS7870_MBOX_RX_MASK,
	.intsr = EXYNOS7870_MBOX_INTSR0,
	.intcr = EXYNOS7870_MBOX_INTCR0,
	.rx_mask = EXYNOS7870_MBOX_RX_MASK,
	.rx_shift = 16,
	.num_chans = EXYNOS7870_MBOX_CHAN_COUNT,
	.fixed_chans = true,
};

static int exynos_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct device *dev = chan->mbox->dev;
	struct exynos_mbox *exynos_mbox = dev_get_drvdata(dev);
	struct exynos_mbox_msg *msg;
	unsigned int chan_id;

	if (exynos_mbox->data->fixed_chans) {
		struct exynos_mbox_chan *chan_data = chan->con_priv;

		chan_id = chan_data->id;
		goto ring_doorbell;
	}

	msg = data;
	if (!msg)
		return -EINVAL;

	if (msg->chan_id >= exynos_mbox->mbox->num_chans) {
		dev_err(dev, "Invalid channel ID %d\n", msg->chan_id);
		return -EINVAL;
	}

	if (msg->chan_type != EXYNOS_MBOX_CHAN_TYPE_DOORBELL) {
		dev_err(dev, "Unsupported channel type [%d]\n", msg->chan_type);
		return -EINVAL;
	}
	chan_id = msg->chan_id;

ring_doorbell:
	/* Ring the doorbell */
	writel(BIT(chan_id) << exynos_mbox->data->intgr_shift,
	       exynos_mbox->regs + exynos_mbox->data->intgr);

	return 0;
}

static int exynos_mbox_startup(struct mbox_chan *chan)
{
	struct exynos_mbox *exynos_mbox = dev_get_drvdata(chan->mbox->dev);
	struct exynos_mbox_chan *chan_data = chan->con_priv;
	unsigned long flags;
	u32 mask;

	if (!exynos_mbox->data->rx_mask)
		return 0;

	spin_lock_irqsave(&exynos_mbox->lock, flags);
	writel(BIT(chan_data->id + exynos_mbox->data->rx_shift),
	       exynos_mbox->regs + exynos_mbox->data->intcr);
	mask = readl(exynos_mbox->regs + exynos_mbox->data->intmr);
	mask &= ~BIT(chan_data->id + exynos_mbox->data->rx_shift);
	writel(mask, exynos_mbox->regs + exynos_mbox->data->intmr);
	spin_unlock_irqrestore(&exynos_mbox->lock, flags);

	return 0;
}

static void exynos_mbox_shutdown(struct mbox_chan *chan)
{
	struct exynos_mbox *exynos_mbox = dev_get_drvdata(chan->mbox->dev);
	struct exynos_mbox_chan *chan_data = chan->con_priv;
	unsigned long flags;
	u32 mask;

	if (!exynos_mbox->data->rx_mask)
		return;

	spin_lock_irqsave(&exynos_mbox->lock, flags);
	mask = readl(exynos_mbox->regs + exynos_mbox->data->intmr);
	mask |= BIT(chan_data->id + exynos_mbox->data->rx_shift);
	writel(mask, exynos_mbox->regs + exynos_mbox->data->intmr);
	writel(BIT(chan_data->id + exynos_mbox->data->rx_shift),
	       exynos_mbox->regs + exynos_mbox->data->intcr);
	spin_unlock_irqrestore(&exynos_mbox->lock, flags);
}

static const struct mbox_chan_ops exynos_mbox_chan_ops = {
	.send_data = exynos_mbox_send_data,
	.startup = exynos_mbox_startup,
	.shutdown = exynos_mbox_shutdown,
};

static struct mbox_chan *exynos_mbox_of_xlate(struct mbox_controller *mbox,
					      const struct of_phandle_args *sp)
{
	struct exynos_mbox *exynos_mbox = dev_get_drvdata(mbox->dev);
	unsigned int chan_id;
	int i;

	if (exynos_mbox->data->fixed_chans) {
		if (sp->args_count != 1)
			return ERR_PTR(-EINVAL);

		chan_id = sp->args[0];
		if (chan_id >= mbox->num_chans)
			return ERR_PTR(-EINVAL);

		return &mbox->chans[chan_id];
	}

	if (sp->args_count != 0)
		return ERR_PTR(-EINVAL);

	/*
	 * Return the first available channel. When we don't pass the
	 * channel ID from device tree, each channel populated by the driver is
	 * just a software construct or a virtual channel. We use 'void *data'
	 * in send_data() to pass the channel identifiers.
	 */
	for (i = 0; i < mbox->num_chans; i++)
		if (!mbox->chans[i].cl)
			return &mbox->chans[i];
	return ERR_PTR(-EINVAL);
}

static irqreturn_t exynos_mbox_irq(int irq, void *data)
{
	struct exynos_mbox *exynos_mbox = data;
	const struct exynos_mbox_driver_data *drv_data = exynos_mbox->data;
	u32 status;
	int i;

	status = readl(exynos_mbox->regs + drv_data->intsr) & drv_data->rx_mask;
	if (!status)
		return IRQ_NONE;

	writel(status, exynos_mbox->regs + drv_data->intcr);
	status >>= drv_data->rx_shift;

	for (i = 0; i < exynos_mbox->mbox->num_chans; i++) {
		if (status & BIT(i))
			mbox_chan_received_data(&exynos_mbox->mbox->chans[i], NULL);
	}

	return IRQ_HANDLED;
}

static const struct of_device_id exynos_mbox_match[] = {
	{
		.compatible = "google,gs101-mbox",
		.data = &exynos_gs101_mbox_data
	},
	{
		.compatible = "samsung,exynos850-mbox",
		.data = &exynos850_mbox_data
	},
	{
		.compatible = "samsung,exynos7870-mbox",
		.data = &exynos7870_mbox_data
	},
	{},
};
MODULE_DEVICE_TABLE(of, exynos_mbox_match);

static int exynos_mbox_probe(struct platform_device *pdev)
{
	const struct exynos_mbox_driver_data *data;
	struct device *dev = &pdev->dev;
	struct exynos_mbox *exynos_mbox;
	struct mbox_controller *mbox;
	struct mbox_chan *chans;
	struct exynos_mbox_chan *chan_data;
	struct clk *pclk;
	int irq;
	int i;
	int ret;

	data = device_get_match_data(&pdev->dev);
	if (!data)
		return -ENODEV;

	exynos_mbox = devm_kzalloc(dev, sizeof(*exynos_mbox), GFP_KERNEL);
	if (!exynos_mbox)
		return -ENOMEM;

	mbox = devm_kzalloc(dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	chans = devm_kcalloc(dev, data->num_chans, sizeof(*chans), GFP_KERNEL);
	if (!chans)
		return -ENOMEM;
	chan_data = devm_kcalloc(dev, data->num_chans, sizeof(*chan_data),
				 GFP_KERNEL);
	if (!chan_data)
		return -ENOMEM;

	exynos_mbox->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(exynos_mbox->regs))
		return PTR_ERR(exynos_mbox->regs);

	if (data->has_clock) {
		pclk = devm_clk_get_enabled(dev, "pclk");
		if (IS_ERR(pclk))
			return dev_err_probe(dev, PTR_ERR(pclk),
					     "Failed to enable clock.\n");
	}

	exynos_mbox->data = data;
	spin_lock_init(&exynos_mbox->lock);
	mbox->num_chans = data->num_chans;
	mbox->chans = chans;
	mbox->dev = dev;
	mbox->ops = &exynos_mbox_chan_ops;
	mbox->of_xlate = exynos_mbox_of_xlate;

	exynos_mbox->mbox = mbox;
	for (i = 0; i < data->num_chans; i++) {
		chan_data[i].id = i;
		chans[i].con_priv = &chan_data[i];
	}

	platform_set_drvdata(pdev, exynos_mbox);

	/* Start with all incoming interrupts masked until a client starts. */
	writel(data->intmr_mask, exynos_mbox->regs + data->intmr);

	if (data->rx_mask) {
		irq = platform_get_irq(pdev, 0);
		if (irq < 0)
			return irq;

		ret = devm_request_irq(dev, irq, exynos_mbox_irq, 0,
				       dev_name(dev), exynos_mbox);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to request mailbox IRQ.\n");
	}

	return devm_mbox_controller_register(dev, mbox);
}

static struct platform_driver exynos_mbox_driver = {
	.probe	= exynos_mbox_probe,
	.driver	= {
		.name = "exynos-acpm-mbox",
		.of_match_table	= exynos_mbox_match,
	},
};
module_platform_driver(exynos_mbox_driver);

MODULE_AUTHOR("Tudor Ambarus <tudor.ambarus@linaro.org>");
MODULE_DESCRIPTION("Samsung Exynos mailbox driver");
MODULE_LICENSE("GPL");
