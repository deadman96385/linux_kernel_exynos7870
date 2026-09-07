/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef S6E3FA3_DIMMING_H
#define S6E3FA3_DIMMING_H

#include "s6e3fa3_tables.h"

#include <linux/types.h>

#define S6E3FA3_NUM_GRAY_LEVELS 256

enum s6e3fa3_color {
	S6E3FA3_RED,
	S6E3FA3_GREEN,
	S6E3FA3_BLUE,
};

struct s6e3fa3_mtp {
	s16 offset[S6E3FA3_MAX][S6E3FA3_NUM_COLORS];
};

struct s6e3fa3_dimming {
	bool valid;
	bool hbm_valid;
	struct s6e3fa3_mtp mtp;
	s64 point_voltage[S6E3FA3_MAX][S6E3FA3_NUM_COLORS];
	s64 gray_voltage[S6E3FA3_NUM_GRAY_LEVELS][S6E3FA3_NUM_COLORS];
	u8 gamma[S6E3FA3_NUM_LEVELS][S6E3FA3_GAMMA_COMMAND_LEN];
};

/* Structural decoder permits synthetic all-zero calibration for KUnit. */
int s6e3fa3_mtp_decode(struct s6e3fa3_mtp *mtp, const u8 *data,
		       size_t length);

/* Live decoder additionally rejects all-zero/all-ff DSI reads. */
int s6e3fa3_mtp_decode_live(struct s6e3fa3_mtp *mtp, const u8 *data,
			    size_t length);

int s6e3fa3_dimming_init(struct s6e3fa3_dimming *dimming,
			 const u8 mtp[S6E3FA3_MTP_LEN]);
int s6e3fa3_dimming_init_live(struct s6e3fa3_dimming *dimming,
			      const u8 mtp[S6E3FA3_MTP_LEN]);

int s6e3fa3_hbm_init(struct s6e3fa3_dimming *dimming,
		     const u8 hbm[S6E3FA3_HBM_MTP_LEN]);
int s6e3fa3_hbm_init_live(struct s6e3fa3_dimming *dimming,
			  const u8 hbm[S6E3FA3_HBM_MTP_LEN]);

const u8 *s6e3fa3_gamma(const struct s6e3fa3_dimming *dimming,
			unsigned int level);

#endif
