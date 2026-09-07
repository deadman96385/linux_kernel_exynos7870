// SPDX-License-Identifier: GPL-2.0-only
/* Exynos7870 shared camera front-end owner, initial power qualification. */
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/mutex.h>
#include <linux/phy/phy.h>
#include "j7y17lte-camera-capture.h"
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <linux/kthread.h>
#include <linux/dma-buf.h>
#include <linux/wait.h>
#include <linux/sizes.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>
#include "j7y17lte-camera-update.h"

struct exynos7870_camera_fe;
#define CAMERA_EVENT_RESULT (V4L2_EVENT_PRIVATE_START + 1)
struct camera_video_buffer {
 struct vb2_v4l2_buffer vb;
 struct list_head list;
};
struct camera_video {
 struct device *dev;
 struct exynos7870_camera_fe *fe;
 struct v4l2_device v4l2_dev;
 struct video_device vdev;
 struct v4l2_ctrl_handler controls;
 struct v4l2_ctrl *exposure_ctrl;
 struct vb2_queue queue;
 struct mutex lock;
 spinlock_t queued_lock;
 struct list_head queued;
 struct task_struct *worker;
 wait_queue_head_t buffer_wait;
 struct camera_video_buffer *active;
 struct dma_buf *active_pin;
 struct completion start_done;
 int start_result;
 bool running, stop_requested, test_pattern;
 unsigned int exposure, analogue_gain, focus, frame_length;
 u32 first_counter;
 u32 stream_id;
 struct j7cam_update_request pending_update;
 bool update_pending, update_busy, update_accepting;
 u32 next_generation, written_generation;
 unsigned int written_exposure, written_gain, written_frame_length;
 unsigned int timeout_frame_length;
};

struct exynos7870_camera_fe {
	void __iomem *csis[2];
	struct clk_bulk_data *clocks;
	int num_clocks;
	struct mutex capture_lock;
	struct completion frame_done;
	struct phy *phy;
	int irq, last_result;
	void *frame_cpu;
	dma_addr_t frame_dma;
	u32 otf_events, dma_events, dma_error, byte_count, active_ctrl;
	bool frame_valid, poisoned;
	u32 target_frames, completed_frames, slot_hits[8];
	u64 first_fe_ns, last_fe_ns;
	struct camera_video *video;
	u32 copy_slot, copy_counter;
	u64 copy_timestamp;
	struct dma_buf *poison_pin;
	u32 copied_frames, rejected_frames, empty_frames;
	u64 max_copy_ns;
};

static int camera_fe_resume(struct device *dev)
{
	struct exynos7870_camera_fe *fe = dev_get_drvdata(dev);

	return clk_bulk_prepare_enable(fe->num_clocks, fe->clocks);
}

static int camera_fe_suspend(struct device *dev)
{
	struct exynos7870_camera_fe *fe = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(fe->num_clocks, fe->clocks);
	return 0;
}

static ssize_t versions_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct exynos7870_camera_fe *fe = dev_get_drvdata(dev);
	u32 rear, front;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;
	rear = readl(fe->csis[0]);
	front = readl(fe->csis[1]);
	pm_runtime_put_sync(dev);
	return sysfs_emit(buf, "CSIS0=0x%08x CSIS1=0x%08x\n", rear, front);
}
static DEVICE_ATTR_RO(versions);

/* Allocation/mapping qualification only: no CSIS DMA registers are written. */
static ssize_t dma_test_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	const size_t size = PAGE_ALIGN(4144 * 3106 * 2);
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
	dma_addr_t addr;
	void *cpu;
	size_t offset;
	int ret;

	if (!domain || (domain->type != IOMMU_DOMAIN_DMA &&
			domain->type != IOMMU_DOMAIN_DMA_FQ))
		return -ENODEV;
	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;
	cpu = dma_alloc_coherent(dev, size, &addr, GFP_KERNEL);
	if (!cpu) {
		ret = -ENOMEM;
		goto suspend;
	}
	if (upper_32_bits(addr) || upper_32_bits(addr + size - 1)) {
		ret = -ERANGE;
		goto free;
	}
	for (offset = 0; offset < size; offset += PAGE_SIZE) {
		if (!iommu_iova_to_phys(domain, addr + offset)) {
			ret = -EFAULT;
			goto free;
		}
	}
	memset(cpu, 0xa5, size);
	ret = sysfs_emit(buf, "mapped_bytes=%zu iova=%pad pages=%zu no_capture=1\n",
			 size, &addr, size / PAGE_SIZE);
free:
	dma_free_coherent(dev, size, cpu, addr);
suspend:
	pm_runtime_put_sync(dev);
	return ret;
}
static DEVICE_ATTR(dma_test, 0400, dma_test_show, NULL);

/* VC0 single-frame qualification. Registers follow the selected 7870 CSIS4.1 API. */
#define CAMERA_FRAME_BYTES (4144 * 3106 * 2)
#define CAMERA_SLOT_BYTES PAGE_ALIGN(CAMERA_FRAME_BYTES)
#define CAMERA_ALLOC_BYTES (CAMERA_SLOT_BYTES * 8)

static int camera_video_dma_loop(struct exynos7870_camera_fe *fe,
                                 struct i2c_client *sensor);

static unsigned int camera_frame_period_us(unsigned int frame_length)
{
 return DIV_ROUND_UP_ULL((u64)frame_length * J7CAM_LINE_LENGTH * 1000000,
                         J7CAM_PIXEL_RATE);
}

static unsigned int camera_dma_idle_timeout_us(unsigned int frame_length)
{
 /* Disable may latch at the next frame boundary. Preserve the old default. */
 return max(150000U, camera_frame_period_us(frame_length) + 100000U);
}

