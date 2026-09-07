// SPDX-License-Identifier: GPL-2.0-only
/* Native ISP processing DMA owner. Initial mapping qualification stage. */
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/iopoll.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-vmalloc.h>

struct exynos7870_isp {
	void __iomem *regs;
	void __iomem *regs_b;
	struct clk_bulk_data *clocks;
	int num_clocks;
	struct miscdevice misc;
	atomic_t busy;
	struct v4l2_device v4l2;
	struct v4l2_m2m_dev *m2m;
	struct video_device video;
	struct mutex video_lock;
	/* Protected by video_lock; M2M serializes jobs on this shared owner. */
	struct isp_frame *video_frame;
	unsigned int video_users;
};

#include "exynos7870-isp-frame.inc"
#include "exynos7870-isp-video.inc"

static int isp_resume(struct device *dev)
{
	struct exynos7870_isp *isp = dev_get_drvdata(dev);

	return clk_bulk_prepare_enable(isp->num_clocks, isp->clocks);
}

static int isp_suspend(struct device *dev)
{
	struct exynos7870_isp *isp = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(isp->num_clocks, isp->clocks);
	return 0;
}

static ssize_t versions_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct exynos7870_isp *isp = dev_get_drvdata(dev);
	u32 version, dma;
	int ret = pm_runtime_resume_and_get(dev);

	if (ret < 0)
		return ret;
	version = readl(isp->regs + 0x30f0);
	dma = readl(isp->regs + 0x31f4);
	pm_runtime_put_sync(dev);
	return sysfs_emit(buf, "ISP=%08x DMA=%08x\n", version, dma);
}
static DEVICE_ATTR_RO(versions);

/* Two simultaneous full-sized mappings; no ISP DMA is submitted. */
static ssize_t dma_test_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	const size_t sizes[2] = {
		PAGE_ALIGN(4144 * 3106 * 2), /* RG10 in 16-bit storage */
		PAGE_ALIGN(4144 * 3106 * 3), /* RGB888 */
	};
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
	dma_addr_t addr[2];
	void *cpu[2] = { NULL, NULL };
	size_t offset;
	int i, ret;

	if (!domain || (domain->type != IOMMU_DOMAIN_DMA &&
			domain->type != IOMMU_DOMAIN_DMA_FQ))
		return -ENODEV;
	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;
	for (i = 0; i < 2; i++) {
		size_t size = sizes[i];

		cpu[i] = dma_alloc_coherent(dev, size, &addr[i], GFP_KERNEL);
		if (!cpu[i]) {
			ret = -ENOMEM;
			goto out;
		}
		if (upper_32_bits(addr[i]) || upper_32_bits(addr[i] + size - 1)) {
			ret = -ERANGE;
			goto out;
		}
		for (offset = 0; offset < size; offset += PAGE_SIZE) {
			if (!iommu_iova_to_phys(domain, addr[i] + offset)) {
				ret = -EFAULT;
				goto out;
			}
		}
	}
	ret = sysfs_emit(buf,
		"input_bytes=%zu output_bytes=%zu input=%pad output=%pad input_pages=%zu output_pages=%zu submitted=0\n",
		sizes[0], sizes[1], &addr[0], &addr[1],
		sizes[0] / PAGE_SIZE, sizes[1] / PAGE_SIZE);
out:
	for (i = 0; i < 2; i++)
		if (cpu[i])
			dma_free_coherent(dev, sizes[i], cpu[i], addr[i]);
	pm_runtime_put_sync(dev);
	return ret;
}
static DEVICE_ATTR(dma_test, 0400, dma_test_show, NULL);

static struct attribute *isp_attrs[] = {
	&dev_attr_versions.attr, &dev_attr_dma_test.attr, NULL,
};
ATTRIBUTE_GROUPS(isp);

static int isp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct exynos7870_isp *isp;
	int ret;

	isp = devm_kzalloc(dev, sizeof(*isp), GFP_KERNEL);
	if (!isp)
		return -ENOMEM;
	platform_set_drvdata(pdev, isp);
	isp->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(isp->regs))
		return PTR_ERR(isp->regs);
	isp->regs_b = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(isp->regs_b))
		return PTR_ERR(isp->regs_b);
	isp->num_clocks = devm_clk_bulk_get_all(dev, &isp->clocks);
	if (isp->num_clocks <= 0)
		return isp->num_clocks ? isp->num_clocks : -EINVAL;
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;
	pm_runtime_set_suspended(dev);
	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;
	isp->misc.minor = MISC_DYNAMIC_MINOR;
	isp->misc.name = "exynos7870-isp-frame";
	isp->misc.fops = &isp_frame_fops;
	isp->misc.parent = dev;
	isp->misc.mode = 0600;
	ret = misc_register(&isp->misc);
	if (ret)
		return ret;
	ret = devm_add_action_or_reset(dev, isp_frame_deregister, isp);
	if (ret)
		return ret;
	return isp_video_register(isp);
}

static const struct dev_pm_ops isp_pm_ops = {
	RUNTIME_PM_OPS(isp_suspend, isp_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
};
static const struct of_device_id isp_match[] = {
	{ .compatible = "samsung,exynos7870-isp" }, { }
};
MODULE_DEVICE_TABLE(of, isp_match);
static struct platform_driver isp_driver = {
	.probe = isp_probe,
	.driver = {
		.name = "exynos7870-isp",
		.of_match_table = isp_match,
		.pm = pm_ptr(&isp_pm_ops),
		.dev_groups = isp_groups,
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(isp_driver);
MODULE_DESCRIPTION("Exynos7870 native ISP processing DMA owner");
MODULE_LICENSE("GPL");
