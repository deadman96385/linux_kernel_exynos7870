// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung Exynos7870 KEPLER GNSS shared-memory transport
 *
 * This driver implements the framing and circular queues documented by the
 * downstream Samsung GNSS interface while using the upstream GNSS, mailbox,
 * remoteproc, and syscon frameworks for ownership and lifecycle.
 */

#include <linux/align.h>
#include <linux/gnss.h>
#include <linux/io.h>
#include <linux/mailbox_client.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/remoteproc.h>
#include <linux/sizes.h>
#include <linux/unaligned.h>
#include <linux/workqueue.h>

#define EXYNOS7870_KEPLER_IPC_OFFSET	SZ_2M
#define EXYNOS7870_KEPLER_IPC_SIZE	SZ_2M
#define EXYNOS7870_KEPLER_QUEUE_SIZE	SZ_1M

#define EXYNOS7870_MBOX_ISSR(index)	(0x80 + (index) * sizeof(u32))
#define EXYNOS7870_RX_HEAD		10
#define EXYNOS7870_RX_TAIL		11
#define EXYNOS7870_TX_HEAD		12
#define EXYNOS7870_TX_TAIL		13
#define EXYNOS7870_TX_IPC_MSG		5

#define EXYNOS7870_IPC_MSG		0x82
#define EXYNOS7870_FRAME_MAGIC		0xabcd
#define EXYNOS7870_FRAME_CONFIG_SINGLE	0xc000
#define EXYNOS7870_FRAME_HEADER_SIZE	12

#define EXYNOS7870_FRAME_MAGIC_OFFSET	0
#define EXYNOS7870_FRAME_SEQUENCE_OFFSET	2
#define EXYNOS7870_FRAME_CONFIG_OFFSET	4
#define EXYNOS7870_FRAME_LENGTH_OFFSET	6
#define EXYNOS7870_FRAME_CHANNEL_OFFSET	8
#define EXYNOS7870_FRAME_CH_SEQUENCE_OFFSET 9

struct exynos7870_gnss {
	struct device *dev;
	struct gnss_device *gdev;
	struct rproc *rproc;
	struct regmap *mbox;
	u8 __iomem *ipc_mem;
	u8 __iomem *rx_mem;
	u8 __iomem *tx_mem;

	struct mbox_client mbox_client;
	struct mbox_chan *ipc_chan;
	struct work_struct rx_work;
	struct mutex rx_lock; /* serializes receive-ring consumers */
	struct mutex tx_lock; /* serializes transmit-ring producers */
	u32 doorbell;
	u16 frame_sequence;
	u8 channel_sequence;
	bool opened;
};

static u32 exynos7870_ring_advance(u32 pointer, size_t count)
{
	pointer += count;
	if (pointer >= EXYNOS7870_KEPLER_QUEUE_SIZE)
		pointer -= EXYNOS7870_KEPLER_QUEUE_SIZE;

	return pointer;
}

static u32 exynos7870_ring_used(u32 head, u32 tail)
{
	if (head >= tail)
		return head - tail;

	return EXYNOS7870_KEPLER_QUEUE_SIZE - tail + head;
}

static u32 exynos7870_ring_space(u32 head, u32 tail)
{
	return EXYNOS7870_KEPLER_QUEUE_SIZE -
	       exynos7870_ring_used(head, tail) - 1;
}

static void exynos7870_ring_read(void *destination, u8 __iomem *ring,
				 u32 offset, size_t count)
{
	u8 *dest = destination;
	size_t first = min_t(size_t, count,
			     EXYNOS7870_KEPLER_QUEUE_SIZE - offset);

	memcpy_fromio(dest, ring + offset, first);
	if (first != count)
		memcpy_fromio(dest + first, ring, count - first);
}

static void exynos7870_ring_write(u8 __iomem *ring, u32 offset,
				  const void *source, size_t count)
{
	const u8 *src = source;
	size_t first = min_t(size_t, count,
			     EXYNOS7870_KEPLER_QUEUE_SIZE - offset);

	memcpy_toio(ring + offset, src, first);
	if (first != count)
		memcpy_toio(ring, src + first, count - first);
}

static int exynos7870_gnss_ring(struct exynos7870_gnss *gnss)
{
	int ret;

	ret = mbox_send_message(gnss->ipc_chan, &gnss->doorbell);
	if (ret < 0)
		return ret;

	mbox_client_txdone(gnss->ipc_chan, 0);

	return 0;
}

