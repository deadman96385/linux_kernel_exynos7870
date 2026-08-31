// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>
#include <linux/string.h>

#include "s6e3fa3_dimming.h"
#include "s6e3fa3_update.h"

static void s6e3fa3_mtp_test(struct kunit *test)
{
	struct s6e3fa3_mtp decoded;
	u8 mtp[S6E3FA3_MTP_LEN] = { };

	KUNIT_ASSERT_EQ(test, s6e3fa3_mtp_decode(&decoded, mtp, sizeof(mtp)),
			0);
	KUNIT_EXPECT_EQ(test, decoded.offset[S6E3FA3_255][S6E3FA3_RED],
			(s16)0);
	KUNIT_EXPECT_EQ(test,
			s6e3fa3_mtp_decode_live(&decoded, mtp, sizeof(mtp)),
			-ENODATA);

	mtp[33] = 0xa5;
	mtp[34] = 0x0c;
	KUNIT_ASSERT_EQ(test, s6e3fa3_mtp_decode(&decoded, mtp, sizeof(mtp)),
			0);
	KUNIT_EXPECT_EQ(test, decoded.offset[S6E3FA3_VT][S6E3FA3_RED],
			(s16)10);
	KUNIT_EXPECT_EQ(test, decoded.offset[S6E3FA3_VT][S6E3FA3_GREEN],
			(s16)5);
	KUNIT_EXPECT_EQ(test, decoded.offset[S6E3FA3_VT][S6E3FA3_BLUE],
			(s16)12);

	mtp[0] = 2;
	KUNIT_EXPECT_EQ(test, s6e3fa3_mtp_decode(&decoded, mtp, sizeof(mtp)),
			-EINVAL);
}

static void s6e3fa3_dimming_test(struct kunit *test)
{
	static const u8 zero_mtp[S6E3FA3_MTP_LEN];
	static const u8 first_gamma[S6E3FA3_GAMMA_COMMAND_LEN] = {
		0xca, 0x00, 0x2d, 0x00, 0x2c, 0x00, 0x2f,
		0xc9, 0xc9, 0xc9, 0xb4, 0xb3, 0xb4,
		0x83, 0x81, 0x83, 0x78, 0x76, 0x78,
		0xba, 0xb7, 0xb9, 0xbc, 0xb8, 0xb8,
		0xd9, 0xd3, 0xd2, 0xd8, 0xd8, 0xd8,
		0x00, 0x00, 0x00, 0x00, 0x00,
	};
	static const u8 center_gamma[S6E3FA3_GAMMA_COMMAND_LEN] = {
		0xca, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00,
		0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		0x00, 0x00, 0x00, 0x00, 0x00,
	};
	struct s6e3fa3_dimming *dimming;
	const u8 *gamma;

	dimming = kunit_kzalloc(test, sizeof(*dimming), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dimming);
	KUNIT_ASSERT_EQ(test, s6e3fa3_dimming_init(dimming, zero_mtp), 0);
	KUNIT_EXPECT_TRUE(test, dimming->valid);
	KUNIT_EXPECT_FALSE(test, dimming->hbm_valid);
	KUNIT_EXPECT_MEMEQ(test,
			s6e3fa3_gamma(dimming, S6E3FA3_LEVEL_005NIT),
			first_gamma, sizeof(first_gamma));

	gamma = s6e3fa3_gamma(dimming, S6E3FA3_LEVEL_360NIT);
	KUNIT_ASSERT_NOT_NULL(test, gamma);
	KUNIT_EXPECT_MEMEQ(test, gamma, center_gamma, sizeof(center_gamma));
	KUNIT_EXPECT_PTR_EQ(test,
			s6e3fa3_gamma(dimming, S6E3FA3_LEVEL_378NIT), NULL);
}

static void s6e3fa3_hbm_test(struct kunit *test)
{
	static const u8 zero_mtp[S6E3FA3_MTP_LEN];
	u8 hbm[S6E3FA3_HBM_MTP_LEN] = {
		0x05, 0x23, 0x34, 0x45,
		0x80, 0x81, 0x82, 0x83, 0x84, 0x85,
		0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b,
		0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91,
		0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
	};
	u8 expected[S6E3FA3_GAMMA_COMMAND_LEN] = {
		0xca, 0x01, 0x23, 0x00, 0x34, 0x01, 0x45,
		0x80, 0x81, 0x82, 0x83, 0x84, 0x85,
		0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b,
		0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91,
		0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
		0x00, 0x00, 0x00, 0x00, 0x00,
	};
	struct s6e3fa3_dimming *dimming;
	u8 empty[S6E3FA3_HBM_MTP_LEN] = { };

	dimming = kunit_kzalloc(test, sizeof(*dimming), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dimming);
	KUNIT_ASSERT_EQ(test, s6e3fa3_dimming_init(dimming, zero_mtp), 0);
	KUNIT_EXPECT_EQ(test, s6e3fa3_hbm_init_live(dimming, empty),
			-ENODATA);
	KUNIT_ASSERT_EQ(test, s6e3fa3_hbm_init_live(dimming, hbm), 0);
	KUNIT_EXPECT_TRUE(test, dimming->hbm_valid);
	KUNIT_EXPECT_MEMEQ(test, s6e3fa3_gamma(dimming,
						S6E3FA3_LEVEL_500NIT),
			   expected, sizeof(expected));
}

