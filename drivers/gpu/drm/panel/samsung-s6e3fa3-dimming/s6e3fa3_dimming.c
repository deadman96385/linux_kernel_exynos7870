// SPDX-License-Identifier: GPL-2.0-only
/*
 * Checked Samsung Dynamic AID implementation for S6E3FA3/AMS549KU15.
 *
 * The calculation order follows Samsung's J7 Pro downstream driver at
 * a3762bb1761aec8543fae511b087da620e24cee5.  Its fail-closed boundaries and
 * atomic output semantics follow the tested S6E8AA5X01 J5/J5X implementation.
 */

#include "s6e3fa3_dimming.h"

#include <kunit/visibility.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

#define S6E3FA3_VREG_X100	600000LL
#define S6E3FA3_VREF_HIGH	0LL

static const u8 s6e3fa3_gray_index[S6E3FA3_MAX] = {
	[S6E3FA3_VT] = 0,
	[S6E3FA3_0] = 0,
	[S6E3FA3_3] = 3,
	[S6E3FA3_11] = 11,
	[S6E3FA3_23] = 23,
	[S6E3FA3_35] = 35,
	[S6E3FA3_51] = 51,
	[S6E3FA3_87] = 87,
	[S6E3FA3_151] = 151,
	[S6E3FA3_203] = 203,
	[S6E3FA3_255] = 255,
};

static bool s6e3fa3_uniform(const u8 *data, size_t length, u8 value)
{
	size_t i;

	for (i = 0; i < length; i++)
		if (data[i] != value)
			return false;

	return true;
}

static s16 s6e3fa3_decode_sign_magnitude(u8 value)
{
	s16 magnitude = value & 0x7f;

	return value & 0x80 ? -magnitude : magnitude;
}

int s6e3fa3_mtp_decode(struct s6e3fa3_mtp *mtp, const u8 *data,
		       size_t length)
{
	struct s6e3fa3_mtp decoded = { };
	u8 unpacked[S6E3FA3_MAX * S6E3FA3_NUM_COLORS +
		    S6E3FA3_NUM_COLORS] = { };
	size_t byte = 0;
	int point;
	int color;

	if (!mtp || !data || length != S6E3FA3_MTP_LEN)
		return -EINVAL;

	memcpy(unpacked, data, 33);
	unpacked[33] = data[33] >> 4;
	unpacked[34] = data[33] & 0x0f;
	unpacked[35] = data[34] & 0x0f;

	for (color = 0; color < S6E3FA3_NUM_COLORS; color++) {
		u8 sign = unpacked[byte++];
		u8 magnitude = unpacked[byte++];

		if (sign > 1)
			return -EINVAL;
		decoded.offset[S6E3FA3_255][color] =
			sign ? -(s16)magnitude : magnitude;
	}

	for (point = S6E3FA3_203; point >= S6E3FA3_VT; point--)
		for (color = 0; color < S6E3FA3_NUM_COLORS; color++)
			decoded.offset[point][color] =
				s6e3fa3_decode_sign_magnitude(unpacked[byte++]);

	for (color = 0; color < S6E3FA3_NUM_COLORS; color++)
		if (decoded.offset[S6E3FA3_VT][color] < 0 ||
		    decoded.offset[S6E3FA3_VT][color] >=
				ARRAY_SIZE(s6e3fa3_vt_voltage_value))
			return -ERANGE;

	*mtp = decoded;
	return 0;
}
EXPORT_SYMBOL_IF_KUNIT(s6e3fa3_mtp_decode);

int s6e3fa3_mtp_decode_live(struct s6e3fa3_mtp *mtp, const u8 *data,
			    size_t length)
{
	if (!mtp || !data || length != S6E3FA3_MTP_LEN)
		return -EINVAL;
	if (s6e3fa3_uniform(data, length, 0x00) ||
	    s6e3fa3_uniform(data, length, 0xff))
		return -ENODATA;

	return s6e3fa3_mtp_decode(mtp, data, length);
}
EXPORT_SYMBOL_IF_KUNIT(s6e3fa3_mtp_decode_live);

static s64 s6e3fa3_reference_voltage(
	const struct s6e3fa3_dimming *dimming, unsigned int point,
	unsigned int color)
{
	if (point >= S6E3FA3_11 && point <= S6E3FA3_203)
		return dimming->point_voltage[S6E3FA3_VT][color];

	return S6E3FA3_VREG_X100;
}

