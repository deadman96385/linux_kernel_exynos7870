/* Experimental single-job V4L2 adapter. Queue memory is CPU-only; hardware
 * uses the shared owner's private coherent buffers in the processing domain. */
/* Diagnostic ABI v1: one event for each pair processed by the worker.
 * Streamoff may discard pending pairs without events. The subscription holds
 * the most recent 16 events; framework event.sequence exposes event loss. */
#define ISP_VIDEO_EVENT_RESULT V4L2_EVENT_PRIVATE_START
/* Private diagnostic statistics ABI, fixed across 32/64-bit userspace.
 * GET requires the exact completed sequence; a newer job returns ESTALE.
 * Successful copy does not imply successful processing: inspect result/flags.
 */
struct isp_video_statistics {
	__u32 version, sequence, bytes, flags;
	__s32 result;
	__u32 reserved;
	__aligned_u64 data;
	__aligned_u64 input_timestamp_ns;
};
static_assert(sizeof(struct isp_video_statistics) == 40);
#define ISP_VIDEO_STATS_ENABLE _IOW('V', BASE_VIDIOC_PRIVATE, __u32)
#define ISP_VIDEO_STATS_GET _IOWR('V', BASE_VIDIOC_PRIVATE + 1, struct isp_video_statistics)
struct isp_video_report {
	u32 version, sequence;
	s32 result;
	u32 flags; /* bit 0 job submitted, bit 1 engine quiescent, bit 2 aborted */
	u32 status[8]; /* Same register/status order as misc I/2. */
	u32 payload, reserved;
	u64 input_timestamp_ns;
};
static_assert(sizeof(struct isp_video_report) == 64);

struct isp_video_ctx {
	struct v4l2_fh fh;
	struct exynos7870_isp *isp;
	struct isp_frame *frame;
	struct mutex queue_lock;
	struct work_struct work;
	struct v4l2_ctrl_handler controls;
	struct v4l2_ctrl *red, *blue;
	/* Application-owned input description, propagated without CSC. */
	u32 colorspace, xfer_func, ycbcr_enc, quantization;
	u32 sequence;
	bool aborting;
	/* Protected by frame->lock; snapshot belongs to this context only. */
	bool statistics_enabled, statistics_valid;
	void *statistics;
	struct isp_video_statistics statistics_report;
};

static size_t isp_video_size(enum v4l2_buf_type type)
{
	return V4L2_TYPE_IS_OUTPUT(type) ? ISP_INPUT_BYTES : 4128U * 3090U * 3U;
}