static void exynos7870_gnss_rx_work(struct work_struct *work)
{
	struct exynos7870_gnss *gnss =
		container_of(work, struct exynos7870_gnss, rx_work);
	u8 header[EXYNOS7870_FRAME_HEADER_SIZE];
	u32 available;
	u32 head;
	u32 tail;
	int ret;

	mutex_lock(&gnss->rx_lock);
	if (!READ_ONCE(gnss->opened))
		goto out_unlock;

	ret = regmap_read(gnss->mbox,
			  EXYNOS7870_MBOX_ISSR(EXYNOS7870_RX_HEAD), &head);
	if (ret)
		goto out_unlock;
	ret = regmap_read(gnss->mbox,
			  EXYNOS7870_MBOX_ISSR(EXYNOS7870_RX_TAIL), &tail);
	if (ret)
		goto out_unlock;

	if (head >= EXYNOS7870_KEPLER_QUEUE_SIZE ||
	    tail >= EXYNOS7870_KEPLER_QUEUE_SIZE)
		goto corrupt;

	/* The remote publishes the ring head after writing complete frames. */
	rmb();

	available = exynos7870_ring_used(head, tail);
	while (available >= EXYNOS7870_FRAME_HEADER_SIZE) {
		u32 frame_length;
		u32 total_length;
		u32 payload_offset;
		u32 payload_length;
		u8 *payload;
		int inserted;

		exynos7870_ring_read(header, gnss->rx_mem, tail,
				     sizeof(header));
		if (get_unaligned_le16(header + EXYNOS7870_FRAME_MAGIC_OFFSET) !=
		    EXYNOS7870_FRAME_MAGIC)
			goto corrupt;

		frame_length = get_unaligned_le16(header +
						  EXYNOS7870_FRAME_LENGTH_OFFSET);
		if (frame_length < EXYNOS7870_FRAME_HEADER_SIZE ||
		    frame_length > EXYNOS7870_KEPLER_QUEUE_SIZE)
			goto corrupt;

		total_length = ALIGN(frame_length, sizeof(u32));
		if (total_length > available)
			break;

		payload_length = frame_length - EXYNOS7870_FRAME_HEADER_SIZE;
		payload = kmalloc(payload_length, GFP_KERNEL);
		if (!payload)
			break;

		payload_offset = exynos7870_ring_advance(tail, EXYNOS7870_FRAME_HEADER_SIZE);
		exynos7870_ring_read(payload, gnss->rx_mem, payload_offset,
				     payload_length);
		inserted = gnss_insert_raw(gnss->gdev, payload, payload_length);
		if (inserted != payload_length)
			dev_warn_ratelimited(gnss->dev,
					     "GNSS receive FIFO full, dropped %u bytes\n",
					     payload_length - inserted);
		kfree(payload);

		tail = exynos7870_ring_advance(tail, total_length);
		available -= total_length;
	}

	regmap_write(gnss->mbox,
		     EXYNOS7870_MBOX_ISSR(EXYNOS7870_RX_TAIL), tail);
	goto out_unlock;

corrupt:
	dev_warn_ratelimited(gnss->dev,
			     "Invalid KEPLER receive ring; dropping pending data\n");
	regmap_write(gnss->mbox,
		     EXYNOS7870_MBOX_ISSR(EXYNOS7870_RX_TAIL), head);
out_unlock:
	mutex_unlock(&gnss->rx_lock);
}

static void exynos7870_gnss_mbox_rx(struct mbox_client *client, void *msg)
{
	struct exynos7870_gnss *gnss =
		container_of(client, struct exynos7870_gnss, mbox_client);

	schedule_work(&gnss->rx_work);
}

static int exynos7870_gnss_open(struct gnss_device *gdev)
{
	struct exynos7870_gnss *gnss = gnss_get_drvdata(gdev);
	int ret;

	memset_io(gnss->ipc_mem, 0, EXYNOS7870_KEPLER_IPC_SIZE);
	ret = regmap_write(gnss->mbox,
			   EXYNOS7870_MBOX_ISSR(EXYNOS7870_RX_HEAD), 0);
	if (ret)
		return ret;
	ret = regmap_write(gnss->mbox,
			   EXYNOS7870_MBOX_ISSR(EXYNOS7870_RX_TAIL), 0);
	if (ret)
		return ret;
	ret = regmap_write(gnss->mbox,
			   EXYNOS7870_MBOX_ISSR(EXYNOS7870_TX_HEAD), 0);
	if (ret)
		return ret;
	ret = regmap_write(gnss->mbox,
			   EXYNOS7870_MBOX_ISSR(EXYNOS7870_TX_TAIL), 0);
	if (ret)
		return ret;

	WRITE_ONCE(gnss->opened, true);
	ret = rproc_boot(gnss->rproc);
	if (ret) {
		WRITE_ONCE(gnss->opened, false);
		cancel_work_sync(&gnss->rx_work);
	}

	return ret;
}

