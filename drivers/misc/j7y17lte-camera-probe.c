// SPDX-License-Identifier: GPL-2.0-only
/* Temporary J7Y17LTE board and rear test-pattern qualification driver.
 * Power order: universal7870 117a2f271e0f, module-imx258 and module-3m3.
 * Deliberately uses experimental compatibles, not production sensor bindings.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include "j7y17lte-camera-capture.h"
#include "j7y17lte-imx258-mode.h"

struct j7cam {
	struct i2c_client *client;
	struct i2c_client *actuator;
	unsigned int focus_position;
	bool focus_active;
	struct clk *clock;
	struct gpio_desc *reset;
	struct regulator *rail[4];
	struct pinctrl *pins;
	struct pinctrl_state *active, *idle;
	struct mutex lock;
	unsigned long enabled;
	bool front, powered, clock_on;
	bool controls_valid;
	unsigned int control_frame_length;
};

#include "j7y17lte-dw9807.inc"

static void j7cam_rail_off(struct j7cam *cam, unsigned int i)
{
	int ret;

	if (!(cam->enabled & BIT(i)))
		return;
	ret = regulator_disable(cam->rail[i]);
	if (ret)
		dev_err(&cam->client->dev, "rail %u disable: %d\n", i, ret);
	else
		cam->enabled &= ~BIT(i);
}

static void j7cam_off(struct j7cam *cam)
{
	int ret;

	cam->controls_valid = false;

	if (cam->front) {
		gpiod_set_value_cansleep(cam->reset, 1);
		usleep_range(1000, 1200);
	} else {
		j7cam_focus_park(cam);
		j7cam_rail_off(cam, 2); /* autofocus */
	}
	ret = pinctrl_select_state(cam->pins, cam->idle);
	if (ret)
		dev_err(&cam->client->dev, "MCLK idle pins: %d\n", ret);
	if (cam->clock_on) {
		clk_disable_unprepare(cam->clock);
		cam->clock_on = false;
	}
	usleep_range(cam->front ? 1000 : 2000, cam->front ? 1200 : 2200);
	if (!cam->front) {
		gpiod_set_value_cansleep(cam->reset, 1);
		j7cam_rail_off(cam, 3); /* shared IO */
	}
	j7cam_rail_off(cam, 1); /* core */
	j7cam_rail_off(cam, 0); /* analog */
	if (cam->front)
		j7cam_rail_off(cam, 2); /* shared IO */
	cam->powered = false;
}

static int j7cam_on(struct j7cam *cam)
{
	unsigned int i, count = cam->front ? 3 : 4;
	int ret;

	if (cam->powered)
		return 0;
	/* A failed regulator disable must not accumulate enable references. */
	if (cam->enabled)
		return -EIO;
	gpiod_set_value_cansleep(cam->reset, 1);
	for (i = 0; i < count; i++) {
		ret = regulator_enable(cam->rail[i]);
		if (ret)
			goto fail;
		cam->enabled |= BIT(i);
		if (!cam->front && i >= 2)
			usleep_range(2000, 2200);
	}
	if (cam->front)
		usleep_range(10, 20);
	if (clk_get_rate(cam->clock) != 26000000) {
		ret = -EINVAL;
		goto fail;
	}
	ret = clk_prepare_enable(cam->clock);
	if (ret)
		goto fail;
	cam->clock_on = true;
	ret = pinctrl_select_state(cam->pins, cam->active);
	if (ret)
		goto fail;
	usleep_range(cam->front ? 1000 : 3000, cam->front ? 1200 : 3200);
	gpiod_set_value_cansleep(cam->reset, 0);
	usleep_range(cam->front ? 1000 : 5000, cam->front ? 1200 : 5200);
	cam->powered = true;
	return 0;
fail:
	j7cam_off(cam);
	return ret;
}

static int j7cam_id(struct j7cam *cam)
{
	u16 reg = cam->front ? 0x0000 : 0x0016;
	u8 addr[] = { reg >> 8, reg & 0xff }, value[2];
	struct i2c_msg msgs[] = {
		{ .addr = cam->client->addr, .len = 2, .buf = addr },
		{ .addr = cam->client->addr, .flags = I2C_M_RD,
		  .len = 2, .buf = value },
	};
	int ret = i2c_transfer(cam->client->adapter, msgs, ARRAY_SIZE(msgs));

	if (ret != ARRAY_SIZE(msgs))
		return ret < 0 ? ret : -EIO;
	ret = (value[0] << 8) | value[1];
	if (!ret || ret == 0xffff || (!cam->front && ret != 0x0258)) {
		dev_err(&cam->client->dev, "unexpected ID 0x%04x at register 0x%04x\n",
			ret, reg);
		return -ENODEV;
	}
	/* Front is reported as an observed ID until its expected value is proven. */
	return ret;
}

