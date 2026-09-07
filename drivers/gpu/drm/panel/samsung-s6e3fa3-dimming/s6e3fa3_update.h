/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef S6E3FA3_UPDATE_H
#define S6E3FA3_UPDATE_H

#include "s6e3fa3_dimming.h"

#include <linux/types.h>

#define S6E3FA3_TSET_COMMAND_LEN 2
#define S6E3FA3_GAMMA_LATCH_COMMAND_LEN 2
#define S6E3FA3_ACL_SET_COMMAND_LEN 5
#define S6E3FA3_ACL_COMMAND_LEN 2
#define S6E3FA3_NUM_UPDATE_COMMANDS 7

enum s6e3fa3_acl_mode {
	S6E3FA3_ACL_OFF,
	S6E3FA3_ACL_8_PERCENT,
	S6E3FA3_ACL_15_PERCENT,
};

typedef int (*s6e3fa3_write_fn)(void *context, const u8 *data,
				size_t length);

struct s6e3fa3_update {
	bool valid;
	bool hbm;
	u8 level;
	u16 nit;
	enum s6e3fa3_acl_mode acl_mode;
	u8 gamma[S6E3FA3_GAMMA_COMMAND_LEN];
	u8 aor[S6E3FA3_AOR_COMMAND_LEN];
	u8 tset[S6E3FA3_TSET_COMMAND_LEN];
	u8 elvss[S6E3FA3_ELVSS_COMMAND_LEN];
	u8 gamma_latch[S6E3FA3_GAMMA_LATCH_COMMAND_LEN];
	u8 acl_set[S6E3FA3_ACL_SET_COMMAND_LEN];
	u8 acl[S6E3FA3_ACL_COMMAND_LEN];
};

/*
 * Build one complete stock-compatible update.  Output is modified only on
 * success; factory_elvss must be the exact 23-byte B6 panel read.
 */
int s6e3fa3_update_build(struct s6e3fa3_update *result,
			 const struct s6e3fa3_dimming *dimming,
			 unsigned int brightness, int temperature,
			 bool adaptive_control,
			 const u8 factory_elvss[S6E3FA3_ELVSS_PAYLOAD_LEN]);

/* Emit the body in J7 downstream order and stop at the first error. */
int s6e3fa3_update_emit(const struct s6e3fa3_update *update,
			s6e3fa3_write_fn write, void *context);

#endif