static void isp_video_work(struct work_struct *work)
{
	struct isp_video_ctx *ctx = container_of(work, struct isp_video_ctx, work);
	struct isp_frame *f = ctx->frame;
	struct vb2_v4l2_buffer *src, *dst;
	enum vb2_buffer_state state = VB2_BUF_STATE_ERROR;
	int ret = -ECANCELED;
	struct v4l2_event event = { .type = ISP_VIDEO_EVENT_RESULT };
	struct isp_video_report report = { .version = 1 };

	src = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	if (!src || !dst)
		goto finish;
	vb2_set_plane_payload(&dst->vb2_buf, 0, 0);
	mutex_lock(&f->lock);
	ctx->statistics_valid = false;
	if (!f->quiescent) {
		ret = -EIO;
		goto unlock;
	}
	if (READ_ONCE(ctx->aborting))
		goto unlock;
	if (f->submitted && isp_frame_rearm(f))
		goto unlock;
	/* Snapshot both controls together at job start, before hardware setup.
	 * They belong to this context; no gains carry over from another client. */
	mutex_lock(ctx->controls.lock);
	f->wbg_mode = 8;
	f->wbg_gains[0] = ctx->red->cur.val;
	f->wbg_gains[1] = 1024;
	f->wbg_gains[2] = ctx->blue->cur.val;
	mutex_unlock(ctx->controls.lock);
	f->statistics_enabled = ctx->statistics_enabled;
	memcpy(f->input, vb2_plane_vaddr(&src->vb2_buf, 0), ISP_INPUT_BYTES);
	f->uploaded = ISP_INPUT_BYTES;
	ret = isp_frame_run(f);
	if (f->submitted)
		report.flags |= BIT(0);
	memcpy(report.status, f->status, sizeof(report.status));
	if (f->submitted && f->quiescent) {
		memcpy(vb2_plane_vaddr(&dst->vb2_buf, 0), f->output, f->layout[3]);
		vb2_set_plane_payload(&dst->vb2_buf, 0, f->layout[3]);
	}

unlock:
	if (f->quiescent)
		report.flags |= BIT(1);
	if (READ_ONCE(ctx->aborting))
		report.flags |= BIT(2);
	report.result = ret;
	if (!ret && (report.flags & BIT(2)))
		report.result = -ECANCELED;
	if (!report.result)
		state = VB2_BUF_STATE_DONE;
	if (ctx->statistics_enabled && (report.flags & BIT(0)) && f->quiescent) {
		memcpy(ctx->statistics, f->statistics, ISP_STATS_BYTES);
		ctx->statistics_report = (struct isp_video_statistics) {
			.version = 1, .sequence = ctx->sequence,
			.bytes = ISP_STATS_BYTES, .flags = report.flags,
			.result = report.result,
			.input_timestamp_ns = src->vb2_buf.timestamp,
		};
		ctx->statistics_valid = true;
	}
	mutex_unlock(&f->lock);
	v4l2_m2m_buf_copy_metadata(src, dst);
	src->sequence = ctx->sequence;
	dst->sequence = ctx->sequence++;
	dst->field = V4L2_FIELD_NONE;
	report.sequence = dst->sequence;
	report.payload = vb2_get_plane_payload(&dst->vb2_buf, 0);
	report.input_timestamp_ns = src->vb2_buf.timestamp;
	memcpy(event.u.data, &report, sizeof(report));
	v4l2_event_queue_fh(&ctx->fh, &event);
	src = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	dst = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	v4l2_m2m_buf_done(src, state);
	v4l2_m2m_buf_done(dst, state);
	if (!f->quiescent) {
		/* Only private ISP buffers remain DMA-owned. Queue buffers have
		 * never been mapped for hardware and can safely return ERROR. */
		vb2_queue_error(v4l2_m2m_get_src_vq(ctx->fh.m2m_ctx));
		vb2_queue_error(v4l2_m2m_get_dst_vq(ctx->fh.m2m_ctx));
	}
finish:
	v4l2_m2m_job_finish(ctx->isp->m2m, ctx->fh.m2m_ctx);
}

static void isp_video_run(void *priv)
{
	struct isp_video_ctx *ctx = priv;
	schedule_work(&ctx->work);
}

static int isp_video_ready(void *priv)
{
	struct isp_video_ctx *ctx = priv;
	/* Let a poisoned engine return ERROR to each context instead of leaving
	 * its queued buffers waiting forever behind job_ready. No DMA is run. */
	return !READ_ONCE(ctx->aborting);
}

static void isp_video_abort(void *priv)
{
	struct isp_video_ctx *ctx = priv;
	/* The bounded worker completes/reset-stops before returning its pair. */
	WRITE_ONCE(ctx->aborting, true);
}

static const struct v4l2_m2m_ops isp_video_m2m_ops = {
	.device_run = isp_video_run, .job_ready = isp_video_ready,
	.job_abort = isp_video_abort,
};

static int isp_video_queue_setup(struct vb2_queue *q, unsigned int *buffers,
		unsigned int *planes, unsigned int sizes[], struct device *alloc_devs[])
{
	if (*planes)
		return *planes == 1 && sizes[0] >= isp_video_size(q->type) ? 0 : -EINVAL;
	*buffers = min_t(unsigned int, *buffers, 4);
	*planes = 1;
	sizes[0] = isp_video_size(q->type);
	return 0;
}

static int isp_video_prepare(struct vb2_buffer *vb)
{
	size_t size = isp_video_size(vb->type);

	if (vb2_plane_size(vb, 0) < size || !vb2_plane_vaddr(vb, 0))
		return -EINVAL;
	if (V4L2_TYPE_IS_OUTPUT(vb->type)) {
		if (vb2_get_plane_payload(vb, 0) != size)
			return -EINVAL;
	} else {
		vb2_set_plane_payload(vb, 0, 0);
	}
	return 0;
}

static int isp_video_validate_output(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *b = to_vb2_v4l2_buffer(vb);

	if (b->field == V4L2_FIELD_ANY)
		b->field = V4L2_FIELD_NONE;
	return b->field == V4L2_FIELD_NONE ? 0 : -EINVAL;
}