static int s6e3fa3_point_voltages_init(struct s6e3fa3_dimming *dimming)
{
	int point;
	int color;

	for (color = 0; color < S6E3FA3_NUM_COLORS; color++) {
		s16 vt = dimming->mtp.offset[S6E3FA3_VT][color];

		dimming->point_voltage[S6E3FA3_VT][color] =
			S6E3FA3_VREG_X100 -
			div_s64(S6E3FA3_VREG_X100 *
				s6e3fa3_vt_voltage_value[vt], 860);
	}

	for (point = S6E3FA3_255; point > S6E3FA3_VT; point--) {
		const struct s6e3fa3_formula *formula =
			&s6e3fa3_gamma_formula[point];

		if (!formula->denominator)
			return -EINVAL;

		for (color = 0; color < S6E3FA3_NUM_COLORS; color++) {
			s64 reference = s6e3fa3_reference_voltage(dimming,
								point, color);
			s64 lower;
			s64 span;
			s64 factor;

			if (point == S6E3FA3_255)
				lower = S6E3FA3_VREF_HIGH;
			else
				lower = dimming->point_voltage[point + 1][color];
			span = reference - lower;
			factor = s6e3fa3_gamma_default[
				point * S6E3FA3_NUM_COLORS + color] +
				dimming->mtp.offset[point][color] +
				formula->numerator;
			if (span <= 0 || factor < 0 ||
			    factor >= formula->denominator)
				return -EDOM;

			dimming->point_voltage[point][color] = reference -
				div_s64(span * factor, formula->denominator);
		}
	}

	return 0;
}

static int s6e3fa3_gray_voltages_init(struct s6e3fa3_dimming *dimming)
{
	int point;
	int color;

	for (color = 0; color < S6E3FA3_NUM_COLORS; color++)
		dimming->gray_voltage[0][color] = S6E3FA3_VREG_X100;

	for (point = S6E3FA3_255; point > S6E3FA3_VT; point--) {
		unsigned int last = s6e3fa3_gray_index[point];
		unsigned int first = s6e3fa3_gray_index[point - 1];
		unsigned int span = last - first;
		unsigned int index = last;
		unsigned int offset;

		if (!span)
			continue;

		for (color = 0; color < S6E3FA3_NUM_COLORS; color++)
			if (dimming->point_voltage[point - 1][color] <=
			    dimming->point_voltage[point][color])
				return -EDOM;

		for (offset = 0; offset < span; offset++, index--)
			for (color = 0; color < S6E3FA3_NUM_COLORS; color++) {
				s64 low = dimming->point_voltage[point][color];
				s64 difference =
					dimming->point_voltage[point - 1][color] - low;

				dimming->gray_voltage[index][color] = low +
					div_s64(difference * offset, span);
			}
	}

	return 0;
}

static int s6e3fa3_gamma_encode(
	u8 command[S6E3FA3_GAMMA_COMMAND_LEN],
	const s16 gamma[S6E3FA3_MAX][S6E3FA3_NUM_COLORS])
{
	size_t byte = 0;
	int point;
	int color;

	command[byte++] = 0xca;
	for (color = 0; color < S6E3FA3_NUM_COLORS; color++) {
		if (gamma[S6E3FA3_255][color] < 0 ||
		    gamma[S6E3FA3_255][color] > 0x1ff)
			return -ERANGE;
		command[byte++] = gamma[S6E3FA3_255][color] >> 8;
		command[byte++] = gamma[S6E3FA3_255][color];
	}

	for (point = S6E3FA3_203; point > S6E3FA3_VT; point--)
		for (color = 0; color < S6E3FA3_NUM_COLORS; color++) {
			if (gamma[point][color] < 0 ||
			    gamma[point][color] > 0xff)
				return -ERANGE;
			command[byte++] = gamma[point][color];
		}

	for (color = 0; color < S6E3FA3_NUM_COLORS; color++)
		if (gamma[S6E3FA3_VT][color] < 0 ||
		    gamma[S6E3FA3_VT][color] > 0x0f)
			return -ERANGE;
	command[byte++] = gamma[S6E3FA3_VT][S6E3FA3_RED] << 4 |
			  gamma[S6E3FA3_VT][S6E3FA3_GREEN];
	command[byte++] = gamma[S6E3FA3_VT][S6E3FA3_BLUE];

	return byte == S6E3FA3_GAMMA_COMMAND_LEN ? 0 : -EINVAL;
}