struct s6e3fa3_emit_capture {
	struct kunit *test;
	const u8 *expected;
	size_t count;
	size_t fail_at;
};

static int s6e3fa3_capture_command(void *context, const u8 *data,
				   size_t length)
{
	static const size_t expected_lengths[S6E3FA3_NUM_UPDATE_COMMANDS] = {
		36, 3, 2, 24, 2, 5, 2,
	};
	struct s6e3fa3_emit_capture *capture = context;
	size_t index = capture->count++;

	if (index >= S6E3FA3_NUM_UPDATE_COMMANDS)
		return -EOVERFLOW;
	KUNIT_EXPECT_EQ(capture->test, data[0], capture->expected[index]);
	KUNIT_EXPECT_EQ(capture->test, length, expected_lengths[index]);

	return index == capture->fail_at ? -EIO : 0;
}

static void s6e3fa3_update_test(struct kunit *test)
{
	static const u8 zero_mtp[S6E3FA3_MTP_LEN];
	static const u8 commands[S6E3FA3_NUM_UPDATE_COMMANDS] = {
		0xca, 0xb2, 0xb8, 0xb6, 0xf7, 0xb5, 0x55,
	};
	u8 hbm[S6E3FA3_HBM_MTP_LEN] = { 0x01 };
	u8 factory_elvss[S6E3FA3_ELVSS_PAYLOAD_LEN];
	struct s6e3fa3_emit_capture capture = {
		.test = test,
		.expected = commands,
		.fail_at = SIZE_MAX,
	};
	struct s6e3fa3_emit_capture failure = {
		.test = test,
		.expected = commands,
		.fail_at = 3,
	};
	struct s6e3fa3_dimming *dimming;
	struct s6e3fa3_update update;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(factory_elvss); i++)
		factory_elvss[i] = i + 1;
	dimming = kunit_kzalloc(test, sizeof(*dimming), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dimming);
	KUNIT_ASSERT_EQ(test, s6e3fa3_dimming_init(dimming, zero_mtp), 0);

	KUNIT_ASSERT_EQ(test,
			s6e3fa3_update_build(&update, dimming, 140, 25, true,
					      factory_elvss), 0);
	KUNIT_EXPECT_TRUE(test, update.valid);
	KUNIT_EXPECT_FALSE(test, update.hbm);
	KUNIT_EXPECT_EQ(test, update.nit, (u16)207);
	KUNIT_EXPECT_EQ(test, update.acl_mode, S6E3FA3_ACL_15_PERCENT);
	KUNIT_EXPECT_EQ(test, update.tset[1], (u8)0x19);
	KUNIT_EXPECT_EQ(test, update.elvss[3], factory_elvss[2]);
	KUNIT_EXPECT_EQ(test,
			s6e3fa3_update_emit(&update, s6e3fa3_capture_command,
					      &capture), 0);
	KUNIT_EXPECT_EQ(test, capture.count,
			(size_t)S6E3FA3_NUM_UPDATE_COMMANDS);

	KUNIT_EXPECT_EQ(test,
			s6e3fa3_update_emit(&update, s6e3fa3_capture_command,
					      &failure), -EIO);
	KUNIT_EXPECT_EQ(test, failure.count, (size_t)4);

	KUNIT_ASSERT_EQ(test, s6e3fa3_hbm_init(dimming, hbm), 0);
	KUNIT_ASSERT_EQ(test,
			s6e3fa3_update_build(&update, dimming, 355, -15, true,
					      factory_elvss), 0);
	KUNIT_EXPECT_TRUE(test, update.hbm);
	KUNIT_EXPECT_EQ(test, update.nit, (u16)500);
	KUNIT_EXPECT_EQ(test, update.acl_mode, S6E3FA3_ACL_8_PERCENT);
	KUNIT_EXPECT_EQ(test, update.tset[1], (u8)0x8f);
	KUNIT_EXPECT_EQ(test, update.elvss[22], factory_elvss[22]);

	KUNIT_ASSERT_EQ(test,
			s6e3fa3_update_build(&update, dimming, 255, 25, false,
					      factory_elvss), 0);
	KUNIT_EXPECT_EQ(test, update.acl_mode, S6E3FA3_ACL_OFF);
	KUNIT_EXPECT_EQ(test, update.acl[1], (u8)0x00);

	memset(&update, 0xa5, sizeof(update));
	KUNIT_EXPECT_EQ(test,
			s6e3fa3_update_build(&update, dimming, 356, 25, true,
					      factory_elvss), -ERANGE);
	KUNIT_EXPECT_EQ(test, update.level, (u8)0xa5);
}

static struct kunit_case s6e3fa3_test_cases[] = {
	KUNIT_CASE(s6e3fa3_mtp_test),
	KUNIT_CASE(s6e3fa3_dimming_test),
	KUNIT_CASE(s6e3fa3_hbm_test),
	KUNIT_CASE(s6e3fa3_update_test),
	{ }
};

static struct kunit_suite s6e3fa3_test_suite = {
	.name = "s6e3fa3-dimming",
	.test_cases = s6e3fa3_test_cases,
};

kunit_test_suite(s6e3fa3_test_suite);

MODULE_DESCRIPTION("KUnit tests for Samsung S6E3FA3 smart dimming");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