static int j7cam_write(struct j7cam *cam, u16 addr, u8 value)
{
	u8 buf[] = { addr >> 8, addr & 0xff, value };
	int ret = i2c_master_send(cam->client, buf, sizeof(buf));

	return ret == sizeof(buf) ? 0 : ret < 0 ? ret : -EIO;
}

static int j7cam_write16(struct j7cam *cam, u16 addr, u16 value)
{
	u8 buf[] = { addr >> 8, addr & 0xff, value >> 8, value & 0xff };
	int ret = i2c_master_send(cam->client, buf, sizeof(buf));

	return ret == sizeof(buf) ? 0 : ret < 0 ? ret : -EIO;
}

int j7cam_capture_update(struct i2c_client *client, unsigned int exposure,
			 unsigned int analogue_gain, unsigned int frame_length,
			 unsigned int *written)
{
	struct j7cam *cam = i2c_get_clientdata(client);
	bool shrinking;
	int ret;

	if (!written)
		return -EINVAL;
	*written = 0;
	if (!cam || cam->front)
		return -ENODEV;
	lockdep_assert_held(&cam->lock);
	if (!cam->powered || !cam->controls_valid)
		return -EIO;
	if (frame_length < J7CAM_FRAME_LENGTH_DEFAULT ||
	    frame_length > J7CAM_FRAME_LENGTH_MAX ||
	    exposure < J7CAM_EXPOSURE_MIN ||
	    exposure > frame_length - J7CAM_EXPOSURE_MARGIN ||
	    analogue_gain > J7CAM_ANALOGUE_GAIN_MAX)
		return -ERANGE;
	shrinking = frame_length < cam->control_frame_length;
	/* A partial transfer can leave unknown sensor state. Never retry against
	 * a presumed old frame length; common shutdown must stop and reprepare.
	 */
	cam->controls_valid = false;
	/* Shrink exposure first so it fits both periods; grow the period first
	 * so a longer exposure fits. Each register is one write16 transaction,
	 * matching stock. Cross-register latch timing still needs measurement.
	 */
	if (!shrinking) {
		ret = j7cam_write16(cam, 0x0340, frame_length);
		if (ret)
			return ret;
		*written |= J7CAM_WRITTEN_FRAME_LENGTH;
	}
	ret = j7cam_write16(cam, 0x0202, exposure);
	if (ret)
		return ret;
	*written |= J7CAM_WRITTEN_EXPOSURE;
	if (shrinking) {
		ret = j7cam_write16(cam, 0x0340, frame_length);
		if (ret)
			return ret;
		*written |= J7CAM_WRITTEN_FRAME_LENGTH;
	}
	ret = j7cam_write16(cam, 0x0204, analogue_gain);
	if (ret)
		return ret;
	*written |= J7CAM_WRITTEN_GAIN;
	cam->control_frame_length = frame_length;
	cam->controls_valid = true;
	return 0;
}
EXPORT_SYMBOL_GPL(j7cam_capture_update);

static int j7cam_table(struct j7cam *cam, const struct j7cam_reg *regs,
		      size_t count)
{
	size_t i;
	int ret;

	for (i = 0; i < count; i++) {
		ret = j7cam_write(cam, regs[i].addr, regs[i].value);
		if (ret) {
			dev_err(&cam->client->dev, "mode write %04x failed: %d\n",
				regs[i].addr, ret);
			return ret;
		}
	}
	return 0;
}