static irqreturn_t camera_fe_irq(int irq, void *data)
{
	struct exynos7870_camera_fe *fe = data;
	void __iomem *base = fe->csis[0];
	u32 otf = readl(base + 0x14), dma = readl(base + 0x1c);

	if (!otf && !dma)
		return IRQ_NONE;
	writel(otf, base + 0x14);
	writel(dma, base + 0x1c);
	fe->otf_events |= otf;
	fe->dma_events |= dma;
	if (dma & BIT(12))
		fe->dma_error |= readl(base + 0x1404);
	if (dma & BIT(8)) {
		u32 slot = (readl(base + 0x1030) >> 2) & 7;

		fe->copy_slot = slot;
		fe->copy_counter = readl(base + 0x100);
		fe->copy_timestamp = ktime_get_ns();
		if (!fe->completed_frames && fe->video)
			fe->video->first_counter = fe->copy_counter;
		fe->slot_hits[slot]++;
		fe->completed_frames++;
		fe->last_fe_ns = ktime_get_mono_fast_ns();
		if (fe->completed_frames == 1)
			fe->first_fe_ns = fe->last_fe_ns;
	}
	if ((fe->video && READ_ONCE(fe->video->running) && (dma & BIT(8))) ||
	    fe->completed_frames >= fe->target_frames ||
	    (otf & 0xfffff) || (dma & BIT(12))) {
		/* Disable the next frame; storage stays owned until quiescence. */
		writel(1, base + 0x1000);
		complete(&fe->frame_done);
	}
	return IRQ_HANDLED;
}

