/* DW9807 experiment for this handset's CRC-verified calibration.
 * The sensor owner holds both AF and I/O rails throughout these accesses.
 */
static int j7cam_focus_read(struct j7cam *cam, u8 reg, u8 *value)
{
	struct i2c_msg msgs[] = {
		{ .addr = cam->actuator->addr, .len = 1, .buf = &reg },
		{ .addr = cam->actuator->addr, .flags = I2C_M_RD, .len = 1, .buf = value },
	};
	int ret = i2c_transfer(cam->actuator->adapter, msgs, ARRAY_SIZE(msgs));

	return ret == ARRAY_SIZE(msgs) ? 0 : ret < 0 ? ret : -EIO;
}

static int j7cam_focus_write(struct j7cam *cam, u8 reg, u8 value)
{
	u8 bytes[] = { reg, value };
	int ret = i2c_master_send(cam->actuator, bytes, sizeof(bytes));

	return ret == sizeof(bytes) ? 0 : ret < 0 ? ret : -EIO;
}

static int j7cam_focus_wait(struct j7cam *cam)
{
	unsigned int i;
	u8 status;
	int ret;

	for (i = 0; i < 100; i++) {
		ret = j7cam_focus_read(cam, 5, &status);
		if (ret)
			return ret;
		if (!(status & 3))
			return 0;
		usleep_range(1000, 1200);
	}
	return -ETIMEDOUT;
}

static int j7cam_focus_position(struct j7cam *cam, unsigned int position)
{
	u8 bytes[] = { 3, (position >> 8) & 3, position & 255 };
	u8 high, low;
	int ret;

	if (position > J7CAM_FOCUS_MAX)
		return -ERANGE;
	ret = j7cam_focus_wait(cam);
	if (ret)
		return ret;
	ret = i2c_master_send(cam->actuator, bytes, sizeof(bytes));
	if (ret != sizeof(bytes))
		return ret < 0 ? ret : -EIO;
	/* Retain the last commanded position even if readback later fails. */
	cam->focus_position = position;
	ret = j7cam_focus_wait(cam);
	if (!ret)
		ret = j7cam_focus_read(cam, 3, &high);
	if (!ret)
		ret = j7cam_focus_read(cam, 4, &low);
	if (!ret && (((high & 3) << 8) | low) != position)
		ret = -EIO;
	return ret;
}

static int j7cam_focus_ramp(struct j7cam *cam, unsigned int target)
{
	int ret;

	while (cam->focus_position != target) {
		unsigned int next;

		if (cam->focus_position < target)
			next = min(cam->focus_position + 16, target);
		else
			next = max((int)cam->focus_position - 16, (int)target);
		ret = j7cam_focus_position(cam, next);
		if (ret)
			return ret;
		usleep_range(1000, 1200);
	}
	return 0;
}

static int j7cam_focus_init(struct j7cam *cam, unsigned int target)
{
	u8 high, low, value;
	int ret;

	ret = j7cam_focus_write(cam, 2, 1);
	if (!ret)
		ret = j7cam_focus_write(cam, 2, 0);
	if (ret)
		return ret;
	usleep_range(100, 200);
	/* Ensure cleanup attempts parking/power-down after subsequent failures. */
	cam->focus_active = true;
	ret = j7cam_focus_read(cam, 3, &high);
	if (!ret)
		ret = j7cam_focus_read(cam, 4, &low);
	if (ret)
		return ret;
	cam->focus_position = ((high & 3) << 8) | low;
	ret = j7cam_focus_write(cam, 2, 2);
	if (!ret)
		ret = j7cam_focus_write(cam, 6, 0x61);
	if (!ret)
		ret = j7cam_focus_write(cam, 7, 0x36);
	if (!ret)
		ret = j7cam_focus_read(cam, 6, &value);
	if (!ret && value != 0x61)
		ret = -EIO;
	if (!ret)
		ret = j7cam_focus_read(cam, 7, &value);
	if (!ret && value != 0x36)
		ret = -EIO;
	if (ret)
		return ret;
	/* Stock startup sequence; then move gradually to the requested position. */
	ret = j7cam_focus_position(cam, 100);
	if (ret)
		return ret;
	msleep(20);
	ret = j7cam_focus_position(cam, 170);
	if (ret)
		return ret;
	msleep(10);
	ret = j7cam_focus_ramp(cam, target);
	if (!ret)
		msleep(12);
	return ret;
}

static void j7cam_focus_park(struct j7cam *cam)
{
	int ret, down;

	if (!cam->focus_active)
		return;
	ret = j7cam_focus_ramp(cam, 0);
	down = j7cam_focus_write(cam, 2, 1);
	if (ret || down)
		dev_err(&cam->client->dev, "focus park=%d power-down=%d\n", ret, down);
	cam->focus_active = false;
}