static void isp_video_queue(struct vb2_buffer *vb)
{
	struct isp_video_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, to_vb2_v4l2_buffer(vb));
}

static int isp_video_start(struct vb2_queue *q, unsigned int count)
{
	struct isp_video_ctx *ctx = vb2_get_drv_priv(q);
	WRITE_ONCE(ctx->aborting, false);
	mutex_lock(&ctx->frame->lock);
	ctx->statistics_valid = false;
	if (!V4L2_TYPE_IS_OUTPUT(q->type))
		ctx->sequence = 0;
	mutex_unlock(&ctx->frame->lock);
	return 0;
}

static void isp_video_stop(struct vb2_queue *q)
{
	struct isp_video_ctx *ctx = vb2_get_drv_priv(q);
	struct vb2_v4l2_buffer *b;

	WRITE_ONCE(ctx->aborting, true);
	/* Do not cancel queued work: it must call job_finish for an active job. */
	flush_work(&ctx->work);
	for (;;) {
		b = V4L2_TYPE_IS_OUTPUT(q->type) ?
			v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx) :
			v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
		if (!b)
			break;
		vb2_set_plane_payload(&b->vb2_buf, 0, 0);
		v4l2_m2m_buf_done(b, VB2_BUF_STATE_ERROR);
	}
}

static const struct vb2_ops isp_video_queue_ops = {
	.queue_setup = isp_video_queue_setup, .buf_prepare = isp_video_prepare,
	.buf_out_validate = isp_video_validate_output,
	.buf_queue = isp_video_queue, .start_streaming = isp_video_start,
	.stop_streaming = isp_video_stop,
};

static int isp_video_queues(void *priv, struct vb2_queue *src, struct vb2_queue *dst)
{
	struct isp_video_ctx *ctx = priv;
	struct vb2_queue *queues[] = { src, dst };
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(queues); i++) {
		struct vb2_queue *q = queues[i];
		q->type = i ? V4L2_BUF_TYPE_VIDEO_CAPTURE : V4L2_BUF_TYPE_VIDEO_OUTPUT;
		q->io_modes = VB2_MMAP;
		/* Keep the core default index capacity (at least VB2_MAX_FRAME). */
		q->drv_priv = ctx;
		q->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
		q->ops = &isp_video_queue_ops;
		q->mem_ops = &vb2_vmalloc_memops;
		q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
		q->lock = &ctx->queue_lock;
		q->dev = ctx->frame->dev;
		ret = vb2_queue_init(q);
		if (ret)
			return ret;
	}
	return 0;
}

static int isp_video_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
	strscpy(cap->driver, "exynos7870-isp", sizeof(cap->driver));
	strscpy(cap->card, "Exynos7870 ISP diagnostic", sizeof(cap->card));
	strscpy(cap->bus_info, "platform:14400000.isp", sizeof(cap->bus_info));
	return 0;
}

static int isp_video_enum_fmt(struct file *file, void *priv, struct v4l2_fmtdesc *f)
{
	if (f->index)
		return -EINVAL;
	f->pixelformat = V4L2_TYPE_IS_OUTPUT(f->type) ? V4L2_PIX_FMT_SRGGB10 : V4L2_PIX_FMT_RGB24;
	return 0;
}

static int isp_video_enum_size(struct file *file, void *priv, struct v4l2_frmsizeenum *f)
{
	if (f->index || (f->pixel_format != V4L2_PIX_FMT_SRGGB10 &&
			f->pixel_format != V4L2_PIX_FMT_RGB24))
		return -EINVAL;
	f->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	f->discrete.width = f->pixel_format == V4L2_PIX_FMT_SRGGB10 ? 4144 : 4128;
	f->discrete.height = f->pixel_format == V4L2_PIX_FMT_SRGGB10 ? 3106 : 3090;
	return 0;
}

static int isp_video_format(struct file *file, void *priv, struct v4l2_format *fmt)
{
	struct isp_video_ctx *ctx = container_of(file_to_v4l2_fh(file), struct isp_video_ctx, fh);
	struct v4l2_pix_format *p = &fmt->fmt.pix;
	bool output = V4L2_TYPE_IS_OUTPUT(fmt->type);

	memset(p, 0, sizeof(*p));
	p->width = output ? 4144 : 4128;
	p->height = output ? 3106 : 3090;
	p->pixelformat = output ? V4L2_PIX_FMT_SRGGB10 : V4L2_PIX_FMT_RGB24;
	p->field = V4L2_FIELD_NONE;
	p->bytesperline = output ? 8288 : 12384;
	p->sizeimage = isp_video_size(fmt->type);
	p->colorspace = ctx->colorspace;
	p->xfer_func = ctx->xfer_func;
	p->ycbcr_enc = ctx->ycbcr_enc;
	p->quantization = ctx->quantization;
	return 0;
}