static int camera_fe_capture(struct device *dev, bool test_pattern)
{
	struct exynos7870_camera_fe *fe = dev_get_drvdata(dev);
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
	void __iomem *base = fe->csis[0];
	struct i2c_client *sensor;
	struct device_node *node;
	u32 value;
	bool prepared = false, phy_on = false;
	bool video_mode = fe->video && READ_ONCE(fe->video->running);
	int i, ret, stop_ret, sensor_stop_ret;

	if (fe->poisoned)
		return -EIO;
	if (!domain || (domain->type != IOMMU_DOMAIN_DMA &&
			domain->type != IOMMU_DOMAIN_DMA_FQ))
		return -ENODEV;
	fe->frame_valid = false;
	node = of_parse_phandle(dev->of_node, "samsung,rear-sensor", 0);
	if (!node)
		return -ENODEV;
	sensor = of_find_i2c_device_by_node(node);
	of_node_put(node);
	if (!sensor)
		return -EPROBE_DEFER;
	device_lock(&sensor->dev);
	if (!sensor->dev.driver) {
		ret = -ENODEV;
		goto put_sensor;
	}
	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		goto put_sensor;
	if (!video_mode && !fe->frame_cpu) {
		fe->frame_cpu = dma_alloc_coherent(dev, CAMERA_ALLOC_BYTES,
						  &fe->frame_dma, GFP_KERNEL);
		if (!fe->frame_cpu) {
			ret = -ENOMEM;
			goto suspend;
		}
	}
	if (upper_32_bits(fe->frame_dma + CAMERA_ALLOC_BYTES - 1)) {
		ret = -ERANGE;
		goto suspend;
	}
	if (!video_mode)
		memset(fe->frame_cpu, 0xa5, CAMERA_ALLOC_BYTES);
	ret = j7cam_capture_prepare(sensor, test_pattern,
		video_mode ? fe->video->exposure : J7CAM_EXPOSURE_DEFAULT,
		video_mode ? fe->video->analogue_gain : 0,
		video_mode ? fe->video->focus : J7CAM_FOCUS_DEFAULT,
		video_mode ? fe->video->frame_length : J7CAM_FRAME_LENGTH_DEFAULT);
	if (ret)
		goto suspend;
	prepared = true;
	ret = phy_power_on(fe->phy);
	if (ret)
		goto release;
	phy_on = true;
	writel(readl(base + 4) | BIT(1), base + 4);
	ret = readl_poll_timeout(base + 4, value, !(value & BIT(1)), 10, 1000);
	if (ret)
		goto release;
	for (i = 0; i < 4; i++)
		writel(1, base + 0x1000 + i * 0x100);
	writel(0, base + 0x10);
	writel(0, base + 0x18);
	writel(readl(base + 0x14), base + 0x14);
	writel(readl(base + 0x1c), base + 0x1c);
	/* VC0 RAW10, 4 lanes, channel-0-only, normal 16-bit storage. */
	writel(3 << 8, base + 4);
	writel(0x2b << 2, base + 0x40);
	writel((3106 << 16) | 4144, base + 0x44);
	writel(0, base + 0x1004);
	writel(0, base + 0x1008);
	writel((readl(base + 0x24) & ~0xff00001f) | (28 << 24) | 0x1f,
	       base + 0x24);
	if (!video_mode)
		for (i = 0; i < 8; i++)
			writel(lower_32_bits(fe->frame_dma + i * CAMERA_SLOT_BYTES),
			       base + 0x1010 + 4 * i);
	fe->completed_frames = 0;
	fe->copied_frames = 0;
	fe->rejected_frames = 0;
	fe->empty_frames = 0;
	fe->max_copy_ns = 0;
	fe->first_fe_ns = 0;
	fe->last_fe_ns = 0;
	memset(fe->slot_hits, 0, sizeof(fe->slot_hits));
	fe->otf_events = 0;
	fe->dma_events = 0;
	fe->dma_error = 0;
	reinit_completion(&fe->frame_done);
	enable_irq(fe->irq);
	writel(0x011fffff, base + 0x10);
	writel(0x00003ff0, base + 0x18);
	dma_wmb();
	if (!video_mode)
		writel(BIT(1), base + 0x1000);
	writel((3 << 8) | (0xf << 16) | 1, base + 4);
	ret = j7cam_capture_stream(sensor, true);
	if (video_mode) {
		fe->video->start_result = ret;
		complete(&fe->video->start_done);
		if (!ret)
			ret = camera_video_dma_loop(fe, sensor);
	} else if (!ret && !wait_for_completion_timeout(&fe->frame_done,
						 msecs_to_jiffies(2000 + fe->target_frames * 100))) {
		ret = -ETIMEDOUT;
	}
	writel(1, base + 0x1000);
	/* Allow the following frame boundary to latch DMA disable. */
	stop_ret = readl_poll_timeout(base + 0x1030, value, value & 1,
				     1000, camera_dma_idle_timeout_us(video_mode ?
				     fe->video->timeout_frame_length : J7CAM_FRAME_LENGTH_DEFAULT));
	sensor_stop_ret = j7cam_capture_stream(sensor, false);
	if (sensor_stop_ret) {
		dev_err(dev, "sensor stream-off failed: %d\n", sensor_stop_ret);
		if (!ret)
			ret = sensor_stop_ret;
	}
	writel(0, base + 0x10);
	writel(0, base + 0x18);
	disable_irq(fe->irq);
	if (stop_ret) {
		/* Explicit abort and completion before CPU ownership. */
		writel(BIT(13), base + 0x1c);
		writel(1, base + 0x1400);
		stop_ret = readl_poll_timeout(base + 0x1c, value,
					     value & BIT(13), 100, 100000);
		if (!stop_ret)
			writel(BIT(13), base + 0x1c);
	}
	fe->byte_count = readl(base + 0x1040);
	fe->active_ctrl = readl(base + 0x1030);
	writel(readl(base + 4) & ~1, base + 4);
	writel(readl(base + 0x24) & ~0x1f, base + 0x24);
	if (stop_ret) {
		/* Keep buffer mapped and power held; never free uncertain DMA storage. */
		fe->poisoned = true;
		ret = -EIO;
		dev_err(dev, "DMA quiescence failed; buffer and runtime power retained\n");
	} else if (!ret && (!(fe->dma_events & BIT(8)) ||
			   (fe->otf_events & 0xfffff) ||
			   (fe->dma_events & BIT(12)) || fe->dma_error)) {
		ret = -EIO;
	}
	if (video_mode && fe->video->active) {
		struct camera_video *video = fe->video;
		if (fe->poisoned) {
			fe->poison_pin = video->active_pin;
			video->active_pin = NULL;
		} else {
			dma_buf_put(video->active_pin);
			video->active_pin = NULL;
		}
		vb2_set_plane_payload(&video->active->vb.vb2_buf, 0, 0);
		vb2_buffer_done(&video->active->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		video->active = NULL;
	}
	dma_rmb();
	if (!ret && !video_mode)
		fe->frame_valid = true;
release:
	if (phy_on)
		phy_power_off(fe->phy);
	if (prepared)
		j7cam_capture_release(sensor);
suspend:
	if (!fe->poisoned)
		pm_runtime_put_sync(dev);
put_sensor:
	device_unlock(&sensor->dev);
	put_device(&sensor->dev);
	return ret;
}

/* VB2 allocations are DMA targets. Every address slot points to the same
 * retained allocation. Publish it only after ACTIVE_DMA_DISABLE acknowledges
 * idle. No IRQ latency assumption transfers ownership to userspace.
 */
static void camera_video_return_buffers(struct camera_video *video,
                                       enum vb2_buffer_state state)
{
 struct camera_video_buffer *buffer;
 unsigned long flags;
 spin_lock_irqsave(&video->queued_lock, flags);
 while (!list_empty(&video->queued)) {
  buffer = list_first_entry(&video->queued, struct camera_video_buffer, list);
  list_del(&buffer->list);
  vb2_buffer_done(&buffer->vb.vb2_buf, state);
 }
 spin_unlock_irqrestore(&video->queued_lock, flags);
}

static bool camera_video_has_buffer(struct camera_video *video)
{
 unsigned long flags;
 bool available;
 spin_lock_irqsave(&video->queued_lock, flags);
 available = !list_empty(&video->queued);
 spin_unlock_irqrestore(&video->queued_lock, flags);
 return available;
}

static void camera_video_update_result(struct camera_video *video,
                                      const struct j7cam_update_request *request,
                                      int result, unsigned int written,
                                      u64 started, u64 ended)
{
 struct v4l2_event event = { .type = J7CAM_EVENT_UPDATE };
 u32 data[16] = { 1, request->stream_id, request->generation, (u32)result,
  written, request->exposure, request->analogue_gain, request->frame_length,
  video->fe->copy_counter, 0, 0, 0, lower_32_bits(started), upper_32_bits(started),
  lower_32_bits(ended), upper_32_bits(ended) };

 memcpy(event.u.data, data, sizeof(data));
 v4l2_event_queue(&video->vdev, &event);
}

static void camera_video_update_timing(struct camera_video *video,
                                      const struct j7cam_update_request *request,
                                      int result, unsigned int written,
                                      u32 before, u32 after, u32 flags,
                                      u64 started, u64 ended)
{
 struct v4l2_event event = { .type = J7CAM_EVENT_UPDATE_TIMING };
 u32 data[16] = { 1, request->stream_id, request->generation, (u32)result,
  written, request->exposure, request->analogue_gain, request->frame_length,
  before, after, flags, 0, lower_32_bits(started), upper_32_bits(started),
  lower_32_bits(ended), upper_32_bits(ended) };

 memcpy(event.u.data, data, sizeof(data));
 v4l2_event_queue(&video->vdev, &event);
}

static void camera_video_cancel_update(struct camera_video *video)
{
 struct j7cam_update_request request;
 unsigned long flags;
 bool pending;

 spin_lock_irqsave(&video->queued_lock, flags);
 video->update_accepting = false;
 pending = video->update_pending;
 request = video->pending_update;
 video->update_pending = false;
 video->update_busy = false;
 spin_unlock_irqrestore(&video->queued_lock, flags);
 if (pending) {
  u64 ended = ktime_get_ns();

  camera_video_update_result(video, &request, -ECANCELED, 0, 0, ended);
  /* Cancellation may run after PM release: never sample MMIO here. */
  camera_video_update_timing(video, &request, -ECANCELED, 0,
                             0, 0, 0, 0, ended);
 }
}

static int camera_video_apply_update(struct camera_video *video,
                                    struct i2c_client *sensor)
{
 struct j7cam_update_request request;
 unsigned long flags;
 unsigned int written = 0;
 u32 before, after, timing_flags = 1;
 u64 started, ended;
 int ret;

 spin_lock_irqsave(&video->queued_lock, flags);
 if (!video->update_pending) {
  spin_unlock_irqrestore(&video->queued_lock, flags);
  return 0;
 }
 request = video->pending_update;
 video->update_pending = false;
 spin_unlock_irqrestore(&video->queued_lock, flags);
 started = ktime_get_ns();
 /* Worker owns prepared sensor/CAM PM, including when RAW DMA is starved.
  * This is the receiver's current counter, not the last delivered buffer.
  * Two MMIO samples bound the update; no sensor register readback is added.
  */
 before = readl(video->fe->csis[0] + 0x100);
 /* Retain a conservative period even if a later transfer partially fails. */
 video->timeout_frame_length = max(video->timeout_frame_length, request.frame_length);
 if (READ_ONCE(video->stop_requested)) {
  ret = -ECANCELED;
 } else {
  timing_flags |= 2; /* Update call attempted, not proof of physical latch. */
  ret = j7cam_capture_update(sensor, request.exposure, request.analogue_gain,
                             request.frame_length, &written);
 }
 after = readl(video->fe->csis[0] + 0x100);
 ended = ktime_get_ns();
 if (!ret) {
  video->written_exposure = request.exposure;
  video->written_gain = request.analogue_gain;
  video->written_frame_length = request.frame_length;
  video->written_generation = request.generation;
 }
 camera_video_update_result(video, &request, ret, written, started, ended);
 camera_video_update_timing(video, &request, ret, written,
                            before, after, timing_flags, started, ended);
 spin_lock_irqsave(&video->queued_lock, flags);
 video->update_busy = false;
 if (ret)
  video->update_accepting = false;
 spin_unlock_irqrestore(&video->queued_lock, flags);
 return ret;
}

static int camera_video_dma_loop(struct exynos7870_camera_fe *fe,
                                 struct i2c_client *sensor)
{
 struct camera_video *video = fe->video;
 void __iomem *base = fe->csis[0];
 unsigned long flags;
 u32 value, before;
 unsigned int idle_timeout = camera_dma_idle_timeout_us(video->timeout_frame_length);
 /* Rearming can miss the current start-of-frame, then wait a full frame. */
 unsigned int frame_timeout_ms = max(1000U,
          DIV_ROUND_UP(2 * camera_frame_period_us(video->timeout_frame_length), 1000) + 250);
 int ret, i;

 /* Sensor keeps running; DMA stays disabled whenever there is no buffer. */
 ret = readl_poll_timeout(base + 0x1030, value, value & 1, 1000, idle_timeout);
 if (ret)
  return ret;
 while (!READ_ONCE(video->stop_requested)) {
  struct camera_video_buffer *buffer;
  struct vb2_buffer *vb;
  dma_addr_t address;
  bool valid;

  wait_event(video->buffer_wait, READ_ONCE(video->stop_requested) ||
             READ_ONCE(video->update_pending) || camera_video_has_buffer(video));
  if (READ_ONCE(video->stop_requested))
   break;
  ret = camera_video_apply_update(video, sensor);
  if (ret)
   return ret;
  if (!camera_video_has_buffer(video))
   continue;
  idle_timeout = camera_dma_idle_timeout_us(video->timeout_frame_length);
  frame_timeout_ms = max(1000U,
          DIV_ROUND_UP(2 * camera_frame_period_us(video->timeout_frame_length), 1000) + 250);
  spin_lock_irqsave(&video->queued_lock, flags);
  buffer = list_first_entry(&video->queued, struct camera_video_buffer, list);
  list_del(&buffer->list);
  spin_unlock_irqrestore(&video->queued_lock, flags);
  vb = &buffer->vb.vb2_buf;
  /* An exported reference pins allocation/IOVA even if abort later fails and
   * userspace closes the queue. No fd is installed or exposed. */
  video->active_pin = vb2_dma_contig_memops.get_dmabuf(vb, vb->planes[0].mem_priv, O_RDWR);
  if (IS_ERR_OR_NULL(video->active_pin)) {
   video->active_pin = NULL;
   vb2_set_plane_payload(vb, 0, 0);
   vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
   return -ENOMEM;
  }
  video->active = buffer;
  address = vb2_dma_contig_plane_dma_addr(vb, 0);
  /* The previous buffer is idle, so discard old causes before rearming. */
  disable_irq(fe->irq);
  writel(readl(base + 0x1c), base + 0x1c);
  reinit_completion(&fe->frame_done);
  before = fe->completed_frames;
  for (i = 0; i < 8; i++)
   writel(lower_32_bits(address), base + 0x1010 + i * 4);
  dma_wmb();
  enable_irq(fe->irq);
  writel(BIT(1), base + 0x1000);
  if (!wait_for_completion_timeout(&fe->frame_done, msecs_to_jiffies(frame_timeout_ms)))
   return -ETIMEDOUT;
  writel(1, base + 0x1000);
  ret = readl_poll_timeout(base + 0x1030, value, value & 1, 1000, idle_timeout);
  if (ret)
   return ret; /* common shutdown attempts abort before returning buffer */
  synchronize_irq(fe->irq);
  valid = fe->completed_frames != before && !fe->dma_error &&
          !(fe->dma_events & BIT(12)) &&
          !(fe->otf_events & 0xfffff) && !READ_ONCE(video->stop_requested);
  dma_rmb();
  buffer->vb.sequence = fe->copy_counter - video->first_counter;
  buffer->vb.field = V4L2_FIELD_NONE;
  buffer->vb.vb2_buf.timestamp = fe->copy_timestamp;
  vb2_set_plane_payload(vb, 0, valid ? CAMERA_FRAME_BYTES : 0);
  if (valid)
   fe->copied_frames++;
  else
   fe->rejected_frames++;
  if (fe->completed_frames != before) {
   struct v4l2_event event = { .type = CAMERA_EVENT_RESULT };
   /* Versioned last-successfully-written controls, not sensor latch metadata.
    * Standard controls remain startup defaults. Publish after DMA is idle.
    * Words14/15 carry the same monotonic timestamp as the returned buffer. */
   u32 data[16] = { 1, video->stream_id, buffer->vb.sequence,
    fe->copy_counter, video->written_exposure, video->written_gain,
    video->written_frame_length, video->focus, video->test_pattern,
    fe->otf_events, fe->dma_events, fe->dma_error,
    valid ? 0 : (u32)-EIO, BIT(0) | BIT(1) | (valid ? BIT(2) : 0),
    lower_32_bits(fe->copy_timestamp), upper_32_bits(fe->copy_timestamp) };

   memcpy(event.u.data, data, sizeof(data));
   v4l2_event_queue(&video->vdev, &event);
   /* Last complete write generation, not proof this image latched it. */
   event.type = J7CAM_EVENT_GENERATION;
   data[0] = 1;
   data[1] = video->stream_id;
   data[2] = video->written_generation;
   data[3] = buffer->vb.sequence;
   data[4] = fe->copy_counter;
   data[5] = video->written_exposure;
   data[6] = video->written_gain;
   data[7] = video->written_frame_length;
   data[8] = 0;
   memcpy(event.u.data, data, sizeof(data));
   v4l2_event_queue(&video->vdev, &event);
  }
  /* No slot can write now. VB2 owns the allocation after buffer_done. */
  dma_buf_put(video->active_pin);
  video->active_pin = NULL;
  video->active = NULL;
  vb2_buffer_done(vb, valid ? VB2_BUF_STATE_DONE : VB2_BUF_STATE_ERROR);
  if (fe->dma_error || (fe->dma_events & BIT(12)) ||
      (fe->otf_events & 0xfffff))
   return -EIO;
 }
 return 0;
}

static int camera_video_worker(void *arg)
{
 struct camera_video *video = arg;
 struct exynos7870_camera_fe *fe = video->fe;
 int ret;

 if (!mutex_trylock(&fe->capture_lock)) {
  ret = -EBUSY;
 } else {
  fe->target_frames = U32_MAX;
  ret = camera_fe_capture(video->dev, video->test_pattern);
  fe->last_result = ret;
  if (!fe->poisoned && fe->frame_cpu) {
   dma_free_coherent(video->dev, CAMERA_ALLOC_BYTES, fe->frame_cpu, fe->frame_dma);
   fe->frame_cpu = NULL;
   fe->frame_valid = false;
  }
  mutex_unlock(&fe->capture_lock);
 }
 if (!completion_done(&video->start_done)) {
  video->start_result = ret;
  complete(&video->start_done);
 } else if (ret) {
  vb2_queue_error(&video->queue);
 }
 camera_video_cancel_update(video);
 /* Keep the task alive until streamoff joins it, also after a fatal error. */
 while (!kthread_should_stop())
  schedule_timeout_interruptible(HZ);
 return ret;
}

static int camera_video_queue_setup(struct vb2_queue *q, unsigned int *count,
                                   unsigned int *planes, unsigned int sizes[],
                                   struct device *alloc_devs[])
{
 if (*planes)
  return *planes == 1 && sizes[0] >= CAMERA_FRAME_BYTES ? 0 : -EINVAL;
 *planes = 1;
 sizes[0] = CAMERA_FRAME_BYTES;
 return 0;
}

static int camera_video_buffer_prepare(struct vb2_buffer *vb)
{
 if (vb2_plane_size(vb, 0) < CAMERA_FRAME_BYTES)
  return -EINVAL;
 if (upper_32_bits(vb2_dma_contig_plane_dma_addr(vb, 0) + CAMERA_FRAME_BYTES - 1))
  return -ERANGE;
 vb2_set_plane_payload(vb, 0, CAMERA_FRAME_BYTES);
 return 0;
}

static void camera_video_buffer_queue(struct vb2_buffer *vb)
{
 struct camera_video *video = vb2_get_drv_priv(vb->vb2_queue);
 struct camera_video_buffer *buffer = container_of(to_vb2_v4l2_buffer(vb),
                                                  struct camera_video_buffer, vb);
 unsigned long flags;
 spin_lock_irqsave(&video->queued_lock, flags);
 list_add_tail(&buffer->list, &video->queued);
 spin_unlock_irqrestore(&video->queued_lock, flags);
 wake_up(&video->buffer_wait);
}

static void camera_video_stop_worker(struct camera_video *video)
{
 if (video->worker) {
  WRITE_ONCE(video->stop_requested, true);
  wake_up(&video->buffer_wait);
  complete(&video->fe->frame_done);
  kthread_stop(video->worker);
  video->worker = NULL;
 }
 WRITE_ONCE(video->running, false);
}

static int camera_video_start(struct vb2_queue *q, unsigned int count)
{
 struct camera_video *video = vb2_get_drv_priv(q);
 int ret;

 if (video->fe->poisoned) {
  ret = -EIO;
  goto return_buffers;
 }
 reinit_completion(&video->start_done);
 WRITE_ONCE(video->stop_requested, false);
 WRITE_ONCE(video->running, true);
 video->stream_id++;
 video->next_generation = 0;
 video->written_generation = 0;
 video->written_exposure = video->exposure;
 video->written_gain = video->analogue_gain;
 video->written_frame_length = video->frame_length;
 video->timeout_frame_length = video->frame_length;
 video->update_pending = false;
 video->update_busy = false;
 video->update_accepting = true;
 video->worker = kthread_run(camera_video_worker, video, "exynos7870-capture");
 if (IS_ERR(video->worker)) {
  ret = PTR_ERR(video->worker);
  video->worker = NULL;
  WRITE_ONCE(video->running, false);
  goto return_buffers;
 }
 if (!wait_for_completion_timeout(&video->start_done, msecs_to_jiffies(5000)))
  ret = -ETIMEDOUT;
 else
  ret = video->start_result;
 if (!ret)
  return 0;
 camera_video_stop_worker(video);
return_buffers:
 camera_video_return_buffers(video, VB2_BUF_STATE_QUEUED);
 return ret;
}

static void camera_video_stop(struct vb2_queue *q)
{
 struct camera_video *video = vb2_get_drv_priv(q);
 camera_video_stop_worker(video);
 camera_video_return_buffers(video, VB2_BUF_STATE_ERROR);
}

static const struct vb2_ops camera_video_queue_ops = {
 .queue_setup = camera_video_queue_setup,
 .buf_prepare = camera_video_buffer_prepare,
 .buf_queue = camera_video_buffer_queue,
 .start_streaming = camera_video_start,
 .stop_streaming = camera_video_stop,
};

static void camera_video_format(struct v4l2_pix_format *pix)
{
 memset(pix, 0, sizeof(*pix));
 pix->width = 4144;
 pix->height = 3106;
 pix->pixelformat = V4L2_PIX_FMT_SRGGB10;
 pix->field = V4L2_FIELD_NONE;
 pix->bytesperline = 4144 * 2;
 pix->sizeimage = CAMERA_FRAME_BYTES;
 pix->colorspace = V4L2_COLORSPACE_RAW;
 pix->quantization = V4L2_QUANTIZATION_FULL_RANGE;
 pix->xfer_func = V4L2_XFER_FUNC_NONE;
}

static int camera_video_querycap(struct file *file, void *priv,
                                 struct v4l2_capability *cap)
{
 strscpy(cap->driver, "exynos7870-csis", sizeof(cap->driver));
 strscpy(cap->card, "Exynos7870 IMX258 RAW", sizeof(cap->card));
 strscpy(cap->bus_info, "platform:exynos7870-camera", sizeof(cap->bus_info));
 return 0;
}

static int camera_video_enum_fmt(struct file *file, void *priv,
                                struct v4l2_fmtdesc *fmt)
{
 if (fmt->index)
  return -EINVAL;
 fmt->pixelformat = V4L2_PIX_FMT_SRGGB10;
 return 0;
}

static int camera_video_get_fmt(struct file *file, void *priv, struct v4l2_format *fmt)
{
 camera_video_format(&fmt->fmt.pix);
 return 0;
}

static int camera_video_set_fmt(struct file *file, void *priv, struct v4l2_format *fmt)
{
 struct camera_video *video = video_drvdata(file);
 if (vb2_is_busy(&video->queue))
  return -EBUSY;
 return camera_video_get_fmt(file, priv, fmt);
}

static int camera_video_enum_size(struct file *file, void *priv,
                                 struct v4l2_frmsizeenum *size)
{
 if (size->index || size->pixel_format != V4L2_PIX_FMT_SRGGB10)
  return -EINVAL;
 size->type = V4L2_FRMSIZE_TYPE_DISCRETE;
 size->discrete.width = 4144;
 size->discrete.height = 3106;
 return 0;
}

static int camera_video_enum_input(struct file *file, void *priv, struct v4l2_input *input)
{
 if (input->index)
  return -EINVAL;
 strscpy(input->name, "IMX258", sizeof(input->name));
 input->type = V4L2_INPUT_TYPE_CAMERA;
 return 0;
}

static int camera_video_get_input(struct file *file, void *priv, unsigned int *input)
{
 *input = 0;
 return 0;
}

static int camera_video_set_input(struct file *file, void *priv, unsigned int input)
{
 return input ? -EINVAL : 0;
}

static int camera_video_control(struct v4l2_ctrl *ctrl)
{
 struct camera_video *video = container_of(ctrl->handler, struct camera_video, controls);
 unsigned int frame_length;
 int ret;
 if (READ_ONCE(video->running))
  return -EBUSY;
 switch (ctrl->id) {
 case V4L2_CID_VBLANK:
  frame_length = J7CAM_HEIGHT + ctrl->val;
  /* Do not silently replace the requested stock exposure when shortening
   * a frame. Userspace must lower exposure first, then lower VBLANK. */
  if (video->exposure > frame_length - J7CAM_EXPOSURE_MARGIN)
   return -ERANGE;
  ret = __v4l2_ctrl_modify_range(video->exposure_ctrl, J7CAM_EXPOSURE_MIN,
          frame_length - J7CAM_EXPOSURE_MARGIN, 1, J7CAM_EXPOSURE_DEFAULT);
  if (ret)
   return ret;
  video->frame_length = frame_length;
  break;
 case V4L2_CID_TEST_PATTERN:
  video->test_pattern = ctrl->val != 0;
  break;
 case V4L2_CID_EXPOSURE:
  video->exposure = ctrl->val;
  break;
 case V4L2_CID_ANALOGUE_GAIN:
  video->analogue_gain = ctrl->val;
  break;
 case V4L2_CID_FOCUS_ABSOLUTE:
  video->focus = ctrl->val;
  break;
 default:
  return -EINVAL;
 }
 return 0;
}

static const struct v4l2_ctrl_ops camera_video_control_ops = {
 .s_ctrl = camera_video_control,
};
static const char * const camera_video_patterns[] = { "Disabled", "Colour bars" };

static long camera_video_private(struct file *file, void *priv, bool valid_prio,
                                 unsigned int cmd, void *arg)
{
 struct camera_video *video = video_drvdata(file);
 struct j7cam_update_request *request = arg;
 unsigned long flags;
 int ret = 0;

 if (cmd != J7CAM_IOC_UPDATE)
  return -ENOTTY;
 if (video->queue.owner != file_to_v4l2_fh(file))
  return -EBUSY;
 if (!valid_prio)
  return -EBUSY;
 if (request->version != 1 || request->stream_id || request->generation ||
     request->reserved[0] || request->reserved[1])
  return -EINVAL;
 if (request->frame_length < J7CAM_FRAME_LENGTH_DEFAULT ||
     request->frame_length > J7CAM_FRAME_LENGTH_MAX ||
     request->exposure < J7CAM_EXPOSURE_MIN ||
     request->exposure > request->frame_length - J7CAM_EXPOSURE_MARGIN ||
     request->analogue_gain > J7CAM_ANALOGUE_GAIN_MAX)
  return -ERANGE;
 spin_lock_irqsave(&video->queued_lock, flags);
 if (!video->running || video->stop_requested || !video->update_accepting)
  ret = -EPIPE;
 else if (video->update_busy)
  ret = -EBUSY;
 else if (video->next_generation == U32_MAX)
  ret = -EOVERFLOW;
 else {
  request->stream_id = video->stream_id;
  request->generation = ++video->next_generation;
  video->pending_update = *request;
  video->update_busy = true;
  video->update_pending = true;
 }
 spin_unlock_irqrestore(&video->queued_lock, flags);
 if (!ret)
  wake_up(&video->buffer_wait);
 return ret;
}

static int camera_video_subscribe(struct v4l2_fh *fh,
                                 const struct v4l2_event_subscription *sub)
{
 if (sub->type == CAMERA_EVENT_RESULT || sub->type == J7CAM_EVENT_UPDATE ||
     sub->type == J7CAM_EVENT_GENERATION || sub->type == J7CAM_EVENT_UPDATE_TIMING) {
  if (sub->id || sub->flags)
   return -EINVAL;
  return v4l2_event_subscribe(fh, sub, 32, NULL);
 }
 return v4l2_ctrl_subscribe_event(fh, sub);
}

static const struct v4l2_ioctl_ops camera_video_ioctl_ops = {
 .vidioc_querycap = camera_video_querycap,
 .vidioc_enum_fmt_vid_cap = camera_video_enum_fmt,
 .vidioc_g_fmt_vid_cap = camera_video_get_fmt,
 .vidioc_try_fmt_vid_cap = camera_video_get_fmt,
 .vidioc_s_fmt_vid_cap = camera_video_set_fmt,
 .vidioc_enum_framesizes = camera_video_enum_size,
 .vidioc_enum_input = camera_video_enum_input,
 .vidioc_g_input = camera_video_get_input,
 .vidioc_s_input = camera_video_set_input,
 .vidioc_reqbufs = vb2_ioctl_reqbufs,
 .vidioc_create_bufs = vb2_ioctl_create_bufs,
 .vidioc_prepare_buf = vb2_ioctl_prepare_buf,
 .vidioc_querybuf = vb2_ioctl_querybuf,
 .vidioc_qbuf = vb2_ioctl_qbuf,
 .vidioc_dqbuf = vb2_ioctl_dqbuf,
 .vidioc_expbuf = vb2_ioctl_expbuf,
 .vidioc_streamon = vb2_ioctl_streamon,
 .vidioc_streamoff = vb2_ioctl_streamoff,
 .vidioc_subscribe_event = camera_video_subscribe,
 .vidioc_unsubscribe_event = v4l2_event_unsubscribe,
 .vidioc_default = camera_video_private,
};
static const struct v4l2_file_operations camera_video_fops = {
 .owner = THIS_MODULE,
 .open = v4l2_fh_open,
 .release = vb2_fop_release,
 .read = vb2_fop_read,
 .poll = vb2_fop_poll,
 .mmap = vb2_fop_mmap,
 .unlocked_ioctl = video_ioctl2,
};

static void camera_video_release(struct video_device *vdev)
{
 struct camera_video *video = container_of(vdev, struct camera_video, vdev);
 v4l2_ctrl_handler_free(&video->controls);
 v4l2_device_unregister(&video->v4l2_dev);
 kfree(video);
}

static int camera_video_register(struct device *dev, struct exynos7870_camera_fe *fe)
{
 struct camera_video *video;
 struct vb2_queue *q;
 int ret;

 video = kzalloc(sizeof(*video), GFP_KERNEL);
 if (!video)
  return -ENOMEM;
 video->dev = dev;
 video->fe = fe;
 mutex_init(&video->lock);
 spin_lock_init(&video->queued_lock);
 INIT_LIST_HEAD(&video->queued);
 init_completion(&video->start_done);
 init_waitqueue_head(&video->buffer_wait);
 ret = v4l2_device_register(dev, &video->v4l2_dev);
 if (ret) {
  kfree(video);
  return ret;
 }
 video->exposure = J7CAM_EXPOSURE_DEFAULT;
 video->frame_length = J7CAM_FRAME_LENGTH_DEFAULT;
 video->analogue_gain = 0;
 video->focus = J7CAM_FOCUS_DEFAULT;
 v4l2_ctrl_handler_init(&video->controls, 5);
 v4l2_ctrl_new_std_menu_items(&video->controls, &camera_video_control_ops,
                            V4L2_CID_TEST_PATTERN, 1, 0, 0, camera_video_patterns);
 video->exposure_ctrl = v4l2_ctrl_new_std(&video->controls, &camera_video_control_ops,
                   V4L2_CID_EXPOSURE, J7CAM_EXPOSURE_MIN,
                   J7CAM_EXPOSURE_MAX, 1, J7CAM_EXPOSURE_DEFAULT);
 v4l2_ctrl_new_std(&video->controls, &camera_video_control_ops,
                   V4L2_CID_VBLANK, J7CAM_FRAME_LENGTH_DEFAULT - J7CAM_HEIGHT,
                   J7CAM_FRAME_LENGTH_MAX - J7CAM_HEIGHT, 1,
                   J7CAM_FRAME_LENGTH_DEFAULT - J7CAM_HEIGHT);
 v4l2_ctrl_new_std(&video->controls, &camera_video_control_ops,
                   V4L2_CID_ANALOGUE_GAIN, 0, J7CAM_ANALOGUE_GAIN_MAX, 1, 0);
 v4l2_ctrl_new_std(&video->controls, &camera_video_control_ops,
                   V4L2_CID_FOCUS_ABSOLUTE, 0, J7CAM_FOCUS_MAX, 1, J7CAM_FOCUS_DEFAULT);
 ret = video->controls.error;
 if (ret)
  goto release;
 q = &video->queue;
 q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
 q->io_modes = VB2_MMAP | VB2_READ;
 q->drv_priv = video;
 q->buf_struct_size = sizeof(struct camera_video_buffer);
 q->ops = &camera_video_queue_ops;
 q->mem_ops = &vb2_dma_contig_memops;
 q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
 q->lock = &video->lock;
 q->dev = dev;
 q->min_queued_buffers = 2;
 ret = vb2_queue_init(q);
 if (ret)
  goto release;
 strscpy(video->vdev.name, "Exynos7870 IMX258 RAW", sizeof(video->vdev.name));
 video->vdev.v4l2_dev = &video->v4l2_dev;
 video->vdev.fops = &camera_video_fops;
 video->vdev.ioctl_ops = &camera_video_ioctl_ops;
 video->vdev.lock = &video->lock;
 video->vdev.queue = q;
 video->vdev.ctrl_handler = &video->controls;
 video->vdev.release = camera_video_release;
 video->vdev.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
 video->vdev.vfl_dir = VFL_DIR_RX;
 video_set_drvdata(&video->vdev, video);
 fe->video = video;
 ret = video_register_device(&video->vdev, VFL_TYPE_VIDEO, -1);
 if (ret) {
  fe->video = NULL;
  vb2_queue_release(q);
  goto release;
 }
 dev_info(dev, "V4L2 RAW capture registered as %s\n", video_device_node_name(&video->vdev));
 return 0;
release:
 camera_video_release(&video->vdev);
 return ret;
}
static ssize_t capture_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct exynos7870_camera_fe *fe = dev_get_drvdata(dev);
	int ret;

	unsigned int frames, pattern = 1;
	char extra;
	int parsed;

	parsed = sscanf(buf, "%u %u %c", &frames, &pattern, &extra);
	if (parsed < 1 || parsed > 2 || !frames || frames > 300 || pattern > 1)
		return -EINVAL;
	if (!mutex_trylock(&fe->capture_lock))
		return -EBUSY;
	fe->target_frames = frames;
	ret = camera_fe_capture(dev, pattern);
	fe->last_result = ret;
	mutex_unlock(&fe->capture_lock);
	return ret ?: count;
}
static DEVICE_ATTR_WO(capture);