static void exynos7870_gnss_close(struct gnss_device *gdev)
{
	struct exynos7870_gnss *gnss = gnss_get_drvdata(gdev);
	int ret;

	WRITE_ONCE(gnss->opened, false);
	cancel_work_sync(&gnss->rx_work);
	ret = rproc_shutdown(gnss->rproc);
	if (ret)
		dev_warn(gnss->dev, "Failed to stop KEPLER: %d\n", ret);
}

static int exynos7870_gnss_write_raw(struct gnss_device *gdev,
				     const unsigned char *data, size_t count)
{
	struct exynos7870_gnss *gnss = gnss_get_drvdata(gdev);
	size_t frame_length = EXYNOS7870_FRAME_HEADER_SIZE + count;
	size_t total_length = ALIGN(frame_length, sizeof(u32));
	u8 *frame;
	u32 head;
	u32 tail;
	int ret;

	if (!READ_ONCE(gnss->opened))
		return -EIO;
	if (frame_length > U16_MAX)
		return -EMSGSIZE;

	frame = kzalloc(total_length, GFP_KERNEL);
	if (!frame)
		return -ENOMEM;

	put_unaligned_le16(EXYNOS7870_FRAME_MAGIC,
			   frame + EXYNOS7870_FRAME_MAGIC_OFFSET);
	put_unaligned_le16(EXYNOS7870_FRAME_CONFIG_SINGLE,
			   frame + EXYNOS7870_FRAME_CONFIG_OFFSET);
	put_unaligned_le16(frame_length,
			   frame + EXYNOS7870_FRAME_LENGTH_OFFSET);
	frame[EXYNOS7870_FRAME_CHANNEL_OFFSET] = 0;
	memcpy(frame + EXYNOS7870_FRAME_HEADER_SIZE, data, count);

	mutex_lock(&gnss->tx_lock);
	ret = regmap_read(gnss->mbox,
			  EXYNOS7870_MBOX_ISSR(EXYNOS7870_TX_HEAD), &head);
	if (ret)
		goto out_unlock;
	ret = regmap_read(gnss->mbox,
			  EXYNOS7870_MBOX_ISSR(EXYNOS7870_TX_TAIL), &tail);
	if (ret)
		goto out_unlock;

	if (head >= EXYNOS7870_KEPLER_QUEUE_SIZE ||
	    tail >= EXYNOS7870_KEPLER_QUEUE_SIZE) {
		ret = -EIO;
		goto out_unlock;
	}
	if (exynos7870_ring_space(head, tail) < total_length) {
		ret = -ENOSPC;
		goto out_unlock;
	}

	put_unaligned_le16(++gnss->frame_sequence,
			   frame + EXYNOS7870_FRAME_SEQUENCE_OFFSET);
	frame[EXYNOS7870_FRAME_CH_SEQUENCE_OFFSET] = ++gnss->channel_sequence;

	exynos7870_ring_write(gnss->tx_mem, head, frame, total_length);
	/* Publish all frame bytes before making the new head visible. */
	wmb();
	ret = regmap_write(gnss->mbox,
			   EXYNOS7870_MBOX_ISSR(EXYNOS7870_TX_HEAD),
			   exynos7870_ring_advance(head, total_length));
	if (ret)
		goto out_unlock;

	ret = regmap_write(gnss->mbox,
			   EXYNOS7870_MBOX_ISSR(EXYNOS7870_TX_IPC_MSG),
			   EXYNOS7870_IPC_MSG);
	if (ret)
		goto restore_head;

	ret = exynos7870_gnss_ring(gnss);
	if (ret)
		goto restore_head;

	ret = count;
	goto out_unlock;

restore_head:
	regmap_write(gnss->mbox,
		     EXYNOS7870_MBOX_ISSR(EXYNOS7870_TX_HEAD), head);
out_unlock:
	mutex_unlock(&gnss->tx_lock);
	kfree(frame);

	return ret;
}

static const struct gnss_operations exynos7870_gnss_ops = {
	.open = exynos7870_gnss_open,
	.close = exynos7870_gnss_close,
	.write_raw = exynos7870_gnss_write_raw,
};

static void exynos7870_gnss_free_mbox(void *data)
{
	mbox_free_channel(data);
}

static void exynos7870_gnss_put_rproc(void *data)
{
	rproc_put(data);
}