static int isp_video_try_format(struct file *file, void *priv, struct v4l2_format *fmt)
{
	struct v4l2_pix_format input = fmt->fmt.pix;
	struct v4l2_pix_format *p = &fmt->fmt.pix;

	isp_video_format(file, priv, fmt);
	if (!V4L2_TYPE_IS_OUTPUT(fmt->type))
		return 0;
	/* The ioctl core sanitizes invalid enums before this callback. Input
	 * colorimetry describes supplied samples; it is not a request to convert
	 * the fixed sensor pipeline. Capture TRY_FMT cannot request CSC. */
	p->colorspace = input.colorspace ?: V4L2_COLORSPACE_RAW;
	p->xfer_func = input.xfer_func ?: V4L2_MAP_XFER_FUNC_DEFAULT(p->colorspace);
	p->ycbcr_enc = input.ycbcr_enc;
	p->quantization = input.quantization ?: V4L2_QUANTIZATION_FULL_RANGE;
	return 0;
}

static int isp_video_set_format(struct file *file, void *priv, struct v4l2_format *fmt)
{
	/* Current V4L2 ioctl dispatch passes NULL for the legacy priv argument. */
	struct isp_video_ctx *ctx = container_of(file_to_v4l2_fh(file), struct isp_video_ctx, fh);
	struct vb2_queue *q = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, fmt->type);
	int ret = -EBUSY;

	/* Format ioctls hold video_lock; allocation ioctls hold queue_lock.
	 * Serialize the busy check with allocation on both linked queues. */
	mutex_lock(&ctx->queue_lock);
	if (!q || vb2_is_busy(q) ||
	    (V4L2_TYPE_IS_OUTPUT(fmt->type) &&
	     vb2_is_busy(v4l2_m2m_get_dst_vq(ctx->fh.m2m_ctx))))
		goto unlock;
	ret = isp_video_try_format(file, priv, fmt);
	if (!ret && V4L2_TYPE_IS_OUTPUT(fmt->type)) {
		ctx->colorspace = fmt->fmt.pix.colorspace;
		ctx->xfer_func = fmt->fmt.pix.xfer_func;
		ctx->ycbcr_enc = fmt->fmt.pix.ycbcr_enc;
		ctx->quantization = fmt->fmt.pix.quantization;
	}
unlock:
	mutex_unlock(&ctx->queue_lock);
	return ret;
}

static int isp_video_create_bufs(struct file *file, void *priv,
		struct v4l2_create_buffers *create)
{
	struct v4l2_fh *fh = file_to_v4l2_fh(file);
	struct vb2_queue *q;
	unsigned int allocated;
	int ret;