static ssize_t capture_status_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct exynos7870_camera_fe *fe = dev_get_drvdata(dev);
	ssize_t ret;

	if (!mutex_trylock(&fe->capture_lock))
		return -EBUSY;
	ret = sysfs_emit(buf,
		"result=%d valid=%u poisoned=%u otf=%08x dma=%08x error=%08x bytes=%u active=%08x frames=%u target=%u duration_ns=%llu delivered=%u rejected=%u empty=%u max_copy_ns=%llu slots=%u,%u,%u,%u,%u,%u,%u,%u\n",
		fe->last_result, fe->frame_valid, fe->poisoned, fe->otf_events,
		fe->dma_events, fe->dma_error, fe->byte_count, fe->active_ctrl,
		fe->completed_frames, fe->target_frames, fe->last_fe_ns - fe->first_fe_ns,
		fe->copied_frames, fe->rejected_frames, fe->empty_frames, fe->max_copy_ns,
		fe->slot_hits[0], fe->slot_hits[1], fe->slot_hits[2], fe->slot_hits[3],
		fe->slot_hits[4], fe->slot_hits[5], fe->slot_hits[6], fe->slot_hits[7]);
	mutex_unlock(&fe->capture_lock);
	return ret;
}
static DEVICE_ATTR_RO(capture_status);