int j7cam_capture_prepare(struct i2c_client *client, bool test_pattern,
                          unsigned int exposure, unsigned int analogue_gain,
                          unsigned int focus, unsigned int frame_length)
{
	struct j7cam *cam = i2c_get_clientdata(client);
	int ret;

	if (!cam || cam->front)
		return -ENODEV;
	if (frame_length < J7CAM_FRAME_LENGTH_DEFAULT ||
	    frame_length > J7CAM_FRAME_LENGTH_MAX ||
	    exposure < J7CAM_EXPOSURE_MIN ||
	    exposure > frame_length - J7CAM_EXPOSURE_MARGIN ||
	    analogue_gain > J7CAM_ANALOGUE_GAIN_MAX || focus > J7CAM_FOCUS_MAX)
		return -ERANGE;
	mutex_lock(&cam->lock);
	if (cam->powered) {
		ret = -EBUSY;
		goto unlock;
	}
	ret = j7cam_on(cam);
	if (ret)
		goto unlock;
	ret = j7cam_id(cam);
	if (ret < 0)
		goto off;
	ret = j7cam_focus_init(cam, focus);
	if (ret)
		goto off;
	ret = j7cam_write(cam, 0x0100, 0);
	if (ret)
		goto off;
#define J7CAM_TABLE(name) do { \
	ret = j7cam_table(cam, j7cam_##name, ARRAY_SIZE(j7cam_##name)); \
	if (ret) \
		goto off; \
} while (0)
	J7CAM_TABLE(initial);
	J7CAM_TABLE(global);
	J7CAM_TABLE(Image);
	J7CAM_TABLE(4144x3106_30fps);
#undef J7CAM_TABLE
	/* Standby after mode setup: no frame can observe partial 16-bit writes.
	 * Preserve the vendor exposure and unity-gain defaults unless selected.
	 */
	ret = j7cam_write(cam, 0x0340, frame_length >> 8);
	if (!ret)
		ret = j7cam_write(cam, 0x0341, frame_length & 0xff);
	if (!ret)
		ret = j7cam_write(cam, 0x0202, exposure >> 8);
	if (!ret)
		ret = j7cam_write(cam, 0x0203, exposure & 0xff);
	if (!ret)
		ret = j7cam_write(cam, 0x0204, analogue_gain >> 8);
	if (!ret)
		ret = j7cam_write(cam, 0x0205, analogue_gain & 0xff);
	if (ret)
		goto off;
	/* Fixed colour bars for first DMA packing/geometry qualification. */
	ret = j7cam_write(cam, 0x0600, 0);
	if (!ret)
		ret = j7cam_write(cam, 0x0601, test_pattern ? 2 : 0);
	if (!ret) {
		cam->control_frame_length = frame_length;
		cam->controls_valid = true;
		return 0;
	}
off:
	j7cam_off(cam);
unlock:
	mutex_unlock(&cam->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(j7cam_capture_prepare);

int j7cam_capture_stream(struct i2c_client *client, bool on)
{
	struct j7cam *cam = i2c_get_clientdata(client);

	lockdep_assert_held(&cam->lock);
	return j7cam_write(cam, 0x0100, on);
}
EXPORT_SYMBOL_GPL(j7cam_capture_stream);

void j7cam_capture_release(struct i2c_client *client)
{
	struct j7cam *cam = i2c_get_clientdata(client);

	lockdep_assert_held(&cam->lock);
	j7cam_off(cam);
	mutex_unlock(&cam->lock);
}
EXPORT_SYMBOL_GPL(j7cam_capture_release);

static ssize_t chip_id_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct j7cam *cam = dev_get_drvdata(dev);
	bool held;
	int id, ret, i;

	mutex_lock(&cam->lock);
	held = cam->powered;
	ret = j7cam_on(cam);
	if (ret)
		goto out;
	id = j7cam_id(cam);
	ret = id;
	for (i = 0; id >= 0 && i < 4; i++) {
		usleep_range(1000, 1200);
		ret = j7cam_id(cam);
		if (ret < 0 || ret != id) {
			ret = ret < 0 ? ret : -EIO;
			break;
		}
	}
	if (ret >= 0)
		ret = sysfs_emit(buf, "0x%04x\n", id);
	if (!held)
		j7cam_off(cam);
out:
	mutex_unlock(&cam->lock);
	return ret;
}
static DEVICE_ATTR_RO(chip_id);

static ssize_t powered_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct j7cam *cam = dev_get_drvdata(dev);
	ssize_t ret;

	/* Capture holds this mutex for the stream lifetime. A status read must
	 * not wait indefinitely for STREAMOFF; preserve serialization via EBUSY.
	 */
	if (!mutex_trylock(&cam->lock))
		return -EBUSY;
	ret = sysfs_emit(buf, "%u\n", cam->powered);
	mutex_unlock(&cam->lock);
	return ret;
}

