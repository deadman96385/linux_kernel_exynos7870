// SPDX-License-Identifier: GPL-2.0-only
/*
 * UFS PHY driver data for Samsung EXYNOS9610 SoC
 *
 * Copyright (C) 2026 Sean Hoyt <seanhoyt963@gmail.com>
 *
 * Translated from LineageOS/android_kernel_motorola_exynos9610
 * (lineage-18.1) drivers/scsi/ufs/ufs-cal-9610.c, cross-checked against
 * how drivers/scsi/ufs/ufs-exynos.c's ufs_cal_config_uic() actually
 * dispatches each table entry: only PHY_PMA_COMN/PHY_PMA_TRSV entries go
 * through a raw MMIO write (ufs_lld_pma_write() -> phy_pma_writel(), which
 * is a plain `writel(val, reg_pma + reg)`, no extra shift). Everything
 * else in that downstream file (PHY_PCS_*, UNIPRO_*) goes through
 * ufshcd_dme_set()/a UniPro DME_SET UIC command instead, i.e. it belongs
 * in the *host* driver (ufs-exynos.c's uic_attr / pre_link hooks), not
 * here.
 *
 * Two address-encoding differences from downstream, both confirmed from
 * source rather than assumed:
 *   - This macro API (PHY_COMN_REG_CFG/PHY_TRSV_REG_CFG) takes a register
 *     *index* and internally does PHY_APB_ADDR(o) = o << 2 to get the
 *     byte offset. Downstream's tables already store raw byte offsets
 *     (PHY_PMA_COMN_ADDR(reg) = (reg), no shift), so every downstream
 *     value below is divided by 4.
 *   - Downstream's per-lane TRSV channel stride is 0x140 bytes
 *     (PHY_PMA_TRSV_ADDR(reg, lane) = reg + 0x140*lane) vs. the 0x30
 *     *index* (0xC0 bytes) exynos7/fsd use -- genuinely a different PMA
 *     layout on this chip, not a units mistake, so PHY_TRSV_REG_CFG_OFFSET
 *     is used directly with 0x140/4 = 0x50 instead of the PHY_TRSV_CH_OFFSET
 *     default.
 *
 * Downstream's PMD_HS_G{1,2,3}_L2 flags looked at first glance like a
 * gear+rate-series encoding (matching this API's PWR_MODE_HS_G2_SER_A
 * concept), but reading the actual enum (ufs-exynos.h) shows L1/L2 means
 * *lane count* (1 lane vs 2 lanes active), not rate series. Troika's
 * calib_of_hs_rate_a[] and calib_of_hs_rate_b[] tables are byte-for-byte
 * identical (no real series A/B differentiation), and only the _L2
 * (2-lane) flag variant is ever used anywhere in ufs-cal-9610.c -- the
 * _L1 variants are defined in the shared downstream header but never
 * referenced for this device, consistent with troika always negotiating
 * 2 lanes. So each downstream *_G{1,2,3}_L2 entry below maps to this
 * driver's gear-only PWR_MODE_HS_G{1,2,3}_ANY (series/lane-count
 * collapsed away, not lost information for this device).
 *
 * Known gaps, deliberately left out rather than guessed:
 *   - PWM-mode-specific PMA retuning (downstream's calib_of_pwm[]) has no
 *     home in this API's CFG_TAG enum (CFG_PRE_INIT/POST_INIT/PRE_PWR_HS/
 *     POST_PWR_HS -- no PWM tag), and neither exynos7 nor fsd populate one
 *     either, so this follows that precedent and omits it. PWM G1 link
 *     startup uses whatever CFG_PRE_INIT already programmed.
 *   - lane1_sq_off[] (asymmetric, lane-1-only squelch disable) has no
 *     equivalent slot in samsung_ufs_phy_drvdata, and I have not yet
 *     traced where downstream actually invokes it (single-lane fallback
 *     path?) to know if it's reachable on this always-2-lane device.
 *     Left unmapped.
 *   - PHY power/isolation control (isol.offset/mask/en): downstream reads
 *     this from a "ufs-phy-iso" device-tree child node's offset/mask/val
 *     properties (exynos_ufs_set_context_for_access() in ufs-exynos.c),
 *     and no such node exists anywhere in the downstream DTS tree for
 *     this device -- but exynos_ufs_populate_dt_system() has an explicit
 *     fallback for exactly that "node missing" case: offset=0x0724,
 *     mask=0x1, val=0x1. That's used below. (It also happens to be an
 *     exact match for FSD_EMBEDDED_COMBO_PHY_CTRL in phy-fsd-ufs.c --
 *     0x724/0x1/BIT(0) -- one more data point for this chip sharing much
 *     of its UFS IP with FSD.)
 */

