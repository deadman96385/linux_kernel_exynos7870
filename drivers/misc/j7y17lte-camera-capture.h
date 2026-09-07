/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef J7Y17LTE_CAMERA_CAPTURE_H
#define J7Y17LTE_CAMERA_CAPTURE_H
struct i2c_client;
/* Qualified vendor mode: 4144x3106, 5352 pixels/line at 561.6 MHz. */
#define J7CAM_HEIGHT 3106
#define J7CAM_LINE_LENGTH 5352
#define J7CAM_PIXEL_RATE 561600000ULL
#define J7CAM_FRAME_LENGTH_DEFAULT 0x0c74
#define J7CAM_FRAME_LENGTH_MAX 0xffff
#define J7CAM_EXPOSURE_MARGIN 10
#define J7CAM_EXPOSURE_MIN 4
#define J7CAM_EXPOSURE_MAX (J7CAM_FRAME_LENGTH_DEFAULT - J7CAM_EXPOSURE_MARGIN)
#define J7CAM_EXPOSURE_DEFAULT 0x0c46
#define J7CAM_ANALOGUE_GAIN_MAX 480
#define J7CAM_FOCUS_MAX 1023
/* This handset's CRC-verified EEPROM distant-focus/default position. */
#define J7CAM_FOCUS_DEFAULT 500
/* Successful I2C transfers, not proof of physical frame latch. */
#define J7CAM_WRITTEN_FRAME_LENGTH (1U << 0)
#define J7CAM_WRITTEN_EXPOSURE (1U << 1)
#define J7CAM_WRITTEN_GAIN (1U << 2)
/* Caller pins the bound sensor driver using device_lock until release.
 * A successful prepare holds the sensor mutex until release on same task.
 */
int j7cam_capture_prepare(struct i2c_client *client, bool test_pattern,
			  unsigned int exposure, unsigned int analogue_gain,
			  unsigned int focus, unsigned int frame_length);
int j7cam_capture_stream(struct i2c_client *client, bool on);
/* Same task/lock ownership as prepare. On a write failure, stop capture and
 * release; the control state becomes unknown and further updates are refused.
 * No group-hold atomicity is implied. Only a zero return permits publication
 * of the complete configured generation; *written preserves partial success.
 */
int j7cam_capture_update(struct i2c_client *client, unsigned int exposure,
			 unsigned int analogue_gain, unsigned int frame_length,
			 unsigned int *written);
void j7cam_capture_release(struct i2c_client *client);
#endif
