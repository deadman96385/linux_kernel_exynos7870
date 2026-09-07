/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef J7Y17LTE_CAMERA_UPDATE_H
#define J7Y17LTE_CAMERA_UPDATE_H
#include <linux/types.h>
#include <linux/videodev2.h>
/* One outstanding update. Input version=1, output IDs/reserved must be zero.
 * Acceptance does not mean writes succeeded. Subscribe before submission.
 */
struct j7cam_update_request {
	__u32 version, exposure, analogue_gain, frame_length;
	__u32 stream_id, generation, reserved[2];
};
#define J7CAM_IOC_UPDATE _IOWR('V', BASE_VIDIOC_PRIVATE, struct j7cam_update_request)
#define J7CAM_EVENT_UPDATE (V4L2_EVENT_PRIVATE_START + 2)
#define J7CAM_EVENT_GENERATION (V4L2_EVENT_PRIVATE_START + 3)
/* Receiver frame counters bracket the update call; not sensor latch proof. */
#define J7CAM_EVENT_UPDATE_TIMING (V4L2_EVENT_PRIVATE_START + 4)
#endif