static ssize_t frame_read(struct file *file, struct kobject *kobj,
			  const struct bin_attribute *attr, char *buf,
			  loff_t off, size_t count)
{
	struct exynos7870_camera_fe *fe = dev_get_drvdata(kobj_to_dev(kobj));
	ssize_t ret = -ENODATA;

	if (!mutex_trylock(&fe->capture_lock))
		return -EBUSY;
	if (fe->frame_valid) {
		memcpy(buf, fe->frame_cpu + off, count);
		ret = count;
	}
	mutex_unlock(&fe->capture_lock);
	return ret;
}
static const struct bin_attribute camera_frame_attr = {
	.attr = { .name = "frame", .mode = 0400 },
	.size = CAMERA_FRAME_BYTES,
	.read = frame_read,
};
static const struct bin_attribute camera_frames_attr = {
	.attr = { .name = "frames", .mode = 0400 },
	.size = CAMERA_ALLOC_BYTES,
	.read = frame_read,
};
static const struct bin_attribute *camera_fe_bin_attrs[] = {
	&camera_frame_attr, &camera_frames_attr, NULL,
};

static struct attribute *camera_fe_attrs[] = {
	&dev_attr_versions.attr, &dev_attr_dma_test.attr,
	&dev_attr_capture.attr, &dev_attr_capture_status.attr, NULL,
};
static const struct attribute_group camera_fe_group = {
	.attrs = camera_fe_attrs,
	.bin_attrs = camera_fe_bin_attrs,
};
__ATTRIBUTE_GROUPS(camera_fe);