static int exynos7870_gnss_probe(struct platform_device *pdev)
{
	struct device_node *memory_node;
	struct reserved_mem *rmem;
	struct exynos7870_gnss *gnss;
	phandle rproc_phandle;
	int ret;

	gnss = devm_kzalloc(&pdev->dev, sizeof(*gnss), GFP_KERNEL);
	if (!gnss)
		return -ENOMEM;

	gnss->dev = &pdev->dev;
	INIT_WORK(&gnss->rx_work, exynos7870_gnss_rx_work);
	mutex_init(&gnss->rx_lock);
	mutex_init(&gnss->tx_lock);

	ret = of_property_read_u32(pdev->dev.of_node, "remoteproc",
				   &rproc_phandle);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Missing remoteproc phandle\n");

	gnss->rproc = rproc_get_by_phandle(rproc_phandle);
	if (!gnss->rproc)
		return -EPROBE_DEFER;

	ret = devm_add_action_or_reset(&pdev->dev,
				       exynos7870_gnss_put_rproc,
				       gnss->rproc);
	if (ret)
		return ret;

	gnss->mbox = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						     "samsung,mailbox-syscon");
	if (IS_ERR(gnss->mbox))
		return dev_err_probe(&pdev->dev, PTR_ERR(gnss->mbox),
				     "Failed to get mailbox syscon\n");

	memory_node = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!memory_node)
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "Missing memory-region\n");

	rmem = of_reserved_mem_lookup(memory_node);
	of_node_put(memory_node);
	if (!rmem || rmem->size < EXYNOS7870_KEPLER_IPC_OFFSET +
				  EXYNOS7870_KEPLER_IPC_SIZE)
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "Invalid memory-region\n");

	gnss->ipc_mem = devm_ioremap_wc(&pdev->dev,
					rmem->base + EXYNOS7870_KEPLER_IPC_OFFSET,
					EXYNOS7870_KEPLER_IPC_SIZE);
	if (!gnss->ipc_mem)
		return -ENOMEM;
	gnss->rx_mem = gnss->ipc_mem;
	gnss->tx_mem = gnss->ipc_mem + EXYNOS7870_KEPLER_QUEUE_SIZE;

	gnss->mbox_client.dev = &pdev->dev;
	gnss->mbox_client.rx_callback = exynos7870_gnss_mbox_rx;
	gnss->mbox_client.knows_txdone = true;
	gnss->ipc_chan = mbox_request_channel_byname(&gnss->mbox_client, "ipc");
	if (IS_ERR(gnss->ipc_chan))
		return dev_err_probe(&pdev->dev, PTR_ERR(gnss->ipc_chan),
				     "Failed to request IPC mailbox\n");

	ret = devm_add_action_or_reset(&pdev->dev,
				       exynos7870_gnss_free_mbox,
				       gnss->ipc_chan);
	if (ret)
		return ret;

	gnss->gdev = gnss_allocate_device(&pdev->dev);
	if (!gnss->gdev)
		return -ENOMEM;

	gnss->gdev->ops = &exynos7870_gnss_ops;
	gnss->gdev->type = GNSS_TYPE_KEPLER;
	gnss_set_drvdata(gnss->gdev, gnss);

	ret = gnss_register_device(gnss->gdev);
	if (ret) {
		gnss_put_device(gnss->gdev);
		return ret;
	}

	platform_set_drvdata(pdev, gnss);

	return 0;
}

static void exynos7870_gnss_remove(struct platform_device *pdev)
{
	struct exynos7870_gnss *gnss = platform_get_drvdata(pdev);

	gnss_deregister_device(gnss->gdev);
	cancel_work_sync(&gnss->rx_work);
	gnss_put_device(gnss->gdev);
}

static const struct of_device_id exynos7870_gnss_of_match[] = {
	{ .compatible = "samsung,exynos7870-kepler-gnss" },
	{ }
};
MODULE_DEVICE_TABLE(of, exynos7870_gnss_of_match);

static struct platform_driver exynos7870_gnss_driver = {
	.probe = exynos7870_gnss_probe,
	.remove = exynos7870_gnss_remove,
	.driver = {
		.name = "exynos7870-kepler-gnss",
		.of_match_table = exynos7870_gnss_of_match,
	},
};
module_platform_driver(exynos7870_gnss_driver);

MODULE_AUTHOR("postmarketOS Exynos7870 maintainers");
MODULE_DESCRIPTION("Samsung Exynos7870 KEPLER GNSS transport");
MODULE_LICENSE("GPL");