	if (create->format.type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
	    create->format.type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	q = v4l2_m2m_get_vq(fh->m2m_ctx, create->format.type);
	allocated = vb2_get_num_buffers(q);
	/* Limit actual memory, not vb2's minimum-sized index storage. Do this
	 * once before allocation so queue_setup retry after partial allocation
	 * does not count the newly allocated buffers twice. */
	if (create->count && allocated >= 4)
		return -ENOBUFS;
	create->count = min_t(unsigned int, create->count, 4 - allocated);
	ret = v4l2_m2m_ioctl_create_bufs(file, priv, create);
	create->max_num_buffers = 4;
	return ret;
}

static int isp_video_subscribe(struct v4l2_fh *fh,
		const struct v4l2_event_subscription *sub)
{
	if (sub->type == ISP_VIDEO_EVENT_RESULT) {
		if (sub->id)
			return -EINVAL;
		return v4l2_event_subscribe(fh, sub, 16, NULL);
	}
	return v4l2_ctrl_subscribe_event(fh, sub);
}

static long isp_video_default(struct file *file, void *priv, bool valid_prio,
			     unsigned int cmd, void *arg)
{
	/* This kernel's __video_do_ioctl passes NULL as the legacy priv arg. */
	struct isp_video_ctx *ctx = container_of(file_to_v4l2_fh(file),
					      struct isp_video_ctx, fh);
	struct isp_video_statistics *request = arg;
	int ret = 0;

	if (cmd != ISP_VIDEO_STATS_ENABLE && cmd != ISP_VIDEO_STATS_GET)
		return -ENOTTY;
	mutex_lock(&ctx->frame->lock);
	if (cmd == ISP_VIDEO_STATS_ENABLE) {
		u32 enable = *(u32 *)arg;

		if (enable > 1) {
			ret = -EINVAL;
			goto out;
		}
		if (vb2_is_streaming(v4l2_m2m_get_src_vq(ctx->fh.m2m_ctx)) ||
		    vb2_is_streaming(v4l2_m2m_get_dst_vq(ctx->fh.m2m_ctx))) {
			ret = -EBUSY;
			goto out;
		}
		if (enable && !ctx->statistics) {
			ctx->statistics = kvzalloc(ISP_STATS_BYTES, GFP_KERNEL);
			if (!ctx->statistics) {
				ret = -ENOMEM;
				goto out;
			}
		}
		ctx->statistics_enabled = enable;
		ctx->statistics_valid = false;
	} else {
		if (request->version != 1 || request->bytes != ISP_STATS_BYTES ||
		    request->flags || request->result || request->reserved ||
		    request->input_timestamp_ns || !request->data) {
			ret = -EINVAL;
			goto out;
		}
		if (!ctx->statistics_enabled || !ctx->statistics_valid) {
			ret = -ENODATA;
			goto out;
		}
		if (request->sequence != ctx->statistics_report.sequence) {
			ret = -ESTALE;
			goto out;
		}
		if (copy_to_user(u64_to_user_ptr(request->data), ctx->statistics,
				 ISP_STATS_BYTES)) {
			ret = -EFAULT;
			goto out;
		}
		{
			u64 data = request->data;

			*request = ctx->statistics_report;
			request->data = data;
		}
	}
out:
	mutex_unlock(&ctx->frame->lock);
	return ret;
}

static const struct v4l2_ioctl_ops isp_video_ioctls = {
	.vidioc_default = isp_video_default,
	.vidioc_querycap = isp_video_querycap,
	.vidioc_subscribe_event = isp_video_subscribe,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
	.vidioc_enum_fmt_vid_cap = isp_video_enum_fmt,
	.vidioc_enum_fmt_vid_out = isp_video_enum_fmt,
	.vidioc_enum_framesizes = isp_video_enum_size,
	.vidioc_g_fmt_vid_cap = isp_video_format, .vidioc_g_fmt_vid_out = isp_video_format,
	.vidioc_try_fmt_vid_cap = isp_video_try_format, .vidioc_try_fmt_vid_out = isp_video_try_format,
	.vidioc_s_fmt_vid_cap = isp_video_set_format, .vidioc_s_fmt_vid_out = isp_video_set_format,
	.vidioc_create_bufs = isp_video_create_bufs,
	.vidioc_remove_bufs = v4l2_m2m_ioctl_remove_bufs,
	.vidioc_reqbufs = v4l2_m2m_ioctl_reqbufs, .vidioc_querybuf = v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf = v4l2_m2m_ioctl_qbuf, .vidioc_dqbuf = v4l2_m2m_ioctl_dqbuf,
	.vidioc_streamon = v4l2_m2m_ioctl_streamon, .vidioc_streamoff = v4l2_m2m_ioctl_streamoff,
};

/* Caller holds video_lock. Every context owns one reference. If stopping
 * failed, isp_frame_put deliberately retains DMA memory, PM and the busy gate. */
static void isp_video_owner_put(struct exynos7870_isp *isp)
{
	if (!--isp->video_users) {
		isp_frame_put(isp->video_frame);
		isp->video_frame = NULL;
	}
}

static int isp_video_open(struct file *file)
{
	struct exynos7870_isp *isp = video_drvdata(file);
	struct isp_video_ctx *ctx;
	int ret;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->isp = isp;
	ctx->colorspace = V4L2_COLORSPACE_RAW;
	ctx->xfer_func = V4L2_XFER_FUNC_NONE;
	ctx->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	mutex_lock(&isp->video_lock);
	if (!isp->video_users) {
		struct isp_frame *f = isp_frame_acquire(isp);

		if (IS_ERR(f)) {
			ret = PTR_ERR(f);
			goto free;
		}
		f->crop_mode = 2;
		f->layout[0] = 4128;
		f->layout[1] = 3090;
		f->layout[2] = 12384;
		f->layout[3] = 4128U * 3090U * 3U;
		isp->video_frame = f;
	}
	isp->video_users++;
	ctx->frame = isp->video_frame;
	mutex_init(&ctx->queue_lock);
	INIT_WORK(&ctx->work, isp_video_work);
	v4l2_fh_init(&ctx->fh, &isp->video);
	v4l2_ctrl_handler_init(&ctx->controls, 2);
	ctx->red = v4l2_ctrl_new_std(&ctx->controls, NULL,
			V4L2_CID_RED_BALANCE, 1, 8191, 1, 1024);
	ctx->blue = v4l2_ctrl_new_std(&ctx->controls, NULL,
			V4L2_CID_BLUE_BALANCE, 1, 8191, 1, 1024);
	if (ctx->controls.error) {
		ret = ctx->controls.error;
		goto owner_put;
	}
	ctx->fh.ctrl_handler = &ctx->controls;
	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(isp->m2m, ctx, isp_video_queues);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		goto owner_put;
	}
	v4l2_fh_add(&ctx->fh, file);
	mutex_unlock(&isp->video_lock);
	return 0;
owner_put:
	v4l2_ctrl_handler_free(&ctx->controls);
	v4l2_fh_exit(&ctx->fh);
	isp_video_owner_put(isp);
free:
	mutex_unlock(&isp->video_lock);
	kfree(ctx);
	return ret;
}