static int camera_fe_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct exynos7870_camera_fe *fe;
	u32 rear, front;
	int i, ret;

	fe = devm_kzalloc(dev, sizeof(*fe), GFP_KERNEL);
	if (!fe)
		return -ENOMEM;
	platform_set_drvdata(pdev, fe);
	mutex_init(&fe->capture_lock);
	init_completion(&fe->frame_done);
	fe->last_result = -ENODATA;
	fe->phy = devm_phy_get(dev, "csis0");
	if (IS_ERR(fe->phy))
		return dev_err_probe(dev, PTR_ERR(fe->phy), "CSIS0 PHY\n");
	fe->irq = platform_get_irq(pdev, 0);
	if (fe->irq < 0)
		return fe->irq;
	for (i = 0; i < ARRAY_SIZE(fe->csis); i++) {
		fe->csis[i] = devm_platform_ioremap_resource(pdev, i);
		if (IS_ERR(fe->csis[i]))
			return PTR_ERR(fe->csis[i]);
	}
	fe->num_clocks = devm_clk_bulk_get_all(dev, &fe->clocks);
	if (fe->num_clocks < 0)
		return dev_err_probe(dev, fe->num_clocks, "front-end clocks\n");
	if (!fe->num_clocks)
		return -EINVAL;
	ret = devm_request_irq(dev, fe->irq, camera_fe_irq,
				IRQF_NO_AUTOEN,
			       dev_name(dev), fe);
	if (ret)
		return ret;
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;
	pm_runtime_set_suspended(dev);
	pm_runtime_enable(dev);
	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		goto disable_pm;
	rear = readl(fe->csis[0]);
	front = readl(fe->csis[1]);
	dev_info(dev, "CSIS0=0x%08x CSIS1=0x%08x; no DMA started\n", rear, front);
	pm_runtime_put_sync(dev);
	ret = camera_video_register(dev, fe);
	if (ret)
		goto disable_pm;
	return 0;