static int s6e3fa3_gamma_level_init(struct s6e3fa3_dimming *dimming,
				    unsigned int level)
{
	s64 target[S6E3FA3_MAX][S6E3FA3_NUM_COLORS];
	s16 gamma[S6E3FA3_MAX][S6E3FA3_NUM_COLORS] = { };
	int point;
	int color;

	for (point = S6E3FA3_255; point > S6E3FA3_VT; point--)
		for (color = 0; color < S6E3FA3_NUM_COLORS; color++)
			target[point][color] = dimming->gray_voltage[
				s6e3fa3_m_gray[level][point]][color];
	for (color = 0; color < S6E3FA3_NUM_COLORS; color++)
		target[S6E3FA3_VT][color] =
			dimming->point_voltage[S6E3FA3_VT][color];

	for (point = S6E3FA3_255; point > S6E3FA3_VT; point--) {
		const struct s6e3fa3_formula *formula =
			&s6e3fa3_gamma_formula[point];

		for (color = 0; color < S6E3FA3_NUM_COLORS; color++) {
			s64 reference = point >= S6E3FA3_11 &&
					point <= S6E3FA3_203 ?
				target[S6E3FA3_VT][color] : S6E3FA3_VREG_X100;
			s64 lower = point == S6E3FA3_255 ?
				S6E3FA3_VREF_HIGH : target[point + 1][color];
			s64 denominator = reference - lower;
			s64 value;

			if (denominator <= 0)
				return -EDOM;
			value = div64_s64((reference - target[point][color] + 1) *
					  formula->denominator, denominator);
			value -= formula->numerator;
			value -= dimming->mtp.offset[point][color];
			value += s6e3fa3_offset_color[level]
				[point * S6E3FA3_NUM_COLORS + color];
			value = max_t(s64, value, 0);
			if (point != S6E3FA3_255)
				value = min_t(s64, value, 255);
			if (value > 0x1ff)
				return -ERANGE;
			gamma[point][color] = value;
		}
	}

	for (color = 0; color < S6E3FA3_NUM_COLORS; color++)
		gamma[S6E3FA3_VT][color] =
			s6e3fa3_gamma_default[color];

	return s6e3fa3_gamma_encode(dimming->gamma[level], gamma);
}

static int s6e3fa3_dimming_init_common(struct s6e3fa3_dimming *dimming,
					const u8 mtp[S6E3FA3_MTP_LEN],
					bool live)
{
	struct s6e3fa3_dimming *calculated;
	unsigned int level;
	int ret;

	if (!dimming || !mtp)
		return -EINVAL;
	calculated = kzalloc_obj(*calculated, GFP_KERNEL);
	if (!calculated)
		return -ENOMEM;

	ret = live ? s6e3fa3_mtp_decode_live(&calculated->mtp, mtp,
						  S6E3FA3_MTP_LEN) :
		     s6e3fa3_mtp_decode(&calculated->mtp, mtp,
					    S6E3FA3_MTP_LEN);
	if (ret)
		goto out;
	ret = s6e3fa3_point_voltages_init(calculated);
	if (ret)
		goto out;
	ret = s6e3fa3_gray_voltages_init(calculated);
	if (ret)
		goto out;
	for (level = 0; level < S6E3FA3_NUM_NORMAL_LEVELS; level++) {
		ret = s6e3fa3_gamma_level_init(calculated, level);
		if (ret)
			goto out;
	}

	calculated->valid = true;
	*dimming = *calculated;
out:
	kfree(calculated);
	return ret;
}

int s6e3fa3_dimming_init(struct s6e3fa3_dimming *dimming,
			 const u8 mtp[S6E3FA3_MTP_LEN])
{
	return s6e3fa3_dimming_init_common(dimming, mtp, false);
}
EXPORT_SYMBOL_IF_KUNIT(s6e3fa3_dimming_init);

int s6e3fa3_dimming_init_live(struct s6e3fa3_dimming *dimming,
			      const u8 mtp[S6E3FA3_MTP_LEN])
{
	return s6e3fa3_dimming_init_common(dimming, mtp, true);
}
EXPORT_SYMBOL_GPL(s6e3fa3_dimming_init_live);