static int isp_video_release(struct file *file)
{
	struct isp_video_ctx *ctx = container_of(file_to_v4l2_fh(file), struct isp_video_ctx, fh);
	struct exynos7870_isp *isp = ctx->isp;

	mutex_lock(&isp->video_lock);
	WRITE_ONCE(ctx->aborting, true);
	/* Joins the active job before the shared owner reference can be dropped.
	 * Workers never take video_lock, so cancellation can finish here. */
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	flush_work(&ctx->work);
	v4l2_fh_del(&ctx->fh, file);
	v4l2_fh_exit(&ctx->fh);
	v4l2_ctrl_handler_free(&ctx->controls);
	isp_video_owner_put(isp);
	mutex_unlock(&isp->video_lock);
	kvfree(ctx->statistics);
	kfree(ctx);
	return 0;
}

static const struct v4l2_file_operations isp_video_fops = {
	.owner = THIS_MODULE, .open = isp_video_open, .release = isp_video_release,
	.unlocked_ioctl = video_ioctl2, .poll = v4l2_m2m_fop_poll, .mmap = v4l2_m2m_fop_mmap,
};

static void isp_video_unregister(void *data)
{
	struct exynos7870_isp *isp = data;
	video_unregister_device(&isp->video);
	v4l2_m2m_release(isp->m2m);
	v4l2_device_unregister(&isp->v4l2);
}

static int isp_video_register(struct exynos7870_isp *isp)
{
	int ret = v4l2_device_register(isp->misc.parent, &isp->v4l2);

	if (ret)
		return ret;
	isp->m2m = v4l2_m2m_init(&isp_video_m2m_ops);
	if (IS_ERR(isp->m2m)) {
		ret = PTR_ERR(isp->m2m);
		goto unregister;
	}
	mutex_init(&isp->video_lock);
	strscpy(isp->video.name, "exynos7870-isp-m2m", sizeof(isp->video.name));
	isp->video.v4l2_dev = &isp->v4l2;
	isp->video.fops = &isp_video_fops;
	isp->video.ioctl_ops = &isp_video_ioctls;
	isp->video.release = video_device_release_empty;
	isp->video.lock = &isp->video_lock;
	isp->video.vfl_dir = VFL_DIR_M2M;
	isp->video.device_caps = V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING;
	video_set_drvdata(&isp->video, isp);
	ret = video_register_device(&isp->video, VFL_TYPE_VIDEO, -1);
	if (ret) {
		v4l2_m2m_release(isp->m2m);
		goto unregister;
	}
	return devm_add_action_or_reset(isp->misc.parent, isp_video_unregister, isp);
unregister:
	v4l2_device_unregister(&isp->v4l2);
	return ret;
}