disable_pm:
	pm_runtime_disable(dev);
	return ret;
}

static void camera_fe_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct exynos7870_camera_fe *fe = dev_get_drvdata(dev);

	if (fe->video) {
		vb2_video_unregister_device(&fe->video->vdev);
		fe->video = NULL;
	}
	if (fe->poisoned)
		return;
	if (fe->frame_cpu)
		dma_free_coherent(dev, CAMERA_ALLOC_BYTES, fe->frame_cpu, fe->frame_dma);
	pm_runtime_disable(dev);
	if (!pm_runtime_status_suspended(dev))
		camera_fe_suspend(dev);
	pm_runtime_set_suspended(dev);
}

static int camera_fe_system_suspend(struct device *dev)
{
	struct exynos7870_camera_fe *fe = dev_get_drvdata(dev);
	int ret;

	if (!mutex_trylock(&fe->capture_lock))
		return -EBUSY;
	ret = fe->poisoned ? -EBUSY : pm_runtime_force_suspend(dev);
	mutex_unlock(&fe->capture_lock);
	return ret;
}

static const struct dev_pm_ops camera_fe_pm_ops = {
	RUNTIME_PM_OPS(camera_fe_suspend, camera_fe_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(camera_fe_system_suspend, pm_runtime_force_resume)
};

static const struct of_device_id camera_fe_match[] = {
	{ .compatible = "samsung,exynos7870-camera-fe" },
	{ }
};
MODULE_DEVICE_TABLE(of, camera_fe_match);

static struct platform_driver camera_fe_driver = {
	.probe = camera_fe_probe,
	.remove = camera_fe_remove,
	.driver = {
		.name = "exynos7870-camera-fe",
		.suppress_bind_attrs = true,
		.of_match_table = camera_fe_match,
		.pm = pm_ptr(&camera_fe_pm_ops),
		.dev_groups = camera_fe_groups,
	},
};
module_platform_driver(camera_fe_driver);
MODULE_DESCRIPTION("Exynos7870 camera front-end power qualification");
MODULE_LICENSE("GPL");

MODULE_IMPORT_NS("DMA_BUF");