#include "phy-samsung-ufs.h"

#define EXYNOS9610_EMBEDDED_COMBO_PHY_CTRL	0x724
#define EXYNOS9610_EMBEDDED_COMBO_PHY_CTRL_MASK	0x1
#define EXYNOS9610_EMBEDDED_COMBO_PHY_CTRL_EN	BIT(0)
#define EXYNOS9610_EMBEDDED_COMBO_PHY_CDR_LOCK_STATUS	0x7f /* 0x1fc / 4, from post_calib_of_hs_rate_a/b's PHY_CDR_WAIT entry */

static const struct samsung_ufs_phy_cfg exynos9610_pre_init_cfg[] = {
	PHY_COMN_REG_CFG(0x1d, 0x10, PWR_MODE_ANY),	/* 0x74 */
	PHY_TRSV_REG_CFG_OFFSET(0x44, 0xb5, PWR_MODE_ANY, 0x50),	/* 0x110 */
	PHY_TRSV_REG_CFG_OFFSET(0x4d, 0x43, PWR_MODE_ANY, 0x50),	/* 0x134 */
	PHY_TRSV_REG_CFG_OFFSET(0x5b, 0x20, PWR_MODE_ANY, 0x50),	/* 0x16c */
	PHY_TRSV_REG_CFG_OFFSET(0x5e, 0xc0, PWR_MODE_ANY, 0x50),	/* 0x178 */
	PHY_TRSV_REG_CFG_OFFSET(0x6c, 0x94, PWR_MODE_ANY, 0x50),	/* 0x1b0 */
	PHY_TRSV_REG_CFG_OFFSET(0x38, 0x12, PWR_MODE_ANY, 0x50),	/* 0xe0 */
	PHY_TRSV_REG_CFG_OFFSET(0x59, 0x58, PWR_MODE_ANY, 0x50),	/* 0x164 */
	/* 0x8c (PHY_PMA_COMN) toggled 0x80 -> 0xc0 -> 0x00, order matters */
	PHY_COMN_REG_CFG(0x23, 0x80, PWR_MODE_ANY),
	PHY_COMN_REG_CFG(0x23, 0xc0, PWR_MODE_ANY),
	PHY_COMN_REG_CFG(0x23, 0x00, PWR_MODE_ANY),
	END_UFS_PHY_CFG
};

/* Calibration for HS mode. calib_of_hs_rate_a[]/calib_of_hs_rate_b[] are
 * identical downstream, so series A/B are not distinguished here.
 */
static const struct samsung_ufs_phy_cfg exynos9610_pre_pwr_hs_cfg[] = {
	PHY_TRSV_REG_CFG_OFFSET(0x32, 0xbc, PWR_MODE_HS_ANY, 0x50),	/* 0xc8 */
	PHY_TRSV_REG_CFG_OFFSET(0x3c, 0x7f, PWR_MODE_HS_ANY, 0x50),	/* 0xf0 */
	PHY_TRSV_REG_CFG_OFFSET(0x48, 0xc0, PWR_MODE_HS_ANY, 0x50),	/* 0x120 */
	PHY_TRSV_REG_CFG_OFFSET(0x4a, 0x08, PWR_MODE_HS_G1_ANY, 0x50),	/* 0x128 */
	PHY_TRSV_REG_CFG_OFFSET(0x4a, 0x02, PWR_MODE_HS_G2_ANY, 0x50),	/* 0x128 */
	PHY_TRSV_REG_CFG_OFFSET(0x4a, 0x00, PWR_MODE_HS_G3_ANY, 0x50),	/* 0x128 */
	PHY_TRSV_REG_CFG_OFFSET(0x4b, 0x10, PWR_MODE_HS_G1_ANY, 0x50),	/* 0x12c */
	PHY_TRSV_REG_CFG_OFFSET(0x4b, 0x00, PWR_MODE_HS_G2_ANY, 0x50),	/* 0x12c */
	PHY_TRSV_REG_CFG_OFFSET(0x4b, 0x10, PWR_MODE_HS_G3_ANY, 0x50),	/* 0x12c */
	PHY_TRSV_REG_CFG_OFFSET(0x4d, 0xd3, PWR_MODE_HS_G1_ANY, 0x50),	/* 0x134 */
	PHY_TRSV_REG_CFG_OFFSET(0x4d, 0x73, PWR_MODE_HS_G2_ANY, 0x50),	/* 0x134 */
	PHY_TRSV_REG_CFG_OFFSET(0x4d, 0x63, PWR_MODE_HS_G3_ANY, 0x50),	/* 0x134 */
	END_UFS_PHY_CFG
};

