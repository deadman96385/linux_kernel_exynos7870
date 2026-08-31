// SPDX-License-Identifier: GPL-2.0-only

#include "s6e3fa3_update.h"

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/string.h>

static enum s6e3fa3_acl_mode
s6e3fa3_acl_resolve(unsigned int brightness, bool adaptive_control)
{
	if (!adaptive_control && brightness >= 255)
		return S6E3FA3_ACL_OFF;
	if (brightness > 255)
		return S6E3FA3_ACL_8_PERCENT;

	return S6E3FA3_ACL_15_PERCENT;
}

static void s6e3fa3_acl_build(struct s6e3fa3_update *update,
			      unsigned int brightness)
{
	update->acl_set[0] = 0xb5;
	update->acl_set[3] = 0x14;
	update->acl[0] = 0x55;

	switch (update->acl_mode) {
	case S6E3FA3_ACL_8_PERCENT:
		update->acl_set[1] = 0x50;
		update->acl_set[2] = 0x99;
		update->acl_set[4] = 0x0a;
		update->acl[1] = 0x02;
		break;
	case S6E3FA3_ACL_15_PERCENT:
		update->acl_set[1] = 0x50;
		update->acl_set[2] = 0x7f;
		update->acl_set[4] = 0x14;
		update->acl[1] = 0x02;
		break;
	case S6E3FA3_ACL_OFF:
		update->acl_set[1] = 0x40;
		if (brightness == 255) {
			update->acl_set[2] = 0x7f;
			update->acl_set[4] = 0x14;
		} else {
			update->acl_set[2] = 0x99;
			update->acl_set[4] = 0x0a;
		}
		update->acl[1] = 0x00;
		break;
	}
}

int s6e3fa3_update_build(struct s6e3fa3_update *result,
			 const struct s6e3fa3_dimming *dimming,
			 unsigned int brightness, int temperature,
			 bool adaptive_control,
			 const u8 factory_elvss[S6E3FA3_ELVSS_PAYLOAD_LEN])
{
	struct s6e3fa3_update calculated = { };
	const u8 *gamma;
	unsigned int aor_index;
	u8 encoded_temperature;

	if (!result || !dimming || !dimming->valid || !factory_elvss)
		return -EINVAL;
	if (brightness > S6E3FA3_MAX_BRIGHTNESS ||
	    temperature < -127 || temperature > 127)
		return -ERANGE;

	calculated.level = s6e3fa3_brightness_to_level[brightness];
	if (calculated.level >= S6E3FA3_NUM_LEVELS)
		return -EINVAL;
	gamma = s6e3fa3_gamma(dimming, calculated.level);
	if (!gamma)
		return -ENODATA;

	calculated.hbm = calculated.level >= S6E3FA3_NUM_NORMAL_LEVELS;
	calculated.nit = s6e3fa3_nit[calculated.level];
	calculated.acl_mode = s6e3fa3_acl_resolve(brightness,
							adaptive_control);
	memcpy(calculated.gamma, gamma, sizeof(calculated.gamma));

	aor_index = brightness > 255 ? 256 : brightness;
	memcpy(calculated.aor, s6e3fa3_aor[aor_index],
	       sizeof(calculated.aor));

	encoded_temperature = temperature < 0 ?
		(u8)(-temperature) | 0x80 : (u8)temperature;
	calculated.tset[0] = 0xb8;
	calculated.tset[1] = encoded_temperature;

	calculated.elvss[0] = 0xb6;
	memcpy(&calculated.elvss[1], factory_elvss,
	       S6E3FA3_ELVSS_PAYLOAD_LEN);
	calculated.elvss[1] =
		s6e3fa3_elvss_offset[calculated.level][1];
	calculated.elvss[2] =
		s6e3fa3_elvss_offset[calculated.level][2];
	if (calculated.hbm)
		calculated.elvss[22] = factory_elvss[22];

	calculated.gamma_latch[0] = 0xf7;
	calculated.gamma_latch[1] = 0x03;
	s6e3fa3_acl_build(&calculated, brightness);
	calculated.valid = true;

	*result = calculated;
	return 0;
}
EXPORT_SYMBOL_GPL(s6e3fa3_update_build);

int s6e3fa3_update_emit(const struct s6e3fa3_update *update,
			s6e3fa3_write_fn write, void *context)
{
	const u8 *commands[S6E3FA3_NUM_UPDATE_COMMANDS];
	const size_t lengths[S6E3FA3_NUM_UPDATE_COMMANDS] = {
		S6E3FA3_GAMMA_COMMAND_LEN,
		S6E3FA3_AOR_COMMAND_LEN,
		S6E3FA3_TSET_COMMAND_LEN,
		S6E3FA3_ELVSS_COMMAND_LEN,
		S6E3FA3_GAMMA_LATCH_COMMAND_LEN,
		S6E3FA3_ACL_SET_COMMAND_LEN,
		S6E3FA3_ACL_COMMAND_LEN,
	};
	size_t i;
	int ret;

	if (!update || !update->valid || !write)
		return -EINVAL;

	commands[0] = update->gamma;
	commands[1] = update->aor;
	commands[2] = update->tset;
	commands[3] = update->elvss;
	commands[4] = update->gamma_latch;
	commands[5] = update->acl_set;
	commands[6] = update->acl;

	for (i = 0; i < S6E3FA3_NUM_UPDATE_COMMANDS; i++) {
		ret = write(context, commands[i], lengths[i]);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(s6e3fa3_update_emit);