static ssize_t powered_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct j7cam *cam = dev_get_drvdata(dev);
	bool on;
	int ret = kstrtobool(buf, &on);

	if (ret)
		return ret;
	mutex_lock(&cam->lock);
	if (on)
		ret = j7cam_on(cam);
	else {
		j7cam_off(cam);
		ret = cam->enabled ? -EIO : 0;
	}
	mutex_unlock(&cam->lock);
	return ret ?: count;
}
static DEVICE_ATTR_RW(powered);

static struct attribute *j7cam_attrs[] = {
	&dev_attr_chip_id.attr, &dev_attr_powered.attr, NULL,
};
ATTRIBUTE_GROUPS(j7cam);

static int j7cam_probe(struct i2c_client *client)
{
	static const char * const rear[] = { "analog", "core", "af", "io" };
	static const char * const front[] = { "analog", "core", "io" };
	struct device *dev = &client->dev;
	struct j7cam *cam;
	const char * const *names;
	int i, ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;
	cam = devm_kzalloc(dev, sizeof(*cam), GFP_KERNEL);
	if (!cam)
		return -ENOMEM;
	cam->client = client;
	cam->front = of_device_is_compatible(dev->of_node, "samsung,j7y17lte-3m3-probe");
	mutex_init(&cam->lock);
	i2c_set_clientdata(client, cam);
	cam->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(cam->reset))
		return dev_err_probe(dev, PTR_ERR(cam->reset), "reset\n");
	cam->clock = devm_clk_get(dev, NULL);
	if (IS_ERR(cam->clock))
		return dev_err_probe(dev, PTR_ERR(cam->clock), "MCLK\n");
	cam->pins = devm_pinctrl_get(dev);
	if (IS_ERR(cam->pins))
		return dev_err_probe(dev, PTR_ERR(cam->pins), "pinctrl\n");
	cam->active = pinctrl_lookup_state(cam->pins, "active");
	if (IS_ERR(cam->active))
		return PTR_ERR(cam->active);
	cam->idle = pinctrl_lookup_state(cam->pins, "default");
	if (IS_ERR(cam->idle))
		return PTR_ERR(cam->idle);
	names = cam->front ? front : rear;
	for (i = 0; i < (cam->front ? 3 : 4); i++) {
		cam->rail[i] = devm_regulator_get(dev, names[i]);
		if (IS_ERR(cam->rail[i]))
			return dev_err_probe(dev, PTR_ERR(cam->rail[i]), "%s\n", names[i]);
	}
	ret = pinctrl_select_state(cam->pins, cam->idle);
	if (ret)
		return ret;
	if (!cam->front) {
		struct device_node *node = of_parse_phandle(dev->of_node, "focus-i2c", 0);
		struct i2c_adapter *adapter;

		if (!node)
			return -EINVAL;
		adapter = of_find_i2c_adapter_by_node(node);
		of_node_put(node);
		if (!adapter)
			return -EPROBE_DEFER;
		cam->actuator = devm_i2c_new_dummy_device(dev, adapter, 0x0c);
		i2c_put_adapter(adapter);
		if (IS_ERR(cam->actuator))
			return dev_err_probe(dev, PTR_ERR(cam->actuator), "focus client\n");
	}
	dev_info(dev, "qualification interface ready; MCLK=%lu; sensors off\n",
		 clk_get_rate(cam->clock));
	return 0;
}

static void j7cam_remove(struct i2c_client *client)
{
	struct j7cam *cam = i2c_get_clientdata(client);

	mutex_lock(&cam->lock);
	j7cam_off(cam);
	mutex_unlock(&cam->lock);
}

static const struct of_device_id j7cam_of_match[] = {
	{ .compatible = "samsung,j7y17lte-imx258-probe" },
	{ .compatible = "samsung,j7y17lte-3m3-probe" },
	{ }
};
MODULE_DEVICE_TABLE(of, j7cam_of_match);

static struct i2c_driver j7cam_driver = {
	.driver = {
		.name = "j7y17lte-camera-probe",
		.of_match_table = j7cam_of_match,
		.dev_groups = j7cam_groups,
	},
	.probe = j7cam_probe,
	.remove = j7cam_remove,
	.shutdown = j7cam_remove,
};
module_i2c_driver(j7cam_driver);
MODULE_DESCRIPTION("J7Y17LTE camera power and identity qualification");
MODULE_LICENSE("GPL");