static const struct samsung_ufs_phy_cfg exynos9610_post_pwr_hs_cfg[] = {
	END_UFS_PHY_CFG
};

/* From downstream post_h8_enter[]/pre_h8_exit[] */
static const struct samsung_ufs_phy_cfg exynos9610_post_h8_enter_cfg[] = {
	PHY_TRSV_REG_CFG_OFFSET(0x31, 0x99, PWR_MODE_ANY, 0x50),	/* 0xc4 */
	PHY_TRSV_REG_CFG_OFFSET(0x3a, 0x7f, PWR_MODE_ANY, 0x50),	/* 0xe8 */
	PHY_COMN_REG_CFG(0x01, 0x02, PWR_MODE_ANY),			/* 0x04 */
	END_UFS_PHY_CFG
};

static const struct samsung_ufs_phy_cfg exynos9610_pre_h8_exit_cfg[] = {
	PHY_COMN_REG_CFG(0x01, 0x00, PWR_MODE_ANY),			/* 0x04 */
	PHY_TRSV_REG_CFG_OFFSET(0x31, 0xd9, PWR_MODE_ANY, 0x50),	/* 0xc4 */
	PHY_TRSV_REG_CFG_OFFSET(0x3a, 0x77, PWR_MODE_ANY, 0x50),	/* 0xe8 */
	END_UFS_PHY_CFG
};

static const struct samsung_ufs_phy_cfg *exynos9610_ufs_phy_cfgs[CFG_TAG_MAX] = {
	[CFG_PRE_INIT]		= exynos9610_pre_init_cfg,
	[CFG_PRE_PWR_HS]	= exynos9610_pre_pwr_hs_cfg,
	[CFG_POST_PWR_HS]	= exynos9610_post_pwr_hs_cfg,
};

static const struct samsung_ufs_phy_cfg *exynos9610_ufs_phy_cfgs_hibern8[] = {
	[CFG_POST_HIBERN8_ENTER]	= exynos9610_post_h8_enter_cfg,
	[CFG_PRE_HIBERN8_EXIT]		= exynos9610_pre_h8_exit_cfg,
};

static const char * const exynos9610_ufs_phy_clks[] = {
	"tx0_symbol_clk", "rx0_symbol_clk", "rx1_symbol_clk", "ref_clk",
};

const struct samsung_ufs_phy_drvdata exynos9610_ufs_phy = {
	.cfgs = exynos9610_ufs_phy_cfgs,
	.cfgs_hibern8 = exynos9610_ufs_phy_cfgs_hibern8,
	.isol = {
		.offset = EXYNOS9610_EMBEDDED_COMBO_PHY_CTRL,
		.mask = EXYNOS9610_EMBEDDED_COMBO_PHY_CTRL_MASK,
		.en = EXYNOS9610_EMBEDDED_COMBO_PHY_CTRL_EN,
	},
	.clk_list = exynos9610_ufs_phy_clks,
	.num_clks = ARRAY_SIZE(exynos9610_ufs_phy_clks),
	.cdr_lock_status_offset = EXYNOS9610_EMBEDDED_COMBO_PHY_CDR_LOCK_STATUS,
	.wait_for_cdr = samsung_ufs_phy_wait_for_lock_acq,
};