static int s6e3fa3_hbm_decode(
	s16 gamma[S6E3FA3_MAX][S6E3FA3_NUM_COLORS], const u8 *data)
{
	u8 unpacked[S6E3FA3_MAX * S6E3FA3_NUM_COLORS +
		    S6E3FA3_NUM_COLORS] = { };
	size_t byte = 0;
	int point;
	int color;

	memcpy(&unpacked[2], data, S6E3FA3_HBM_MTP_LEN);
	unpacked[0] = data[0] >> 2 & 1;
	unpacked[1] = data[1];
	unpacked[2] = data[0] >> 1 & 1;
	unpacked[3] = data[2];
	unpacked[4] = data[0] & 1;
	unpacked[5] = data[3];

	for (color = 0; color < S6E3FA3_NUM_COLORS; color++) {
		gamma[S6E3FA3_255][color] = unpacked[byte++] << 8;
		gamma[S6E3FA3_255][color] |= unpacked[byte++];
	}
	for (point = S6E3FA3_203; point >= S6E3FA3_VT; point--)
		for (color = 0; color < S6E3FA3_NUM_COLORS; color++)
			gamma[point][color] = unpacked[byte++];

	return 0;
}

static int s6e3fa3_hbm_init_common(struct s6e3fa3_dimming *dimming,
				    const u8 hbm[S6E3FA3_HBM_MTP_LEN],
				    bool live)
{
	s16 hbm_gamma[S6E3FA3_MAX][S6E3FA3_NUM_COLORS] = { };
	u8 commands[S6E3FA3_NUM_LEVELS - S6E3FA3_NUM_NORMAL_LEVELS]
		   [S6E3FA3_GAMMA_COMMAND_LEN];
	unsigned int level;
	int point;
	int color;
	int ret;

	if (!dimming || !dimming->valid || !hbm)
		return -EINVAL;
	if (live && (s6e3fa3_uniform(hbm, S6E3FA3_HBM_MTP_LEN, 0x00) ||
		     s6e3fa3_uniform(hbm, S6E3FA3_HBM_MTP_LEN, 0xff)))
		return -ENODATA;

	s6e3fa3_hbm_decode(hbm_gamma, hbm);
	for (level = S6E3FA3_NUM_NORMAL_LEVELS;
	     level < S6E3FA3_NUM_LEVELS; level++) {
		s16 interpolated[S6E3FA3_MAX][S6E3FA3_NUM_COLORS];
		s64 ratio = div_s64((s64)(s6e3fa3_nit[level] - 360) << 10,
				    500 - 360);

		for (point = S6E3FA3_VT; point < S6E3FA3_MAX; point++)
			for (color = 0; color < S6E3FA3_NUM_COLORS; color++) {
				s16 base = s6e3fa3_gamma_default[
					point * S6E3FA3_NUM_COLORS + color];

				interpolated[point][color] = base +
					(((hbm_gamma[point][color] - base) * ratio) >> 10);
			}
		ret = s6e3fa3_gamma_encode(
			commands[level - S6E3FA3_NUM_NORMAL_LEVELS],
			interpolated);
		if (ret)
			return ret;
	}

	memcpy(&dimming->gamma[S6E3FA3_NUM_NORMAL_LEVELS], commands,
	       sizeof(commands));
	dimming->hbm_valid = true;
	return 0;
}

int s6e3fa3_hbm_init(struct s6e3fa3_dimming *dimming,
		     const u8 hbm[S6E3FA3_HBM_MTP_LEN])
{
	return s6e3fa3_hbm_init_common(dimming, hbm, false);
}
EXPORT_SYMBOL_IF_KUNIT(s6e3fa3_hbm_init);

int s6e3fa3_hbm_init_live(struct s6e3fa3_dimming *dimming,
			  const u8 hbm[S6E3FA3_HBM_MTP_LEN])
{
	return s6e3fa3_hbm_init_common(dimming, hbm, true);
}
EXPORT_SYMBOL_GPL(s6e3fa3_hbm_init_live);

const u8 *s6e3fa3_gamma(const struct s6e3fa3_dimming *dimming,
			unsigned int level)
{
	if (!dimming || !dimming->valid || level >= S6E3FA3_NUM_LEVELS)
		return NULL;
	if (level >= S6E3FA3_NUM_NORMAL_LEVELS && !dimming->hbm_valid)
		return NULL;

	return dimming->gamma[level];
}
EXPORT_SYMBOL_IF_KUNIT(s6e3fa3_gamma);

MODULE_DESCRIPTION("Samsung S6E3FA3 Dynamic AID calibration");
MODULE_LICENSE("GPL");
