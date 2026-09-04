// SPDX-License-Identifier: GPL-2.0-only
/*
 * Common Clock Framework support for Exynos9610 SoC.
 *
 * Register data mechanically extracted from the downstream CMUCAL tables
 * (erfanoabdi/android_kernel_samsung_universal9610, lineage-17.1,
 * drivers/soc/samsung/cal-if/exynos9610/{cmucal-sfr,cmucal-node,cmucal-qch}.c),
 * covering every CMU domain on the SoC.
 *
 * NONE of this has been verified against real hardware yet (no clk_summary or
 * register read-back pulled from a running Exynos9610 device -- a real one is
 * available for this effort, just not yet used for this). The Q-Channel
 * register layout (ENABLE at bit 0) matches what was independently confirmed
 * across three separate Exynos9810 downstream trees earlier in this effort,
 * which is reassuring but not itself Exynos9610-hardware verification. Gate
 * flags (CLK_IS_CRITICAL vs 0) are a conservative first pass: only CMU
 * self-access PCLKs and the GIC clock are marked critical, matching the
 * pattern used for Exynos8895/990/9810 in this tree.
 *
 * PLL types pll_1054x/1061x were added to clk-pll.c/clk-pll.h in a companion
 * change after confirming their P/M/S/(K)/ENABLE/STABLE bit layout is
 * identical to the already-supported pll_1017x/pll_1031x respectively.
 * pll_1050x/1051x/1052x were already supported (added for Exynos9810/8895).
 * The PLL_35XX_RATE() macro's build-time PLL_VALID_RATE check independently
 * re-derives each rate from its m/p/s divider and fails the build if it
 * doesn't match what's written here -- this generator recomputes the exact
 * rate from the divider values rather than trusting the downstream table's
 * (sometimes only approximate) rate label, for the same reason it was needed
 * for Exynos9810: several downstream rate-table entries don't survive an
 * exact-match check as written.
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/samsung,exynos9610.h>

#include "clk.h"
#include "clk-pll.h"
#include "clk-exynos-arm64.h"

/* NOTE: Must be equal to the last clock ID for this CMU, increased by one. */
#define CLKS_NR_APM	(CLK_CLK_DLL_DCO + 1)
#define CLKS_NR_CAM	(CLK_GOUT_CAM_is6p10p0_CAM_PCLK_PGEN_LITE_CAM1 + 1)
#define CLKS_NR_CMGP	(CLK_GOUT_CMGP_SYSREG_CMGP2PMU_SHUB_PCLK + 1)
#define CLKS_NR_TOP	(CLK_O_USB30_PIPE_PCLK_1 + 1)
#define CLKS_NR_CORE	(CLK_GOUT_CORE_AD_APB_PGEN_PDMA_PCLKM + 1)
#define CLKS_NR_CPUCL0	(CLK_GOUT_CPUCL0_LHM_AXI_P_CPUCL0_I_CLK + 1)
#define CLKS_NR_CPUCL1	(CLK_GOUT_CPUCL1_LHS_ACE_D_CPUCL1_I_CLK + 1)
#define CLKS_NR_DISPAUD	(CLK_TICK_USB + 1)
#define CLKS_NR_FSYS	(CLK_GOUT_FSYS_UFS_EMBD_I_CLK_UNIPRO + 1)
#define CLKS_NR_G2D	(CLK_GOUT_G2D_BTM_G2D_I_PCLK + 1)
#define CLKS_NR_G3D	(CLK_GOUT_G3D_LHS_AXI_D_G3D_I_CLK + 1)
#define CLKS_NR_ISP	(CLK_GOUT_ISP_BTM_ISP1_I_PCLK + 1)
#define CLKS_NR_MFC	(CLK_GOUT_MFC_RSTnSYNC_CLK_MFC_WFD_SW_RESET_CLK + 1)
#define CLKS_NR_MIF	(CLK_GOUT_MIF_LHM_AXI_D_MIF_RT_I_CLK + 1)
#define CLKS_NR_MIF1	(CLK_GOUT_MIF1_LHM_AXI_D_MIF1_RT_I_CLK + 1)
#define CLKS_NR_PERI	(CLK_GOUT_PERI_USI00_USI_IPCLK + 1)
#define CLKS_NR_SHUB	(CLK_RTCCLK_SHUB__ALV + 1)
#define CLKS_NR_USB	(CLK_GOUT_USB_LHS_ACEL_D_USB_I_CLK + 1)
#define CLKS_NR_VIPX1	(CLK_GOUT_VIPX1_LHS_AXI_P_VIPX1_LOCAL_I_CLK + 1)
#define CLKS_NR_VIPX2	(CLK_GOUT_VIPX2_RSTnSYNC_CLK_VIPX2_OSCCLK_CLK + 1)

/* ---- CMU_APM ---------------------------------------------------------*/

/* Register Offset definitions for CMU_APM (0x11800000) */
#define MUX_CLKCMU_APM_BUS_USER			0x0100
#define MUX_DLL_USER			0x0120
#define MUX_CLKCMU_SHUB_BUS			0x1000
#define MUX_CLK_APM_BUS			0x1004
#define CLKCMU_SHUB_BUS			0x1800
#define DIV_CLK_APM_BUS			0x1804
#define CLKCMU_CMGP_BUS			0x2000
#define CLK_BLK_APM_UID_APM_CMU_APM_IPCLKPORT_PCLK	0x2004
#define CLK_BLK_APM_UID_RSTnSYNC_CLK_APM_OSCCLK_IPCLKPORT_CLK	0x2008
#define CLK_BLK_APM_UID_RSTnSYNC_CLK_APM_OSCCLK_RCO_IPCLKPORT_CLK	0x200c
#define GATE_CLKCMU_SHUB_BUS			0x2010
#define GOUT_BLK_APM_UID_APBIF_GPIO_ALIVE_IPCLKPORT_PCLK	0x2014
#define GOUT_BLK_APM_UID_APBIF_PMU_ALIVE_IPCLKPORT_PCLK	0x2018
#define GOUT_BLK_APM_UID_APBIF_RTC_IPCLKPORT_PCLK	0x201c
#define GOUT_BLK_APM_UID_APBIF_TOP_RTC_IPCLKPORT_PCLK	0x2020
#define GOUT_BLK_APM_UID_GREBEINTEGRATION_IPCLKPORT_HCLK	0x2024
#define GOUT_BLK_APM_UID_INTMEM_IPCLKPORT_ACLK			0x2028
#define GOUT_BLK_APM_UID_INTMEM_IPCLKPORT_PCLK			0x202c
#define GOUT_BLK_APM_UID_LHM_AXI_P_APM_GNSS_IPCLKPORT_I_CLK	0x2030
#define GOUT_BLK_APM_UID_LHM_AXI_P_APM_IPCLKPORT_I_CLK	0x2034
#define GOUT_BLK_APM_UID_LHM_AXI_P_APM_MODEM_IPCLKPORT_I_CLK	0x2038
#define GOUT_BLK_APM_UID_LHM_AXI_P_APM_SHUB_IPCLKPORT_I_CLK	0x203c
#define GOUT_BLK_APM_UID_LHM_AXI_P_APM_WLBT_IPCLKPORT_I_CLK	0x2040
#define GOUT_BLK_APM_UID_LHS_AXI_D_APM_IPCLKPORT_I_CLK	0x2044
#define GOUT_BLK_APM_UID_LHS_AXI_LP_SHUB_IPCLKPORT_I_CLK	0x2048
#define GOUT_BLK_APM_UID_MAILBOX_AP2CP_IPCLKPORT_PCLK	0x204c
#define GOUT_BLK_APM_UID_MAILBOX_AP2CP_S_IPCLKPORT_PCLK	0x2050
#define GOUT_BLK_APM_UID_MAILBOX_AP2GNSS_IPCLKPORT_PCLK	0x2054
#define GOUT_BLK_APM_UID_MAILBOX_AP2SHUB_IPCLKPORT_PCLK	0x2058
#define GOUT_BLK_APM_UID_MAILBOX_AP2WLBT_IPCLKPORT_PCLK	0x205c
#define GOUT_BLK_APM_UID_MAILBOX_APM2AP_IPCLKPORT_PCLK	0x2060
#define GOUT_BLK_APM_UID_MAILBOX_APM2CP_IPCLKPORT_PCLK	0x2064
#define GOUT_BLK_APM_UID_MAILBOX_APM2GNSS_IPCLKPORT_PCLK	0x2068
#define GOUT_BLK_APM_UID_MAILBOX_APM2SHUB_IPCLKPORT_PCLK	0x206c
#define GOUT_BLK_APM_UID_MAILBOX_APM2WLBT_IPCLKPORT_PCLK	0x2070
#define GOUT_BLK_APM_UID_MAILBOX_CP2GNSS_IPCLKPORT_PCLK	0x2074
#define GOUT_BLK_APM_UID_MAILBOX_CP2SHUB_IPCLKPORT_PCLK	0x2078
#define GOUT_BLK_APM_UID_MAILBOX_CP2WLBT_IPCLKPORT_PCLK	0x207c
#define GOUT_BLK_APM_UID_MAILBOX_SHUB2GNSS_IPCLKPORT_PCLK	0x2080
#define GOUT_BLK_APM_UID_MAILBOX_SHUB2WLBT_IPCLKPORT_PCLK	0x2084
#define GOUT_BLK_APM_UID_MAILBOX_WLBT2ABOX_IPCLKPORT_PCLK	0x2088
#define GOUT_BLK_APM_UID_MAILBOX_WLBT2GNSS_IPCLKPORT_PCLK	0x208c
#define GOUT_BLK_APM_UID_PEM_IPCLKPORT_I_CLK			0x2090
#define GOUT_BLK_APM_UID_PGEN_LITE_APM_IPCLKPORT_CLK	0x2094
#define GOUT_BLK_APM_UID_PMU_INTR_GEN_IPCLKPORT_PCLK	0x2098
#define GOUT_BLK_APM_UID_RSTnSYNC_CLK_APM_BUS_IPCLKPORT_CLK	0x209c
#define GOUT_BLK_APM_UID_RSTnSYNC_CLK_APM_GREBE_IPCLKPORT_CLK	0x20a0
#define GOUT_BLK_APM_UID_SPEEDY_APM_IPCLKPORT_PCLK	0x20a4
#define GOUT_BLK_APM_UID_SYSREG_APM_IPCLKPORT_PCLK	0x20a8
#define GOUT_BLK_APM_UID_WDT_APM_IPCLKPORT_PCLK			0x20ac
#define GOUT_BLK_APM_UID_XIU_DP_APM_IPCLKPORT_ACLK	0x20b0

static const unsigned long apm_clk_regs[] __initconst = {
	MUX_CLKCMU_APM_BUS_USER,
	MUX_DLL_USER,
	MUX_CLKCMU_SHUB_BUS,
	MUX_CLK_APM_BUS,
	CLKCMU_SHUB_BUS,
	DIV_CLK_APM_BUS,
	CLKCMU_CMGP_BUS,
	CLK_BLK_APM_UID_APM_CMU_APM_IPCLKPORT_PCLK,
	CLK_BLK_APM_UID_RSTnSYNC_CLK_APM_OSCCLK_IPCLKPORT_CLK,
	CLK_BLK_APM_UID_RSTnSYNC_CLK_APM_OSCCLK_RCO_IPCLKPORT_CLK,
	GATE_CLKCMU_SHUB_BUS,
	GOUT_BLK_APM_UID_APBIF_GPIO_ALIVE_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_APBIF_PMU_ALIVE_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_APBIF_RTC_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_APBIF_TOP_RTC_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_GREBEINTEGRATION_IPCLKPORT_HCLK,
	GOUT_BLK_APM_UID_INTMEM_IPCLKPORT_ACLK,
	GOUT_BLK_APM_UID_INTMEM_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_LHM_AXI_P_APM_GNSS_IPCLKPORT_I_CLK,
	GOUT_BLK_APM_UID_LHM_AXI_P_APM_IPCLKPORT_I_CLK,
	GOUT_BLK_APM_UID_LHM_AXI_P_APM_MODEM_IPCLKPORT_I_CLK,
	GOUT_BLK_APM_UID_LHM_AXI_P_APM_SHUB_IPCLKPORT_I_CLK,
	GOUT_BLK_APM_UID_LHM_AXI_P_APM_WLBT_IPCLKPORT_I_CLK,
	GOUT_BLK_APM_UID_LHS_AXI_D_APM_IPCLKPORT_I_CLK,
	GOUT_BLK_APM_UID_LHS_AXI_LP_SHUB_IPCLKPORT_I_CLK,
	GOUT_BLK_APM_UID_MAILBOX_AP2CP_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_AP2CP_S_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_AP2GNSS_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_AP2SHUB_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_AP2WLBT_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_APM2AP_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_APM2CP_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_APM2GNSS_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_APM2SHUB_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_APM2WLBT_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_CP2GNSS_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_CP2SHUB_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_CP2WLBT_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_SHUB2GNSS_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_SHUB2WLBT_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_WLBT2ABOX_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_WLBT2GNSS_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_PEM_IPCLKPORT_I_CLK,
	GOUT_BLK_APM_UID_PGEN_LITE_APM_IPCLKPORT_CLK,
	GOUT_BLK_APM_UID_PMU_INTR_GEN_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_RSTnSYNC_CLK_APM_BUS_IPCLKPORT_CLK,
	GOUT_BLK_APM_UID_RSTnSYNC_CLK_APM_GREBE_IPCLKPORT_CLK,
	GOUT_BLK_APM_UID_SPEEDY_APM_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_SYSREG_APM_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_WDT_APM_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_XIU_DP_APM_IPCLKPORT_ACLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long apm_qch_regs[] __initconst = {
	0x301c,	/* APBIF_GPIO_ALIVE_QCH */
	0x3020,	/* APBIF_PMU_ALIVE_QCH */
	0x3024,	/* APBIF_RTC_QCH */
	0x3028,	/* APBIF_TOP_RTC_QCH */
	0x302c,	/* APM_CMU_APM_QCH */
	0x3030,	/* GREBEINTEGRATION_QCH_DBG */
	0x3034,	/* GREBEINTEGRATION_QCH_GREBE */
	0x3038,	/* INTMEM_QCH */
	0x303c,	/* LHM_AXI_P_APM_GNSS_QCH */
	0x3040,	/* LHM_AXI_P_APM_MODEM_QCH */
	0x3044,	/* LHM_AXI_P_APM_QCH */
	0x3048,	/* LHM_AXI_P_APM_SHUB_QCH */
	0x304c,	/* LHM_AXI_P_APM_WLBT_QCH */
	0x3050,	/* LHS_AXI_D_APM_QCH */
	0x3054,	/* LHS_AXI_LP_SHUB_QCH */
	0x3058,	/* MAILBOX_AP2CP_QCH */
	0x305c,	/* MAILBOX_AP2CP_S_QCH */
	0x3060,	/* MAILBOX_AP2GNSS_QCH */
	0x3064,	/* MAILBOX_AP2SHUB_QCH */
	0x3068,	/* MAILBOX_AP2WLBT_QCH */
	0x306c,	/* MAILBOX_APM2AP_QCH */
	0x3070,	/* MAILBOX_APM2CP_QCH */
	0x3074,	/* MAILBOX_APM2GNSS_QCH */
	0x3078,	/* MAILBOX_APM2SHUB_QCH */
	0x307c,	/* MAILBOX_APM2WLBT_QCH */
	0x3080,	/* MAILBOX_CP2GNSS_QCH */
	0x3084,	/* MAILBOX_CP2SHUB_QCH */
	0x3088,	/* MAILBOX_CP2WLBT_QCH */
	0x308c,	/* MAILBOX_SHUB2GNSS_QCH */
	0x3090,	/* MAILBOX_SHUB2WLBT_QCH */
	0x3094,	/* MAILBOX_WLBT2ABOX_QCH */
	0x3098,	/* MAILBOX_WLBT2GNSS_QCH */
	0x309c,	/* PEM_QCH */
	0x30a0,	/* PGEN_LITE_APM_QCH */
	0x30a4,	/* PMU_INTR_GEN_QCH */
	0x30a8,	/* RSTNSYNC_CLK_APM_GREBE_QCH */
	0x30ac,	/* SPEEDY_APM_QCH */
	0x30b0,	/* SYSREG_APM_QCH */
	0x30b4,	/* WDT_APM_QCH */
};

static const struct samsung_fixed_rate_clock apm_fixed_clks[] __initconst = {
	FRATE(CLK_CLK_DLL_DCO, "clk_dll_dco", NULL, 0, 104005000),
};

/* List of parent clocks for Muxes in CMU_APM */
PNAME(mout_apm_bus_p) = { "mout_cmu_apm_bus_user", "mout_dll_user" };
PNAME(mout_cmu_shub_bus_p) = { "mout_cmu_apm_bus_user", "mout_dll_user" };
PNAME(mout_cmu_apm_bus_user_p) = { "oscclk", "dout_clkcmu_apm_bus" };
PNAME(mout_dll_user_p) = { "oscclk", "clk_dll_dco" };

static const struct samsung_mux_clock apm_mux_clks[] __initconst = {
	MUX(CLK_MOUT_APM_BUS, "mout_apm_bus", mout_apm_bus_p,
	    MUX_CLK_APM_BUS, 0, 1),
	MUX(CLK_MOUT_CMU_SHUB_BUS, "mout_cmu_shub_bus", mout_cmu_shub_bus_p,
	    MUX_CLKCMU_SHUB_BUS, 0, 1),
	MUX(CLK_MOUT_CMU_APM_BUS_USER, "mout_cmu_apm_bus_user", mout_cmu_apm_bus_user_p,
	    MUX_CLKCMU_APM_BUS_USER, 4, 1),
	MUX(CLK_MOUT_DLL_USER, "mout_dll_user", mout_dll_user_p,
	    MUX_DLL_USER, 4, 1),
};

static const struct samsung_div_clock apm_div_clks[] __initconst = {
	DIV(CLK_DOUT_APM_BUS, "dout_apm_bus", "mout_apm_bus",
	    DIV_CLK_APM_BUS, 0, 3),
	DIV(CLK_DOUT_CLKCMU_SHUB_BUS, "dout_clkcmu_shub_bus", "gout_clkcmu_shub_bus",
	    CLKCMU_SHUB_BUS, 0, 3),
};

static const struct samsung_gate_clock apm_gate_clks[] __initconst = {
	GATE(CLK_GOUT_APM_LHM_AXI_P_APM_I_CLK, "gout_apm_lhm_axi_p_apm_i_clk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_LHM_AXI_P_APM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_LHS_AXI_D_APM_I_CLK, "gout_apm_lhs_axi_d_apm_i_clk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_LHS_AXI_D_APM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_APM2AP_PCLK, "gout_apm_mailbox_apm2ap_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_APM2AP_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_APM2CP_PCLK, "gout_apm_mailbox_apm2cp_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_APM2CP_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_APM2GNSS_PCLK, "gout_apm_mailbox_apm2gnss_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_APM2GNSS_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_APM2WLBT_PCLK, "gout_apm_mailbox_apm2wlbt_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_APM2WLBT_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_SYSREG_APM_PCLK, "gout_apm_sysreg_apm_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_SYSREG_APM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_RSTnSYNC_CLK_APM_BUS_CLK, "gout_apm_rstnsync_clk_apm_bus_clk",
	     "dout_apm_bus",
	     GOUT_BLK_APM_UID_RSTnSYNC_CLK_APM_BUS_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_APBIF_PMU_ALIVE_PCLK, "gout_apm_apbif_pmu_alive_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_APBIF_PMU_ALIVE_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_APBIF_GPIO_ALIVE_PCLK, "gout_apm_apbif_gpio_alive_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_APBIF_GPIO_ALIVE_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_APM_CMU_APM_PCLK, "gout_apm_apm_cmu_apm_pclk", "dout_apm_bus",
	     CLK_BLK_APM_UID_APM_CMU_APM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_SHUB_BUS, "gout_clkcmu_shub_bus", "mout_cmu_shub_bus",
	     GATE_CLKCMU_SHUB_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_CMGP_BUS, "gout_clkcmu_cmgp_bus", "dout_apm_bus",
	     CLKCMU_CMGP_BUS, 21, 0, 0),
	GATE(CLK_GOUT_APM_PEM_I_CLK, "gout_apm_pem_i_clk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_PEM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_APBIF_RTC_PCLK, "gout_apm_apbif_rtc_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_APBIF_RTC_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_APBIF_TOP_RTC_PCLK, "gout_apm_apbif_top_rtc_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_APBIF_TOP_RTC_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_INTMEM_ACLK, "gout_apm_intmem_aclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_INTMEM_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_INTMEM_PCLK, "gout_apm_intmem_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_INTMEM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_LHM_AXI_P_APM_SHUB_I_CLK, "gout_apm_lhm_axi_p_apm_shub_i_clk",
	     "dout_apm_bus",
	     GOUT_BLK_APM_UID_LHM_AXI_P_APM_SHUB_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_LHM_AXI_P_APM_MODEM_I_CLK, "gout_apm_lhm_axi_p_apm_modem_i_clk",
	     "dout_apm_bus",
	     GOUT_BLK_APM_UID_LHM_AXI_P_APM_MODEM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_LHM_AXI_P_APM_GNSS_I_CLK, "gout_apm_lhm_axi_p_apm_gnss_i_clk",
	     "dout_apm_bus",
	     GOUT_BLK_APM_UID_LHM_AXI_P_APM_GNSS_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_LHS_AXI_LP_SHUB_I_CLK, "gout_apm_lhs_axi_lp_shub_i_clk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_LHS_AXI_LP_SHUB_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_AP2CP_PCLK, "gout_apm_mailbox_ap2cp_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_AP2CP_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_AP2CP_S_PCLK, "gout_apm_mailbox_ap2cp_s_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_AP2CP_S_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_AP2GNSS_PCLK, "gout_apm_mailbox_ap2gnss_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_AP2GNSS_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_AP2SHUB_PCLK, "gout_apm_mailbox_ap2shub_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_AP2SHUB_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_AP2WLBT_PCLK, "gout_apm_mailbox_ap2wlbt_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_AP2WLBT_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_APM2SHUB_PCLK, "gout_apm_mailbox_apm2shub_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_APM2SHUB_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_WLBT2GNSS_PCLK, "gout_apm_mailbox_wlbt2gnss_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_WLBT2GNSS_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_SHUB2GNSS_PCLK, "gout_apm_mailbox_shub2gnss_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_SHUB2GNSS_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_WLBT2ABOX_PCLK, "gout_apm_mailbox_wlbt2abox_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_WLBT2ABOX_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_PGEN_LITE_APM_CLK, "gout_apm_pgen_lite_apm_clk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_PGEN_LITE_APM_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_PMU_INTR_GEN_PCLK, "gout_apm_pmu_intr_gen_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_PMU_INTR_GEN_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_LHM_AXI_P_APM_WLBT_I_CLK, "gout_apm_lhm_axi_p_apm_wlbt_i_clk",
	     "dout_apm_bus",
	     GOUT_BLK_APM_UID_LHM_AXI_P_APM_WLBT_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_XIU_DP_APM_ACLK, "gout_apm_xiu_dp_apm_aclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_XIU_DP_APM_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_WDT_APM_PCLK, "gout_apm_wdt_apm_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_WDT_APM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_SHUB2WLBT_PCLK, "gout_apm_mailbox_shub2wlbt_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_SHUB2WLBT_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_CP2GNSS_PCLK, "gout_apm_mailbox_cp2gnss_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_CP2GNSS_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_CP2SHUB_PCLK, "gout_apm_mailbox_cp2shub_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_CP2SHUB_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_CP2WLBT_PCLK, "gout_apm_mailbox_cp2wlbt_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_CP2WLBT_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_SPEEDY_APM_PCLK, "gout_apm_speedy_apm_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_SPEEDY_APM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_RSTnSYNC_CLK_APM_GREBE_CLK, "gout_apm_rstnsync_clk_apm_grebe_clk",
	     "dout_apm_bus",
	     GOUT_BLK_APM_UID_RSTnSYNC_CLK_APM_GREBE_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_GREBEINTEGRATION_HCLK, "gout_apm_grebeintegration_hclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_GREBEINTEGRATION_IPCLKPORT_HCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_RSTnSYNC_CLK_APM_OSCCLK_CLK, "gout_apm_rstnsync_clk_apm_oscclk_clk",
	     "oscclk",
	     CLK_BLK_APM_UID_RSTnSYNC_CLK_APM_OSCCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_RSTnSYNC_CLK_APM_OSCCLK_RCO_CLK, "gout_apm_rstnsync_clk_apm_oscclk_rco_clk",
	     "oscclk",
	     CLK_BLK_APM_UID_RSTnSYNC_CLK_APM_OSCCLK_RCO_IPCLKPORT_CLK, 21, 0, 0),
};

static const struct samsung_cmu_info apm_cmu_info __initconst = {
	.mux_clks		= apm_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(apm_mux_clks),
	.div_clks		= apm_div_clks,
	.nr_div_clks		= ARRAY_SIZE(apm_div_clks),
	.gate_clks		= apm_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(apm_gate_clks),
	.fixed_clks		= apm_fixed_clks,
	.nr_fixed_clks		= ARRAY_SIZE(apm_fixed_clks),
	.nr_clk_ids		= CLKS_NR_APM,
	.clk_regs		= apm_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(apm_clk_regs),
	.qch_regs		= apm_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(apm_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9610_cmu_apm_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &apm_cmu_info);
}

CLK_OF_DECLARE(exynos9610_cmu_apm, "samsung,exynos9610-cmu-apm",
	       exynos9610_cmu_apm_init);

/* ---- CMU_CAM ---------------------------------------------------------*/

/* Register Offset definitions for CMU_CAM (0x14500000) */
#define MUX_CLKCMU_CAM_BUS_USER			0x0100
#define DIV_CLK_CAM_BUSP			0x1800
#define CLK_BLK_CAM_UID_CAM_CMU_CAM_IPCLKPORT_PCLK	0x2000
#define CLK_BLK_CAM_UID_RSTnSYNC_CLK_CAM_OSCCLK_IPCLKPORT_CLK	0x2004
#define GOUT_BLK_CAM_UID_BLK_CAM_IPCLKPORT_CLK_CAM_BUSD	0x2008
#define GOUT_BLK_CAM_UID_BTM_CAM_IPCLKPORT_I_ACLK	0x200c
#define GOUT_BLK_CAM_UID_BTM_CAM_IPCLKPORT_I_PCLK	0x2010
#define GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_3AA	0x2014
#define GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_CSIS0	0x2018
#define GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_CSIS1	0x201c
#define GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_CSIS2	0x2020
#define GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_CSIS3	0x2024
#define GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_DMA	0x2028
#define GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_GLUE_CSIS0	0x202c
#define GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_GLUE_CSIS1	0x2030
#define GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_GLUE_CSIS2	0x2034
#define GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_GLUE_CSIS3	0x2038
#define GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_PAFSTAT_CORE	0x203c
#define GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_PPMU_CAM	0x2040
#define GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_RDMA	0x2044
#define GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_SMMU_CAM	0x2048
#define GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_XIU_D_CAM	0x204c
#define GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_PCLK_PGEN_LITE_CAM0	0x2050
#define GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_PCLK_PGEN_LITE_CAM1	0x2054
#define GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_PCLK_PPMU_CAM	0x2058
#define GOUT_BLK_CAM_UID_LHM_AXI_P_CAM_IPCLKPORT_I_CLK	0x205c
#define GOUT_BLK_CAM_UID_LHS_ACEL_D_CAM_IPCLKPORT_I_CLK	0x2060
#define GOUT_BLK_CAM_UID_LHS_ATB_CAMISP_IPCLKPORT_I_CLK	0x2064
#define GOUT_BLK_CAM_UID_RSTnSYNC_CLK_CAM_BUSD_IPCLKPORT_CLK	0x2068
#define GOUT_BLK_CAM_UID_RSTnSYNC_CLK_CAM_BUSP_IPCLKPORT_CLK	0x206c
#define GOUT_BLK_CAM_UID_SYSREG_CAM_IPCLKPORT_PCLK	0x2070

static const unsigned long cam_clk_regs[] __initconst = {
	MUX_CLKCMU_CAM_BUS_USER,
	DIV_CLK_CAM_BUSP,
	CLK_BLK_CAM_UID_CAM_CMU_CAM_IPCLKPORT_PCLK,
	CLK_BLK_CAM_UID_RSTnSYNC_CLK_CAM_OSCCLK_IPCLKPORT_CLK,
	GOUT_BLK_CAM_UID_BLK_CAM_IPCLKPORT_CLK_CAM_BUSD,
	GOUT_BLK_CAM_UID_BTM_CAM_IPCLKPORT_I_ACLK,
	GOUT_BLK_CAM_UID_BTM_CAM_IPCLKPORT_I_PCLK,
	GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_3AA,
	GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_CSIS0,
	GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_CSIS1,
	GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_CSIS2,
	GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_CSIS3,
	GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_DMA,
	GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_GLUE_CSIS0,
	GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_GLUE_CSIS1,
	GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_GLUE_CSIS2,
	GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_GLUE_CSIS3,
	GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_PAFSTAT_CORE,
	GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_PPMU_CAM,
	GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_RDMA,
	GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_SMMU_CAM,
	GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_XIU_D_CAM,
	GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_PCLK_PGEN_LITE_CAM0,
	GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_PCLK_PGEN_LITE_CAM1,
	GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_PCLK_PPMU_CAM,
	GOUT_BLK_CAM_UID_LHM_AXI_P_CAM_IPCLKPORT_I_CLK,
	GOUT_BLK_CAM_UID_LHS_ACEL_D_CAM_IPCLKPORT_I_CLK,
	GOUT_BLK_CAM_UID_LHS_ATB_CAMISP_IPCLKPORT_I_CLK,
	GOUT_BLK_CAM_UID_RSTnSYNC_CLK_CAM_BUSD_IPCLKPORT_CLK,
	GOUT_BLK_CAM_UID_RSTnSYNC_CLK_CAM_BUSP_IPCLKPORT_CLK,
	GOUT_BLK_CAM_UID_SYSREG_CAM_IPCLKPORT_PCLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long cam_qch_regs[] __initconst = {
	0x300c,	/* BTM_CAM_QCH */
	0x3010,	/* CAM_CMU_CAM_QCH */
	0x3014,	/* IS6P10P0_CAM_QCH_S_CAM0_PGEN_LITE */
	0x3018,	/* IS6P10P0_CAM_QCH_S_CAM1_PGEN_LITE */
	0x301c,	/* IS6P10P0_CAM_QCH_S_CAM_3AA */
	0x3020,	/* IS6P10P0_CAM_QCH_S_CAM_CSIS0 */
	0x3024,	/* IS6P10P0_CAM_QCH_S_CAM_CSIS1 */
	0x3028,	/* IS6P10P0_CAM_QCH_S_CAM_CSIS2 */
	0x302c,	/* IS6P10P0_CAM_QCH_S_CAM_CSIS3 */
	0x3030,	/* IS6P10P0_CAM_QCH_S_CAM_PDP_CORE */
	0x3034,	/* IS6P10P0_CAM_QCH_S_CAM_PDP_DMA */
	0x3038,	/* IS6P10P0_CAM_QCH_S_CAM_PPMU */
	0x303c,	/* IS6P10P0_CAM_QCH_S_CAM_RDMA */
	0x3040,	/* IS6P10P0_CAM_QCH_S_CAM_SMMU */
	0x3044,	/* LHM_AXI_P_CAM_QCH */
	0x3048,	/* LHS_ACEL_D_CAM_QCH */
	0x304c,	/* LHS_ATB_CAMISP_QCH */
	0x3050,	/* SYSREG_CAM_QCH */
};

/* List of parent clocks for Muxes in CMU_CAM */
PNAME(mout_cmu_cam_bus_user_p) = { "oscclk", "dout_clkcmu_cam_bus" };

static const struct samsung_mux_clock cam_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_CAM_BUS_USER, "mout_cmu_cam_bus_user", mout_cmu_cam_bus_user_p,
	    MUX_CLKCMU_CAM_BUS_USER, 4, 1),
};

static const struct samsung_div_clock cam_div_clks[] __initconst = {
	DIV(CLK_DOUT_CAM_BUSP, "dout_cam_busp", "mout_cmu_cam_bus_user",
	    DIV_CLK_CAM_BUSP, 0, 2),
};

static const struct samsung_gate_clock cam_gate_clks[] __initconst = {
	GATE(CLK_GOUT_CAM_CAM_CMU_CAM_PCLK, "gout_cam_cam_cmu_cam_pclk", "dout_cam_busp",
	     CLK_BLK_CAM_UID_CAM_CMU_CAM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CAM_LHM_AXI_P_CAM_I_CLK, "gout_cam_lhm_axi_p_cam_i_clk", "dout_cam_busp",
	     GOUT_BLK_CAM_UID_LHM_AXI_P_CAM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CAM_LHS_ATB_CAMISP_I_CLK, "gout_cam_lhs_atb_camisp_i_clk",
	     "mout_cmu_cam_bus_user",
	     GOUT_BLK_CAM_UID_LHS_ATB_CAMISP_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CAM_LHS_ACEL_D_CAM_I_CLK, "gout_cam_lhs_acel_d_cam_i_clk",
	     "mout_cmu_cam_bus_user",
	     GOUT_BLK_CAM_UID_LHS_ACEL_D_CAM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CAM_RSTnSYNC_CLK_CAM_BUSD_CLK, "gout_cam_rstnsync_clk_cam_busd_clk",
	     "mout_cmu_cam_bus_user",
	     GOUT_BLK_CAM_UID_RSTnSYNC_CLK_CAM_BUSD_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CAM_RSTnSYNC_CLK_CAM_BUSP_CLK, "gout_cam_rstnsync_clk_cam_busp_clk",
	     "dout_cam_busp",
	     GOUT_BLK_CAM_UID_RSTnSYNC_CLK_CAM_BUSP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CAM_RSTnSYNC_CLK_CAM_OSCCLK_CLK, "gout_cam_rstnsync_clk_cam_oscclk_clk",
	     "oscclk",
	     CLK_BLK_CAM_UID_RSTnSYNC_CLK_CAM_OSCCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CAM_is6p10p0_CAM_ACLK_XIU_D_CAM, "gout_cam_is6p10p0_cam_aclk_xiu_d_cam",
	     "mout_cmu_cam_bus_user",
	     GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_XIU_D_CAM, 21, 0, 0),
	GATE(CLK_GOUT_CAM_is6p10p0_CAM_PCLK_PPMU_CAM, "gout_cam_is6p10p0_cam_pclk_ppmu_cam",
	     "dout_cam_busp",
	     GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_PCLK_PPMU_CAM, 21, 0, 0),
	GATE(CLK_GOUT_CAM_is6p10p0_CAM_ACLK_PPMU_CAM, "gout_cam_is6p10p0_cam_aclk_ppmu_cam",
	     "mout_cmu_cam_bus_user",
	     GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_PPMU_CAM, 21, 0, 0),
	GATE(CLK_GOUT_CAM_is6p10p0_CAM_ACLK_SMMU_CAM, "gout_cam_is6p10p0_cam_aclk_smmu_cam",
	     "mout_cmu_cam_bus_user",
	     GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_SMMU_CAM, 21, 0, 0),
	GATE(CLK_GOUT_CAM_is6p10p0_CAM_ACLK_3AA, "gout_cam_is6p10p0_cam_aclk_3aa",
	     "mout_cmu_cam_bus_user",
	     GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_3AA, 21, 0, 0),
	GATE(CLK_GOUT_CAM_is6p10p0_CAM_ACLK_CSIS0, "gout_cam_is6p10p0_cam_aclk_csis0",
	     "mout_cmu_cam_bus_user",
	     GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_CSIS0, 21, 0, 0),
	GATE(CLK_GOUT_CAM_is6p10p0_CAM_ACLK_CSIS1, "gout_cam_is6p10p0_cam_aclk_csis1",
	     "mout_cmu_cam_bus_user",
	     GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_CSIS1, 21, 0, 0),
	GATE(CLK_GOUT_CAM_is6p10p0_CAM_ACLK_CSIS2, "gout_cam_is6p10p0_cam_aclk_csis2",
	     "mout_cmu_cam_bus_user",
	     GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_CSIS2, 21, 0, 0),
	GATE(CLK_GOUT_CAM_is6p10p0_CAM_ACLK_CSIS3, "gout_cam_is6p10p0_cam_aclk_csis3",
	     "mout_cmu_cam_bus_user",
	     GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_CSIS3, 21, 0, 0),
	GATE(CLK_GOUT_CAM_BLK_CAM_CLK_CAM_BUSD, "gout_cam_blk_cam_clk_cam_busd",
	     "mout_cmu_cam_bus_user",
	     GOUT_BLK_CAM_UID_BLK_CAM_IPCLKPORT_CLK_CAM_BUSD, 21, 0, 0),
	GATE(CLK_GOUT_CAM_SYSREG_CAM_PCLK, "gout_cam_sysreg_cam_pclk", "dout_cam_busp",
	     GOUT_BLK_CAM_UID_SYSREG_CAM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CAM_is6p10p0_CAM_PCLK_PGEN_LITE_CAM0, "gout_cam_is6p10p0_cam_pclk_pgen_lite_cam0",
	     "dout_cam_busp",
	     GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_PCLK_PGEN_LITE_CAM0, 21, 0, 0),
	GATE(CLK_GOUT_CAM_is6p10p0_CAM_ACLK_DMA, "gout_cam_is6p10p0_cam_aclk_dma",
	     "mout_cmu_cam_bus_user",
	     GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_DMA, 21, 0, 0),
	GATE(CLK_GOUT_CAM_is6p10p0_CAM_ACLK_PAFSTAT_CORE, "gout_cam_is6p10p0_cam_aclk_pafstat_core",
	     "mout_cmu_cam_bus_user",
	     GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_PAFSTAT_CORE, 21, 0, 0),
	GATE(CLK_GOUT_CAM_is6p10p0_CAM_ACLK_RDMA, "gout_cam_is6p10p0_cam_aclk_rdma",
	     "mout_cmu_cam_bus_user",
	     GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_RDMA, 21, 0, 0),
	GATE(CLK_GOUT_CAM_BTM_CAM_I_ACLK, "gout_cam_btm_cam_i_aclk", "mout_cmu_cam_bus_user",
	     GOUT_BLK_CAM_UID_BTM_CAM_IPCLKPORT_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_CAM_BTM_CAM_I_PCLK, "gout_cam_btm_cam_i_pclk", "dout_cam_busp",
	     GOUT_BLK_CAM_UID_BTM_CAM_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CAM_is6p10p0_CAM_ACLK_GLUE_CSIS0, "gout_cam_is6p10p0_cam_aclk_glue_csis0",
	     "mout_cmu_cam_bus_user",
	     GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_GLUE_CSIS0, 21, 0, 0),
	GATE(CLK_GOUT_CAM_is6p10p0_CAM_ACLK_GLUE_CSIS1, "gout_cam_is6p10p0_cam_aclk_glue_csis1",
	     "mout_cmu_cam_bus_user",
	     GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_GLUE_CSIS1, 21, 0, 0),
	GATE(CLK_GOUT_CAM_is6p10p0_CAM_ACLK_GLUE_CSIS2, "gout_cam_is6p10p0_cam_aclk_glue_csis2",
	     "mout_cmu_cam_bus_user",
	     GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_GLUE_CSIS2, 21, 0, 0),
	GATE(CLK_GOUT_CAM_is6p10p0_CAM_ACLK_GLUE_CSIS3, "gout_cam_is6p10p0_cam_aclk_glue_csis3",
	     "mout_cmu_cam_bus_user",
	     GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_ACLK_GLUE_CSIS3, 21, 0, 0),
	GATE(CLK_GOUT_CAM_is6p10p0_CAM_PCLK_PGEN_LITE_CAM1, "gout_cam_is6p10p0_cam_pclk_pgen_lite_cam1",
	     "dout_cam_busp",
	     GOUT_BLK_CAM_UID_is6p10p0_CAM_IPCLKPORT_PCLK_PGEN_LITE_CAM1, 21, 0, 0),
};

static const struct samsung_cmu_info cam_cmu_info __initconst = {
	.mux_clks		= cam_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(cam_mux_clks),
	.div_clks		= cam_div_clks,
	.nr_div_clks		= ARRAY_SIZE(cam_div_clks),
	.gate_clks		= cam_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(cam_gate_clks),
	.nr_clk_ids		= CLKS_NR_CAM,
	.clk_regs		= cam_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cam_clk_regs),
	.qch_regs		= cam_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(cam_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9610_cmu_cam_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &cam_cmu_info);
}

CLK_OF_DECLARE(exynos9610_cmu_cam, "samsung,exynos9610-cmu-cam",
	       exynos9610_cmu_cam_init);

/* ---- CMU_CMGP --------------------------------------------------------*/

/* Register Offset definitions for CMU_CMGP (0x11c00000) */
#define MUX_CLK_CMGP_ADC			0x1000
#define MUX_CLK_CMGP_I2C			0x1004
#define MUX_CLK_CMGP_USI00			0x1008
#define MUX_CLK_CMGP_USI01			0x100c
#define MUX_CLK_CMGP_USI02			0x1010
#define MUX_CLK_CMGP_USI03			0x1014
#define MUX_CLK_CMGP_USI04			0x1018
#define DIV_CLK_CMGP_ADC			0x1800
#define DIV_CLK_CMGP_I2C			0x1804
#define DIV_CLK_CMGP_USI00			0x1808
#define DIV_CLK_CMGP_USI01			0x180c
#define DIV_CLK_CMGP_USI02			0x1810
#define DIV_CLK_CMGP_USI03			0x1814
#define DIV_CLK_CMGP_USI04			0x1818
#define CLK_BLK_CMGP_UID_CMGP_CMU_CMGP_IPCLKPORT_PCLK	0x2004
#define CLK_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_OSCCLK_RCO_IPCLKPORT_CLK	0x2008
#define GOUT_BLK_CMGP_UID_ADC_CMGP_IPCLKPORT_PCLK_S0	0x200c
#define GOUT_BLK_CMGP_UID_ADC_CMGP_IPCLKPORT_PCLK_S1	0x2010
#define GOUT_BLK_CMGP_UID_GPIO_CMGP_IPCLKPORT_PCLK	0x2014
#define GOUT_BLK_CMGP_UID_I2C_CMGP00_IPCLKPORT_IPCLK	0x2018
#define GOUT_BLK_CMGP_UID_I2C_CMGP00_IPCLKPORT_PCLK	0x201c
#define GOUT_BLK_CMGP_UID_I2C_CMGP01_IPCLKPORT_IPCLK	0x2020
#define GOUT_BLK_CMGP_UID_I2C_CMGP01_IPCLKPORT_PCLK	0x2024
#define GOUT_BLK_CMGP_UID_I2C_CMGP02_IPCLKPORT_IPCLK	0x2028
#define GOUT_BLK_CMGP_UID_I2C_CMGP02_IPCLKPORT_PCLK	0x202c
#define GOUT_BLK_CMGP_UID_I2C_CMGP03_IPCLKPORT_IPCLK	0x2030
#define GOUT_BLK_CMGP_UID_I2C_CMGP03_IPCLKPORT_PCLK	0x2034
#define GOUT_BLK_CMGP_UID_I2C_CMGP04_IPCLKPORT_IPCLK	0x2038
#define GOUT_BLK_CMGP_UID_I2C_CMGP04_IPCLKPORT_PCLK	0x203c
#define GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_BUS_IPCLKPORT_CLK	0x2040
#define GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_I2C_IPCLKPORT_CLK	0x2044
#define GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI00_IPCLKPORT_CLK	0x2048
#define GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI01_IPCLKPORT_CLK	0x204c
#define GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI02_IPCLKPORT_CLK	0x2050
#define GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI03_IPCLKPORT_CLK	0x2054
#define GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI04_IPCLKPORT_CLK	0x2058
#define GOUT_BLK_CMGP_UID_SYSREG_CMGP2CP_IPCLKPORT_PCLK	0x205c
#define GOUT_BLK_CMGP_UID_SYSREG_CMGP2GNSS_IPCLKPORT_PCLK	0x2060
#define GOUT_BLK_CMGP_UID_SYSREG_CMGP2PMU_AP_IPCLKPORT_PCLK	0x2064
#define GOUT_BLK_CMGP_UID_SYSREG_CMGP2PMU_SHUB_IPCLKPORT_PCLK	0x2068
#define GOUT_BLK_CMGP_UID_SYSREG_CMGP2SHUB_IPCLKPORT_PCLK	0x206c
#define GOUT_BLK_CMGP_UID_SYSREG_CMGP2WLBT_IPCLKPORT_PCLK	0x2070
#define GOUT_BLK_CMGP_UID_SYSREG_CMGP_IPCLKPORT_PCLK	0x2074
#define GOUT_BLK_CMGP_UID_USI_CMGP00_IPCLKPORT_IPCLK	0x2078
#define GOUT_BLK_CMGP_UID_USI_CMGP00_IPCLKPORT_PCLK	0x207c
#define GOUT_BLK_CMGP_UID_USI_CMGP01_IPCLKPORT_IPCLK	0x2080
#define GOUT_BLK_CMGP_UID_USI_CMGP01_IPCLKPORT_PCLK	0x2084
#define GOUT_BLK_CMGP_UID_USI_CMGP02_IPCLKPORT_IPCLK	0x2088
#define GOUT_BLK_CMGP_UID_USI_CMGP02_IPCLKPORT_PCLK	0x208c
#define GOUT_BLK_CMGP_UID_USI_CMGP03_IPCLKPORT_IPCLK	0x2090
#define GOUT_BLK_CMGP_UID_USI_CMGP03_IPCLKPORT_PCLK	0x2094
#define GOUT_BLK_CMGP_UID_USI_CMGP04_IPCLKPORT_IPCLK	0x2098
#define GOUT_BLK_CMGP_UID_USI_CMGP04_IPCLKPORT_PCLK	0x209c

static const unsigned long cmgp_clk_regs[] __initconst = {
	MUX_CLK_CMGP_ADC,
	MUX_CLK_CMGP_I2C,
	MUX_CLK_CMGP_USI00,
	MUX_CLK_CMGP_USI01,
	MUX_CLK_CMGP_USI02,
	MUX_CLK_CMGP_USI03,
	MUX_CLK_CMGP_USI04,
	DIV_CLK_CMGP_ADC,
	DIV_CLK_CMGP_I2C,
	DIV_CLK_CMGP_USI00,
	DIV_CLK_CMGP_USI01,
	DIV_CLK_CMGP_USI02,
	DIV_CLK_CMGP_USI03,
	DIV_CLK_CMGP_USI04,
	CLK_BLK_CMGP_UID_CMGP_CMU_CMGP_IPCLKPORT_PCLK,
	CLK_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_OSCCLK_RCO_IPCLKPORT_CLK,
	GOUT_BLK_CMGP_UID_ADC_CMGP_IPCLKPORT_PCLK_S0,
	GOUT_BLK_CMGP_UID_ADC_CMGP_IPCLKPORT_PCLK_S1,
	GOUT_BLK_CMGP_UID_GPIO_CMGP_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_I2C_CMGP00_IPCLKPORT_IPCLK,
	GOUT_BLK_CMGP_UID_I2C_CMGP00_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_I2C_CMGP01_IPCLKPORT_IPCLK,
	GOUT_BLK_CMGP_UID_I2C_CMGP01_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_I2C_CMGP02_IPCLKPORT_IPCLK,
	GOUT_BLK_CMGP_UID_I2C_CMGP02_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_I2C_CMGP03_IPCLKPORT_IPCLK,
	GOUT_BLK_CMGP_UID_I2C_CMGP03_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_I2C_CMGP04_IPCLKPORT_IPCLK,
	GOUT_BLK_CMGP_UID_I2C_CMGP04_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_BUS_IPCLKPORT_CLK,
	GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_I2C_IPCLKPORT_CLK,
	GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI00_IPCLKPORT_CLK,
	GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI01_IPCLKPORT_CLK,
	GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI02_IPCLKPORT_CLK,
	GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI03_IPCLKPORT_CLK,
	GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI04_IPCLKPORT_CLK,
	GOUT_BLK_CMGP_UID_SYSREG_CMGP2CP_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_SYSREG_CMGP2GNSS_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_SYSREG_CMGP2PMU_AP_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_SYSREG_CMGP2PMU_SHUB_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_SYSREG_CMGP2SHUB_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_SYSREG_CMGP2WLBT_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_SYSREG_CMGP_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_USI_CMGP00_IPCLKPORT_IPCLK,
	GOUT_BLK_CMGP_UID_USI_CMGP00_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_USI_CMGP01_IPCLKPORT_IPCLK,
	GOUT_BLK_CMGP_UID_USI_CMGP01_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_USI_CMGP02_IPCLKPORT_IPCLK,
	GOUT_BLK_CMGP_UID_USI_CMGP02_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_USI_CMGP03_IPCLKPORT_IPCLK,
	GOUT_BLK_CMGP_UID_USI_CMGP03_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_USI_CMGP04_IPCLKPORT_IPCLK,
	GOUT_BLK_CMGP_UID_USI_CMGP04_IPCLKPORT_PCLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long cmgp_qch_regs[] __initconst = {
	0x3000,	/* ADC_CMGP_QCH_ADC */
	0x3004,	/* ADC_CMGP_QCH_S0 */
	0x3008,	/* ADC_CMGP_QCH_S1 */
	0x300c,	/* CMGP_CMU_CMGP_QCH */
	0x3010,	/* GPIO_CMGP_QCH */
	0x3014,	/* I2C_CMGP00_QCH */
	0x3018,	/* I2C_CMGP01_QCH */
	0x301c,	/* I2C_CMGP02_QCH */
	0x3020,	/* I2C_CMGP03_QCH */
	0x3024,	/* I2C_CMGP04_QCH */
	0x3028,	/* SYSREG_CMGP2CP_QCH */
	0x302c,	/* SYSREG_CMGP2GNSS_QCH */
	0x3030,	/* SYSREG_CMGP2PMU_AP_QCH */
	0x3034,	/* SYSREG_CMGP2PMU_SHUB_QCH */
	0x3038,	/* SYSREG_CMGP2SHUB_QCH */
	0x303c,	/* SYSREG_CMGP2WLBT_QCH */
	0x3040,	/* SYSREG_CMGP_QCH */
	0x3044,	/* USI_CMGP00_QCH */
	0x3048,	/* USI_CMGP01_QCH */
	0x304c,	/* USI_CMGP02_QCH */
	0x3050,	/* USI_CMGP03_QCH */
	0x3054,	/* USI_CMGP04_QCH */
};

/* List of parent clocks for Muxes in CMU_CMGP */
PNAME(mout_cmgp_usi01_p) = { "oscclk", "gout_clkcmu_cmgp_bus" };
PNAME(mout_cmgp_i2c_p) = { "oscclk", "gout_clkcmu_cmgp_bus" };
PNAME(mout_cmgp_usi00_p) = { "oscclk", "gout_clkcmu_cmgp_bus" };
PNAME(mout_cmgp_usi04_p) = { "oscclk", "gout_clkcmu_cmgp_bus" };
PNAME(mout_cmgp_usi02_p) = { "oscclk", "gout_clkcmu_cmgp_bus" };
PNAME(mout_cmgp_usi03_p) = { "oscclk", "gout_clkcmu_cmgp_bus" };
PNAME(mout_cmgp_adc_p) = { "oscclk", "dout_cmgp_adc" };

static const struct samsung_mux_clock cmgp_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMGP_USI01, "mout_cmgp_usi01", mout_cmgp_usi01_p,
	    MUX_CLK_CMGP_USI01, 0, 1),
	MUX(CLK_MOUT_CMGP_I2C, "mout_cmgp_i2c", mout_cmgp_i2c_p,
	    MUX_CLK_CMGP_I2C, 0, 1),
	MUX(CLK_MOUT_CMGP_USI00, "mout_cmgp_usi00", mout_cmgp_usi00_p,
	    MUX_CLK_CMGP_USI00, 0, 1),
	MUX(CLK_MOUT_CMGP_USI04, "mout_cmgp_usi04", mout_cmgp_usi04_p,
	    MUX_CLK_CMGP_USI04, 0, 1),
	MUX(CLK_MOUT_CMGP_USI02, "mout_cmgp_usi02", mout_cmgp_usi02_p,
	    MUX_CLK_CMGP_USI02, 0, 1),
	MUX(CLK_MOUT_CMGP_USI03, "mout_cmgp_usi03", mout_cmgp_usi03_p,
	    MUX_CLK_CMGP_USI03, 0, 1),
	MUX(CLK_MOUT_CMGP_ADC, "mout_cmgp_adc", mout_cmgp_adc_p,
	    MUX_CLK_CMGP_ADC, 0, 1),
};

static const struct samsung_div_clock cmgp_div_clks[] __initconst = {
	DIV(CLK_DOUT_CMGP_USI03, "dout_cmgp_usi03", "mout_cmgp_usi03",
	    DIV_CLK_CMGP_USI03, 0, 4),
	DIV(CLK_DOUT_CMGP_USI00, "dout_cmgp_usi00", "mout_cmgp_usi00",
	    DIV_CLK_CMGP_USI00, 0, 4),
	DIV(CLK_DOUT_CMGP_I2C, "dout_cmgp_i2c", "mout_cmgp_i2c",
	    DIV_CLK_CMGP_I2C, 0, 4),
	DIV(CLK_DOUT_CMGP_USI01, "dout_cmgp_usi01", "mout_cmgp_usi01",
	    DIV_CLK_CMGP_USI01, 0, 4),
	DIV(CLK_DOUT_CMGP_USI04, "dout_cmgp_usi04", "mout_cmgp_usi04",
	    DIV_CLK_CMGP_USI04, 0, 4),
	DIV(CLK_DOUT_CMGP_USI02, "dout_cmgp_usi02", "mout_cmgp_usi02",
	    DIV_CLK_CMGP_USI02, 0, 4),
	DIV(CLK_DOUT_CMGP_ADC, "dout_cmgp_adc", "gout_clkcmu_cmgp_bus",
	    DIV_CLK_CMGP_ADC, 0, 4),
};

static const struct samsung_gate_clock cmgp_gate_clks[] __initconst = {
	GATE(CLK_GOUT_CMGP_CMGP_CMU_CMGP_PCLK, "gout_cmgp_cmgp_cmu_cmgp_pclk",
	     "gout_clkcmu_cmgp_bus",
	     CLK_BLK_CMGP_UID_CMGP_CMU_CMGP_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_SYSREG_CMGP2CP_PCLK, "gout_cmgp_sysreg_cmgp2cp_pclk",
	     "gout_clkcmu_cmgp_bus",
	     GOUT_BLK_CMGP_UID_SYSREG_CMGP2CP_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_SYSREG_CMGP2GNSS_PCLK, "gout_cmgp_sysreg_cmgp2gnss_pclk",
	     "gout_clkcmu_cmgp_bus",
	     GOUT_BLK_CMGP_UID_SYSREG_CMGP2GNSS_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_SYSREG_CMGP2SHUB_PCLK, "gout_cmgp_sysreg_cmgp2shub_pclk",
	     "gout_clkcmu_cmgp_bus",
	     GOUT_BLK_CMGP_UID_SYSREG_CMGP2SHUB_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_SYSREG_CMGP2WLBT_PCLK, "gout_cmgp_sysreg_cmgp2wlbt_pclk",
	     "gout_clkcmu_cmgp_bus",
	     GOUT_BLK_CMGP_UID_SYSREG_CMGP2WLBT_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_GPIO_CMGP_PCLK, "gout_cmgp_gpio_cmgp_pclk", "gout_clkcmu_cmgp_bus",
	     GOUT_BLK_CMGP_UID_GPIO_CMGP_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_ADC_CMGP_PCLK_S0, "gout_cmgp_adc_cmgp_pclk_s0", "gout_clkcmu_cmgp_bus",
	     GOUT_BLK_CMGP_UID_ADC_CMGP_IPCLKPORT_PCLK_S0, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_ADC_CMGP_PCLK_S1, "gout_cmgp_adc_cmgp_pclk_s1", "gout_clkcmu_cmgp_bus",
	     GOUT_BLK_CMGP_UID_ADC_CMGP_IPCLKPORT_PCLK_S1, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_I2C_CMGP00_PCLK, "gout_cmgp_i2c_cmgp00_pclk", "gout_clkcmu_cmgp_bus",
	     GOUT_BLK_CMGP_UID_I2C_CMGP00_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_I2C_CMGP01_PCLK, "gout_cmgp_i2c_cmgp01_pclk", "gout_clkcmu_cmgp_bus",
	     GOUT_BLK_CMGP_UID_I2C_CMGP01_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_I2C_CMGP02_PCLK, "gout_cmgp_i2c_cmgp02_pclk", "gout_clkcmu_cmgp_bus",
	     GOUT_BLK_CMGP_UID_I2C_CMGP02_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_I2C_CMGP03_PCLK, "gout_cmgp_i2c_cmgp03_pclk", "gout_clkcmu_cmgp_bus",
	     GOUT_BLK_CMGP_UID_I2C_CMGP03_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_I2C_CMGP04_PCLK, "gout_cmgp_i2c_cmgp04_pclk", "gout_clkcmu_cmgp_bus",
	     GOUT_BLK_CMGP_UID_I2C_CMGP04_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_SYSREG_CMGP_PCLK, "gout_cmgp_sysreg_cmgp_pclk", "gout_clkcmu_cmgp_bus",
	     GOUT_BLK_CMGP_UID_SYSREG_CMGP_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_USI_CMGP00_PCLK, "gout_cmgp_usi_cmgp00_pclk", "gout_clkcmu_cmgp_bus",
	     GOUT_BLK_CMGP_UID_USI_CMGP00_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_USI_CMGP01_PCLK, "gout_cmgp_usi_cmgp01_pclk", "gout_clkcmu_cmgp_bus",
	     GOUT_BLK_CMGP_UID_USI_CMGP01_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_USI_CMGP02_PCLK, "gout_cmgp_usi_cmgp02_pclk", "gout_clkcmu_cmgp_bus",
	     GOUT_BLK_CMGP_UID_USI_CMGP02_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_USI_CMGP03_PCLK, "gout_cmgp_usi_cmgp03_pclk", "gout_clkcmu_cmgp_bus",
	     GOUT_BLK_CMGP_UID_USI_CMGP03_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_USI_CMGP04_PCLK, "gout_cmgp_usi_cmgp04_pclk", "gout_clkcmu_cmgp_bus",
	     GOUT_BLK_CMGP_UID_USI_CMGP04_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_RSTnSYNC_CLK_CMGP_USI00_CLK, "gout_cmgp_rstnsync_clk_cmgp_usi00_clk",
	     "dout_cmgp_usi00",
	     GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI00_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_RSTnSYNC_CLK_CMGP_BUS_CLK, "gout_cmgp_rstnsync_clk_cmgp_bus_clk",
	     "gout_clkcmu_cmgp_bus",
	     GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_BUS_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_RSTnSYNC_CLK_CMGP_I2C_CLK, "gout_cmgp_rstnsync_clk_cmgp_i2c_clk",
	     "dout_cmgp_i2c",
	     GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_I2C_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_RSTnSYNC_CLK_CMGP_USI01_CLK, "gout_cmgp_rstnsync_clk_cmgp_usi01_clk",
	     "dout_cmgp_usi01",
	     GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI01_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_RSTnSYNC_CLK_CMGP_USI02_CLK, "gout_cmgp_rstnsync_clk_cmgp_usi02_clk",
	     "dout_cmgp_usi02",
	     GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI02_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_RSTnSYNC_CLK_CMGP_USI03_CLK, "gout_cmgp_rstnsync_clk_cmgp_usi03_clk",
	     "dout_cmgp_usi03",
	     GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI03_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_RSTnSYNC_CLK_CMGP_USI04_CLK, "gout_cmgp_rstnsync_clk_cmgp_usi04_clk",
	     "dout_cmgp_usi04",
	     GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI04_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_I2C_CMGP00_IPCLK, "gout_cmgp_i2c_cmgp00_ipclk", "dout_cmgp_i2c",
	     GOUT_BLK_CMGP_UID_I2C_CMGP00_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_I2C_CMGP01_IPCLK, "gout_cmgp_i2c_cmgp01_ipclk", "dout_cmgp_i2c",
	     GOUT_BLK_CMGP_UID_I2C_CMGP01_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_I2C_CMGP02_IPCLK, "gout_cmgp_i2c_cmgp02_ipclk", "dout_cmgp_i2c",
	     GOUT_BLK_CMGP_UID_I2C_CMGP02_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_I2C_CMGP03_IPCLK, "gout_cmgp_i2c_cmgp03_ipclk", "dout_cmgp_i2c",
	     GOUT_BLK_CMGP_UID_I2C_CMGP03_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_I2C_CMGP04_IPCLK, "gout_cmgp_i2c_cmgp04_ipclk", "dout_cmgp_i2c",
	     GOUT_BLK_CMGP_UID_I2C_CMGP04_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_USI_CMGP00_IPCLK, "gout_cmgp_usi_cmgp00_ipclk", "dout_cmgp_usi00",
	     GOUT_BLK_CMGP_UID_USI_CMGP00_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_USI_CMGP01_IPCLK, "gout_cmgp_usi_cmgp01_ipclk", "dout_cmgp_usi01",
	     GOUT_BLK_CMGP_UID_USI_CMGP01_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_USI_CMGP02_IPCLK, "gout_cmgp_usi_cmgp02_ipclk", "dout_cmgp_usi02",
	     GOUT_BLK_CMGP_UID_USI_CMGP02_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_USI_CMGP03_IPCLK, "gout_cmgp_usi_cmgp03_ipclk", "dout_cmgp_usi03",
	     GOUT_BLK_CMGP_UID_USI_CMGP03_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_USI_CMGP04_IPCLK, "gout_cmgp_usi_cmgp04_ipclk", "dout_cmgp_usi04",
	     GOUT_BLK_CMGP_UID_USI_CMGP04_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_RSTnSYNC_CLK_CMGP_OSCCLK_RCO_CLK, "gout_cmgp_rstnsync_clk_cmgp_oscclk_rco_clk",
	     "oscclk",
	     CLK_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_OSCCLK_RCO_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_SYSREG_CMGP2PMU_AP_PCLK, "gout_cmgp_sysreg_cmgp2pmu_ap_pclk",
	     "gout_clkcmu_cmgp_bus",
	     GOUT_BLK_CMGP_UID_SYSREG_CMGP2PMU_AP_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_SYSREG_CMGP2PMU_SHUB_PCLK, "gout_cmgp_sysreg_cmgp2pmu_shub_pclk",
	     "gout_clkcmu_cmgp_bus",
	     GOUT_BLK_CMGP_UID_SYSREG_CMGP2PMU_SHUB_IPCLKPORT_PCLK, 21, 0, 0),
};

static const struct samsung_cmu_info cmgp_cmu_info __initconst = {
	.mux_clks		= cmgp_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(cmgp_mux_clks),
	.div_clks		= cmgp_div_clks,
	.nr_div_clks		= ARRAY_SIZE(cmgp_div_clks),
	.gate_clks		= cmgp_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(cmgp_gate_clks),
	.nr_clk_ids		= CLKS_NR_CMGP,
	.clk_regs		= cmgp_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cmgp_clk_regs),
	.qch_regs		= cmgp_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(cmgp_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9610_cmu_cmgp_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &cmgp_cmu_info);
}

CLK_OF_DECLARE(exynos9610_cmu_cmgp, "samsung,exynos9610-cmu-cmgp",
	       exynos9610_cmu_cmgp_init);

/* ---- CMU_TOP ---------------------------------------------------------*/

/* Register Offset definitions for CMU_TOP (0x12100000) */
#define PLL_LOCKTIME_PLL_MMC_PLL_LOCK_TIME			0x0000
#define PLL_LOCKTIME_PLL_SHARED0_PLL_LOCK_TIME			0x0004
#define PLL_LOCKTIME_PLL_SHARED1_PLL_LOCK_TIME			0x0008
#define PLL_CON0_PLL_MMC_ENABLE			0x0100
#define PLL_CON0_PLL_SHARED0_ENABLE			0x0120
#define PLL_CON0_PLL_SHARED1_ENABLE			0x0140
#define MUX_CLKCMU_APM_BUS			0x1000
#define MUX_CLKCMU_CAM_BUS			0x1004
#define MUX_CLKCMU_CIS_CLK0			0x1008
#define MUX_CLKCMU_CIS_CLK1			0x100c
#define MUX_CLKCMU_CIS_CLK2			0x1010
#define MUX_CLKCMU_CIS_CLK3			0x1014
#define MUX_CLKCMU_CORE_BUS			0x1018
#define MUX_CLKCMU_CORE_CCI			0x101c
#define MUX_CLKCMU_CORE_G3D			0x1020
#define MUX_CLKCMU_CPUCL0_DBG			0x1024
#define MUX_CLKCMU_CPUCL0_SWITCH			0x1028
#define MUX_CLKCMU_CPUCL1_SWITCH			0x102c
#define MUX_CLKCMU_DISPAUD_AUD			0x1030
#define MUX_CLKCMU_DISPAUD_CPU			0x1034
#define MUX_CLKCMU_DISPAUD_DISP			0x1038
#define MUX_CLKCMU_FSYS_BUS			0x103c
#define MUX_CLKCMU_FSYS_MMC_CARD			0x1040
#define MUX_CLKCMU_FSYS_MMC_EMBD			0x1044
#define MUX_CLKCMU_FSYS_UFS_EMBD			0x1048
#define MUX_CLKCMU_G2D_G2D			0x104c
#define MUX_CLKCMU_G2D_MSCL			0x1050
#define MUX_CLKCMU_G3D_SWITCH			0x1054
#define MUX_CLKCMU_HPM			0x1058
#define MUX_CLKCMU_ISP_BUS			0x105c
#define MUX_CLKCMU_ISP_GDC			0x1060
#define MUX_CLKCMU_ISP_VRA			0x1064
#define MUX_CLKCMU_MFC_MFC			0x1068
#define MUX_CLKCMU_MFC_WFD			0x106c
#define MUX_CLKCMU_MIF_BUSP			0x1070
#define MUX_CLKCMU_MIF_SWITCH			0x1074
#define MUX_CLKCMU_PERI_BUS			0x1078
#define MUX_CLKCMU_PERI_IP			0x107c
#define MUX_CLKCMU_PERI_UART			0x1080
#define MUX_CLKCMU_USB_BUS			0x1084
#define MUX_CLKCMU_USB_DPGTC			0x1088
#define MUX_CLKCMU_USB_USB30DRD			0x108c
#define MUX_CLKCMU_VIPX1_BUS			0x1090
#define MUX_CLKCMU_VIPX2_BUS			0x1094
#define MUX_CLK_CMU_CMUREF			0x1098
#define MUX_CMU_CMUREF			0x109c
#define AP2CP_SHARED0_PLL_CLK			0x1800
#define AP2CP_SHARED1_PLL_CLK			0x1804
#define CLKCMU_APM_BUS			0x1808
#define CLKCMU_CAM_BUS			0x180c
#define CLKCMU_CIS_CLK0			0x1810
#define CLKCMU_CIS_CLK1			0x1814
#define CLKCMU_CIS_CLK2			0x1818
#define CLKCMU_CIS_CLK3			0x181c
#define CLKCMU_CORE_BUS			0x1820
#define CLKCMU_CORE_CCI			0x1824
#define CLKCMU_CORE_G3D			0x1828
#define CLKCMU_CPUCL0_DBG			0x182c
#define CLKCMU_CPUCL0_SWITCH			0x1830
#define CLKCMU_CPUCL1_SWITCH			0x1834
#define CLKCMU_DISPAUD_AUD			0x1838
#define CLKCMU_DISPAUD_CPU			0x183c
#define CLKCMU_DISPAUD_DISP			0x1840
#define CLKCMU_FSYS_BUS			0x1844
#define CLKCMU_FSYS_MMC_CARD			0x1848
#define CLKCMU_FSYS_MMC_EMBD			0x184c
#define CLKCMU_FSYS_UFS_EMBD			0x1850
#define CLKCMU_G2D_G2D			0x1854
#define CLKCMU_G2D_MSCL			0x1858
#define CLKCMU_G3D_SWITCH			0x185c
#define CLKCMU_HPM			0x1860
#define CLKCMU_ISP_BUS			0x1864
#define CLKCMU_ISP_GDC			0x1868
#define CLKCMU_ISP_VRA			0x186c
#define CLKCMU_MFC_MFC			0x1870
#define CLKCMU_MFC_WFD			0x1874
#define CLKCMU_MIF_BUSP			0x1878
#define CLKCMU_PERI_BUS			0x1880
#define CLKCMU_PERI_IP			0x1884
#define CLKCMU_PERI_UART			0x1888
#define CLKCMU_USB_BUS			0x188c
#define CLKCMU_USB_DPGTC			0x1890
#define CLKCMU_USB_USB30DRD			0x1894
#define CLKCMU_VIPX1_BUS			0x1898
#define CLKCMU_VIPX2_BUS			0x189c
#define DIV_CLK_CMU_CMUREF			0x18a0
#define PLL_MMC_DIV2			0x18a4
#define PLL_SHARED0_DIV2			0x18a8
#define PLL_SHARED0_DIV3			0x18ac
#define PLL_SHARED0_DIV4			0x18b0
#define PLL_SHARED1_DIV2			0x18b4
#define PLL_SHARED1_DIV3			0x18b8
#define PLL_SHARED1_DIV4			0x18bc
#define CLKCMU_MIF_SWITCH			0x2000
#define CLK_BLK_CMU_UID_OTP_IPCLKPORT_CLK			0x2004
#define GATE_CLKCMU_APM_BUS			0x2008
#define GATE_CLKCMU_CAM_BUS			0x200c
#define GATE_CLKCMU_CIS_CLK0			0x2010
#define GATE_CLKCMU_CIS_CLK1			0x2014
#define GATE_CLKCMU_CIS_CLK2			0x2018
#define GATE_CLKCMU_CIS_CLK3			0x201c
#define GATE_CLKCMU_CORE_BUS			0x2020
#define GATE_CLKCMU_CORE_CCI			0x2024
#define GATE_CLKCMU_CORE_G3D			0x2028
#define GATE_CLKCMU_CPUCL0_DBG			0x202c
#define GATE_CLKCMU_CPUCL0_SWITCH			0x2030
#define GATE_CLKCMU_CPUCL1_SWITCH			0x2034
#define GATE_CLKCMU_DISPAUD_AUD			0x2038
#define GATE_CLKCMU_DISPAUD_CPU			0x203c
#define GATE_CLKCMU_DISPAUD_DISP			0x2040
#define GATE_CLKCMU_FSYS_BUS			0x2044
#define GATE_CLKCMU_FSYS_MMC_CARD			0x2048
#define GATE_CLKCMU_FSYS_MMC_EMBD			0x204c
#define GATE_CLKCMU_FSYS_UFS_EMBD			0x2050
#define GATE_CLKCMU_G2D_G2D			0x2054
#define GATE_CLKCMU_G2D_MSCL			0x2058
#define GATE_CLKCMU_G3D_SWITCH			0x205c
#define GATE_CLKCMU_HPM			0x2060
#define GATE_CLKCMU_ISP_BUS			0x2064
#define GATE_CLKCMU_ISP_GDC			0x2068
#define GATE_CLKCMU_ISP_VRA			0x206c
#define GATE_CLKCMU_MFC_MFC			0x2070
#define GATE_CLKCMU_MFC_WFD			0x2074
#define GATE_CLKCMU_MIF_BUSP			0x2078
#define GATE_CLKCMU_MODEM_SHARED0			0x207c
#define GATE_CLKCMU_MODEM_SHARED1			0x2080
#define GATE_CLKCMU_PERI_BUS			0x2084
#define GATE_CLKCMU_PERI_IP			0x2088
#define GATE_CLKCMU_PERI_UART			0x208c
#define GATE_CLKCMU_USB_BUS			0x2090
#define GATE_CLKCMU_USB_DPGTC			0x2094
#define GATE_CLKCMU_USB_USB30DRD			0x2098
#define GATE_CLKCMU_VIPX1_BUS			0x209c
#define GATE_CLKCMU_VIPX2_BUS			0x20a0

static const unsigned long top_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_MMC_PLL_LOCK_TIME,
	PLL_LOCKTIME_PLL_SHARED0_PLL_LOCK_TIME,
	PLL_LOCKTIME_PLL_SHARED1_PLL_LOCK_TIME,
	PLL_CON0_PLL_MMC_ENABLE,
	PLL_CON0_PLL_SHARED0_ENABLE,
	PLL_CON0_PLL_SHARED1_ENABLE,
	MUX_CLKCMU_APM_BUS,
	MUX_CLKCMU_CAM_BUS,
	MUX_CLKCMU_CIS_CLK0,
	MUX_CLKCMU_CIS_CLK1,
	MUX_CLKCMU_CIS_CLK2,
	MUX_CLKCMU_CIS_CLK3,
	MUX_CLKCMU_CORE_BUS,
	MUX_CLKCMU_CORE_CCI,
	MUX_CLKCMU_CORE_G3D,
	MUX_CLKCMU_CPUCL0_DBG,
	MUX_CLKCMU_CPUCL0_SWITCH,
	MUX_CLKCMU_CPUCL1_SWITCH,
	MUX_CLKCMU_DISPAUD_AUD,
	MUX_CLKCMU_DISPAUD_CPU,
	MUX_CLKCMU_DISPAUD_DISP,
	MUX_CLKCMU_FSYS_BUS,
	MUX_CLKCMU_FSYS_MMC_CARD,
	MUX_CLKCMU_FSYS_MMC_EMBD,
	MUX_CLKCMU_FSYS_UFS_EMBD,
	MUX_CLKCMU_G2D_G2D,
	MUX_CLKCMU_G2D_MSCL,
	MUX_CLKCMU_G3D_SWITCH,
	MUX_CLKCMU_HPM,
	MUX_CLKCMU_ISP_BUS,
	MUX_CLKCMU_ISP_GDC,
	MUX_CLKCMU_ISP_VRA,
	MUX_CLKCMU_MFC_MFC,
	MUX_CLKCMU_MFC_WFD,
	MUX_CLKCMU_MIF_BUSP,
	MUX_CLKCMU_MIF_SWITCH,
	MUX_CLKCMU_PERI_BUS,
	MUX_CLKCMU_PERI_IP,
	MUX_CLKCMU_PERI_UART,
	MUX_CLKCMU_USB_BUS,
	MUX_CLKCMU_USB_DPGTC,
	MUX_CLKCMU_USB_USB30DRD,
	MUX_CLKCMU_VIPX1_BUS,
	MUX_CLKCMU_VIPX2_BUS,
	MUX_CLK_CMU_CMUREF,
	MUX_CMU_CMUREF,
	AP2CP_SHARED0_PLL_CLK,
	AP2CP_SHARED1_PLL_CLK,
	CLKCMU_APM_BUS,
	CLKCMU_CAM_BUS,
	CLKCMU_CIS_CLK0,
	CLKCMU_CIS_CLK1,
	CLKCMU_CIS_CLK2,
	CLKCMU_CIS_CLK3,
	CLKCMU_CORE_BUS,
	CLKCMU_CORE_CCI,
	CLKCMU_CORE_G3D,
	CLKCMU_CPUCL0_DBG,
	CLKCMU_CPUCL0_SWITCH,
	CLKCMU_CPUCL1_SWITCH,
	CLKCMU_DISPAUD_AUD,
	CLKCMU_DISPAUD_CPU,
	CLKCMU_DISPAUD_DISP,
	CLKCMU_FSYS_BUS,
	CLKCMU_FSYS_MMC_CARD,
	CLKCMU_FSYS_MMC_EMBD,
	CLKCMU_FSYS_UFS_EMBD,
	CLKCMU_G2D_G2D,
	CLKCMU_G2D_MSCL,
	CLKCMU_G3D_SWITCH,
	CLKCMU_HPM,
	CLKCMU_ISP_BUS,
	CLKCMU_ISP_GDC,
	CLKCMU_ISP_VRA,
	CLKCMU_MFC_MFC,
	CLKCMU_MFC_WFD,
	CLKCMU_MIF_BUSP,
	CLKCMU_PERI_BUS,
	CLKCMU_PERI_IP,
	CLKCMU_PERI_UART,
	CLKCMU_USB_BUS,
	CLKCMU_USB_DPGTC,
	CLKCMU_USB_USB30DRD,
	CLKCMU_VIPX1_BUS,
	CLKCMU_VIPX2_BUS,
	DIV_CLK_CMU_CMUREF,
	PLL_MMC_DIV2,
	PLL_SHARED0_DIV2,
	PLL_SHARED0_DIV3,
	PLL_SHARED0_DIV4,
	PLL_SHARED1_DIV2,
	PLL_SHARED1_DIV3,
	PLL_SHARED1_DIV4,
	CLKCMU_MIF_SWITCH,
	CLK_BLK_CMU_UID_OTP_IPCLKPORT_CLK,
	GATE_CLKCMU_APM_BUS,
	GATE_CLKCMU_CAM_BUS,
	GATE_CLKCMU_CIS_CLK0,
	GATE_CLKCMU_CIS_CLK1,
	GATE_CLKCMU_CIS_CLK2,
	GATE_CLKCMU_CIS_CLK3,
	GATE_CLKCMU_CORE_BUS,
	GATE_CLKCMU_CORE_CCI,
	GATE_CLKCMU_CORE_G3D,
	GATE_CLKCMU_CPUCL0_DBG,
	GATE_CLKCMU_CPUCL0_SWITCH,
	GATE_CLKCMU_CPUCL1_SWITCH,
	GATE_CLKCMU_DISPAUD_AUD,
	GATE_CLKCMU_DISPAUD_CPU,
	GATE_CLKCMU_DISPAUD_DISP,
	GATE_CLKCMU_FSYS_BUS,
	GATE_CLKCMU_FSYS_MMC_CARD,
	GATE_CLKCMU_FSYS_MMC_EMBD,
	GATE_CLKCMU_FSYS_UFS_EMBD,
	GATE_CLKCMU_G2D_G2D,
	GATE_CLKCMU_G2D_MSCL,
	GATE_CLKCMU_G3D_SWITCH,
	GATE_CLKCMU_HPM,
	GATE_CLKCMU_ISP_BUS,
	GATE_CLKCMU_ISP_GDC,
	GATE_CLKCMU_ISP_VRA,
	GATE_CLKCMU_MFC_MFC,
	GATE_CLKCMU_MFC_WFD,
	GATE_CLKCMU_MIF_BUSP,
	GATE_CLKCMU_MODEM_SHARED0,
	GATE_CLKCMU_MODEM_SHARED1,
	GATE_CLKCMU_PERI_BUS,
	GATE_CLKCMU_PERI_IP,
	GATE_CLKCMU_PERI_UART,
	GATE_CLKCMU_USB_BUS,
	GATE_CLKCMU_USB_DPGTC,
	GATE_CLKCMU_USB_USB30DRD,
	GATE_CLKCMU_VIPX1_BUS,
	GATE_CLKCMU_VIPX2_BUS,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long top_qch_regs[] __initconst = {
	0x3000,	/* CMU_TOP_CMUREF_QCH */
	0x3004,	/* DFTMUX_TOP_QCH_CLK_CSIS0 */
	0x3008,	/* DFTMUX_TOP_QCH_CLK_CSIS1 */
	0x300c,	/* DFTMUX_TOP_QCH_CLK_CSIS2 */
	0x3010,	/* DFTMUX_TOP_QCH_CLK_CSIS3 */
	0x3014,	/* OTP_QCH */
};

static const struct samsung_fixed_rate_clock top_fixed_clks[] __initconst = {
	FRATE(CLK_CLK_CLUSTER0_DIV_ACLK, "clk_cluster0_div_aclk", NULL, 0, 100000000),
	FRATE(CLK_CLK_CLUSTER0_DIV_PCLKDBG, "clk_cluster0_div_pclkdbg", NULL, 0, 100000000),
	FRATE(CLK_CLK_CLUSTER0_DIV_CNTCLK, "clk_cluster0_div_cntclk", NULL, 0, 100000000),
	FRATE(CLK_CLK_DEBUG_DECON, "clk_debug_decon", NULL, 0, 100000000),
	FRATE(CLK_CLKCMU_MIF_SWITCH_CLKOUT, "clkcmu_mif_switch_clkout", NULL, 0, 1600000000),
	FRATE(CLK_RTC_CLK_USB__ALV, "rtc_clk_usb__alv", NULL, 0, 26000000),
	FRATE(CLK_O_USB20_PHY_CLOCK, "o_usb20_phy_clock", NULL, 0, 60000000),
	FRATE(CLK_O_USB30_PHY_RX0CLK_0, "o_usb30_phy_rx0clk_0", NULL, 0, 250000000),
	FRATE(CLK_O_USB30_PHY_RX0CLK_1, "o_usb30_phy_rx0clk_1", NULL, 0, 250000000),
	FRATE(CLK_O_USB30_PIPE_PCLK_0, "o_usb30_pipe_pclk_0", NULL, 0, 125000000),
	FRATE(CLK_O_USB30_PIPE_PCLK_1, "o_usb30_pipe_pclk_1", NULL, 0, 125000000),
};

static const struct samsung_pll_rate_table fout_shared0_rate_table[] __initconst = {
	PLL_35XX_RATE(26 * MHZ, 1599000000U, 246, 4, 0),
};

static const struct samsung_pll_rate_table fout_shared1_rate_table[] __initconst = {
	PLL_35XX_RATE(26 * MHZ, 1332500000U, 205, 4, 0),
};

static const struct samsung_pll_clock top_pll_clks[] __initconst = {
	PLL(pll_1051x, CLK_FOUT_SHARED0, "fout_shared0", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED0_PLL_LOCK_TIME, PLL_CON0_PLL_SHARED0_ENABLE, fout_shared0_rate_table),
	PLL(pll_1051x, CLK_FOUT_SHARED1, "fout_shared1", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED1_PLL_LOCK_TIME, PLL_CON0_PLL_SHARED1_ENABLE, fout_shared1_rate_table),
	PLL(pll_1061x, CLK_FOUT_MMC, "fout_mmc", "oscclk",
	    PLL_LOCKTIME_PLL_MMC_PLL_LOCK_TIME, PLL_CON0_PLL_MMC_ENABLE, NULL),
};

/* List of parent clocks for Muxes in CMU_TOP */
PNAME(mout_cmu_g2d_mscl_p) = { "dout_pll_shared0_div3", "dout_pll_shared1_div3", "dout_pll_shared0_div4", "dout_pll_shared1_div4" };
PNAME(mout_cmu_dispaud_disp_p) = { "dout_pll_shared0_div3", "dout_pll_shared1_div3", "dout_pll_shared0_div4", "dout_pll_shared1_div4" };
PNAME(mout_cmu_fsys_bus_p) = { "dout_pll_shared0_div2", "dout_pll_shared1_div2" };
PNAME(mout_cmu_fsys_mmc_embd_p) = { "oscclk", "dout_pll_shared0_div2", "dout_pll_shared1_div2", "dout_pll_shared0_div3", "dout_pll_shared1_div3", "fout_mmc", "oscclk", "oscclk" };
PNAME(mout_cmu_peri_bus_p) = { "dout_pll_shared0_div4", "dout_pll_shared1_div4" };
PNAME(mout_cmu_peri_ip_p) = { "oscclk", "dout_pll_shared0_div4", "dout_pll_shared1_div4", "oscclk" };
PNAME(mout_cmu_fsys_mmc_card_p) = { "oscclk", "dout_pll_shared0_div2", "dout_pll_shared1_div2", "dout_pll_shared0_div3", "dout_pll_shared1_div3", "fout_mmc", "oscclk", "oscclk" };
PNAME(mout_cmu_cis_clk0_p) = { "oscclk", "dout_pll_shared0_div4" };
PNAME(mout_cmu_cis_clk1_p) = { "oscclk", "dout_pll_shared0_div4" };
PNAME(mout_cmu_cis_clk2_p) = { "oscclk", "dout_pll_shared0_div4" };
PNAME(mout_cmu_cmuref_p) = { "oscclk", "dout_cmu_cmuref" };
PNAME(mout_mux_clk_cmu_cmuref_p) = { "dout_pll_shared0_div4", "dout_pll_shared1_div4" };
PNAME(mout_cmu_apm_bus_p) = { "dout_pll_shared0_div4", "dout_pll_shared1_div4" };
PNAME(mout_cmu_core_cci_p) = { "dout_pll_shared0_div2", "dout_pll_shared1_div2", "dout_pll_shared0_div3", "dout_pll_mmc_div2" };
PNAME(mout_cmu_core_g3d_p) = { "dout_pll_shared0_div2", "dout_pll_shared1_div2", "dout_pll_shared0_div3", "dout_pll_mmc_div2" };
PNAME(mout_cmu_core_bus_p) = { "dout_pll_shared1_div2", "dout_pll_shared0_div3", "dout_pll_shared0_div4", "dout_pll_mmc_div2" };
PNAME(mout_cmu_mif_busp_p) = { "dout_pll_shared0_div4", "dout_pll_shared1_div4", "dout_pll_mmc_div2", "oscclk" };
PNAME(mout_cmu_fsys_ufs_embd_p) = { "oscclk", "dout_pll_shared0_div4", "dout_pll_shared1_div4", "oscclk" };
PNAME(mout_cmu_cam_bus_p) = { "dout_pll_shared1_div2", "dout_pll_shared0_div3", "dout_pll_shared1_div3", "dout_pll_shared0_div4" };
PNAME(mout_cmu_vipx1_bus_p) = { "dout_pll_shared1_div2", "dout_pll_shared0_div3", "dout_pll_shared1_div3", "dout_pll_shared0_div4" };
PNAME(mout_cmu_isp_bus_p) = { "dout_pll_shared1_div2", "dout_pll_shared0_div3", "dout_pll_shared1_div3", "dout_pll_shared0_div4" };
PNAME(mout_cmu_isp_vra_p) = { "dout_pll_shared0_div3", "dout_pll_shared1_div3", "dout_pll_shared0_div4", "dout_pll_shared1_div4" };
PNAME(mout_cmu_isp_gdc_p) = { "dout_pll_shared0_div3", "dout_pll_shared1_div3", "dout_pll_shared0_div4", "dout_pll_shared1_div4" };
PNAME(mout_cmu_g2d_g2d_p) = { "dout_pll_shared1_div2", "dout_pll_shared0_div3", "dout_pll_shared1_div3", "dout_pll_shared0_div4" };
PNAME(mout_cmu_cpucl0_switch_p) = { "dout_pll_shared0_div2", "dout_pll_shared1_div2", "dout_pll_shared0_div3", "dout_pll_shared1_div3" };
PNAME(mout_cmu_cpucl1_switch_p) = { "dout_pll_shared0_div2", "dout_pll_shared1_div2", "dout_pll_shared0_div3", "dout_pll_shared1_div3" };
PNAME(mout_cmu_g3d_switch_p) = { "dout_pll_shared0_div2", "dout_pll_shared1_div2", "dout_pll_shared0_div3", "dout_pll_shared1_div3" };
PNAME(mout_cmu_dispaud_cpu_p) = { "fout_shared1", "dout_pll_shared0_div2", "dout_pll_shared1_div2", "dout_pll_shared0_div3", "dout_pll_shared1_div3", "fout_mmc", "oscclk", "oscclk" };
PNAME(mout_cmu_mif_switch_p) = { "fout_shared0", "fout_shared1", "dout_pll_shared0_div2", "fout_mmc", "dout_pll_shared0_div3", "dout_pll_shared1_div3", "dout_pll_shared0_div4", "dout_pll_shared1_div4" };
PNAME(mout_cmu_cpucl0_dbg_p) = { "dout_pll_shared0_div4", "dout_pll_shared1_div4" };
PNAME(mout_cmu_usb_bus_p) = { "dout_pll_shared0_div3", "dout_pll_shared1_div3", "dout_pll_shared0_div4", "dout_pll_shared1_div4" };
PNAME(mout_cmu_usb_usb30drd_p) = { "oscclk", "dout_pll_shared0_div4", "dout_pll_shared1_div4", "oscclk" };
PNAME(mout_cmu_usb_dpgtc_p) = { "oscclk", "dout_pll_shared0_div4", "dout_pll_shared1_div4", "oscclk" };
PNAME(mout_cmu_dispaud_aud_p) = { "dout_pll_shared1_div2", "dout_pll_shared0_div3", "dout_pll_shared1_div3", "dout_pll_shared0_div4" };
PNAME(mout_cmu_mfc_mfc_p) = { "dout_pll_shared1_div2", "dout_pll_shared0_div3", "dout_pll_shared1_div3", "dout_pll_shared0_div4" };
PNAME(mout_cmu_mfc_wfd_p) = { "dout_pll_shared0_div3", "dout_pll_shared1_div3", "dout_pll_shared0_div4", "dout_pll_shared1_div4" };
PNAME(mout_cmu_hpm_p) = { "oscclk", "dout_pll_shared0_div2", "dout_pll_shared1_div2", "dout_pll_shared0_div3", "dout_pll_mmc_div2", "oscclk", "oscclk", "oscclk" };
PNAME(mout_cmu_peri_uart_p) = { "oscclk", "dout_pll_shared0_div4", "dout_pll_shared1_div4", "oscclk" };
PNAME(mout_cmu_vipx2_bus_p) = { "dout_pll_shared1_div2", "dout_pll_shared0_div3", "dout_pll_shared1_div3", "dout_pll_shared0_div4" };
PNAME(mout_cmu_cis_clk3_p) = { "oscclk", "dout_pll_shared0_div4" };

static const struct samsung_mux_clock top_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_G2D_MSCL, "mout_cmu_g2d_mscl", mout_cmu_g2d_mscl_p,
	    MUX_CLKCMU_G2D_MSCL, 0, 2),
	MUX(CLK_MOUT_CMU_DISPAUD_DISP, "mout_cmu_dispaud_disp", mout_cmu_dispaud_disp_p,
	    MUX_CLKCMU_DISPAUD_DISP, 0, 2),
	MUX(CLK_MOUT_CMU_FSYS_BUS, "mout_cmu_fsys_bus", mout_cmu_fsys_bus_p,
	    MUX_CLKCMU_FSYS_BUS, 0, 1),
	MUX(CLK_MOUT_CMU_FSYS_MMC_EMBD, "mout_cmu_fsys_mmc_embd", mout_cmu_fsys_mmc_embd_p,
	    MUX_CLKCMU_FSYS_MMC_EMBD, 0, 3),
	MUX(CLK_MOUT_CMU_PERI_BUS, "mout_cmu_peri_bus", mout_cmu_peri_bus_p,
	    MUX_CLKCMU_PERI_BUS, 0, 1),
	MUX(CLK_MOUT_CMU_PERI_IP, "mout_cmu_peri_ip", mout_cmu_peri_ip_p,
	    MUX_CLKCMU_PERI_IP, 0, 2),
	MUX(CLK_MOUT_CMU_FSYS_MMC_CARD, "mout_cmu_fsys_mmc_card", mout_cmu_fsys_mmc_card_p,
	    MUX_CLKCMU_FSYS_MMC_CARD, 0, 3),
	MUX(CLK_MOUT_CMU_CIS_CLK0, "mout_cmu_cis_clk0", mout_cmu_cis_clk0_p,
	    MUX_CLKCMU_CIS_CLK0, 0, 1),
	MUX(CLK_MOUT_CMU_CIS_CLK1, "mout_cmu_cis_clk1", mout_cmu_cis_clk1_p,
	    MUX_CLKCMU_CIS_CLK1, 0, 1),
	MUX(CLK_MOUT_CMU_CIS_CLK2, "mout_cmu_cis_clk2", mout_cmu_cis_clk2_p,
	    MUX_CLKCMU_CIS_CLK2, 0, 1),
	MUX(CLK_MOUT_CMU_CMUREF, "mout_cmu_cmuref", mout_cmu_cmuref_p,
	    MUX_CMU_CMUREF, 0, 1),
	MUX(CLK_MOUT_MUX_CLK_CMU_CMUREF, "mout_mux_clk_cmu_cmuref", mout_mux_clk_cmu_cmuref_p,
	    MUX_CLK_CMU_CMUREF, 0, 1),
	MUX(CLK_MOUT_CMU_APM_BUS, "mout_cmu_apm_bus", mout_cmu_apm_bus_p,
	    MUX_CLKCMU_APM_BUS, 0, 1),
	MUX(CLK_MOUT_CMU_CORE_CCI, "mout_cmu_core_cci", mout_cmu_core_cci_p,
	    MUX_CLKCMU_CORE_CCI, 0, 2),
	MUX(CLK_MOUT_CMU_CORE_G3D, "mout_cmu_core_g3d", mout_cmu_core_g3d_p,
	    MUX_CLKCMU_CORE_G3D, 0, 2),
	MUX(CLK_MOUT_CMU_CORE_BUS, "mout_cmu_core_bus", mout_cmu_core_bus_p,
	    MUX_CLKCMU_CORE_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_MIF_BUSP, "mout_cmu_mif_busp", mout_cmu_mif_busp_p,
	    MUX_CLKCMU_MIF_BUSP, 0, 2),
	MUX(CLK_MOUT_CMU_FSYS_UFS_EMBD, "mout_cmu_fsys_ufs_embd", mout_cmu_fsys_ufs_embd_p,
	    MUX_CLKCMU_FSYS_UFS_EMBD, 0, 2),
	MUX(CLK_MOUT_CMU_CAM_BUS, "mout_cmu_cam_bus", mout_cmu_cam_bus_p,
	    MUX_CLKCMU_CAM_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_VIPX1_BUS, "mout_cmu_vipx1_bus", mout_cmu_vipx1_bus_p,
	    MUX_CLKCMU_VIPX1_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_ISP_BUS, "mout_cmu_isp_bus", mout_cmu_isp_bus_p,
	    MUX_CLKCMU_ISP_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_ISP_VRA, "mout_cmu_isp_vra", mout_cmu_isp_vra_p,
	    MUX_CLKCMU_ISP_VRA, 0, 2),
	MUX(CLK_MOUT_CMU_ISP_GDC, "mout_cmu_isp_gdc", mout_cmu_isp_gdc_p,
	    MUX_CLKCMU_ISP_GDC, 0, 2),
	MUX(CLK_MOUT_CMU_G2D_G2D, "mout_cmu_g2d_g2d", mout_cmu_g2d_g2d_p,
	    MUX_CLKCMU_G2D_G2D, 0, 2),
	MUX(CLK_MOUT_CMU_CPUCL0_SWITCH, "mout_cmu_cpucl0_switch", mout_cmu_cpucl0_switch_p,
	    MUX_CLKCMU_CPUCL0_SWITCH, 0, 2),
	MUX(CLK_MOUT_CMU_CPUCL1_SWITCH, "mout_cmu_cpucl1_switch", mout_cmu_cpucl1_switch_p,
	    MUX_CLKCMU_CPUCL1_SWITCH, 0, 2),
	MUX(CLK_MOUT_CMU_G3D_SWITCH, "mout_cmu_g3d_switch", mout_cmu_g3d_switch_p,
	    MUX_CLKCMU_G3D_SWITCH, 0, 2),
	MUX(CLK_MOUT_CMU_DISPAUD_CPU, "mout_cmu_dispaud_cpu", mout_cmu_dispaud_cpu_p,
	    MUX_CLKCMU_DISPAUD_CPU, 0, 3),
	MUX(CLK_MOUT_CMU_MIF_SWITCH, "mout_cmu_mif_switch", mout_cmu_mif_switch_p,
	    MUX_CLKCMU_MIF_SWITCH, 0, 3),
	MUX(CLK_MOUT_CMU_CPUCL0_DBG, "mout_cmu_cpucl0_dbg", mout_cmu_cpucl0_dbg_p,
	    MUX_CLKCMU_CPUCL0_DBG, 0, 1),
	MUX(CLK_MOUT_CMU_USB_BUS, "mout_cmu_usb_bus", mout_cmu_usb_bus_p,
	    MUX_CLKCMU_USB_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_USB_USB30DRD, "mout_cmu_usb_usb30drd", mout_cmu_usb_usb30drd_p,
	    MUX_CLKCMU_USB_USB30DRD, 0, 2),
	MUX(CLK_MOUT_CMU_USB_DPGTC, "mout_cmu_usb_dpgtc", mout_cmu_usb_dpgtc_p,
	    MUX_CLKCMU_USB_DPGTC, 0, 2),
	MUX(CLK_MOUT_CMU_DISPAUD_AUD, "mout_cmu_dispaud_aud", mout_cmu_dispaud_aud_p,
	    MUX_CLKCMU_DISPAUD_AUD, 0, 2),
	MUX(CLK_MOUT_CMU_MFC_MFC, "mout_cmu_mfc_mfc", mout_cmu_mfc_mfc_p,
	    MUX_CLKCMU_MFC_MFC, 0, 2),
	MUX(CLK_MOUT_CMU_MFC_WFD, "mout_cmu_mfc_wfd", mout_cmu_mfc_wfd_p,
	    MUX_CLKCMU_MFC_WFD, 0, 2),
	MUX(CLK_MOUT_CMU_HPM, "mout_cmu_hpm", mout_cmu_hpm_p,
	    MUX_CLKCMU_HPM, 0, 3),
	MUX(CLK_MOUT_CMU_PERI_UART, "mout_cmu_peri_uart", mout_cmu_peri_uart_p,
	    MUX_CLKCMU_PERI_UART, 0, 2),
	MUX(CLK_MOUT_CMU_VIPX2_BUS, "mout_cmu_vipx2_bus", mout_cmu_vipx2_bus_p,
	    MUX_CLKCMU_VIPX2_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_CIS_CLK3, "mout_cmu_cis_clk3", mout_cmu_cis_clk3_p,
	    MUX_CLKCMU_CIS_CLK3, 0, 1),
};

static const struct samsung_div_clock top_div_clks[] __initconst = {
	DIV(CLK_DOUT_CLKCMU_DISPAUD_DISP, "dout_clkcmu_dispaud_disp", "gout_clkcmu_dispaud_disp",
	    CLKCMU_DISPAUD_DISP, 0, 4),
	DIV(CLK_DOUT_CLKCMU_FSYS_BUS, "dout_clkcmu_fsys_bus", "gout_clkcmu_fsys_bus",
	    CLKCMU_FSYS_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_G2D_MSCL, "dout_clkcmu_g2d_mscl", "gout_clkcmu_g2d_mscl",
	    CLKCMU_G2D_MSCL, 0, 4),
	DIV(CLK_DOUT_AP2CP_SHARED0_PLL_CLK, "dout_ap2cp_shared0_pll_clk",
	    "gout_clkcmu_modem_shared0",
	    AP2CP_SHARED0_PLL_CLK, 0, 4),
	DIV(CLK_DOUT_CLKCMU_PERI_BUS, "dout_clkcmu_peri_bus", "gout_clkcmu_peri_bus",
	    CLKCMU_PERI_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_PERI_IP, "dout_clkcmu_peri_ip", "gout_clkcmu_peri_ip",
	    CLKCMU_PERI_IP, 0, 4),
	DIV(CLK_DOUT_CLKCMU_APM_BUS, "dout_clkcmu_apm_bus", "gout_clkcmu_apm_bus",
	    CLKCMU_APM_BUS, 0, 3),
	DIV(CLK_DOUT_CLKCMU_FSYS_MMC_CARD, "dout_clkcmu_fsys_mmc_card", "gout_clkcmu_fsys_mmc_card",
	    CLKCMU_FSYS_MMC_CARD, 0, 9),
	DIV(CLK_DOUT_CLKCMU_CIS_CLK0, "dout_clkcmu_cis_clk0", "gout_clkcmu_cis_clk0",
	    CLKCMU_CIS_CLK0, 0, 5),
	DIV(CLK_DOUT_CLKCMU_CIS_CLK1, "dout_clkcmu_cis_clk1", "gout_clkcmu_cis_clk1",
	    CLKCMU_CIS_CLK1, 0, 5),
	DIV(CLK_DOUT_CLKCMU_CIS_CLK2, "dout_clkcmu_cis_clk2", "gout_clkcmu_cis_clk2",
	    CLKCMU_CIS_CLK2, 0, 5),
	DIV(CLK_DOUT_CLKCMU_FSYS_MMC_EMBD, "dout_clkcmu_fsys_mmc_embd", "gout_clkcmu_fsys_mmc_embd",
	    CLKCMU_FSYS_MMC_EMBD, 0, 9),
	DIV(CLK_DOUT_AP2CP_SHARED1_PLL_CLK, "dout_ap2cp_shared1_pll_clk",
	    "gout_clkcmu_modem_shared1",
	    AP2CP_SHARED1_PLL_CLK, 0, 4),
	DIV(CLK_DOUT_CMU_CMUREF, "dout_cmu_cmuref", "mout_mux_clk_cmu_cmuref",
	    DIV_CLK_CMU_CMUREF, 0, 2),
	DIV(CLK_DOUT_CLKCMU_CORE_BUS, "dout_clkcmu_core_bus", "gout_clkcmu_core_bus",
	    CLKCMU_CORE_BUS, 0, 4),
	DIV(CLK_DOUT_PLL_SHARED0_DIV3, "dout_pll_shared0_div3", "fout_shared0",
	    PLL_SHARED0_DIV3, 0, 2),
	DIV(CLK_DOUT_CLKCMU_CPUCL0_DBG, "dout_clkcmu_cpucl0_dbg", "gout_clkcmu_cpucl0_dbg",
	    CLKCMU_CPUCL0_DBG, 0, 3),
	DIV(CLK_DOUT_PLL_SHARED0_DIV2, "dout_pll_shared0_div2", "fout_shared0",
	    PLL_SHARED0_DIV2, 0, 1),
	DIV(CLK_DOUT_PLL_SHARED0_DIV4, "dout_pll_shared0_div4", "dout_pll_shared0_div2",
	    PLL_SHARED0_DIV4, 0, 1),
	DIV(CLK_DOUT_PLL_SHARED1_DIV2, "dout_pll_shared1_div2", "fout_shared1",
	    PLL_SHARED1_DIV2, 0, 1),
	DIV(CLK_DOUT_PLL_SHARED1_DIV4, "dout_pll_shared1_div4", "dout_pll_shared1_div2",
	    PLL_SHARED1_DIV4, 0, 1),
	DIV(CLK_DOUT_CLKCMU_CORE_CCI, "dout_clkcmu_core_cci", "gout_clkcmu_core_cci",
	    CLKCMU_CORE_CCI, 0, 4),
	DIV(CLK_DOUT_CLKCMU_CORE_G3D, "dout_clkcmu_core_g3d", "gout_clkcmu_core_g3d",
	    CLKCMU_CORE_G3D, 0, 4),
	DIV(CLK_DOUT_CLKCMU_MIF_BUSP, "dout_clkcmu_mif_busp", "gout_clkcmu_mif_busp",
	    CLKCMU_MIF_BUSP, 0, 3),
	DIV(CLK_DOUT_PLL_SHARED1_DIV3, "dout_pll_shared1_div3", "fout_shared1",
	    PLL_SHARED1_DIV3, 0, 2),
	DIV(CLK_DOUT_CLKCMU_FSYS_UFS_EMBD, "dout_clkcmu_fsys_ufs_embd", "gout_clkcmu_fsys_ufs_embd",
	    CLKCMU_FSYS_UFS_EMBD, 0, 4),
	DIV(CLK_DOUT_CLKCMU_CAM_BUS, "dout_clkcmu_cam_bus", "gout_clkcmu_cam_bus",
	    CLKCMU_CAM_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_VIPX1_BUS, "dout_clkcmu_vipx1_bus", "gout_clkcmu_vipx1_bus",
	    CLKCMU_VIPX1_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_ISP_BUS, "dout_clkcmu_isp_bus", "gout_clkcmu_isp_bus",
	    CLKCMU_ISP_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_ISP_VRA, "dout_clkcmu_isp_vra", "gout_clkcmu_isp_vra",
	    CLKCMU_ISP_VRA, 0, 4),
	DIV(CLK_DOUT_CLKCMU_ISP_GDC, "dout_clkcmu_isp_gdc", "gout_clkcmu_isp_gdc",
	    CLKCMU_ISP_GDC, 0, 4),
	DIV(CLK_DOUT_CLKCMU_G2D_G2D, "dout_clkcmu_g2d_g2d", "gout_clkcmu_g2d_g2d",
	    CLKCMU_G2D_G2D, 0, 4),
	DIV(CLK_DOUT_CLKCMU_CPUCL0_SWITCH, "dout_clkcmu_cpucl0_switch", "gout_clkcmu_cpucl0_switch",
	    CLKCMU_CPUCL0_SWITCH, 0, 3),
	DIV(CLK_DOUT_CLKCMU_CPUCL1_SWITCH, "dout_clkcmu_cpucl1_switch", "gout_clkcmu_cpucl1_switch",
	    CLKCMU_CPUCL1_SWITCH, 0, 3),
	DIV(CLK_DOUT_CLKCMU_G3D_SWITCH, "dout_clkcmu_g3d_switch", "gout_clkcmu_g3d_switch",
	    CLKCMU_G3D_SWITCH, 0, 3),
	DIV(CLK_DOUT_CLKCMU_DISPAUD_CPU, "dout_clkcmu_dispaud_cpu", "gout_clkcmu_dispaud_cpu",
	    CLKCMU_DISPAUD_CPU, 0, 4),
	DIV(CLK_DOUT_CLKCMU_USB_BUS, "dout_clkcmu_usb_bus", "gout_clkcmu_usb_bus",
	    CLKCMU_USB_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_USB_USB30DRD, "dout_clkcmu_usb_usb30drd", "gout_clkcmu_usb_usb30drd",
	    CLKCMU_USB_USB30DRD, 0, 4),
	DIV(CLK_DOUT_CLKCMU_USB_DPGTC, "dout_clkcmu_usb_dpgtc", "gout_clkcmu_usb_dpgtc",
	    CLKCMU_USB_DPGTC, 0, 4),
	DIV(CLK_DOUT_CLKCMU_DISPAUD_AUD, "dout_clkcmu_dispaud_aud", "gout_clkcmu_dispaud_aud",
	    CLKCMU_DISPAUD_AUD, 0, 4),
	DIV(CLK_DOUT_CLKCMU_MFC_MFC, "dout_clkcmu_mfc_mfc", "gout_clkcmu_mfc_mfc",
	    CLKCMU_MFC_MFC, 0, 4),
	DIV(CLK_DOUT_CLKCMU_MFC_WFD, "dout_clkcmu_mfc_wfd", "gout_clkcmu_mfc_wfd",
	    CLKCMU_MFC_WFD, 0, 4),
	DIV(CLK_DOUT_CLKCMU_HPM, "dout_clkcmu_hpm", "gout_clkcmu_hpm",
	    CLKCMU_HPM, 0, 2),
	DIV(CLK_DOUT_CLKCMU_PERI_UART, "dout_clkcmu_peri_uart", "gout_clkcmu_peri_uart",
	    CLKCMU_PERI_UART, 0, 4),
	DIV(CLK_DOUT_CLKCMU_VIPX2_BUS, "dout_clkcmu_vipx2_bus", "gout_clkcmu_vipx2_bus",
	    CLKCMU_VIPX2_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_CIS_CLK3, "dout_clkcmu_cis_clk3", "gout_clkcmu_cis_clk3",
	    CLKCMU_CIS_CLK3, 0, 5),
	DIV(CLK_DOUT_PLL_MMC_DIV2, "dout_pll_mmc_div2", "fout_mmc",
	    PLL_MMC_DIV2, 0, 1),
};

static const struct samsung_gate_clock top_gate_clks[] __initconst = {
	GATE(CLK_GOUT_CLKCMU_G2D_MSCL, "gout_clkcmu_g2d_mscl", "mout_cmu_g2d_mscl",
	     GATE_CLKCMU_G2D_MSCL, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_DISPAUD_DISP, "gout_clkcmu_dispaud_disp", "mout_cmu_dispaud_disp",
	     GATE_CLKCMU_DISPAUD_DISP, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_FSYS_MMC_EMBD, "gout_clkcmu_fsys_mmc_embd", "mout_cmu_fsys_mmc_embd",
	     GATE_CLKCMU_FSYS_MMC_EMBD, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_FSYS_BUS, "gout_clkcmu_fsys_bus", "mout_cmu_fsys_bus",
	     GATE_CLKCMU_FSYS_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_MODEM_SHARED0, "gout_clkcmu_modem_shared0", "dout_pll_shared0_div2",
	     GATE_CLKCMU_MODEM_SHARED0, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_PERI_BUS, "gout_clkcmu_peri_bus", "mout_cmu_peri_bus",
	     GATE_CLKCMU_PERI_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_PERI_IP, "gout_clkcmu_peri_ip", "mout_cmu_peri_ip",
	     GATE_CLKCMU_PERI_IP, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_APM_BUS, "gout_clkcmu_apm_bus", "mout_cmu_apm_bus",
	     GATE_CLKCMU_APM_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_FSYS_MMC_CARD, "gout_clkcmu_fsys_mmc_card", "mout_cmu_fsys_mmc_card",
	     GATE_CLKCMU_FSYS_MMC_CARD, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_CIS_CLK0, "gout_clkcmu_cis_clk0", "mout_cmu_cis_clk0",
	     GATE_CLKCMU_CIS_CLK0, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_CIS_CLK1, "gout_clkcmu_cis_clk1", "mout_cmu_cis_clk1",
	     GATE_CLKCMU_CIS_CLK1, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_CIS_CLK2, "gout_clkcmu_cis_clk2", "mout_cmu_cis_clk2",
	     GATE_CLKCMU_CIS_CLK2, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_MODEM_SHARED1, "gout_clkcmu_modem_shared1", "dout_pll_shared1_div2",
	     GATE_CLKCMU_MODEM_SHARED1, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_CORE_BUS, "gout_clkcmu_core_bus", "mout_cmu_core_bus",
	     GATE_CLKCMU_CORE_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_CPUCL0_DBG, "gout_clkcmu_cpucl0_dbg", "mout_cmu_cpucl0_dbg",
	     GATE_CLKCMU_CPUCL0_DBG, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_CORE_CCI, "gout_clkcmu_core_cci", "mout_cmu_core_cci",
	     GATE_CLKCMU_CORE_CCI, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_CORE_G3D, "gout_clkcmu_core_g3d", "mout_cmu_core_g3d",
	     GATE_CLKCMU_CORE_G3D, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_MIF_BUSP, "gout_clkcmu_mif_busp", "mout_cmu_mif_busp",
	     GATE_CLKCMU_MIF_BUSP, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_FSYS_UFS_EMBD, "gout_clkcmu_fsys_ufs_embd", "mout_cmu_fsys_ufs_embd",
	     GATE_CLKCMU_FSYS_UFS_EMBD, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_CAM_BUS, "gout_clkcmu_cam_bus", "mout_cmu_cam_bus",
	     GATE_CLKCMU_CAM_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_VIPX1_BUS, "gout_clkcmu_vipx1_bus", "mout_cmu_vipx1_bus",
	     GATE_CLKCMU_VIPX1_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_ISP_BUS, "gout_clkcmu_isp_bus", "mout_cmu_isp_bus",
	     GATE_CLKCMU_ISP_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_ISP_VRA, "gout_clkcmu_isp_vra", "mout_cmu_isp_vra",
	     GATE_CLKCMU_ISP_VRA, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_ISP_GDC, "gout_clkcmu_isp_gdc", "mout_cmu_isp_gdc",
	     GATE_CLKCMU_ISP_GDC, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_G2D_G2D, "gout_clkcmu_g2d_g2d", "mout_cmu_g2d_g2d",
	     GATE_CLKCMU_G2D_G2D, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_CPUCL0_SWITCH, "gout_clkcmu_cpucl0_switch", "mout_cmu_cpucl0_switch",
	     GATE_CLKCMU_CPUCL0_SWITCH, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_CPUCL1_SWITCH, "gout_clkcmu_cpucl1_switch", "mout_cmu_cpucl1_switch",
	     GATE_CLKCMU_CPUCL1_SWITCH, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_G3D_SWITCH, "gout_clkcmu_g3d_switch", "mout_cmu_g3d_switch",
	     GATE_CLKCMU_G3D_SWITCH, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_DISPAUD_CPU, "gout_clkcmu_dispaud_cpu", "mout_cmu_dispaud_cpu",
	     GATE_CLKCMU_DISPAUD_CPU, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_MIF_SWITCH, "gout_clkcmu_mif_switch", "mout_cmu_mif_switch",
	     CLKCMU_MIF_SWITCH, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_USB_BUS, "gout_clkcmu_usb_bus", "mout_cmu_usb_bus",
	     GATE_CLKCMU_USB_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_USB_USB30DRD, "gout_clkcmu_usb_usb30drd", "mout_cmu_usb_usb30drd",
	     GATE_CLKCMU_USB_USB30DRD, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_USB_DPGTC, "gout_clkcmu_usb_dpgtc", "mout_cmu_usb_dpgtc",
	     GATE_CLKCMU_USB_DPGTC, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_DISPAUD_AUD, "gout_clkcmu_dispaud_aud", "mout_cmu_dispaud_aud",
	     GATE_CLKCMU_DISPAUD_AUD, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_MFC_MFC, "gout_clkcmu_mfc_mfc", "mout_cmu_mfc_mfc",
	     GATE_CLKCMU_MFC_MFC, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_MFC_WFD, "gout_clkcmu_mfc_wfd", "mout_cmu_mfc_wfd",
	     GATE_CLKCMU_MFC_WFD, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_HPM, "gout_clkcmu_hpm", "mout_cmu_hpm",
	     GATE_CLKCMU_HPM, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_PERI_UART, "gout_clkcmu_peri_uart", "mout_cmu_peri_uart",
	     GATE_CLKCMU_PERI_UART, 21, 0, 0),
	GATE(CLK_GOUT_CMU_OTP_CLK, "gout_cmu_otp_clk", "UNRESOLVED_CLKCMU_OTP",
	     CLK_BLK_CMU_UID_OTP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_VIPX2_BUS, "gout_clkcmu_vipx2_bus", "mout_cmu_vipx2_bus",
	     GATE_CLKCMU_VIPX2_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_CIS_CLK3, "gout_clkcmu_cis_clk3", "mout_cmu_cis_clk3",
	     GATE_CLKCMU_CIS_CLK3, 21, 0, 0),
};

static const struct samsung_cmu_info top_cmu_info __initconst = {
	.pll_clks		= top_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(top_pll_clks),
	.mux_clks		= top_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(top_mux_clks),
	.div_clks		= top_div_clks,
	.nr_div_clks		= ARRAY_SIZE(top_div_clks),
	.gate_clks		= top_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(top_gate_clks),
	.fixed_clks		= top_fixed_clks,
	.nr_fixed_clks		= ARRAY_SIZE(top_fixed_clks),
	.nr_clk_ids		= CLKS_NR_TOP,
	.clk_regs		= top_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(top_clk_regs),
	.qch_regs		= top_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(top_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9610_cmu_top_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &top_cmu_info);
}

CLK_OF_DECLARE(exynos9610_cmu_top, "samsung,exynos9610-cmu-top",
	       exynos9610_cmu_top_init);

/* ---- CMU_CORE --------------------------------------------------------*/

/* Register Offset definitions for CMU_CORE (0x120f0000) */
#define MUX_CLKCMU_CORE_BUS_USER			0x0100
#define MUX_CLKCMU_CORE_CCI_USER			0x0120
#define MUX_CLKCMU_CORE_G3D_USER			0x0140
#define MUX_CLK_CORE_GIC			0x1000
#define DIV_CLK_CORE_BUSP			0x1800
#define CLK_BLK_CORE_UID_CORE_CMU_CORE_IPCLKPORT_PCLK	0x2000
#define GOUT_BLK_CORE_UID_AD_APB_CCI_550_IPCLKPORT_PCLKM	0x2004
#define GOUT_BLK_CORE_UID_AD_APB_DIT_IPCLKPORT_PCLKM	0x2008
#define GOUT_BLK_CORE_UID_AD_APB_PDMA0_IPCLKPORT_PCLKM	0x200c
#define GOUT_BLK_CORE_UID_AD_APB_PGEN_PDMA_IPCLKPORT_PCLKM	0x2010
#define GOUT_BLK_CORE_UID_AD_APB_PPFW_MEM0_IPCLKPORT_PCLKM	0x2014
#define GOUT_BLK_CORE_UID_AD_APB_PPFW_MEM1_IPCLKPORT_PCLKM	0x2018
#define GOUT_BLK_CORE_UID_AD_APB_PPFW_PERI_IPCLKPORT_PCLKM	0x201c
#define GOUT_BLK_CORE_UID_AD_APB_SPDMA_IPCLKPORT_PCLKM	0x2020
#define GOUT_BLK_CORE_UID_AD_AXI_GIC_IPCLKPORT_ACLKM	0x2024
#define GOUT_BLK_CORE_UID_ASYNCSFR_WR_DMC0_IPCLKPORT_I_PCLK	0x2028
#define GOUT_BLK_CORE_UID_ASYNCSFR_WR_DMC1_IPCLKPORT_I_PCLK	0x202c
#define GOUT_BLK_CORE_UID_AXI_US_A40_64to128_DIT_IPCLKPORT_aclk	0x2030
#define GOUT_BLK_CORE_UID_BAAW_P_GNSS_IPCLKPORT_I_PCLK	0x2034
#define GOUT_BLK_CORE_UID_BAAW_P_MODEM_IPCLKPORT_I_PCLK	0x2038
#define GOUT_BLK_CORE_UID_BAAW_P_SHUB_IPCLKPORT_I_PCLK	0x203c
#define GOUT_BLK_CORE_UID_BAAW_P_WLBT_IPCLKPORT_I_PCLK	0x2040
#define GOUT_BLK_CORE_UID_CCI_550_IPCLKPORT_ACLK	0x2044
#define GOUT_BLK_CORE_UID_DIT_IPCLKPORT_iClkL2A			0x2048
#define GOUT_BLK_CORE_UID_GIC400_AIHWACG_IPCLKPORT_CLK	0x204c
#define GOUT_BLK_CORE_UID_LHM_ACEL_D0_ISP_IPCLKPORT_I_CLK	0x2050
#define GOUT_BLK_CORE_UID_LHM_ACEL_D0_MFC_IPCLKPORT_I_CLK	0x2054
#define GOUT_BLK_CORE_UID_LHM_ACEL_D1_ISP_IPCLKPORT_I_CLK	0x2058
#define GOUT_BLK_CORE_UID_LHM_ACEL_D1_MFC_IPCLKPORT_I_CLK	0x205c
#define GOUT_BLK_CORE_UID_LHM_ACEL_D_CAM_IPCLKPORT_I_CLK	0x2060
#define GOUT_BLK_CORE_UID_LHM_ACEL_D_DPU_IPCLKPORT_I_CLK	0x2064
#define GOUT_BLK_CORE_UID_LHM_ACEL_D_FSYS_IPCLKPORT_I_CLK	0x2068
#define GOUT_BLK_CORE_UID_LHM_ACEL_D_G2D_IPCLKPORT_I_CLK	0x206c
#define GOUT_BLK_CORE_UID_LHM_ACEL_D_USB_IPCLKPORT_I_CLK	0x2070
#define GOUT_BLK_CORE_UID_LHM_ACEL_D_VIPX1_IPCLKPORT_I_CLK	0x2074
#define GOUT_BLK_CORE_UID_LHM_ACEL_D_VIPX2_IPCLKPORT_I_CLK	0x2078
#define GOUT_BLK_CORE_UID_LHM_ACE_D_CPUCL0_IPCLKPORT_I_CLK	0x207c
#define GOUT_BLK_CORE_UID_LHM_ACE_D_CPUCL1_IPCLKPORT_I_CLK	0x2080
#define GOUT_BLK_CORE_UID_LHM_AXI_D0_MODEM_IPCLKPORT_I_CLK	0x2084
#define GOUT_BLK_CORE_UID_LHM_AXI_D1_MODEM_IPCLKPORT_I_CLK	0x2088
#define GOUT_BLK_CORE_UID_LHM_AXI_D_ABOX_IPCLKPORT_I_CLK	0x208c
#define GOUT_BLK_CORE_UID_LHM_AXI_D_APM_IPCLKPORT_I_CLK	0x2090
#define GOUT_BLK_CORE_UID_LHM_AXI_D_CSSYS_IPCLKPORT_I_CLK	0x2094
#define GOUT_BLK_CORE_UID_LHM_AXI_D_G3D_IPCLKPORT_I_CLK	0x2098
#define GOUT_BLK_CORE_UID_LHM_AXI_D_GNSS_IPCLKPORT_I_CLK	0x209c
#define GOUT_BLK_CORE_UID_LHM_AXI_D_SHUB_IPCLKPORT_I_CLK	0x20a0
#define GOUT_BLK_CORE_UID_LHM_AXI_D_WLBT_IPCLKPORT_I_CLK	0x20a4
#define GOUT_BLK_CORE_UID_LHS_AXI_D0_MIF_CPU_IPCLKPORT_I_CLK	0x20a8
#define GOUT_BLK_CORE_UID_LHS_AXI_D0_MIF_CP_IPCLKPORT_I_CLK	0x20ac
#define GOUT_BLK_CORE_UID_LHS_AXI_D0_MIF_NRT_IPCLKPORT_I_CLK	0x20b0
#define GOUT_BLK_CORE_UID_LHS_AXI_D0_MIF_RT_IPCLKPORT_I_CLK	0x20b4
#define GOUT_BLK_CORE_UID_LHS_AXI_D1_MIF_CPU_IPCLKPORT_I_CLK	0x20b8
#define GOUT_BLK_CORE_UID_LHS_AXI_D1_MIF_CP_IPCLKPORT_I_CLK	0x20bc
#define GOUT_BLK_CORE_UID_LHS_AXI_D1_MIF_NRT_IPCLKPORT_I_CLK	0x20c0
#define GOUT_BLK_CORE_UID_LHS_AXI_D1_MIF_RT_IPCLKPORT_I_CLK	0x20c4
#define GOUT_BLK_CORE_UID_LHS_AXI_P_APM_IPCLKPORT_I_CLK	0x20c8
#define GOUT_BLK_CORE_UID_LHS_AXI_P_CAM_IPCLKPORT_I_CLK	0x20cc
#define GOUT_BLK_CORE_UID_LHS_AXI_P_CPUCL0_IPCLKPORT_I_CLK	0x20d0
#define GOUT_BLK_CORE_UID_LHS_AXI_P_CPUCL1_IPCLKPORT_I_CLK	0x20d4
#define GOUT_BLK_CORE_UID_LHS_AXI_P_DISPAUD_IPCLKPORT_I_CLK	0x20d8
#define GOUT_BLK_CORE_UID_LHS_AXI_P_FSYS_IPCLKPORT_I_CLK	0x20dc
#define GOUT_BLK_CORE_UID_LHS_AXI_P_G2D_IPCLKPORT_I_CLK	0x20e0
#define GOUT_BLK_CORE_UID_LHS_AXI_P_G3D_IPCLKPORT_I_CLK	0x20e4
#define GOUT_BLK_CORE_UID_LHS_AXI_P_GNSS_IPCLKPORT_I_CLK	0x20e8
#define GOUT_BLK_CORE_UID_LHS_AXI_P_ISP_IPCLKPORT_I_CLK	0x20ec
#define GOUT_BLK_CORE_UID_LHS_AXI_P_MFC_IPCLKPORT_I_CLK	0x20f0
#define GOUT_BLK_CORE_UID_LHS_AXI_P_MIF0_IPCLKPORT_I_CLK	0x20f4
#define GOUT_BLK_CORE_UID_LHS_AXI_P_MIF1_IPCLKPORT_I_CLK	0x20f8
#define GOUT_BLK_CORE_UID_LHS_AXI_P_MODEM_IPCLKPORT_I_CLK	0x20fc
#define GOUT_BLK_CORE_UID_LHS_AXI_P_PERI_IPCLKPORT_I_CLK	0x2100
#define GOUT_BLK_CORE_UID_LHS_AXI_P_SHUB_IPCLKPORT_I_CLK	0x2104
#define GOUT_BLK_CORE_UID_LHS_AXI_P_USB_IPCLKPORT_I_CLK	0x2108
#define GOUT_BLK_CORE_UID_LHS_AXI_P_VIPX1_IPCLKPORT_I_CLK	0x210c
#define GOUT_BLK_CORE_UID_LHS_AXI_P_VIPX2_IPCLKPORT_I_CLK	0x2110
#define GOUT_BLK_CORE_UID_LHS_AXI_P_WLBT_IPCLKPORT_I_CLK	0x2114
#define GOUT_BLK_CORE_UID_PDMA_CORE_IPCLKPORT_ACLK_PDMA0	0x2118
#define GOUT_BLK_CORE_UID_PGEN_LITE_SIREX_IPCLKPORT_CLK	0x211c
#define GOUT_BLK_CORE_UID_PGEN_PDMA_IPCLKPORT_CLK	0x2120
#define GOUT_BLK_CORE_UID_PPCFW_G3D_IPCLKPORT_ACLK	0x2124
#define GOUT_BLK_CORE_UID_PPCFW_G3D_IPCLKPORT_PCLK	0x2128
#define GOUT_BLK_CORE_UID_PPFW_CORE_MEM0_IPCLKPORT_CLK	0x212c
#define GOUT_BLK_CORE_UID_PPFW_CORE_MEM1_IPCLKPORT_CLK	0x2130
#define GOUT_BLK_CORE_UID_PPFW_CORE_PERI_IPCLKPORT_CLK	0x2134
#define GOUT_BLK_CORE_UID_PPMU_ACE_CPUCL0_IPCLKPORT_ACLK	0x2138
#define GOUT_BLK_CORE_UID_PPMU_ACE_CPUCL0_IPCLKPORT_PCLK	0x213c
#define GOUT_BLK_CORE_UID_PPMU_ACE_CPUCL1_IPCLKPORT_ACLK	0x2140
#define GOUT_BLK_CORE_UID_PPMU_ACE_CPUCL1_IPCLKPORT_PCLK	0x2144
#define GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_BUSD_IPCLKPORT_CLK	0x2148
#define GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_BUSP_G3D_OCC_IPCLKPORT_CLK	0x214c
#define GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_BUSP_IPCLKPORT_CLK	0x2150
#define GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_BUSP_OCC_IPCLKPORT_CLK	0x2154
#define GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_CCI_IPCLKPORT_CLK	0x2158
#define GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_CCI_OCC_IPCLKPORT_CLK	0x215c
#define GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_G3D_IPCLKPORT_CLK	0x2160
#define GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_G3D_OCC_IPCLKPORT_CLK	0x2164
#define GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_GIC_IPCLKPORT_CLK	0x2168
#define GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_OSCCLK_IPCLKPORT_CLK	0x216c
#define GOUT_BLK_CORE_UID_SFR_APBIF_CMU_TOPC_IPCLKPORT_PCLK	0x2170
#define GOUT_BLK_CORE_UID_SIREX_IPCLKPORT_i_ACLK	0x2174
#define GOUT_BLK_CORE_UID_SIREX_IPCLKPORT_i_PCLK	0x2178
#define GOUT_BLK_CORE_UID_SPDMA_CORE_IPCLKPORT_ACLK_PDMA1	0x217c
#define GOUT_BLK_CORE_UID_SYSREG_CORE_IPCLKPORT_PCLK	0x2180
#define GOUT_BLK_CORE_UID_TREX_D_CORE_IPCLKPORT_ACLK	0x2184
#define GOUT_BLK_CORE_UID_TREX_D_CORE_IPCLKPORT_CCLK	0x2188
#define GOUT_BLK_CORE_UID_TREX_D_CORE_IPCLKPORT_GCLK	0x218c
#define GOUT_BLK_CORE_UID_TREX_D_CORE_IPCLKPORT_pclk	0x2190
#define GOUT_BLK_CORE_UID_TREX_D_NRT_IPCLKPORT_ACLK	0x2194
#define GOUT_BLK_CORE_UID_TREX_D_NRT_IPCLKPORT_pclk	0x2198
#define GOUT_BLK_CORE_UID_TREX_P_CORE_IPCLKPORT_ACLK_P_CORE	0x219c
#define GOUT_BLK_CORE_UID_TREX_P_CORE_IPCLKPORT_CCLK_P_CORE	0x21a0
#define GOUT_BLK_CORE_UID_TREX_P_CORE_IPCLKPORT_pclk	0x21a4
#define GOUT_BLK_CORE_UID_TREX_P_CORE_IPCLKPORT_PCLK_P_CORE	0x21a8
#define GOUT_BLK_CORE_UID_XIU_D_CORE_IPCLKPORT_ACLK	0x21ac

static const unsigned long core_clk_regs[] __initconst = {
	MUX_CLKCMU_CORE_BUS_USER,
	MUX_CLKCMU_CORE_CCI_USER,
	MUX_CLKCMU_CORE_G3D_USER,
	MUX_CLK_CORE_GIC,
	DIV_CLK_CORE_BUSP,
	CLK_BLK_CORE_UID_CORE_CMU_CORE_IPCLKPORT_PCLK,
	GOUT_BLK_CORE_UID_AD_APB_CCI_550_IPCLKPORT_PCLKM,
	GOUT_BLK_CORE_UID_AD_APB_DIT_IPCLKPORT_PCLKM,
	GOUT_BLK_CORE_UID_AD_APB_PDMA0_IPCLKPORT_PCLKM,
	GOUT_BLK_CORE_UID_AD_APB_PGEN_PDMA_IPCLKPORT_PCLKM,
	GOUT_BLK_CORE_UID_AD_APB_PPFW_MEM0_IPCLKPORT_PCLKM,
	GOUT_BLK_CORE_UID_AD_APB_PPFW_MEM1_IPCLKPORT_PCLKM,
	GOUT_BLK_CORE_UID_AD_APB_PPFW_PERI_IPCLKPORT_PCLKM,
	GOUT_BLK_CORE_UID_AD_APB_SPDMA_IPCLKPORT_PCLKM,
	GOUT_BLK_CORE_UID_AD_AXI_GIC_IPCLKPORT_ACLKM,
	GOUT_BLK_CORE_UID_ASYNCSFR_WR_DMC0_IPCLKPORT_I_PCLK,
	GOUT_BLK_CORE_UID_ASYNCSFR_WR_DMC1_IPCLKPORT_I_PCLK,
	GOUT_BLK_CORE_UID_AXI_US_A40_64to128_DIT_IPCLKPORT_aclk,
	GOUT_BLK_CORE_UID_BAAW_P_GNSS_IPCLKPORT_I_PCLK,
	GOUT_BLK_CORE_UID_BAAW_P_MODEM_IPCLKPORT_I_PCLK,
	GOUT_BLK_CORE_UID_BAAW_P_SHUB_IPCLKPORT_I_PCLK,
	GOUT_BLK_CORE_UID_BAAW_P_WLBT_IPCLKPORT_I_PCLK,
	GOUT_BLK_CORE_UID_CCI_550_IPCLKPORT_ACLK,
	GOUT_BLK_CORE_UID_DIT_IPCLKPORT_iClkL2A,
	GOUT_BLK_CORE_UID_GIC400_AIHWACG_IPCLKPORT_CLK,
	GOUT_BLK_CORE_UID_LHM_ACEL_D0_ISP_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHM_ACEL_D0_MFC_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHM_ACEL_D1_ISP_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHM_ACEL_D1_MFC_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHM_ACEL_D_CAM_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHM_ACEL_D_DPU_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHM_ACEL_D_FSYS_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHM_ACEL_D_G2D_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHM_ACEL_D_USB_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHM_ACEL_D_VIPX1_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHM_ACEL_D_VIPX2_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHM_ACE_D_CPUCL0_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHM_ACE_D_CPUCL1_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHM_AXI_D0_MODEM_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHM_AXI_D1_MODEM_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHM_AXI_D_ABOX_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHM_AXI_D_APM_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHM_AXI_D_CSSYS_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHM_AXI_D_G3D_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHM_AXI_D_GNSS_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHM_AXI_D_SHUB_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHM_AXI_D_WLBT_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_D0_MIF_CPU_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_D0_MIF_CP_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_D0_MIF_NRT_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_D0_MIF_RT_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_D1_MIF_CPU_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_D1_MIF_CP_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_D1_MIF_NRT_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_D1_MIF_RT_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_APM_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_CAM_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_CPUCL0_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_CPUCL1_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_DISPAUD_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_FSYS_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_G2D_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_G3D_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_GNSS_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_ISP_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_MFC_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_MIF0_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_MIF1_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_MODEM_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_PERI_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_SHUB_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_USB_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_VIPX1_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_VIPX2_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_WLBT_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_PDMA_CORE_IPCLKPORT_ACLK_PDMA0,
	GOUT_BLK_CORE_UID_PGEN_LITE_SIREX_IPCLKPORT_CLK,
	GOUT_BLK_CORE_UID_PGEN_PDMA_IPCLKPORT_CLK,
	GOUT_BLK_CORE_UID_PPCFW_G3D_IPCLKPORT_ACLK,
	GOUT_BLK_CORE_UID_PPCFW_G3D_IPCLKPORT_PCLK,
	GOUT_BLK_CORE_UID_PPFW_CORE_MEM0_IPCLKPORT_CLK,
	GOUT_BLK_CORE_UID_PPFW_CORE_MEM1_IPCLKPORT_CLK,
	GOUT_BLK_CORE_UID_PPFW_CORE_PERI_IPCLKPORT_CLK,
	GOUT_BLK_CORE_UID_PPMU_ACE_CPUCL0_IPCLKPORT_ACLK,
	GOUT_BLK_CORE_UID_PPMU_ACE_CPUCL0_IPCLKPORT_PCLK,
	GOUT_BLK_CORE_UID_PPMU_ACE_CPUCL1_IPCLKPORT_ACLK,
	GOUT_BLK_CORE_UID_PPMU_ACE_CPUCL1_IPCLKPORT_PCLK,
	GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_BUSD_IPCLKPORT_CLK,
	GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_BUSP_G3D_OCC_IPCLKPORT_CLK,
	GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_BUSP_IPCLKPORT_CLK,
	GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_BUSP_OCC_IPCLKPORT_CLK,
	GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_CCI_IPCLKPORT_CLK,
	GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_CCI_OCC_IPCLKPORT_CLK,
	GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_G3D_IPCLKPORT_CLK,
	GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_G3D_OCC_IPCLKPORT_CLK,
	GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_GIC_IPCLKPORT_CLK,
	GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_OSCCLK_IPCLKPORT_CLK,
	GOUT_BLK_CORE_UID_SFR_APBIF_CMU_TOPC_IPCLKPORT_PCLK,
	GOUT_BLK_CORE_UID_SIREX_IPCLKPORT_i_ACLK,
	GOUT_BLK_CORE_UID_SIREX_IPCLKPORT_i_PCLK,
	GOUT_BLK_CORE_UID_SPDMA_CORE_IPCLKPORT_ACLK_PDMA1,
	GOUT_BLK_CORE_UID_SYSREG_CORE_IPCLKPORT_PCLK,
	GOUT_BLK_CORE_UID_TREX_D_CORE_IPCLKPORT_ACLK,
	GOUT_BLK_CORE_UID_TREX_D_CORE_IPCLKPORT_CCLK,
	GOUT_BLK_CORE_UID_TREX_D_CORE_IPCLKPORT_GCLK,
	GOUT_BLK_CORE_UID_TREX_D_CORE_IPCLKPORT_pclk,
	GOUT_BLK_CORE_UID_TREX_D_NRT_IPCLKPORT_ACLK,
	GOUT_BLK_CORE_UID_TREX_D_NRT_IPCLKPORT_pclk,
	GOUT_BLK_CORE_UID_TREX_P_CORE_IPCLKPORT_ACLK_P_CORE,
	GOUT_BLK_CORE_UID_TREX_P_CORE_IPCLKPORT_CCLK_P_CORE,
	GOUT_BLK_CORE_UID_TREX_P_CORE_IPCLKPORT_pclk,
	GOUT_BLK_CORE_UID_TREX_P_CORE_IPCLKPORT_PCLK_P_CORE,
	GOUT_BLK_CORE_UID_XIU_D_CORE_IPCLKPORT_ACLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long core_qch_regs[] __initconst = {
	0x30c8,	/* BAAW_P_GNSS_QCH */
	0x30cc,	/* BAAW_P_MODEM_QCH */
	0x30d0,	/* BAAW_P_SHUB_QCH */
	0x30d4,	/* BAAW_P_WLBT_QCH */
	0x30d8,	/* CCI_550_QCH */
	0x30dc,	/* CORE_CMU_CORE_QCH */
	0x30e0,	/* DIT_QCH */
	0x30e4,	/* GIC400_AIHWACG_QCH */
	0x30e8,	/* LHM_ACEL_D0_ISP_QCH */
	0x30ec,	/* LHM_ACEL_D0_MFC_QCH */
	0x30f0,	/* LHM_ACEL_D1_ISP_QCH */
	0x30f4,	/* LHM_ACEL_D1_MFC_QCH */
	0x30f8,	/* LHM_ACEL_D_CAM_QCH */
	0x30fc,	/* LHM_ACEL_D_DPU_QCH */
	0x3100,	/* LHM_ACEL_D_FSYS_QCH */
	0x3104,	/* LHM_ACEL_D_G2D_QCH */
	0x3108,	/* LHM_ACEL_D_USB_QCH */
	0x310c,	/* LHM_ACEL_D_VIPX1_QCH */
	0x3110,	/* LHM_ACEL_D_VIPX2_QCH */
	0x3114,	/* LHM_ACE_D_CPUCL0_QCH */
	0x3118,	/* LHM_ACE_D_CPUCL1_QCH */
	0x311c,	/* LHM_AXI_D0_MODEM_QCH */
	0x3120,	/* LHM_AXI_D1_MODEM_QCH */
	0x3124,	/* LHM_AXI_D_ABOX_QCH */
	0x3128,	/* LHM_AXI_D_APM_QCH */
	0x312c,	/* LHM_AXI_D_CSSYS_QCH */
	0x3130,	/* LHM_AXI_D_G3D_QCH */
	0x3134,	/* LHM_AXI_D_GNSS_QCH */
	0x3138,	/* LHM_AXI_D_SHUB_QCH */
	0x313c,	/* LHM_AXI_D_WLBT_QCH */
	0x3140,	/* LHS_AXI_D0_MIF_CPU_QCH */
	0x3144,	/* LHS_AXI_D0_MIF_CP_QCH */
	0x3148,	/* LHS_AXI_D0_MIF_NRT_QCH */
	0x314c,	/* LHS_AXI_D0_MIF_RT_QCH */
	0x3150,	/* LHS_AXI_D1_MIF_CPU_QCH */
	0x3154,	/* LHS_AXI_D1_MIF_CP_QCH */
	0x3158,	/* LHS_AXI_D1_MIF_NRT_QCH */
	0x315c,	/* LHS_AXI_D1_MIF_RT_QCH */
	0x3160,	/* LHS_AXI_P_APM_QCH */
	0x3164,	/* LHS_AXI_P_CAM_QCH */
	0x3168,	/* LHS_AXI_P_CPUCL0_QCH */
	0x316c,	/* LHS_AXI_P_CPUCL1_QCH */
	0x3170,	/* LHS_AXI_P_DISPAUD_QCH */
	0x3174,	/* LHS_AXI_P_FSYS_QCH */
	0x3178,	/* LHS_AXI_P_G2D_QCH */
	0x317c,	/* LHS_AXI_P_G3D_QCH */
	0x3180,	/* LHS_AXI_P_GNSS_QCH */
	0x3184,	/* LHS_AXI_P_ISP_QCH */
	0x3188,	/* LHS_AXI_P_MFC_QCH */
	0x318c,	/* LHS_AXI_P_MIF0_QCH */
	0x3190,	/* LHS_AXI_P_MIF1_QCH */
	0x3194,	/* LHS_AXI_P_MODEM_QCH */
	0x3198,	/* LHS_AXI_P_PERI_QCH */
	0x319c,	/* LHS_AXI_P_SHUB_QCH */
	0x31a0,	/* LHS_AXI_P_USB_QCH */
	0x31a4,	/* LHS_AXI_P_VIPX1_QCH */
	0x31a8,	/* LHS_AXI_P_VIPX2_QCH */
	0x31ac,	/* LHS_AXI_P_WLBT_QCH */
	0x31b0,	/* PDMA_CORE_QCH */
	0x31b4,	/* PGEN_LITE_SIREX_QCH */
	0x31b8,	/* PGEN_PDMA_QCH */
	0x31bc,	/* PPCFW_G3D_QCH */
	0x31c0,	/* PPFW_CORE_MEM0_QCH */
	0x31c4,	/* PPFW_CORE_MEM1_QCH */
	0x31c8,	/* PPFW_CORE_PERI_QCH */
	0x31cc,	/* PPMU_ACE_CPUCL0_QCH */
	0x31d0,	/* PPMU_ACE_CPUCL1_QCH */
	0x31d4,	/* RSTNSYNC_CLK_CORE_BUSP_G3D_OCC_QCH */
	0x31d8,	/* RSTNSYNC_CLK_CORE_BUSP_OCC_QCH */
	0x31dc,	/* RSTNSYNC_CLK_CORE_CCI_OCC_QCH */
	0x31e0,	/* RSTNSYNC_CLK_CORE_G3D_OCC_QCH */
	0x31e4,	/* SFR_APBIF_CMU_TOPC_QCH */
	0x31e8,	/* SIREX_QCH */
	0x31ec,	/* SPDMA_CORE_QCH */
	0x31f0,	/* SYSREG_CORE_QCH */
	0x31f4,	/* TREX_D_CORE_QCH */
	0x31f8,	/* TREX_D_NRT_QCH */
	0x31fc,	/* TREX_P_CORE_QCH */
};

/* List of parent clocks for Muxes in CMU_CORE */
PNAME(mout_core_gic_p) = { "dout_core_busp", "oscclk" };
PNAME(mout_cmu_core_bus_user_p) = { "oscclk", "dout_clkcmu_core_bus" };
PNAME(mout_cmu_core_cci_user_p) = { "oscclk", "dout_clkcmu_core_cci" };
PNAME(mout_cmu_core_g3d_user_p) = { "oscclk", "dout_clkcmu_core_g3d" };

static const struct samsung_mux_clock core_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CORE_GIC, "mout_core_gic", mout_core_gic_p,
	    MUX_CLK_CORE_GIC, 0, 1),
	MUX(CLK_MOUT_CMU_CORE_BUS_USER, "mout_cmu_core_bus_user", mout_cmu_core_bus_user_p,
	    MUX_CLKCMU_CORE_BUS_USER, 4, 1),
	MUX(CLK_MOUT_CMU_CORE_CCI_USER, "mout_cmu_core_cci_user", mout_cmu_core_cci_user_p,
	    MUX_CLKCMU_CORE_CCI_USER, 4, 1),
	MUX(CLK_MOUT_CMU_CORE_G3D_USER, "mout_cmu_core_g3d_user", mout_cmu_core_g3d_user_p,
	    MUX_CLKCMU_CORE_G3D_USER, 4, 1),
};

static const struct samsung_div_clock core_div_clks[] __initconst = {
	DIV(CLK_DOUT_CORE_BUSP, "dout_core_busp", "mout_cmu_core_bus_user",
	    DIV_CLK_CORE_BUSP, 0, 2),
};

static const struct samsung_gate_clock core_gate_clks[] __initconst = {
	GATE(CLK_GOUT_CORE_AD_AXI_GIC_ACLKM, "gout_core_ad_axi_gic_aclkm", "mout_core_gic",
	     GOUT_BLK_CORE_UID_AD_AXI_GIC_IPCLKPORT_ACLKM, 21, 0, 0),
	GATE(CLK_GOUT_CORE_GIC400_AIHWACG_CLK, "gout_core_gic400_aihwacg_clk", "mout_core_gic",
	     GOUT_BLK_CORE_UID_GIC400_AIHWACG_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHM_ACE_D_CPUCL0_I_CLK, "gout_core_lhm_ace_d_cpucl0_i_clk",
	     "mout_cmu_core_cci_user",
	     GOUT_BLK_CORE_UID_LHM_ACE_D_CPUCL0_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHM_ACE_D_CPUCL1_I_CLK, "gout_core_lhm_ace_d_cpucl1_i_clk",
	     "mout_cmu_core_cci_user",
	     GOUT_BLK_CORE_UID_LHM_ACE_D_CPUCL1_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHM_AXI_D_ABOX_I_CLK, "gout_core_lhm_axi_d_abox_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHM_AXI_D_ABOX_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHM_AXI_D0_MODEM_I_CLK, "gout_core_lhm_axi_d0_modem_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHM_AXI_D0_MODEM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHM_ACEL_D_DPU_I_CLK, "gout_core_lhm_acel_d_dpu_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHM_ACEL_D_DPU_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHM_ACEL_D_CAM_I_CLK, "gout_core_lhm_acel_d_cam_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHM_ACEL_D_CAM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHM_AXI_D_WLBT_I_CLK, "gout_core_lhm_axi_d_wlbt_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHM_AXI_D_WLBT_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_D0_MIF_CPU_I_CLK, "gout_core_lhs_axi_d0_mif_cpu_i_clk",
	     "mout_cmu_core_cci_user",
	     GOUT_BLK_CORE_UID_LHS_AXI_D0_MIF_CPU_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_D1_MIF_CPU_I_CLK, "gout_core_lhs_axi_d1_mif_cpu_i_clk",
	     "mout_cmu_core_cci_user",
	     GOUT_BLK_CORE_UID_LHS_AXI_D1_MIF_CPU_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_APM_I_CLK, "gout_core_lhs_axi_p_apm_i_clk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_APM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_CPUCL0_I_CLK, "gout_core_lhs_axi_p_cpucl0_i_clk",
	     "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_CPUCL0_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_CPUCL1_I_CLK, "gout_core_lhs_axi_p_cpucl1_i_clk",
	     "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_CPUCL1_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_DISPAUD_I_CLK, "gout_core_lhs_axi_p_dispaud_i_clk",
	     "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_DISPAUD_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_FSYS_I_CLK, "gout_core_lhs_axi_p_fsys_i_clk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_FSYS_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_MFC_I_CLK, "gout_core_lhs_axi_p_mfc_i_clk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_MFC_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_PERI_I_CLK, "gout_core_lhs_axi_p_peri_i_clk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_PERI_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_PPCFW_G3D_PCLK, "gout_core_ppcfw_g3d_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_PPCFW_G3D_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_PPMU_ACE_CPUCL0_ACLK, "gout_core_ppmu_ace_cpucl0_aclk",
	     "mout_cmu_core_cci_user",
	     GOUT_BLK_CORE_UID_PPMU_ACE_CPUCL0_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_PPMU_ACE_CPUCL0_PCLK, "gout_core_ppmu_ace_cpucl0_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_PPMU_ACE_CPUCL0_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_PPMU_ACE_CPUCL1_ACLK, "gout_core_ppmu_ace_cpucl1_aclk",
	     "mout_cmu_core_cci_user",
	     GOUT_BLK_CORE_UID_PPMU_ACE_CPUCL1_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_PPMU_ACE_CPUCL1_PCLK, "gout_core_ppmu_ace_cpucl1_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_PPMU_ACE_CPUCL1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_SFR_APBIF_CMU_TOPC_PCLK, "gout_core_sfr_apbif_cmu_topc_pclk",
	     "dout_core_busp",
	     GOUT_BLK_CORE_UID_SFR_APBIF_CMU_TOPC_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_SYSREG_CORE_PCLK, "gout_core_sysreg_core_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_SYSREG_CORE_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_TREX_D_CORE_pclk, "gout_core_trex_d_core_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_TREX_D_CORE_IPCLKPORT_pclk, 21, 0, 0),
	GATE(CLK_GOUT_CORE_RSTnSYNC_CLK_CORE_CCI_OCC_CLK, "gout_core_rstnsync_clk_core_cci_occ_clk",
	     "mout_cmu_core_cci_user",
	     GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_CCI_OCC_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_RSTnSYNC_CLK_CORE_BUSD_CLK, "gout_core_rstnsync_clk_core_busd_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_BUSD_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_RSTnSYNC_CLK_CORE_BUSP_OCC_CLK, "gout_core_rstnsync_clk_core_busp_occ_clk",
	     "dout_core_busp",
	     GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_BUSP_OCC_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_AD_APB_CCI_550_PCLKM, "gout_core_ad_apb_cci_550_pclkm",
	     "mout_cmu_core_cci_user",
	     GOUT_BLK_CORE_UID_AD_APB_CCI_550_IPCLKPORT_PCLKM, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_MIF1_I_CLK, "gout_core_lhs_axi_p_mif1_i_clk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_MIF1_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_RSTnSYNC_CLK_CORE_G3D_OCC_CLK, "gout_core_rstnsync_clk_core_g3d_occ_clk",
	     "mout_cmu_core_g3d_user",
	     GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_G3D_OCC_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_PPCFW_G3D_ACLK, "gout_core_ppcfw_g3d_aclk", "mout_cmu_core_g3d_user",
	     GOUT_BLK_CORE_UID_PPCFW_G3D_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_RSTnSYNC_CLK_CORE_BUSP_CLK, "gout_core_rstnsync_clk_core_busp_clk",
	     "dout_core_busp",
	     GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_BUSP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_RSTnSYNC_CLK_CORE_CCI_CLK, "gout_core_rstnsync_clk_core_cci_clk",
	     "mout_cmu_core_cci_user",
	     GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_CCI_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_RSTnSYNC_CLK_CORE_G3D_CLK, "gout_core_rstnsync_clk_core_g3d_clk",
	     "mout_cmu_core_g3d_user",
	     GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_G3D_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHM_AXI_D_G3D_I_CLK, "gout_core_lhm_axi_d_g3d_i_clk",
	     "mout_cmu_core_g3d_user",
	     GOUT_BLK_CORE_UID_LHM_AXI_D_G3D_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_RSTnSYNC_CLK_CORE_GIC_CLK, "gout_core_rstnsync_clk_core_gic_clk",
	     "mout_core_gic",
	     GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_GIC_IPCLKPORT_CLK, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_CORE_RSTnSYNC_CLK_CORE_OSCCLK_CLK, "gout_core_rstnsync_clk_core_oscclk_clk",
	     "oscclk",
	     GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_OSCCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_CORE_CMU_CORE_PCLK, "gout_core_core_cmu_core_pclk", "dout_core_busp",
	     CLK_BLK_CORE_UID_CORE_CMU_CORE_IPCLKPORT_PCLK, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_D0_MIF_NRT_I_CLK, "gout_core_lhs_axi_d0_mif_nrt_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHS_AXI_D0_MIF_NRT_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_D1_MIF_NRT_I_CLK, "gout_core_lhs_axi_d1_mif_nrt_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHS_AXI_D1_MIF_NRT_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_ASYNCSFR_WR_DMC0_I_PCLK, "gout_core_asyncsfr_wr_dmc0_i_pclk",
	     "dout_core_busp",
	     GOUT_BLK_CORE_UID_ASYNCSFR_WR_DMC0_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_ISP_I_CLK, "gout_core_lhs_axi_p_isp_i_clk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_ISP_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_MODEM_I_CLK, "gout_core_lhs_axi_p_modem_i_clk",
	     "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_MODEM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_AD_APB_PDMA0_PCLKM, "gout_core_ad_apb_pdma0_pclkm",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_AD_APB_PDMA0_IPCLKPORT_PCLKM, 21, 0, 0),
	GATE(CLK_GOUT_CORE_AD_APB_SPDMA_PCLKM, "gout_core_ad_apb_spdma_pclkm",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_AD_APB_SPDMA_IPCLKPORT_PCLKM, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHM_AXI_D_APM_I_CLK, "gout_core_lhm_axi_d_apm_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHM_AXI_D_APM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHM_AXI_D_CSSYS_I_CLK, "gout_core_lhm_axi_d_cssys_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHM_AXI_D_CSSYS_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHM_ACEL_D_FSYS_I_CLK, "gout_core_lhm_acel_d_fsys_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHM_ACEL_D_FSYS_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHM_AXI_D_GNSS_I_CLK, "gout_core_lhm_axi_d_gnss_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHM_AXI_D_GNSS_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHM_ACEL_D0_ISP_I_CLK, "gout_core_lhm_acel_d0_isp_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHM_ACEL_D0_ISP_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHM_ACEL_D_G2D_I_CLK, "gout_core_lhm_acel_d_g2d_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHM_ACEL_D_G2D_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHM_AXI_D_SHUB_I_CLK, "gout_core_lhm_axi_d_shub_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHM_AXI_D_SHUB_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_SHUB_I_CLK, "gout_core_lhs_axi_p_shub_i_clk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_SHUB_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_WLBT_I_CLK, "gout_core_lhs_axi_p_wlbt_i_clk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_WLBT_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_GNSS_I_CLK, "gout_core_lhs_axi_p_gnss_i_clk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_GNSS_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_PDMA_CORE_ACLK_PDMA0, "gout_core_pdma_core_aclk_pdma0",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_PDMA_CORE_IPCLKPORT_ACLK_PDMA0, 21, 0, 0),
	GATE(CLK_GOUT_CORE_SPDMA_CORE_ACLK_PDMA1, "gout_core_spdma_core_aclk_pdma1",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_SPDMA_CORE_IPCLKPORT_ACLK_PDMA1, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_MIF0_I_CLK, "gout_core_lhs_axi_p_mif0_i_clk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_MIF0_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHM_ACEL_D1_ISP_I_CLK, "gout_core_lhm_acel_d1_isp_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHM_ACEL_D1_ISP_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_D1_MIF_CP_I_CLK, "gout_core_lhs_axi_d1_mif_cp_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHS_AXI_D1_MIF_CP_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_D1_MIF_RT_I_CLK, "gout_core_lhs_axi_d1_mif_rt_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHS_AXI_D1_MIF_RT_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_D0_MIF_RT_I_CLK, "gout_core_lhs_axi_d0_mif_rt_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHS_AXI_D0_MIF_RT_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_D0_MIF_CP_I_CLK, "gout_core_lhs_axi_d0_mif_cp_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHS_AXI_D0_MIF_CP_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHM_ACEL_D0_MFC_I_CLK, "gout_core_lhm_acel_d0_mfc_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHM_ACEL_D0_MFC_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHM_ACEL_D1_MFC_I_CLK, "gout_core_lhm_acel_d1_mfc_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHM_ACEL_D1_MFC_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHM_ACEL_D_USB_I_CLK, "gout_core_lhm_acel_d_usb_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHM_ACEL_D_USB_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHM_ACEL_D_VIPX1_I_CLK, "gout_core_lhm_acel_d_vipx1_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHM_ACEL_D_VIPX1_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHM_ACEL_D_VIPX2_I_CLK, "gout_core_lhm_acel_d_vipx2_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHM_ACEL_D_VIPX2_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_USB_I_CLK, "gout_core_lhs_axi_p_usb_i_clk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_USB_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_CAM_I_CLK, "gout_core_lhs_axi_p_cam_i_clk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_CAM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_G2D_I_CLK, "gout_core_lhs_axi_p_g2d_i_clk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_G2D_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_VIPX1_I_CLK, "gout_core_lhs_axi_p_vipx1_i_clk",
	     "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_VIPX1_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_ASYNCSFR_WR_DMC1_I_PCLK, "gout_core_asyncsfr_wr_dmc1_i_pclk",
	     "dout_core_busp",
	     GOUT_BLK_CORE_UID_ASYNCSFR_WR_DMC1_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_TREX_P_CORE_pclk, "gout_core_trex_p_core_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_TREX_P_CORE_IPCLKPORT_pclk, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_G3D_I_CLK, "gout_core_lhs_axi_p_g3d_i_clk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_G3D_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_PPFW_CORE_MEM0_CLK, "gout_core_ppfw_core_mem0_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_PPFW_CORE_MEM0_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_PPFW_CORE_MEM1_CLK, "gout_core_ppfw_core_mem1_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_PPFW_CORE_MEM1_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_PPFW_CORE_PERI_CLK, "gout_core_ppfw_core_peri_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_PPFW_CORE_PERI_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_BAAW_P_GNSS_I_PCLK, "gout_core_baaw_p_gnss_i_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_BAAW_P_GNSS_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_BAAW_P_MODEM_I_PCLK, "gout_core_baaw_p_modem_i_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_BAAW_P_MODEM_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_BAAW_P_SHUB_I_PCLK, "gout_core_baaw_p_shub_i_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_BAAW_P_SHUB_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_BAAW_P_WLBT_I_PCLK, "gout_core_baaw_p_wlbt_i_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_BAAW_P_WLBT_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_SIREX_i_ACLK, "gout_core_sirex_i_aclk", "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_SIREX_IPCLKPORT_i_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_SIREX_i_PCLK, "gout_core_sirex_i_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_SIREX_IPCLKPORT_i_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_TREX_D_CORE_ACLK, "gout_core_trex_d_core_aclk", "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_TREX_D_CORE_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_TREX_P_CORE_ACLK_P_CORE, "gout_core_trex_p_core_aclk_p_core",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_TREX_P_CORE_IPCLKPORT_ACLK_P_CORE, 21, 0, 0),
	GATE(CLK_GOUT_CORE_CCI_550_ACLK, "gout_core_cci_550_aclk", "mout_cmu_core_cci_user",
	     GOUT_BLK_CORE_UID_CCI_550_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_TREX_D_NRT_pclk, "gout_core_trex_d_nrt_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_TREX_D_NRT_IPCLKPORT_pclk, 21, 0, 0),
	GATE(CLK_GOUT_CORE_XIU_D_CORE_ACLK, "gout_core_xiu_d_core_aclk", "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_XIU_D_CORE_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_TREX_D_CORE_CCLK, "gout_core_trex_d_core_cclk", "mout_cmu_core_cci_user",
	     GOUT_BLK_CORE_UID_TREX_D_CORE_IPCLKPORT_CCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_RSTnSYNC_CLK_CORE_BUSP_G3D_OCC_CLK, "gout_core_rstnsync_clk_core_busp_g3d_occ_clk",
	     "dout_core_busp",
	     GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_BUSP_G3D_OCC_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_PGEN_LITE_SIREX_CLK, "gout_core_pgen_lite_sirex_clk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_PGEN_LITE_SIREX_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHM_AXI_D1_MODEM_I_CLK, "gout_core_lhm_axi_d1_modem_i_clk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_LHM_AXI_D1_MODEM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_TREX_D_CORE_GCLK, "gout_core_trex_d_core_gclk", "mout_cmu_core_g3d_user",
	     GOUT_BLK_CORE_UID_TREX_D_CORE_IPCLKPORT_GCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_TREX_D_NRT_ACLK, "gout_core_trex_d_nrt_aclk", "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_TREX_D_NRT_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_TREX_P_CORE_CCLK_P_CORE, "gout_core_trex_p_core_cclk_p_core",
	     "mout_cmu_core_cci_user",
	     GOUT_BLK_CORE_UID_TREX_P_CORE_IPCLKPORT_CCLK_P_CORE, 21, 0, 0),
	GATE(CLK_GOUT_CORE_TREX_P_CORE_PCLK_P_CORE, "gout_core_trex_p_core_pclk_p_core",
	     "dout_core_busp",
	     GOUT_BLK_CORE_UID_TREX_P_CORE_IPCLKPORT_PCLK_P_CORE, 21, 0, 0),
	GATE(CLK_GOUT_CORE_DIT_iClkL2A, "gout_core_dit_iclkl2a", "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_DIT_IPCLKPORT_iClkL2A, 21, 0, 0),
	GATE(CLK_GOUT_CORE_AD_APB_PPFW_MEM0_PCLKM, "gout_core_ad_apb_ppfw_mem0_pclkm",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_AD_APB_PPFW_MEM0_IPCLKPORT_PCLKM, 21, 0, 0),
	GATE(CLK_GOUT_CORE_AD_APB_PPFW_MEM1_PCLKM, "gout_core_ad_apb_ppfw_mem1_pclkm",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_AD_APB_PPFW_MEM1_IPCLKPORT_PCLKM, 21, 0, 0),
	GATE(CLK_GOUT_CORE_AD_APB_PPFW_PERI_PCLKM, "gout_core_ad_apb_ppfw_peri_pclkm",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_AD_APB_PPFW_PERI_IPCLKPORT_PCLKM, 21, 0, 0),
	GATE(CLK_GOUT_CORE_AXI_US_A40_64to128_DIT_aclk, "gout_core_axi_us_a40_64to128_dit_aclk",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_AXI_US_A40_64to128_DIT_IPCLKPORT_aclk, 21, 0, 0),
	GATE(CLK_GOUT_CORE_AD_APB_DIT_PCLKM, "gout_core_ad_apb_dit_pclkm", "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_AD_APB_DIT_IPCLKPORT_PCLKM, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_VIPX2_I_CLK, "gout_core_lhs_axi_p_vipx2_i_clk",
	     "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_VIPX2_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_PGEN_PDMA_CLK, "gout_core_pgen_pdma_clk", "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_PGEN_PDMA_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_AD_APB_PGEN_PDMA_PCLKM, "gout_core_ad_apb_pgen_pdma_pclkm",
	     "mout_cmu_core_bus_user",
	     GOUT_BLK_CORE_UID_AD_APB_PGEN_PDMA_IPCLKPORT_PCLKM, 21, 0, 0),
};

static const struct samsung_cmu_info core_cmu_info __initconst = {
	.mux_clks		= core_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(core_mux_clks),
	.div_clks		= core_div_clks,
	.nr_div_clks		= ARRAY_SIZE(core_div_clks),
	.gate_clks		= core_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(core_gate_clks),
	.nr_clk_ids		= CLKS_NR_CORE,
	.clk_regs		= core_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(core_clk_regs),
	.qch_regs		= core_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(core_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9610_cmu_core_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &core_cmu_info);
}

CLK_OF_DECLARE(exynos9610_cmu_core, "samsung,exynos9610-cmu-core",
	       exynos9610_cmu_core_init);

/* ---- CMU_CPUCL0 ------------------------------------------------------*/

/* Register Offset definitions for CMU_CPUCL0 (0x10900000) */
#define PLL_LOCKTIME_PLL_CPUCL0_PLL_LOCK_TIME			0x0000
#define MUX_CLKCMU_CPUCL0_DBG_USER			0x0100
#define MUX_CLKCMU_CPUCL0_SWITCH_USER			0x0120
#define PLL_CON0_PLL_CPUCL0_ENABLE			0x0140
#define MUX_CLK_CPUCL0_PLL			0x1000
#define DIV_CLK_CLUSTER0_ACLK			0x1800
#define DIV_CLK_CLUSTER0_CNTCLK			0x1804
#define DIV_CLK_CLUSTER0_PCLKDBG			0x1808
#define DIV_CLK_CPUCL0_CMUREF			0x180c
#define DIV_CLK_CPUCL0_PCLK			0x1814
#define CLK_BLK_CPUCL0_UID_CPUCL0_CMU_CPUCL0_IPCLKPORT_PCLK	0x2000
#define CLK_BLK_CPUCL0_UID_HPM_CPUCL0_IPCLKPORT_hpm_targetclk_c	0x2004
#define CLK_BLK_CPUCL0_UID_RSTnSYNC_CLK_CPUCL0_OSCCLK_IPCLKPORT_CLK	0x2008
#define GATE_CLK_CLUSTER0_CPU			0x2010
#define GOUT_BLK_CPUCL0_UID_ADM_APB_G_CSSYS_CORE_IPCLKPORT_PCLKM	0x2014
#define GOUT_BLK_CPUCL0_UID_ADS_AHB_G_CSSYS_FSYS_IPCLKPORT_HCLKS	0x2018
#define GOUT_BLK_CPUCL0_UID_ADS_APB_G_CSSYS_CPUCL1_IPCLKPORT_PCLKS	0x201c
#define GOUT_BLK_CPUCL0_UID_ADS_APB_G_P8Q_IPCLKPORT_PCLKS	0x2020
#define GOUT_BLK_CPUCL0_UID_AD_APB_P_DUMP_PC_CPUCL0_IPCLKPORT_PCLKM	0x2024
#define GOUT_BLK_CPUCL0_UID_AD_APB_P_DUMP_PC_CPUCL1_IPCLKPORT_PCLKM	0x2028
#define GOUT_BLK_CPUCL0_UID_BUSIF_HPMCPUCL0_IPCLKPORT_PCLK	0x202c
#define GOUT_BLK_CPUCL0_UID_CSSYS_DBG_IPCLKPORT_PCLKDBG	0x2030
#define GOUT_BLK_CPUCL0_UID_DUMP_PC_CPUCL0_IPCLKPORT_I_PCLK	0x2034
#define GOUT_BLK_CPUCL0_UID_DUMP_PC_CPUCL1_IPCLKPORT_I_PCLK	0x2038
#define GOUT_BLK_CPUCL0_UID_LHM_AXI_P_CPUCL0_IPCLKPORT_I_CLK	0x203c
#define GOUT_BLK_CPUCL0_UID_LHS_AXI_D_CSSYS_IPCLKPORT_I_CLK	0x2040
#define GOUT_BLK_CPUCL0_UID_RSTnSYNC_CLK_CPUCL0_DBG_IPCLKPORT_CLK	0x2044
#define GOUT_BLK_CPUCL0_UID_RSTnSYNC_CLK_CPUCL0_PCLK_IPCLKPORT_CLK	0x2048
#define GOUT_BLK_CPUCL0_UID_SECJTAG_IPCLKPORT_i_clk	0x204c
#define GOUT_BLK_CPUCL0_UID_SYSREG_CPUCL0_IPCLKPORT_PCLK	0x2050

static const unsigned long cpucl0_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_CPUCL0_PLL_LOCK_TIME,
	MUX_CLKCMU_CPUCL0_DBG_USER,
	MUX_CLKCMU_CPUCL0_SWITCH_USER,
	PLL_CON0_PLL_CPUCL0_ENABLE,
	MUX_CLK_CPUCL0_PLL,
	DIV_CLK_CLUSTER0_ACLK,
	DIV_CLK_CLUSTER0_CNTCLK,
	DIV_CLK_CLUSTER0_PCLKDBG,
	DIV_CLK_CPUCL0_CMUREF,
	DIV_CLK_CPUCL0_PCLK,
	CLK_BLK_CPUCL0_UID_CPUCL0_CMU_CPUCL0_IPCLKPORT_PCLK,
	CLK_BLK_CPUCL0_UID_HPM_CPUCL0_IPCLKPORT_hpm_targetclk_c,
	CLK_BLK_CPUCL0_UID_RSTnSYNC_CLK_CPUCL0_OSCCLK_IPCLKPORT_CLK,
	GATE_CLK_CLUSTER0_CPU,
	GOUT_BLK_CPUCL0_UID_ADM_APB_G_CSSYS_CORE_IPCLKPORT_PCLKM,
	GOUT_BLK_CPUCL0_UID_ADS_AHB_G_CSSYS_FSYS_IPCLKPORT_HCLKS,
	GOUT_BLK_CPUCL0_UID_ADS_APB_G_CSSYS_CPUCL1_IPCLKPORT_PCLKS,
	GOUT_BLK_CPUCL0_UID_ADS_APB_G_P8Q_IPCLKPORT_PCLKS,
	GOUT_BLK_CPUCL0_UID_AD_APB_P_DUMP_PC_CPUCL0_IPCLKPORT_PCLKM,
	GOUT_BLK_CPUCL0_UID_AD_APB_P_DUMP_PC_CPUCL1_IPCLKPORT_PCLKM,
	GOUT_BLK_CPUCL0_UID_BUSIF_HPMCPUCL0_IPCLKPORT_PCLK,
	GOUT_BLK_CPUCL0_UID_CSSYS_DBG_IPCLKPORT_PCLKDBG,
	GOUT_BLK_CPUCL0_UID_DUMP_PC_CPUCL0_IPCLKPORT_I_PCLK,
	GOUT_BLK_CPUCL0_UID_DUMP_PC_CPUCL1_IPCLKPORT_I_PCLK,
	GOUT_BLK_CPUCL0_UID_LHM_AXI_P_CPUCL0_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL0_UID_LHS_AXI_D_CSSYS_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL0_UID_RSTnSYNC_CLK_CPUCL0_DBG_IPCLKPORT_CLK,
	GOUT_BLK_CPUCL0_UID_RSTnSYNC_CLK_CPUCL0_PCLK_IPCLKPORT_CLK,
	GOUT_BLK_CPUCL0_UID_SECJTAG_IPCLKPORT_i_clk,
	GOUT_BLK_CPUCL0_UID_SYSREG_CPUCL0_IPCLKPORT_PCLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long cpucl0_qch_regs[] __initconst = {
	0x3000,	/* CLUSTER0_QCH_CPU */
	0x3004,	/* CLUSTER0_QCH_DBG */
	0x3008,	/* CSSYS_DBG_QCH */
	0x3018,	/* BUSIF_HPMCPUCL0_QCH */
	0x301c,	/* CLUSTER0_QCH_LHS_ACE_D_CPUCL0 */
	0x3020,	/* CMU_CPUCL0_SHORTSTOP_QCH */
	0x3024,	/* CPUCL0_CMU_CPUCL0_QCH */
	0x3028,	/* DUMP_PC_CPUCL0_QCH */
	0x302c,	/* DUMP_PC_CPUCL1_QCH */
	0x3030,	/* LHM_AXI_P_CPUCL0_QCH */
	0x3034,	/* LHS_AXI_D_CSSYS_QCH */
	0x3038,	/* SECJTAG_QCH */
	0x303c,	/* SYSREG_CPUCL0_QCH */
};

static const struct samsung_pll_rate_table fout_cpucl0_rate_table[] __initconst = {
	PLL_35XX_RATE(26 * MHZ, 1850333333U, 427, 6, 0),
	PLL_35XX_RATE(26 * MHZ, 1449500000U, 223, 4, 0),
	PLL_35XX_RATE(26 * MHZ, 1049750000U, 323, 4, 1),
	PLL_35XX_RATE(26 * MHZ, 600166666U, 277, 6, 1),
	PLL_35XX_RATE(26 * MHZ, 300083333U, 277, 6, 2),
};

static const struct samsung_pll_clock cpucl0_pll_clks[] __initconst = {
	PLL(pll_1051x, CLK_FOUT_CPUCL0, "fout_cpucl0", "oscclk",
	    PLL_LOCKTIME_PLL_CPUCL0_PLL_LOCK_TIME, PLL_CON0_PLL_CPUCL0_ENABLE, fout_cpucl0_rate_table),
};

/* List of parent clocks for Muxes in CMU_CPUCL0 */
PNAME(mout_cpucl0_pll_p) = { "fout_cpucl0", "mout_cmu_cpucl0_switch_user" };
PNAME(mout_cmu_cpucl0_switch_user_p) = { "oscclk", "dout_clkcmu_cpucl0_switch" };
PNAME(mout_cmu_cpucl0_dbg_user_p) = { "oscclk", "dout_clkcmu_cpucl0_dbg" };

static const struct samsung_mux_clock cpucl0_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CPUCL0_PLL, "mout_cpucl0_pll", mout_cpucl0_pll_p,
	    MUX_CLK_CPUCL0_PLL, 0, 1),
	MUX(CLK_MOUT_CMU_CPUCL0_SWITCH_USER, "mout_cmu_cpucl0_switch_user",
	    mout_cmu_cpucl0_switch_user_p,
	    MUX_CLKCMU_CPUCL0_SWITCH_USER, 4, 1),
	MUX(CLK_MOUT_CMU_CPUCL0_DBG_USER, "mout_cmu_cpucl0_dbg_user", mout_cmu_cpucl0_dbg_user_p,
	    MUX_CLKCMU_CPUCL0_DBG_USER, 4, 1),
};

static const struct samsung_div_clock cpucl0_div_clks[] __initconst = {
	DIV(CLK_DOUT_CPUCL0_PCLK, "dout_cpucl0_pclk", "UNRESOLVED_DIV_CLK_CPUCL0_CPU",
	    DIV_CLK_CPUCL0_PCLK, 0, 4),
	DIV(CLK_DOUT_CPUCL0_CMUREF, "dout_cpucl0_cmuref", "UNRESOLVED_DIV_CLK_CPUCL0_CPU",
	    DIV_CLK_CPUCL0_CMUREF, 0, 3),
	DIV(CLK_DOUT_CLUSTER0_ACLK, "dout_cluster0_aclk", "gout_cluster0_cpu",
	    DIV_CLK_CLUSTER0_ACLK, 0, 4),
	DIV(CLK_DOUT_CLUSTER0_PCLKDBG, "dout_cluster0_pclkdbg", "gout_cluster0_cpu",
	    DIV_CLK_CLUSTER0_PCLKDBG, 0, 4),
	DIV(CLK_DOUT_CLUSTER0_CNTCLK, "dout_cluster0_cntclk", "gout_cluster0_cpu",
	    DIV_CLK_CLUSTER0_CNTCLK, 0, 4),
};

static const struct samsung_gate_clock cpucl0_gate_clks[] __initconst = {
	GATE(CLK_GOUT_CPUCL0_SYSREG_CPUCL0_PCLK, "gout_cpucl0_sysreg_cpucl0_pclk",
	     "dout_cpucl0_pclk",
	     GOUT_BLK_CPUCL0_UID_SYSREG_CPUCL0_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_RSTnSYNC_CLK_CPUCL0_PCLK_CLK, "gout_cpucl0_rstnsync_clk_cpucl0_pclk_clk",
	     "dout_cpucl0_pclk",
	     GOUT_BLK_CPUCL0_UID_RSTnSYNC_CLK_CPUCL0_PCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_RSTnSYNC_CLK_CPUCL0_OSCCLK_CLK, "gout_cpucl0_rstnsync_clk_cpucl0_oscclk_clk",
	     "oscclk",
	     CLK_BLK_CPUCL0_UID_RSTnSYNC_CLK_CPUCL0_OSCCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_RSTnSYNC_CLK_CPUCL0_DBG_CLK, "gout_cpucl0_rstnsync_clk_cpucl0_dbg_clk",
	     "mout_cmu_cpucl0_dbg_user",
	     GOUT_BLK_CPUCL0_UID_RSTnSYNC_CLK_CPUCL0_DBG_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_CPUCL0_CMU_CPUCL0_PCLK, "gout_cpucl0_cpucl0_cmu_cpucl0_pclk",
	     "dout_cpucl0_pclk",
	     CLK_BLK_CPUCL0_UID_CPUCL0_CMU_CPUCL0_IPCLKPORT_PCLK, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_CLUSTER0_CPU, "gout_cluster0_cpu", "UNRESOLVED_DIV_CLK_CPUCL0_CPU",
	     GATE_CLK_CLUSTER0_CPU, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_ADM_APB_G_CSSYS_CORE_PCLKM, "gout_cpucl0_adm_apb_g_cssys_core_pclkm",
	     "mout_cmu_cpucl0_dbg_user",
	     GOUT_BLK_CPUCL0_UID_ADM_APB_G_CSSYS_CORE_IPCLKPORT_PCLKM, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_ADS_AHB_G_CSSYS_FSYS_HCLKS, "gout_cpucl0_ads_ahb_g_cssys_fsys_hclks",
	     "mout_cmu_cpucl0_dbg_user",
	     GOUT_BLK_CPUCL0_UID_ADS_AHB_G_CSSYS_FSYS_IPCLKPORT_HCLKS, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_ADS_APB_G_CSSYS_CPUCL1_PCLKS, "gout_cpucl0_ads_apb_g_cssys_cpucl1_pclks",
	     "mout_cmu_cpucl0_dbg_user",
	     GOUT_BLK_CPUCL0_UID_ADS_APB_G_CSSYS_CPUCL1_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_ADS_APB_G_P8Q_PCLKS, "gout_cpucl0_ads_apb_g_p8q_pclks",
	     "mout_cmu_cpucl0_dbg_user",
	     GOUT_BLK_CPUCL0_UID_ADS_APB_G_P8Q_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_AD_APB_P_DUMP_PC_CPUCL0_PCLKM, "gout_cpucl0_ad_apb_p_dump_pc_cpucl0_pclkm",
	     "mout_cmu_cpucl0_dbg_user",
	     GOUT_BLK_CPUCL0_UID_AD_APB_P_DUMP_PC_CPUCL0_IPCLKPORT_PCLKM, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_DUMP_PC_CPUCL0_I_PCLK, "gout_cpucl0_dump_pc_cpucl0_i_pclk",
	     "mout_cmu_cpucl0_dbg_user",
	     GOUT_BLK_CPUCL0_UID_DUMP_PC_CPUCL0_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_LHS_AXI_D_CSSYS_I_CLK, "gout_cpucl0_lhs_axi_d_cssys_i_clk",
	     "mout_cmu_cpucl0_dbg_user",
	     GOUT_BLK_CPUCL0_UID_LHS_AXI_D_CSSYS_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_AD_APB_P_DUMP_PC_CPUCL1_PCLKM, "gout_cpucl0_ad_apb_p_dump_pc_cpucl1_pclkm",
	     "mout_cmu_cpucl0_dbg_user",
	     GOUT_BLK_CPUCL0_UID_AD_APB_P_DUMP_PC_CPUCL1_IPCLKPORT_PCLKM, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_DUMP_PC_CPUCL1_I_PCLK, "gout_cpucl0_dump_pc_cpucl1_i_pclk",
	     "mout_cmu_cpucl0_dbg_user",
	     GOUT_BLK_CPUCL0_UID_DUMP_PC_CPUCL1_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_BUSIF_HPMCPUCL0_PCLK, "gout_cpucl0_busif_hpmcpucl0_pclk",
	     "dout_cpucl0_pclk",
	     GOUT_BLK_CPUCL0_UID_BUSIF_HPMCPUCL0_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_HPM_CPUCL0_hpm_targetclk_c, "gout_cpucl0_hpm_cpucl0_hpm_targetclk_c",
	     "dout_clkcmu_hpm",
	     CLK_BLK_CPUCL0_UID_HPM_CPUCL0_IPCLKPORT_hpm_targetclk_c, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_CSSYS_DBG_PCLKDBG, "gout_cpucl0_cssys_dbg_pclkdbg",
	     "mout_cmu_cpucl0_dbg_user",
	     GOUT_BLK_CPUCL0_UID_CSSYS_DBG_IPCLKPORT_PCLKDBG, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_SECJTAG_i_clk, "gout_cpucl0_secjtag_i_clk", "mout_cmu_cpucl0_dbg_user",
	     GOUT_BLK_CPUCL0_UID_SECJTAG_IPCLKPORT_i_clk, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_LHM_AXI_P_CPUCL0_I_CLK, "gout_cpucl0_lhm_axi_p_cpucl0_i_clk",
	     "dout_cpucl0_pclk",
	     GOUT_BLK_CPUCL0_UID_LHM_AXI_P_CPUCL0_IPCLKPORT_I_CLK, 21, 0, 0),
};

static const struct samsung_cmu_info cpucl0_cmu_info __initconst = {
	.pll_clks		= cpucl0_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(cpucl0_pll_clks),
	.mux_clks		= cpucl0_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(cpucl0_mux_clks),
	.div_clks		= cpucl0_div_clks,
	.nr_div_clks		= ARRAY_SIZE(cpucl0_div_clks),
	.gate_clks		= cpucl0_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(cpucl0_gate_clks),
	.nr_clk_ids		= CLKS_NR_CPUCL0,
	.clk_regs		= cpucl0_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cpucl0_clk_regs),
	.qch_regs		= cpucl0_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(cpucl0_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9610_cmu_cpucl0_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &cpucl0_cmu_info);
}

CLK_OF_DECLARE(exynos9610_cmu_cpucl0, "samsung,exynos9610-cmu-cpucl0",
	       exynos9610_cmu_cpucl0_init);

/* ---- CMU_CPUCL1 ------------------------------------------------------*/

/* Register Offset definitions for CMU_CPUCL1 (0x10800000) */
#define PLL_LOCKTIME_PLL_CPUCL1_PLL_LOCK_TIME			0x0000
#define MUX_CLKCMU_CPUCL1_SWITCH_USER			0x0100
#define PLL_CON0_PLL_CPUCL1_ENABLE			0x0120
#define MUX_CLK_CPUCL1_PLL			0x1000
#define DIV_CLK_CLUSTER1_ACLK			0x1800
#define DIV_CLK_CLUSTER1_CNTCLK			0x1804
#define DIV_CLK_CPUCL1_CMUREF			0x180c
#define DIV_CLK_CPUCL1_PCLK			0x1814
#define DIV_CLK_CPUCL1_PCLKDBG			0x1818
#define CLK_BLK_CPUCL1_UID_CPUCL1_CMU_CPUCL1_IPCLKPORT_PCLK	0x2000
#define CLK_BLK_CPUCL1_UID_HPM_CPUCL1_IPCLKPORT_hpm_targetclk_c	0x2004
#define CLK_BLK_CPUCL1_UID_RSTnSYNC_CLK_CPUCL1_OSCCLK_IPCLKPORT_CLK	0x2008
#define GATE_CLK_CLUSTER1_CPU			0x2010
#define GOUT_BLK_CPUCL1_UID_ADM_APB_G_CSSYS_CPUCL1_IPCLKPORT_PCLKM	0x2014
#define GOUT_BLK_CPUCL1_UID_BUSIF_HPMCPUCL1_IPCLKPORT_PCLK	0x2018
#define GOUT_BLK_CPUCL1_UID_LHM_AXI_P_CPUCL1_IPCLKPORT_I_CLK	0x201c
#define GOUT_BLK_CPUCL1_UID_LHS_ACE_D_CPUCL1_IPCLKPORT_I_CLK	0x2020
#define GOUT_BLK_CPUCL1_UID_RSTnSYNC_CLK_CPUCL1_ACLK_IPCLKPORT_CLK	0x2024
#define GOUT_BLK_CPUCL1_UID_RSTnSYNC_CLK_CPUCL1_PCLKDBG_IPCLKPORT_CLK	0x2028
#define GOUT_BLK_CPUCL1_UID_RSTnSYNC_CLK_CPUCL1_PCLK_IPCLKPORT_CLK	0x202c
#define GOUT_BLK_CPUCL1_UID_SYSREG_CPUCL1_IPCLKPORT_PCLK	0x2030

static const unsigned long cpucl1_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_CPUCL1_PLL_LOCK_TIME,
	MUX_CLKCMU_CPUCL1_SWITCH_USER,
	PLL_CON0_PLL_CPUCL1_ENABLE,
	MUX_CLK_CPUCL1_PLL,
	DIV_CLK_CLUSTER1_ACLK,
	DIV_CLK_CLUSTER1_CNTCLK,
	DIV_CLK_CPUCL1_CMUREF,
	DIV_CLK_CPUCL1_PCLK,
	DIV_CLK_CPUCL1_PCLKDBG,
	CLK_BLK_CPUCL1_UID_CPUCL1_CMU_CPUCL1_IPCLKPORT_PCLK,
	CLK_BLK_CPUCL1_UID_HPM_CPUCL1_IPCLKPORT_hpm_targetclk_c,
	CLK_BLK_CPUCL1_UID_RSTnSYNC_CLK_CPUCL1_OSCCLK_IPCLKPORT_CLK,
	GATE_CLK_CLUSTER1_CPU,
	GOUT_BLK_CPUCL1_UID_ADM_APB_G_CSSYS_CPUCL1_IPCLKPORT_PCLKM,
	GOUT_BLK_CPUCL1_UID_BUSIF_HPMCPUCL1_IPCLKPORT_PCLK,
	GOUT_BLK_CPUCL1_UID_LHM_AXI_P_CPUCL1_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL1_UID_LHS_ACE_D_CPUCL1_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL1_UID_RSTnSYNC_CLK_CPUCL1_ACLK_IPCLKPORT_CLK,
	GOUT_BLK_CPUCL1_UID_RSTnSYNC_CLK_CPUCL1_PCLKDBG_IPCLKPORT_CLK,
	GOUT_BLK_CPUCL1_UID_RSTnSYNC_CLK_CPUCL1_PCLK_IPCLKPORT_CLK,
	GOUT_BLK_CPUCL1_UID_SYSREG_CPUCL1_IPCLKPORT_PCLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long cpucl1_qch_regs[] __initconst = {
	0x3000,	/* ADM_APB_G_CSSYS_CPUCL1_QCH */
	0x3004,	/* CLUSTER1_QCH_CPU */
	0x3008,	/* CLUSTER1_QCH_DBG */
	0x3014,	/* BUSIF_HPMCPUCL1_QCH */
	0x3018,	/* CMU_CPUCL1_SHORTSTOP_QCH */
	0x301c,	/* CPUCL1_CMU_CPUCL1_QCH */
	0x3020,	/* LHM_AXI_P_CPUCL1_QCH */
	0x3024,	/* LHS_ACE_D_CPUCL1_QCH */
	0x3028,	/* SYSREG_CPUCL1_QCH */
};

static const struct samsung_pll_rate_table fout_cpucl1_rate_table[] __initconst = {
	PLL_35XX_RATE(26 * MHZ, 2400666666U, 277, 3, 0),
	PLL_35XX_RATE(26 * MHZ, 1898000000U, 292, 4, 0),
	PLL_35XX_RATE(26 * MHZ, 1499333333U, 346, 3, 1),
	PLL_35XX_RATE(26 * MHZ, 850200000U, 327, 5, 1),
	PLL_35XX_RATE(26 * MHZ, 549900000U, 423, 5, 2),
};

static const struct samsung_pll_clock cpucl1_pll_clks[] __initconst = {
	PLL(pll_1054x, CLK_FOUT_CPUCL1, "fout_cpucl1", "oscclk",
	    PLL_LOCKTIME_PLL_CPUCL1_PLL_LOCK_TIME, PLL_CON0_PLL_CPUCL1_ENABLE, fout_cpucl1_rate_table),
};

/* List of parent clocks for Muxes in CMU_CPUCL1 */
PNAME(mout_cpucl1_pll_p) = { "fout_cpucl1", "mout_cmu_cpucl1_switch_user" };
PNAME(mout_cmu_cpucl1_switch_user_p) = { "oscclk", "dout_clkcmu_cpucl1_switch" };

static const struct samsung_mux_clock cpucl1_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CPUCL1_PLL, "mout_cpucl1_pll", mout_cpucl1_pll_p,
	    MUX_CLK_CPUCL1_PLL, 0, 1),
	MUX(CLK_MOUT_CMU_CPUCL1_SWITCH_USER, "mout_cmu_cpucl1_switch_user",
	    mout_cmu_cpucl1_switch_user_p,
	    MUX_CLKCMU_CPUCL1_SWITCH_USER, 4, 1),
};

static const struct samsung_div_clock cpucl1_div_clks[] __initconst = {
	DIV(CLK_DOUT_CPUCL1_PCLK, "dout_cpucl1_pclk", "UNRESOLVED_DIV_CLK_CPUCL1_CPU",
	    DIV_CLK_CPUCL1_PCLK, 0, 4),
	DIV(CLK_DOUT_CPUCL1_CMUREF, "dout_cpucl1_cmuref", "UNRESOLVED_DIV_CLK_CPUCL1_CPU",
	    DIV_CLK_CPUCL1_CMUREF, 0, 4),
	DIV(CLK_DOUT_CLUSTER1_ACLK, "dout_cluster1_aclk", "gout_cluster1_cpu",
	    DIV_CLK_CLUSTER1_ACLK, 0, 4),
	DIV(CLK_DOUT_CLUSTER1_CNTCLK, "dout_cluster1_cntclk", "gout_cluster1_cpu",
	    DIV_CLK_CLUSTER1_CNTCLK, 0, 4),
	DIV(CLK_DOUT_CPUCL1_PCLKDBG, "dout_cpucl1_pclkdbg", "UNRESOLVED_DIV_CLK_CPUCL1_CPU",
	    DIV_CLK_CPUCL1_PCLKDBG, 0, 4),
};

static const struct samsung_gate_clock cpucl1_gate_clks[] __initconst = {
	GATE(CLK_GOUT_CPUCL1_RSTnSYNC_CLK_CPUCL1_PCLK_CLK, "gout_cpucl1_rstnsync_clk_cpucl1_pclk_clk",
	     "dout_cpucl1_pclk",
	     GOUT_BLK_CPUCL1_UID_RSTnSYNC_CLK_CPUCL1_PCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL1_RSTnSYNC_CLK_CPUCL1_OSCCLK_CLK, "gout_cpucl1_rstnsync_clk_cpucl1_oscclk_clk",
	     "oscclk",
	     CLK_BLK_CPUCL1_UID_RSTnSYNC_CLK_CPUCL1_OSCCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL1_RSTnSYNC_CLK_CPUCL1_ACLK_CLK, "gout_cpucl1_rstnsync_clk_cpucl1_aclk_clk",
	     "dout_cluster1_aclk",
	     GOUT_BLK_CPUCL1_UID_RSTnSYNC_CLK_CPUCL1_ACLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL1_CPUCL1_CMU_CPUCL1_PCLK, "gout_cpucl1_cpucl1_cmu_cpucl1_pclk",
	     "dout_cpucl1_pclk",
	     CLK_BLK_CPUCL1_UID_CPUCL1_CMU_CPUCL1_IPCLKPORT_PCLK, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_CPUCL1_ADM_APB_G_CSSYS_CPUCL1_PCLKM, "gout_cpucl1_adm_apb_g_cssys_cpucl1_pclkm",
	     "dout_cpucl1_pclkdbg",
	     GOUT_BLK_CPUCL1_UID_ADM_APB_G_CSSYS_CPUCL1_IPCLKPORT_PCLKM, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL1_RSTnSYNC_CLK_CPUCL1_PCLKDBG_CLK, "gout_cpucl1_rstnsync_clk_cpucl1_pclkdbg_clk",
	     "dout_cpucl1_pclkdbg",
	     GOUT_BLK_CPUCL1_UID_RSTnSYNC_CLK_CPUCL1_PCLKDBG_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CLUSTER1_CPU, "gout_cluster1_cpu", "UNRESOLVED_DIV_CLK_CPUCL1_CPU",
	     GATE_CLK_CLUSTER1_CPU, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL1_HPM_CPUCL1_hpm_targetclk_c, "gout_cpucl1_hpm_cpucl1_hpm_targetclk_c",
	     "dout_clkcmu_hpm",
	     CLK_BLK_CPUCL1_UID_HPM_CPUCL1_IPCLKPORT_hpm_targetclk_c, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL1_BUSIF_HPMCPUCL1_PCLK, "gout_cpucl1_busif_hpmcpucl1_pclk",
	     "dout_cpucl1_pclk",
	     GOUT_BLK_CPUCL1_UID_BUSIF_HPMCPUCL1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL1_SYSREG_CPUCL1_PCLK, "gout_cpucl1_sysreg_cpucl1_pclk",
	     "dout_cpucl1_pclk",
	     GOUT_BLK_CPUCL1_UID_SYSREG_CPUCL1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL1_LHM_AXI_P_CPUCL1_I_CLK, "gout_cpucl1_lhm_axi_p_cpucl1_i_clk",
	     "dout_cpucl1_pclk",
	     GOUT_BLK_CPUCL1_UID_LHM_AXI_P_CPUCL1_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL1_LHS_ACE_D_CPUCL1_I_CLK, "gout_cpucl1_lhs_ace_d_cpucl1_i_clk",
	     "dout_cluster1_aclk",
	     GOUT_BLK_CPUCL1_UID_LHS_ACE_D_CPUCL1_IPCLKPORT_I_CLK, 21, 0, 0),
};

static const struct samsung_cmu_info cpucl1_cmu_info __initconst = {
	.pll_clks		= cpucl1_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(cpucl1_pll_clks),
	.mux_clks		= cpucl1_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(cpucl1_mux_clks),
	.div_clks		= cpucl1_div_clks,
	.nr_div_clks		= ARRAY_SIZE(cpucl1_div_clks),
	.gate_clks		= cpucl1_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(cpucl1_gate_clks),
	.nr_clk_ids		= CLKS_NR_CPUCL1,
	.clk_regs		= cpucl1_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cpucl1_clk_regs),
	.qch_regs		= cpucl1_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(cpucl1_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9610_cmu_cpucl1_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &cpucl1_cmu_info);
}

CLK_OF_DECLARE(exynos9610_cmu_cpucl1, "samsung,exynos9610-cmu-cpucl1",
	       exynos9610_cmu_cpucl1_init);

/* ---- CMU_DISPAUD -----------------------------------------------------*/

/* Register Offset definitions for CMU_DISPAUD (0x14980000) */
#define PLL_LOCKTIME_PLL_AUD_PLL_LOCK_TIME			0x0000
#define MUX_CLKCMU_DISPAUD_AUD_USER			0x0100
#define MUX_CLKCMU_DISPAUD_CPU_USER			0x0120
#define MUX_CLKCMU_DISPAUD_DISP_USER			0x0140
#define PLL_CON0_PLL_AUD_ENABLE			0x0160
#define MUX_CLK_AUD_BUS			0x1000
#define MUX_CLK_AUD_CPU			0x1004
#define MUX_CLK_AUD_CPU_HCH			0x1008
#define MUX_CLK_AUD_FM			0x100c
#define MUX_CLK_AUD_UAIF0			0x1010
#define MUX_CLK_AUD_UAIF1			0x1014
#define MUX_CLK_AUD_UAIF2			0x1018
#define DIV_CLK_AUD_AUDIF			0x1800
#define DIV_CLK_AUD_BUS			0x1808
#define DIV_CLK_AUD_CPU			0x180c
#define DIV_CLK_AUD_CPU_ACLK			0x1810
#define DIV_CLK_AUD_CPU_PCLKDBG			0x1814
#define DIV_CLK_AUD_DSIF			0x1818
#define DIV_CLK_AUD_FM			0x181c
#define DIV_CLK_AUD_FM_SPDY			0x1820
#define DIV_CLK_AUD_UAIF0			0x1824
#define DIV_CLK_AUD_UAIF1			0x1828
#define DIV_CLK_AUD_UAIF2			0x182c
#define DIV_CLK_DISPAUD_BUSP			0x1830
#define CLK_BLK_DISPAUD_UID_ABOX_IPCLKPORT_BCLK_UAIF0	0x2000
#define CLK_BLK_DISPAUD_UID_ABOX_IPCLKPORT_BCLK_UAIF1	0x2004
#define CLK_BLK_DISPAUD_UID_ABOX_IPCLKPORT_BCLK_UAIF2	0x2008
#define CLK_BLK_DISPAUD_UID_DISPAUD_CMU_DISPAUD_IPCLKPORT_PCLK	0x200c
#define CLK_BLK_DISPAUD_UID_RSTnSYNC_CLK_AUD_UAIF0_IPCLKPORT_CLK	0x2010
#define CLK_BLK_DISPAUD_UID_RSTnSYNC_CLK_AUD_UAIF1_IPCLKPORT_CLK	0x2014
#define CLK_BLK_DISPAUD_UID_RSTnSYNC_CLK_AUD_UAIF2_IPCLKPORT_CLK	0x2018
#define CLK_BLK_DISPAUD_UID_RSTnSYNC_CLK_DISPAUD_OSCCLK_IPCLKPORT_CLK	0x201c
#define GOUT_BLK_DISPAUD_UID_ABOX_IPCLKPORT_ACLK	0x2020
#define GOUT_BLK_DISPAUD_UID_ABOX_IPCLKPORT_BCLK_DSIF	0x2024
#define GOUT_BLK_DISPAUD_UID_ABOX_IPCLKPORT_BCLK_SPDY	0x2028
#define GOUT_BLK_DISPAUD_UID_ABOX_IPCLKPORT_CCLK_ASB	0x202c
#define GOUT_BLK_DISPAUD_UID_ABOX_IPCLKPORT_CCLK_CA7	0x2030
#define GOUT_BLK_DISPAUD_UID_ABOX_IPCLKPORT_CCLK_DBG	0x2034
#define GOUT_BLK_DISPAUD_UID_ABOX_IPCLKPORT_OSC_SPDY	0x2038
#define GOUT_BLK_DISPAUD_UID_AXI_US_32to128_IPCLKPORT_aclk	0x203c
#define GOUT_BLK_DISPAUD_UID_BLK_DISPAUD_IPCLKPORT_CLK_DISPAUD_AUD	0x2040
#define GOUT_BLK_DISPAUD_UID_BLK_DISPAUD_IPCLKPORT_CLK_DISPAUD_DISP	0x2044
#define GOUT_BLK_DISPAUD_UID_BTM_ABOX_IPCLKPORT_I_ACLK	0x2048
#define GOUT_BLK_DISPAUD_UID_BTM_ABOX_IPCLKPORT_I_PCLK	0x204c
#define GOUT_BLK_DISPAUD_UID_BTM_DPU_IPCLKPORT_I_ACLK	0x2050
#define GOUT_BLK_DISPAUD_UID_BTM_DPU_IPCLKPORT_I_PCLK	0x2054
#define GOUT_BLK_DISPAUD_UID_DFTMUX_DISPAUD_IPCLKPORT_AUD_CODEC_MCLK	0x2058
#define GOUT_BLK_DISPAUD_UID_DPU_IPCLKPORT_ACLK_DECON	0x2060
#define GOUT_BLK_DISPAUD_UID_DPU_IPCLKPORT_ACLK_DMA	0x2064
#define GOUT_BLK_DISPAUD_UID_DPU_IPCLKPORT_ACLK_DPP	0x2068
#define GOUT_BLK_DISPAUD_UID_GPIO_DISPAUD_IPCLKPORT_PCLK	0x206c
#define GOUT_BLK_DISPAUD_UID_LHM_AXI_P_DISPAUD_IPCLKPORT_I_CLK	0x2070
#define GOUT_BLK_DISPAUD_UID_LHS_ACEL_D_DPU_IPCLKPORT_I_CLK	0x2074
#define GOUT_BLK_DISPAUD_UID_LHS_AXI_D_ABOX_IPCLKPORT_I_CLK	0x2078
#define GOUT_BLK_DISPAUD_UID_PERI_AXI_ASB_IPCLKPORT_ACLKM	0x207c
#define GOUT_BLK_DISPAUD_UID_PERI_AXI_ASB_IPCLKPORT_PCLK	0x2080
#define GOUT_BLK_DISPAUD_UID_PPMU_ABOX_IPCLKPORT_ACLK	0x2084
#define GOUT_BLK_DISPAUD_UID_PPMU_ABOX_IPCLKPORT_PCLK	0x2088
#define GOUT_BLK_DISPAUD_UID_PPMU_DPU_IPCLKPORT_ACLK	0x208c
#define GOUT_BLK_DISPAUD_UID_PPMU_DPU_IPCLKPORT_PCLK	0x2090
#define GOUT_BLK_DISPAUD_UID_RSTnSYNC_CLK_AUD_CPU_ACLK_IPCLKPORT_CLK	0x2094
#define GOUT_BLK_DISPAUD_UID_RSTnSYNC_CLK_AUD_CPU_CLKIN_IPCLKPORT_CLK	0x2098
#define GOUT_BLK_DISPAUD_UID_RSTnSYNC_CLK_AUD_CPU_PCLKDBG_IPCLKPORT_CLK	0x209c
#define GOUT_BLK_DISPAUD_UID_RSTnSYNC_CLK_AUD_DSIF_IPCLKPORT_CLK	0x20a0
#define GOUT_BLK_DISPAUD_UID_RSTnSYNC_CLK_DISPAUD_AUD_IPCLKPORT_CLK	0x20a4
#define GOUT_BLK_DISPAUD_UID_RSTnSYNC_CLK_DISPAUD_BUSP_IPCLKPORT_CLK	0x20a8
#define GOUT_BLK_DISPAUD_UID_RSTnSYNC_CLK_DISPAUD_DISP_IPCLKPORT_CLK	0x20ac
#define GOUT_BLK_DISPAUD_UID_SMMU_ABOX_IPCLKPORT_CLK	0x20b0
#define GOUT_BLK_DISPAUD_UID_SMMU_DPU_IPCLKPORT_CLK	0x20b4
#define GOUT_BLK_DISPAUD_UID_SYSREG_DISPAUD_IPCLKPORT_PCLK	0x20b8
#define GOUT_BLK_DISPAUD_UID_WDT_AUD_IPCLKPORT_PCLK	0x20bc

static const unsigned long dispaud_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_AUD_PLL_LOCK_TIME,
	MUX_CLKCMU_DISPAUD_AUD_USER,
	MUX_CLKCMU_DISPAUD_CPU_USER,
	MUX_CLKCMU_DISPAUD_DISP_USER,
	PLL_CON0_PLL_AUD_ENABLE,
	MUX_CLK_AUD_BUS,
	MUX_CLK_AUD_CPU,
	MUX_CLK_AUD_CPU_HCH,
	MUX_CLK_AUD_FM,
	MUX_CLK_AUD_UAIF0,
	MUX_CLK_AUD_UAIF1,
	MUX_CLK_AUD_UAIF2,
	DIV_CLK_AUD_AUDIF,
	DIV_CLK_AUD_BUS,
	DIV_CLK_AUD_CPU,
	DIV_CLK_AUD_CPU_ACLK,
	DIV_CLK_AUD_CPU_PCLKDBG,
	DIV_CLK_AUD_DSIF,
	DIV_CLK_AUD_FM,
	DIV_CLK_AUD_FM_SPDY,
	DIV_CLK_AUD_UAIF0,
	DIV_CLK_AUD_UAIF1,
	DIV_CLK_AUD_UAIF2,
	DIV_CLK_DISPAUD_BUSP,
	CLK_BLK_DISPAUD_UID_ABOX_IPCLKPORT_BCLK_UAIF0,
	CLK_BLK_DISPAUD_UID_ABOX_IPCLKPORT_BCLK_UAIF1,
	CLK_BLK_DISPAUD_UID_ABOX_IPCLKPORT_BCLK_UAIF2,
	CLK_BLK_DISPAUD_UID_DISPAUD_CMU_DISPAUD_IPCLKPORT_PCLK,
	CLK_BLK_DISPAUD_UID_RSTnSYNC_CLK_AUD_UAIF0_IPCLKPORT_CLK,
	CLK_BLK_DISPAUD_UID_RSTnSYNC_CLK_AUD_UAIF1_IPCLKPORT_CLK,
	CLK_BLK_DISPAUD_UID_RSTnSYNC_CLK_AUD_UAIF2_IPCLKPORT_CLK,
	CLK_BLK_DISPAUD_UID_RSTnSYNC_CLK_DISPAUD_OSCCLK_IPCLKPORT_CLK,
	GOUT_BLK_DISPAUD_UID_ABOX_IPCLKPORT_ACLK,
	GOUT_BLK_DISPAUD_UID_ABOX_IPCLKPORT_BCLK_DSIF,
	GOUT_BLK_DISPAUD_UID_ABOX_IPCLKPORT_BCLK_SPDY,
	GOUT_BLK_DISPAUD_UID_ABOX_IPCLKPORT_CCLK_ASB,
	GOUT_BLK_DISPAUD_UID_ABOX_IPCLKPORT_CCLK_CA7,
	GOUT_BLK_DISPAUD_UID_ABOX_IPCLKPORT_CCLK_DBG,
	GOUT_BLK_DISPAUD_UID_ABOX_IPCLKPORT_OSC_SPDY,
	GOUT_BLK_DISPAUD_UID_AXI_US_32to128_IPCLKPORT_aclk,
	GOUT_BLK_DISPAUD_UID_BLK_DISPAUD_IPCLKPORT_CLK_DISPAUD_AUD,
	GOUT_BLK_DISPAUD_UID_BLK_DISPAUD_IPCLKPORT_CLK_DISPAUD_DISP,
	GOUT_BLK_DISPAUD_UID_BTM_ABOX_IPCLKPORT_I_ACLK,
	GOUT_BLK_DISPAUD_UID_BTM_ABOX_IPCLKPORT_I_PCLK,
	GOUT_BLK_DISPAUD_UID_BTM_DPU_IPCLKPORT_I_ACLK,
	GOUT_BLK_DISPAUD_UID_BTM_DPU_IPCLKPORT_I_PCLK,
	GOUT_BLK_DISPAUD_UID_DFTMUX_DISPAUD_IPCLKPORT_AUD_CODEC_MCLK,
	GOUT_BLK_DISPAUD_UID_DPU_IPCLKPORT_ACLK_DECON,
	GOUT_BLK_DISPAUD_UID_DPU_IPCLKPORT_ACLK_DMA,
	GOUT_BLK_DISPAUD_UID_DPU_IPCLKPORT_ACLK_DPP,
	GOUT_BLK_DISPAUD_UID_GPIO_DISPAUD_IPCLKPORT_PCLK,
	GOUT_BLK_DISPAUD_UID_LHM_AXI_P_DISPAUD_IPCLKPORT_I_CLK,
	GOUT_BLK_DISPAUD_UID_LHS_ACEL_D_DPU_IPCLKPORT_I_CLK,
	GOUT_BLK_DISPAUD_UID_LHS_AXI_D_ABOX_IPCLKPORT_I_CLK,
	GOUT_BLK_DISPAUD_UID_PERI_AXI_ASB_IPCLKPORT_ACLKM,
	GOUT_BLK_DISPAUD_UID_PERI_AXI_ASB_IPCLKPORT_PCLK,
	GOUT_BLK_DISPAUD_UID_PPMU_ABOX_IPCLKPORT_ACLK,
	GOUT_BLK_DISPAUD_UID_PPMU_ABOX_IPCLKPORT_PCLK,
	GOUT_BLK_DISPAUD_UID_PPMU_DPU_IPCLKPORT_ACLK,
	GOUT_BLK_DISPAUD_UID_PPMU_DPU_IPCLKPORT_PCLK,
	GOUT_BLK_DISPAUD_UID_RSTnSYNC_CLK_AUD_CPU_ACLK_IPCLKPORT_CLK,
	GOUT_BLK_DISPAUD_UID_RSTnSYNC_CLK_AUD_CPU_CLKIN_IPCLKPORT_CLK,
	GOUT_BLK_DISPAUD_UID_RSTnSYNC_CLK_AUD_CPU_PCLKDBG_IPCLKPORT_CLK,
	GOUT_BLK_DISPAUD_UID_RSTnSYNC_CLK_AUD_DSIF_IPCLKPORT_CLK,
	GOUT_BLK_DISPAUD_UID_RSTnSYNC_CLK_DISPAUD_AUD_IPCLKPORT_CLK,
	GOUT_BLK_DISPAUD_UID_RSTnSYNC_CLK_DISPAUD_BUSP_IPCLKPORT_CLK,
	GOUT_BLK_DISPAUD_UID_RSTnSYNC_CLK_DISPAUD_DISP_IPCLKPORT_CLK,
	GOUT_BLK_DISPAUD_UID_SMMU_ABOX_IPCLKPORT_CLK,
	GOUT_BLK_DISPAUD_UID_SMMU_DPU_IPCLKPORT_CLK,
	GOUT_BLK_DISPAUD_UID_SYSREG_DISPAUD_IPCLKPORT_PCLK,
	GOUT_BLK_DISPAUD_UID_WDT_AUD_IPCLKPORT_PCLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long dispaud_qch_regs[] __initconst = {
	0x3000,	/* ABOX_QCH_CPU */
	0x3004,	/* ABOX_QCH_FM */
	0x3014,	/* ABOX_QCH_S_ACLK */
	0x3018,	/* ABOX_QCH_S_BCLK0 */
	0x301c,	/* ABOX_QCH_S_BCLK1 */
	0x3020,	/* ABOX_QCH_S_BCLK2 */
	0x3024,	/* ABOX_QCH_S_BCLK_DSIF */
	0x3028,	/* BTM_ABOX_QCH */
	0x302c,	/* BTM_DPU_QCH */
	0x3030,	/* DISPAUD_CMU_DISPAUD_QCH */
	0x3034,	/* DPU_QCH_S_DECON */
	0x3038,	/* DPU_QCH_S_DMA */
	0x303c,	/* DPU_QCH_S_DPP */
	0x3040,	/* GPIO_DISPAUD_QCH */
	0x3044,	/* LHM_AXI_P_DISPAUD_QCH */
	0x3048,	/* LHS_ACEL_D_DPU_QCH */
	0x304c,	/* LHS_AXI_D_ABOX_QCH */
	0x3050,	/* PPMU_ABOX_QCH */
	0x3054,	/* PPMU_DPU_QCH */
	0x3058,	/* RSTNSYNC_CLK_AUD_CPU_CLKIN_QCH */
	0x305c,	/* RSTNSYNC_CLK_AUD_CPU_PCLKDBG_QCH */
	0x3060,	/* SMMU_ABOX_QCH */
	0x3064,	/* SMMU_DPU_QCH */
	0x3068,	/* SYSREG_DISPAUD_QCH */
	0x306c,	/* WDT_AUD_QCH */
};

static const struct samsung_fixed_rate_clock dispaud_fixed_clks[] __initconst = {
	FRATE(CLK_IOCLK_AUDIOCDCLK0, "ioclk_audiocdclk0", NULL, 0, 10000000),
	FRATE(CLK_IOCLK_AUDIOCDCLK2, "ioclk_audiocdclk2", NULL, 0, 10000000),
	FRATE(CLK_IOCLK_AUDIOCDCLK1, "ioclk_audiocdclk1", NULL, 0, 100000000),
	FRATE(CLK_TICK_USB, "tick_usb", NULL, 0, 60000000),
};

static const struct samsung_pll_clock dispaud_pll_clks[] __initconst = {
	PLL(pll_1061x, CLK_FOUT_AUD, "fout_aud", "oscclk",
	    PLL_LOCKTIME_PLL_AUD_PLL_LOCK_TIME, PLL_CON0_PLL_AUD_ENABLE, NULL),
};

/* List of parent clocks for Muxes in CMU_DISPAUD */
PNAME(mout_aud_cpu_p) = { "dout_aud_cpu", "mout_cmu_dispaud_cpu_user" };
PNAME(mout_aud_uaif0_p) = { "dout_aud_uaif0", "ioclk_audiocdclk0" };
PNAME(mout_aud_uaif2_p) = { "dout_aud_uaif2", "ioclk_audiocdclk2" };
PNAME(mout_aud_uaif1_p) = { "dout_aud_uaif1", "ioclk_audiocdclk1" };
PNAME(mout_aud_cpu_hch_p) = { "mout_aud_cpu", "oscclk" };
PNAME(mout_aud_fm_p) = { "oscclk", "dout_aud_fm_spdy" };
PNAME(mout_aud_bus_p) = { "dout_aud_bus", "mout_cmu_dispaud_aud_user" };
PNAME(mout_cmu_dispaud_cpu_user_p) = { "oscclk", "dout_clkcmu_dispaud_cpu" };
PNAME(mout_cmu_dispaud_disp_user_p) = { "oscclk", "dout_clkcmu_dispaud_disp" };
PNAME(mout_cmu_dispaud_aud_user_p) = { "oscclk", "dout_clkcmu_dispaud_aud" };

static const struct samsung_mux_clock dispaud_mux_clks[] __initconst = {
	MUX(CLK_MOUT_AUD_CPU, "mout_aud_cpu", mout_aud_cpu_p,
	    MUX_CLK_AUD_CPU, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF0, "mout_aud_uaif0", mout_aud_uaif0_p,
	    MUX_CLK_AUD_UAIF0, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF2, "mout_aud_uaif2", mout_aud_uaif2_p,
	    MUX_CLK_AUD_UAIF2, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF1, "mout_aud_uaif1", mout_aud_uaif1_p,
	    MUX_CLK_AUD_UAIF1, 0, 1),
	MUX(CLK_MOUT_AUD_CPU_HCH, "mout_aud_cpu_hch", mout_aud_cpu_hch_p,
	    MUX_CLK_AUD_CPU_HCH, 0, 1),
	MUX(CLK_MOUT_AUD_FM, "mout_aud_fm", mout_aud_fm_p,
	    MUX_CLK_AUD_FM, 0, 1),
	MUX(CLK_MOUT_AUD_BUS, "mout_aud_bus", mout_aud_bus_p,
	    MUX_CLK_AUD_BUS, 0, 1),
	MUX(CLK_MOUT_CMU_DISPAUD_CPU_USER, "mout_cmu_dispaud_cpu_user", mout_cmu_dispaud_cpu_user_p,
	    MUX_CLKCMU_DISPAUD_CPU_USER, 4, 1),
	MUX(CLK_MOUT_CMU_DISPAUD_DISP_USER, "mout_cmu_dispaud_disp_user",
	    mout_cmu_dispaud_disp_user_p,
	    MUX_CLKCMU_DISPAUD_DISP_USER, 4, 1),
	MUX(CLK_MOUT_CMU_DISPAUD_AUD_USER, "mout_cmu_dispaud_aud_user", mout_cmu_dispaud_aud_user_p,
	    MUX_CLKCMU_DISPAUD_AUD_USER, 4, 1),
};

static const struct samsung_div_clock dispaud_div_clks[] __initconst = {
	DIV(CLK_DOUT_AUD_CPU, "dout_aud_cpu", "fout_aud",
	    DIV_CLK_AUD_CPU, 0, 4),
	DIV(CLK_DOUT_AUD_CPU_PCLKDBG, "dout_aud_cpu_pclkdbg", "mout_aud_cpu_hch",
	    DIV_CLK_AUD_CPU_PCLKDBG, 0, 3),
	DIV(CLK_DOUT_AUD_CPU_ACLK, "dout_aud_cpu_aclk", "mout_aud_cpu_hch",
	    DIV_CLK_AUD_CPU_ACLK, 0, 3),
	DIV(CLK_DOUT_AUD_UAIF0, "dout_aud_uaif0", "dout_aud_audif",
	    DIV_CLK_AUD_UAIF0, 0, 9),
	DIV(CLK_DOUT_AUD_AUDIF, "dout_aud_audif", "fout_aud",
	    DIV_CLK_AUD_AUDIF, 0, 9),
	DIV(CLK_DOUT_AUD_UAIF2, "dout_aud_uaif2", "dout_aud_audif",
	    DIV_CLK_AUD_UAIF2, 0, 9),
	DIV(CLK_DOUT_AUD_UAIF1, "dout_aud_uaif1", "dout_aud_audif",
	    DIV_CLK_AUD_UAIF1, 0, 9),
	DIV(CLK_DOUT_DISPAUD_BUSP, "dout_dispaud_busp", "mout_cmu_dispaud_disp_user",
	    DIV_CLK_DISPAUD_BUSP, 0, 3),
	DIV(CLK_DOUT_AUD_DSIF, "dout_aud_dsif", "dout_aud_audif",
	    DIV_CLK_AUD_DSIF, 0, 9),
	DIV(CLK_DOUT_AUD_FM_SPDY, "dout_aud_fm_spdy", "tick_usb",
	    DIV_CLK_AUD_FM_SPDY, 0, 1),
	DIV(CLK_DOUT_AUD_FM, "dout_aud_fm", "mout_aud_fm",
	    DIV_CLK_AUD_FM, 0, 10),
	DIV(CLK_DOUT_AUD_BUS, "dout_aud_bus", "fout_aud",
	    DIV_CLK_AUD_BUS, 0, 3),
};

static const struct samsung_gate_clock dispaud_gate_clks[] __initconst = {
	GATE(CLK_GOUT_DISPAUD_ABOX_CCLK_ASB, "gout_dispaud_abox_cclk_asb", "dout_aud_cpu_aclk",
	     GOUT_BLK_DISPAUD_UID_ABOX_IPCLKPORT_CCLK_ASB, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_AXI_US_32to128_aclk, "gout_dispaud_axi_us_32to128_aclk",
	     "mout_aud_bus",
	     GOUT_BLK_DISPAUD_UID_AXI_US_32to128_IPCLKPORT_aclk, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_RSTnSYNC_CLK_AUD_CPU_CLKIN_CLK, "gout_dispaud_rstnsync_clk_aud_cpu_clkin_clk",
	     "mout_aud_cpu_hch",
	     GOUT_BLK_DISPAUD_UID_RSTnSYNC_CLK_AUD_CPU_CLKIN_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_RSTnSYNC_CLK_AUD_CPU_PCLKDBG_CLK, "gout_dispaud_rstnsync_clk_aud_cpu_pclkdbg_clk",
	     "dout_aud_cpu_pclkdbg",
	     GOUT_BLK_DISPAUD_UID_RSTnSYNC_CLK_AUD_CPU_PCLKDBG_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_RSTnSYNC_CLK_DISPAUD_AUD_CLK, "gout_dispaud_rstnsync_clk_dispaud_aud_clk",
	     "mout_aud_bus",
	     GOUT_BLK_DISPAUD_UID_RSTnSYNC_CLK_DISPAUD_AUD_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_PERI_AXI_ASB_ACLKM, "gout_dispaud_peri_axi_asb_aclkm", "mout_aud_bus",
	     GOUT_BLK_DISPAUD_UID_PERI_AXI_ASB_IPCLKPORT_ACLKM, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_PERI_AXI_ASB_PCLK, "gout_dispaud_peri_axi_asb_pclk", "mout_aud_bus",
	     GOUT_BLK_DISPAUD_UID_PERI_AXI_ASB_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_WDT_AUD_PCLK, "gout_dispaud_wdt_aud_pclk", "mout_aud_bus",
	     GOUT_BLK_DISPAUD_UID_WDT_AUD_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_RSTnSYNC_CLK_DISPAUD_OSCCLK_CLK, "gout_dispaud_rstnsync_clk_dispaud_oscclk_clk",
	     "oscclk",
	     CLK_BLK_DISPAUD_UID_RSTnSYNC_CLK_DISPAUD_OSCCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_DFTMUX_DISPAUD_AUD_CODEC_MCLK, "gout_dispaud_dftmux_dispaud_aud_codec_mclk",
	     "dout_aud_audif",
	     GOUT_BLK_DISPAUD_UID_DFTMUX_DISPAUD_IPCLKPORT_AUD_CODEC_MCLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_PPMU_ABOX_ACLK, "gout_dispaud_ppmu_abox_aclk", "mout_aud_bus",
	     GOUT_BLK_DISPAUD_UID_PPMU_ABOX_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_PPMU_ABOX_PCLK, "gout_dispaud_ppmu_abox_pclk", "mout_aud_bus",
	     GOUT_BLK_DISPAUD_UID_PPMU_ABOX_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_SMMU_ABOX_CLK, "gout_dispaud_smmu_abox_clk", "mout_aud_bus",
	     GOUT_BLK_DISPAUD_UID_SMMU_ABOX_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_LHS_ACEL_D_DPU_I_CLK, "gout_dispaud_lhs_acel_d_dpu_i_clk",
	     "mout_cmu_dispaud_disp_user",
	     GOUT_BLK_DISPAUD_UID_LHS_ACEL_D_DPU_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_RSTnSYNC_CLK_DISPAUD_DISP_CLK, "gout_dispaud_rstnsync_clk_dispaud_disp_clk",
	     "mout_cmu_dispaud_disp_user",
	     GOUT_BLK_DISPAUD_UID_RSTnSYNC_CLK_DISPAUD_DISP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_RSTnSYNC_CLK_DISPAUD_BUSP_CLK, "gout_dispaud_rstnsync_clk_dispaud_busp_clk",
	     "dout_dispaud_busp",
	     GOUT_BLK_DISPAUD_UID_RSTnSYNC_CLK_DISPAUD_BUSP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_PPMU_DPU_ACLK, "gout_dispaud_ppmu_dpu_aclk",
	     "mout_cmu_dispaud_disp_user",
	     GOUT_BLK_DISPAUD_UID_PPMU_DPU_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_PPMU_DPU_PCLK, "gout_dispaud_ppmu_dpu_pclk", "dout_dispaud_busp",
	     GOUT_BLK_DISPAUD_UID_PPMU_DPU_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_SYSREG_DISPAUD_PCLK, "gout_dispaud_sysreg_dispaud_pclk",
	     "dout_dispaud_busp",
	     GOUT_BLK_DISPAUD_UID_SYSREG_DISPAUD_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_LHM_AXI_P_DISPAUD_I_CLK, "gout_dispaud_lhm_axi_p_dispaud_i_clk",
	     "dout_dispaud_busp",
	     GOUT_BLK_DISPAUD_UID_LHM_AXI_P_DISPAUD_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_RSTnSYNC_CLK_AUD_CPU_ACLK_CLK, "gout_dispaud_rstnsync_clk_aud_cpu_aclk_clk",
	     "dout_aud_cpu_aclk",
	     GOUT_BLK_DISPAUD_UID_RSTnSYNC_CLK_AUD_CPU_ACLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_DISPAUD_CMU_DISPAUD_PCLK, "gout_dispaud_dispaud_cmu_dispaud_pclk",
	     "dout_dispaud_busp",
	     CLK_BLK_DISPAUD_UID_DISPAUD_CMU_DISPAUD_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_LHS_AXI_D_ABOX_I_CLK, "gout_dispaud_lhs_axi_d_abox_i_clk",
	     "mout_aud_bus",
	     GOUT_BLK_DISPAUD_UID_LHS_AXI_D_ABOX_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_ABOX_BCLK_SPDY, "gout_dispaud_abox_bclk_spdy", "dout_aud_fm",
	     GOUT_BLK_DISPAUD_UID_ABOX_IPCLKPORT_BCLK_SPDY, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_SMMU_DPU_CLK, "gout_dispaud_smmu_dpu_clk",
	     "mout_cmu_dispaud_disp_user",
	     GOUT_BLK_DISPAUD_UID_SMMU_DPU_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_BLK_DISPAUD_CLK_DISPAUD_DISP, "gout_dispaud_blk_dispaud_clk_dispaud_disp",
	     "mout_cmu_dispaud_disp_user",
	     GOUT_BLK_DISPAUD_UID_BLK_DISPAUD_IPCLKPORT_CLK_DISPAUD_DISP, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_ABOX_CCLK_DBG, "gout_dispaud_abox_cclk_dbg", "dout_aud_cpu_pclkdbg",
	     GOUT_BLK_DISPAUD_UID_ABOX_IPCLKPORT_CCLK_DBG, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_RSTnSYNC_CLK_AUD_DSIF_CLK, "gout_dispaud_rstnsync_clk_aud_dsif_clk",
	     "dout_aud_dsif",
	     GOUT_BLK_DISPAUD_UID_RSTnSYNC_CLK_AUD_DSIF_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_DPU_ACLK_DECON, "gout_dispaud_dpu_aclk_decon",
	     "mout_cmu_dispaud_disp_user",
	     GOUT_BLK_DISPAUD_UID_DPU_IPCLKPORT_ACLK_DECON, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_DPU_ACLK_DPP, "gout_dispaud_dpu_aclk_dpp",
	     "mout_cmu_dispaud_disp_user",
	     GOUT_BLK_DISPAUD_UID_DPU_IPCLKPORT_ACLK_DPP, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_DPU_ACLK_DMA, "gout_dispaud_dpu_aclk_dma",
	     "mout_cmu_dispaud_disp_user",
	     GOUT_BLK_DISPAUD_UID_DPU_IPCLKPORT_ACLK_DMA, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_ABOX_ACLK, "gout_dispaud_abox_aclk", "mout_aud_bus",
	     GOUT_BLK_DISPAUD_UID_ABOX_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_ABOX_BCLK_DSIF, "gout_dispaud_abox_bclk_dsif", "dout_aud_dsif",
	     GOUT_BLK_DISPAUD_UID_ABOX_IPCLKPORT_BCLK_DSIF, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_ABOX_OSC_SPDY, "gout_dispaud_abox_osc_spdy", "mout_aud_fm",
	     GOUT_BLK_DISPAUD_UID_ABOX_IPCLKPORT_OSC_SPDY, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_ABOX_BCLK_UAIF0, "gout_dispaud_abox_bclk_uaif0", "mout_aud_uaif0",
	     CLK_BLK_DISPAUD_UID_ABOX_IPCLKPORT_BCLK_UAIF0, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_ABOX_BCLK_UAIF1, "gout_dispaud_abox_bclk_uaif1", "mout_aud_uaif1",
	     CLK_BLK_DISPAUD_UID_ABOX_IPCLKPORT_BCLK_UAIF1, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_ABOX_BCLK_UAIF2, "gout_dispaud_abox_bclk_uaif2", "mout_aud_uaif2",
	     CLK_BLK_DISPAUD_UID_ABOX_IPCLKPORT_BCLK_UAIF2, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_RSTnSYNC_CLK_AUD_UAIF0_CLK, "gout_dispaud_rstnsync_clk_aud_uaif0_clk",
	     "mout_aud_uaif0",
	     CLK_BLK_DISPAUD_UID_RSTnSYNC_CLK_AUD_UAIF0_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_RSTnSYNC_CLK_AUD_UAIF1_CLK, "gout_dispaud_rstnsync_clk_aud_uaif1_clk",
	     "mout_aud_uaif1",
	     CLK_BLK_DISPAUD_UID_RSTnSYNC_CLK_AUD_UAIF1_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_RSTnSYNC_CLK_AUD_UAIF2_CLK, "gout_dispaud_rstnsync_clk_aud_uaif2_clk",
	     "mout_aud_uaif2",
	     CLK_BLK_DISPAUD_UID_RSTnSYNC_CLK_AUD_UAIF2_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_BTM_ABOX_I_ACLK, "gout_dispaud_btm_abox_i_aclk", "mout_aud_bus",
	     GOUT_BLK_DISPAUD_UID_BTM_ABOX_IPCLKPORT_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_BTM_ABOX_I_PCLK, "gout_dispaud_btm_abox_i_pclk", "dout_dispaud_busp",
	     GOUT_BLK_DISPAUD_UID_BTM_ABOX_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_BTM_DPU_I_ACLK, "gout_dispaud_btm_dpu_i_aclk",
	     "mout_cmu_dispaud_disp_user",
	     GOUT_BLK_DISPAUD_UID_BTM_DPU_IPCLKPORT_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_BTM_DPU_I_PCLK, "gout_dispaud_btm_dpu_i_pclk", "dout_dispaud_busp",
	     GOUT_BLK_DISPAUD_UID_BTM_DPU_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_BLK_DISPAUD_CLK_DISPAUD_AUD, "gout_dispaud_blk_dispaud_clk_dispaud_aud",
	     "mout_aud_bus",
	     GOUT_BLK_DISPAUD_UID_BLK_DISPAUD_IPCLKPORT_CLK_DISPAUD_AUD, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_ABOX_CCLK_CA7, "gout_dispaud_abox_cclk_ca7", "mout_aud_cpu_hch",
	     GOUT_BLK_DISPAUD_UID_ABOX_IPCLKPORT_CCLK_CA7, 21, 0, 0),
	GATE(CLK_GOUT_DISPAUD_GPIO_DISPAUD_PCLK, "gout_dispaud_gpio_dispaud_pclk", "mout_aud_bus",
	     GOUT_BLK_DISPAUD_UID_GPIO_DISPAUD_IPCLKPORT_PCLK, 21, 0, 0),
};

static const struct samsung_cmu_info dispaud_cmu_info __initconst = {
	.pll_clks		= dispaud_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(dispaud_pll_clks),
	.mux_clks		= dispaud_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(dispaud_mux_clks),
	.div_clks		= dispaud_div_clks,
	.nr_div_clks		= ARRAY_SIZE(dispaud_div_clks),
	.gate_clks		= dispaud_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(dispaud_gate_clks),
	.fixed_clks		= dispaud_fixed_clks,
	.nr_fixed_clks		= ARRAY_SIZE(dispaud_fixed_clks),
	.nr_clk_ids		= CLKS_NR_DISPAUD,
	.clk_regs		= dispaud_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(dispaud_clk_regs),
	.qch_regs		= dispaud_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(dispaud_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9610_cmu_dispaud_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &dispaud_cmu_info);
}

CLK_OF_DECLARE(exynos9610_cmu_dispaud, "samsung,exynos9610-cmu-dispaud",
	       exynos9610_cmu_dispaud_init);

/* ---- CMU_FSYS --------------------------------------------------------*/

/* Register Offset definitions for CMU_FSYS (0x13400000) */
#define MUX_CLKCMU_FSYS_BUS_USER			0x0100
#define MUX_CLKCMU_FSYS_MMC_CARD_USER			0x0120
#define MUX_CLKCMU_FSYS_MMC_EMBD_USER			0x0140
#define MUX_CLKCMU_FSYS_UFS_EMBD_USER			0x0160
#define CLK_BLK_FSYS_UID_FSYS_CMU_FSYS_IPCLKPORT_PCLK	0x2000
#define CLK_BLK_FSYS_UID_RSTnSYNC_CLK_FSYS_OSCCLK_IPCLKPORT_CLK	0x2004
#define GOUT_BLK_FSYS_UID_ADM_AHB_SSS_IPCLKPORT_HCLKM	0x2008
#define GOUT_BLK_FSYS_UID_BTM_FSYS_IPCLKPORT_I_ACLK	0x200c
#define GOUT_BLK_FSYS_UID_BTM_FSYS_IPCLKPORT_I_PCLK	0x2010
#define GOUT_BLK_FSYS_UID_GPIO_FSYS_IPCLKPORT_PCLK	0x2014
#define GOUT_BLK_FSYS_UID_LHM_AXI_P_FSYS_IPCLKPORT_I_CLK	0x2018
#define GOUT_BLK_FSYS_UID_LHS_ACEL_D_FSYS_IPCLKPORT_I_CLK	0x201c
#define GOUT_BLK_FSYS_UID_MMC_CARD_IPCLKPORT_I_ACLK	0x2020
#define GOUT_BLK_FSYS_UID_MMC_CARD_IPCLKPORT_SDCLKIN	0x2024
#define GOUT_BLK_FSYS_UID_MMC_EMBD_IPCLKPORT_I_ACLK	0x2028
#define GOUT_BLK_FSYS_UID_MMC_EMBD_IPCLKPORT_SDCLKIN	0x202c
#define GOUT_BLK_FSYS_UID_PGEN_LITE_FSYS_IPCLKPORT_CLK	0x2030
#define GOUT_BLK_FSYS_UID_PPMU_FSYS_IPCLKPORT_ACLK	0x2034
#define GOUT_BLK_FSYS_UID_PPMU_FSYS_IPCLKPORT_PCLK	0x2038
#define GOUT_BLK_FSYS_UID_RSTnSYNC_CLK_FSYS_BUS_IPCLKPORT_CLK	0x203c
#define GOUT_BLK_FSYS_UID_RTIC_IPCLKPORT_i_ACLK			0x2040
#define GOUT_BLK_FSYS_UID_RTIC_IPCLKPORT_i_PCLK			0x2044
#define GOUT_BLK_FSYS_UID_SSS_IPCLKPORT_i_ACLK			0x2048
#define GOUT_BLK_FSYS_UID_SSS_IPCLKPORT_i_PCLK			0x204c
#define GOUT_BLK_FSYS_UID_SYSREG_FSYS_IPCLKPORT_PCLK	0x2050
#define GOUT_BLK_FSYS_UID_UFS_EMBD_IPCLKPORT_I_ACLK	0x2054
#define GOUT_BLK_FSYS_UID_UFS_EMBD_IPCLKPORT_I_CLK_UNIPRO	0x2058
#define GOUT_BLK_FSYS_UID_UFS_EMBD_IPCLKPORT_I_FMP_CLK	0x205c
#define GOUT_BLK_FSYS_UID_XIU_D_FSYS_IPCLKPORT_ACLK	0x2060

static const unsigned long fsys_clk_regs[] __initconst = {
	MUX_CLKCMU_FSYS_BUS_USER,
	MUX_CLKCMU_FSYS_MMC_CARD_USER,
	MUX_CLKCMU_FSYS_MMC_EMBD_USER,
	MUX_CLKCMU_FSYS_UFS_EMBD_USER,
	CLK_BLK_FSYS_UID_FSYS_CMU_FSYS_IPCLKPORT_PCLK,
	CLK_BLK_FSYS_UID_RSTnSYNC_CLK_FSYS_OSCCLK_IPCLKPORT_CLK,
	GOUT_BLK_FSYS_UID_ADM_AHB_SSS_IPCLKPORT_HCLKM,
	GOUT_BLK_FSYS_UID_BTM_FSYS_IPCLKPORT_I_ACLK,
	GOUT_BLK_FSYS_UID_BTM_FSYS_IPCLKPORT_I_PCLK,
	GOUT_BLK_FSYS_UID_GPIO_FSYS_IPCLKPORT_PCLK,
	GOUT_BLK_FSYS_UID_LHM_AXI_P_FSYS_IPCLKPORT_I_CLK,
	GOUT_BLK_FSYS_UID_LHS_ACEL_D_FSYS_IPCLKPORT_I_CLK,
	GOUT_BLK_FSYS_UID_MMC_CARD_IPCLKPORT_I_ACLK,
	GOUT_BLK_FSYS_UID_MMC_CARD_IPCLKPORT_SDCLKIN,
	GOUT_BLK_FSYS_UID_MMC_EMBD_IPCLKPORT_I_ACLK,
	GOUT_BLK_FSYS_UID_MMC_EMBD_IPCLKPORT_SDCLKIN,
	GOUT_BLK_FSYS_UID_PGEN_LITE_FSYS_IPCLKPORT_CLK,
	GOUT_BLK_FSYS_UID_PPMU_FSYS_IPCLKPORT_ACLK,
	GOUT_BLK_FSYS_UID_PPMU_FSYS_IPCLKPORT_PCLK,
	GOUT_BLK_FSYS_UID_RSTnSYNC_CLK_FSYS_BUS_IPCLKPORT_CLK,
	GOUT_BLK_FSYS_UID_RTIC_IPCLKPORT_i_ACLK,
	GOUT_BLK_FSYS_UID_RTIC_IPCLKPORT_i_PCLK,
	GOUT_BLK_FSYS_UID_SSS_IPCLKPORT_i_ACLK,
	GOUT_BLK_FSYS_UID_SSS_IPCLKPORT_i_PCLK,
	GOUT_BLK_FSYS_UID_SYSREG_FSYS_IPCLKPORT_PCLK,
	GOUT_BLK_FSYS_UID_UFS_EMBD_IPCLKPORT_I_ACLK,
	GOUT_BLK_FSYS_UID_UFS_EMBD_IPCLKPORT_I_CLK_UNIPRO,
	GOUT_BLK_FSYS_UID_UFS_EMBD_IPCLKPORT_I_FMP_CLK,
	GOUT_BLK_FSYS_UID_XIU_D_FSYS_IPCLKPORT_ACLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long fsys_qch_regs[] __initconst = {
	0x3008,	/* ADM_AHB_SSS_QCH */
	0x300c,	/* BTM_FSYS_QCH */
	0x3010,	/* FSYS_CMU_FSYS_QCH */
	0x3014,	/* GPIO_FSYS_QCH */
	0x3018,	/* LHM_AXI_P_FSYS_QCH */
	0x301c,	/* LHS_ACEL_D_FSYS_QCH */
	0x3020,	/* MMC_CARD_QCH */
	0x3024,	/* MMC_EMBD_QCH */
	0x3028,	/* PGEN_LITE_FSYS_QCH */
	0x302c,	/* PPMU_FSYS_QCH */
	0x3030,	/* RTIC_QCH */
	0x3034,	/* SSS_QCH */
	0x3038,	/* SYSREG_FSYS_QCH */
	0x303c,	/* UFS_EMBD_QCH_FMP */
	0x3040,	/* UFS_EMBD_QCH_UFS */
};

/* List of parent clocks for Muxes in CMU_FSYS */
PNAME(mout_cmu_fsys_bus_user_p) = { "oscclk", "dout_clkcmu_fsys_bus" };
PNAME(mout_cmu_fsys_mmc_card_user_p) = { "oscclk", "dout_clkcmu_fsys_mmc_card" };
PNAME(mout_cmu_fsys_mmc_embd_user_p) = { "oscclk", "dout_clkcmu_fsys_mmc_embd" };
PNAME(mout_cmu_fsys_ufs_embd_user_p) = { "oscclk", "dout_clkcmu_fsys_ufs_embd" };

static const struct samsung_mux_clock fsys_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_FSYS_BUS_USER, "mout_cmu_fsys_bus_user", mout_cmu_fsys_bus_user_p,
	    MUX_CLKCMU_FSYS_BUS_USER, 4, 1),
	MUX(CLK_MOUT_CMU_FSYS_MMC_CARD_USER, "mout_cmu_fsys_mmc_card_user",
	    mout_cmu_fsys_mmc_card_user_p,
	    MUX_CLKCMU_FSYS_MMC_CARD_USER, 4, 1),
	MUX(CLK_MOUT_CMU_FSYS_MMC_EMBD_USER, "mout_cmu_fsys_mmc_embd_user",
	    mout_cmu_fsys_mmc_embd_user_p,
	    MUX_CLKCMU_FSYS_MMC_EMBD_USER, 4, 1),
	MUX(CLK_MOUT_CMU_FSYS_UFS_EMBD_USER, "mout_cmu_fsys_ufs_embd_user",
	    mout_cmu_fsys_ufs_embd_user_p,
	    MUX_CLKCMU_FSYS_UFS_EMBD_USER, 4, 1),
};

static const struct samsung_gate_clock fsys_gate_clks[] __initconst = {
	GATE(CLK_GOUT_FSYS_SSS_i_PCLK, "gout_fsys_sss_i_pclk", "mout_cmu_fsys_bus_user",
	     GOUT_BLK_FSYS_UID_SSS_IPCLKPORT_i_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_RTIC_i_PCLK, "gout_fsys_rtic_i_pclk", "mout_cmu_fsys_bus_user",
	     GOUT_BLK_FSYS_UID_RTIC_IPCLKPORT_i_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_RSTnSYNC_CLK_FSYS_BUS_CLK, "gout_fsys_rstnsync_clk_fsys_bus_clk",
	     "mout_cmu_fsys_bus_user",
	     GOUT_BLK_FSYS_UID_RSTnSYNC_CLK_FSYS_BUS_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_GPIO_FSYS_PCLK, "gout_fsys_gpio_fsys_pclk", "mout_cmu_fsys_bus_user",
	     GOUT_BLK_FSYS_UID_GPIO_FSYS_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_LHM_AXI_P_FSYS_I_CLK, "gout_fsys_lhm_axi_p_fsys_i_clk",
	     "mout_cmu_fsys_bus_user",
	     GOUT_BLK_FSYS_UID_LHM_AXI_P_FSYS_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_LHS_ACEL_D_FSYS_I_CLK, "gout_fsys_lhs_acel_d_fsys_i_clk",
	     "mout_cmu_fsys_bus_user",
	     GOUT_BLK_FSYS_UID_LHS_ACEL_D_FSYS_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_PPMU_FSYS_ACLK, "gout_fsys_ppmu_fsys_aclk", "mout_cmu_fsys_bus_user",
	     GOUT_BLK_FSYS_UID_PPMU_FSYS_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_PPMU_FSYS_PCLK, "gout_fsys_ppmu_fsys_pclk", "mout_cmu_fsys_bus_user",
	     GOUT_BLK_FSYS_UID_PPMU_FSYS_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_SYSREG_FSYS_PCLK, "gout_fsys_sysreg_fsys_pclk", "mout_cmu_fsys_bus_user",
	     GOUT_BLK_FSYS_UID_SYSREG_FSYS_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_RSTnSYNC_CLK_FSYS_OSCCLK_CLK, "gout_fsys_rstnsync_clk_fsys_oscclk_clk",
	     "oscclk",
	     CLK_BLK_FSYS_UID_RSTnSYNC_CLK_FSYS_OSCCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_XIU_D_FSYS_ACLK, "gout_fsys_xiu_d_fsys_aclk", "mout_cmu_fsys_bus_user",
	     GOUT_BLK_FSYS_UID_XIU_D_FSYS_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_ADM_AHB_SSS_HCLKM, "gout_fsys_adm_ahb_sss_hclkm",
	     "mout_cmu_fsys_bus_user",
	     GOUT_BLK_FSYS_UID_ADM_AHB_SSS_IPCLKPORT_HCLKM, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_FSYS_CMU_FSYS_PCLK, "gout_fsys_fsys_cmu_fsys_pclk",
	     "mout_cmu_fsys_bus_user",
	     CLK_BLK_FSYS_UID_FSYS_CMU_FSYS_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_MMC_CARD_I_ACLK, "gout_fsys_mmc_card_i_aclk", "mout_cmu_fsys_bus_user",
	     GOUT_BLK_FSYS_UID_MMC_CARD_IPCLKPORT_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_MMC_EMBD_I_ACLK, "gout_fsys_mmc_embd_i_aclk", "mout_cmu_fsys_bus_user",
	     GOUT_BLK_FSYS_UID_MMC_EMBD_IPCLKPORT_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_RTIC_i_ACLK, "gout_fsys_rtic_i_aclk", "mout_cmu_fsys_bus_user",
	     GOUT_BLK_FSYS_UID_RTIC_IPCLKPORT_i_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_SSS_i_ACLK, "gout_fsys_sss_i_aclk", "mout_cmu_fsys_bus_user",
	     GOUT_BLK_FSYS_UID_SSS_IPCLKPORT_i_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_UFS_EMBD_I_ACLK, "gout_fsys_ufs_embd_i_aclk", "mout_cmu_fsys_bus_user",
	     GOUT_BLK_FSYS_UID_UFS_EMBD_IPCLKPORT_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_UFS_EMBD_I_FMP_CLK, "gout_fsys_ufs_embd_i_fmp_clk",
	     "mout_cmu_fsys_bus_user",
	     GOUT_BLK_FSYS_UID_UFS_EMBD_IPCLKPORT_I_FMP_CLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_PGEN_LITE_FSYS_CLK, "gout_fsys_pgen_lite_fsys_clk",
	     "mout_cmu_fsys_bus_user",
	     GOUT_BLK_FSYS_UID_PGEN_LITE_FSYS_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_BTM_FSYS_I_ACLK, "gout_fsys_btm_fsys_i_aclk", "mout_cmu_fsys_bus_user",
	     GOUT_BLK_FSYS_UID_BTM_FSYS_IPCLKPORT_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_BTM_FSYS_I_PCLK, "gout_fsys_btm_fsys_i_pclk", "mout_cmu_fsys_bus_user",
	     GOUT_BLK_FSYS_UID_BTM_FSYS_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_MMC_CARD_SDCLKIN, "gout_fsys_mmc_card_sdclkin",
	     "mout_cmu_fsys_mmc_card_user",
	     GOUT_BLK_FSYS_UID_MMC_CARD_IPCLKPORT_SDCLKIN, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_MMC_EMBD_SDCLKIN, "gout_fsys_mmc_embd_sdclkin",
	     "mout_cmu_fsys_mmc_embd_user",
	     GOUT_BLK_FSYS_UID_MMC_EMBD_IPCLKPORT_SDCLKIN, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_UFS_EMBD_I_CLK_UNIPRO, "gout_fsys_ufs_embd_i_clk_unipro",
	     "mout_cmu_fsys_ufs_embd_user",
	     GOUT_BLK_FSYS_UID_UFS_EMBD_IPCLKPORT_I_CLK_UNIPRO, 21, 0, 0),
};

static const struct samsung_cmu_info fsys_cmu_info __initconst = {
	.mux_clks		= fsys_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(fsys_mux_clks),
	.gate_clks		= fsys_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(fsys_gate_clks),
	.nr_clk_ids		= CLKS_NR_FSYS,
	.clk_regs		= fsys_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(fsys_clk_regs),
	.qch_regs		= fsys_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(fsys_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9610_cmu_fsys_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &fsys_cmu_info);
}

CLK_OF_DECLARE(exynos9610_cmu_fsys, "samsung,exynos9610-cmu-fsys",
	       exynos9610_cmu_fsys_init);

/* ---- CMU_G2D ---------------------------------------------------------*/

/* Register Offset definitions for CMU_G2D (0x12e00000) */
#define MUX_CLKCMU_G2D_G2D_USER			0x0100
#define MUX_CLKCMU_G2D_MSCL_USER			0x0120
#define DIV_CLK_G2D_BUSP			0x1800
#define CLK_BLK_G2D_UID_G2D_CMU_G2D_IPCLKPORT_PCLK	0x2000
#define CLK_BLK_G2D_UID_RSTnSYNC_CLK_G2D_OSCCLK_IPCLKPORT_CLK	0x2004
#define GOUT_BLK_G2D_UID_AS_AXI_JPEG_IPCLKPORT_ACLKM	0x2008
#define GOUT_BLK_G2D_UID_AS_AXI_JPEG_IPCLKPORT_ACLKS	0x200c
#define GOUT_BLK_G2D_UID_AS_AXI_MSCL_IPCLKPORT_ACLKM	0x2010
#define GOUT_BLK_G2D_UID_AS_AXI_MSCL_IPCLKPORT_ACLKS	0x2014
#define GOUT_BLK_G2D_UID_BLK_G2D_IPCLKPORT_CLK_G2D_G2D	0x2018
#define GOUT_BLK_G2D_UID_BLK_G2D_IPCLKPORT_CLK_G2D_MSCL	0x201c
#define GOUT_BLK_G2D_UID_BTM_G2D_IPCLKPORT_I_ACLK	0x2020
#define GOUT_BLK_G2D_UID_BTM_G2D_IPCLKPORT_I_PCLK	0x2024
#define GOUT_BLK_G2D_UID_G2D_IPCLKPORT_ACLK			0x2028
#define GOUT_BLK_G2D_UID_JPEG_IPCLKPORT_I_FIMP_CLK	0x202c
#define GOUT_BLK_G2D_UID_LHM_AXI_P_G2D_IPCLKPORT_I_CLK	0x2030
#define GOUT_BLK_G2D_UID_LHS_ACEL_D_G2D_IPCLKPORT_I_CLK	0x2034
#define GOUT_BLK_G2D_UID_MSCL_IPCLKPORT_ACLK			0x2038
#define GOUT_BLK_G2D_UID_PGEN100_LITE_G2D_IPCLKPORT_CLK	0x203c
#define GOUT_BLK_G2D_UID_PPMU_G2D_IPCLKPORT_ACLK	0x2040
#define GOUT_BLK_G2D_UID_PPMU_G2D_IPCLKPORT_PCLK	0x2044
#define GOUT_BLK_G2D_UID_RSTnSYNC_CLK_G2D_BUSP_IPCLKPORT_CLK	0x2048
#define GOUT_BLK_G2D_UID_RSTnSYNC_CLK_G2D_G2D_IPCLKPORT_CLK	0x204c
#define GOUT_BLK_G2D_UID_RSTnSYNC_CLK_G2D_MSCL_IPCLKPORT_CLK	0x2050
#define GOUT_BLK_G2D_UID_SYSMMU_G2D_IPCLKPORT_CLK	0x2054
#define GOUT_BLK_G2D_UID_SYSREG_G2D_IPCLKPORT_PCLK	0x2058
#define GOUT_BLK_G2D_UID_XIU_D_MSCL_IPCLKPORT_ACLK	0x205c

static const unsigned long g2d_clk_regs[] __initconst = {
	MUX_CLKCMU_G2D_G2D_USER,
	MUX_CLKCMU_G2D_MSCL_USER,
	DIV_CLK_G2D_BUSP,
	CLK_BLK_G2D_UID_G2D_CMU_G2D_IPCLKPORT_PCLK,
	CLK_BLK_G2D_UID_RSTnSYNC_CLK_G2D_OSCCLK_IPCLKPORT_CLK,
	GOUT_BLK_G2D_UID_AS_AXI_JPEG_IPCLKPORT_ACLKM,
	GOUT_BLK_G2D_UID_AS_AXI_JPEG_IPCLKPORT_ACLKS,
	GOUT_BLK_G2D_UID_AS_AXI_MSCL_IPCLKPORT_ACLKM,
	GOUT_BLK_G2D_UID_AS_AXI_MSCL_IPCLKPORT_ACLKS,
	GOUT_BLK_G2D_UID_BLK_G2D_IPCLKPORT_CLK_G2D_G2D,
	GOUT_BLK_G2D_UID_BLK_G2D_IPCLKPORT_CLK_G2D_MSCL,
	GOUT_BLK_G2D_UID_BTM_G2D_IPCLKPORT_I_ACLK,
	GOUT_BLK_G2D_UID_BTM_G2D_IPCLKPORT_I_PCLK,
	GOUT_BLK_G2D_UID_G2D_IPCLKPORT_ACLK,
	GOUT_BLK_G2D_UID_JPEG_IPCLKPORT_I_FIMP_CLK,
	GOUT_BLK_G2D_UID_LHM_AXI_P_G2D_IPCLKPORT_I_CLK,
	GOUT_BLK_G2D_UID_LHS_ACEL_D_G2D_IPCLKPORT_I_CLK,
	GOUT_BLK_G2D_UID_MSCL_IPCLKPORT_ACLK,
	GOUT_BLK_G2D_UID_PGEN100_LITE_G2D_IPCLKPORT_CLK,
	GOUT_BLK_G2D_UID_PPMU_G2D_IPCLKPORT_ACLK,
	GOUT_BLK_G2D_UID_PPMU_G2D_IPCLKPORT_PCLK,
	GOUT_BLK_G2D_UID_RSTnSYNC_CLK_G2D_BUSP_IPCLKPORT_CLK,
	GOUT_BLK_G2D_UID_RSTnSYNC_CLK_G2D_G2D_IPCLKPORT_CLK,
	GOUT_BLK_G2D_UID_RSTnSYNC_CLK_G2D_MSCL_IPCLKPORT_CLK,
	GOUT_BLK_G2D_UID_SYSMMU_G2D_IPCLKPORT_CLK,
	GOUT_BLK_G2D_UID_SYSREG_G2D_IPCLKPORT_PCLK,
	GOUT_BLK_G2D_UID_XIU_D_MSCL_IPCLKPORT_ACLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long g2d_qch_regs[] __initconst = {
	0x3008,	/* BTM_G2D_QCH */
	0x300c,	/* G2D_CMU_G2D_QCH */
	0x3010,	/* G2D_QCH */
	0x3014,	/* JPEG_QCH */
	0x3018,	/* LHM_AXI_P_G2D_QCH */
	0x301c,	/* LHS_ACEL_D_G2D_QCH */
	0x3020,	/* MSCL_QCH */
	0x3024,	/* PGEN100_LITE_G2D_QCH */
	0x3028,	/* PPMU_G2D_QCH */
	0x302c,	/* SYSMMU_G2D_QCH */
	0x3030,	/* SYSREG_G2D_QCH */
};

/* List of parent clocks for Muxes in CMU_G2D */
PNAME(mout_cmu_g2d_mscl_user_p) = { "oscclk", "dout_clkcmu_g2d_mscl" };
PNAME(mout_cmu_g2d_g2d_user_p) = { "oscclk", "dout_clkcmu_g2d_g2d" };

static const struct samsung_mux_clock g2d_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_G2D_MSCL_USER, "mout_cmu_g2d_mscl_user", mout_cmu_g2d_mscl_user_p,
	    MUX_CLKCMU_G2D_MSCL_USER, 4, 1),
	MUX(CLK_MOUT_CMU_G2D_G2D_USER, "mout_cmu_g2d_g2d_user", mout_cmu_g2d_g2d_user_p,
	    MUX_CLKCMU_G2D_G2D_USER, 4, 1),
};

static const struct samsung_div_clock g2d_div_clks[] __initconst = {
	DIV(CLK_DOUT_G2D_BUSP, "dout_g2d_busp", "mout_cmu_g2d_mscl_user",
	    DIV_CLK_G2D_BUSP, 0, 3),
};

static const struct samsung_gate_clock g2d_gate_clks[] __initconst = {
	GATE(CLK_GOUT_G2D_LHM_AXI_P_G2D_I_CLK, "gout_g2d_lhm_axi_p_g2d_i_clk", "dout_g2d_busp",
	     GOUT_BLK_G2D_UID_LHM_AXI_P_G2D_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G2D_SYSREG_G2D_PCLK, "gout_g2d_sysreg_g2d_pclk", "dout_g2d_busp",
	     GOUT_BLK_G2D_UID_SYSREG_G2D_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_G2D_RSTnSYNC_CLK_G2D_MSCL_CLK, "gout_g2d_rstnsync_clk_g2d_mscl_clk",
	     "mout_cmu_g2d_mscl_user",
	     GOUT_BLK_G2D_UID_RSTnSYNC_CLK_G2D_MSCL_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G2D_RSTnSYNC_CLK_G2D_OSCCLK_CLK, "gout_g2d_rstnsync_clk_g2d_oscclk_clk",
	     "oscclk",
	     CLK_BLK_G2D_UID_RSTnSYNC_CLK_G2D_OSCCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G2D_RSTnSYNC_CLK_G2D_BUSP_CLK, "gout_g2d_rstnsync_clk_g2d_busp_clk",
	     "dout_g2d_busp",
	     GOUT_BLK_G2D_UID_RSTnSYNC_CLK_G2D_BUSP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G2D_G2D_CMU_G2D_PCLK, "gout_g2d_g2d_cmu_g2d_pclk", "dout_g2d_busp",
	     CLK_BLK_G2D_UID_G2D_CMU_G2D_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_G2D_PPMU_G2D_PCLK, "gout_g2d_ppmu_g2d_pclk", "dout_g2d_busp",
	     GOUT_BLK_G2D_UID_PPMU_G2D_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_G2D_JPEG_I_FIMP_CLK, "gout_g2d_jpeg_i_fimp_clk", "mout_cmu_g2d_mscl_user",
	     GOUT_BLK_G2D_UID_JPEG_IPCLKPORT_I_FIMP_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G2D_MSCL_ACLK, "gout_g2d_mscl_aclk", "mout_cmu_g2d_mscl_user",
	     GOUT_BLK_G2D_UID_MSCL_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_G2D_RSTnSYNC_CLK_G2D_G2D_CLK, "gout_g2d_rstnsync_clk_g2d_g2d_clk",
	     "mout_cmu_g2d_g2d_user",
	     GOUT_BLK_G2D_UID_RSTnSYNC_CLK_G2D_G2D_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G2D_AS_AXI_JPEG_ACLKM, "gout_g2d_as_axi_jpeg_aclkm", "mout_cmu_g2d_g2d_user",
	     GOUT_BLK_G2D_UID_AS_AXI_JPEG_IPCLKPORT_ACLKM, 21, 0, 0),
	GATE(CLK_GOUT_G2D_AS_AXI_JPEG_ACLKS, "gout_g2d_as_axi_jpeg_aclks", "mout_cmu_g2d_mscl_user",
	     GOUT_BLK_G2D_UID_AS_AXI_JPEG_IPCLKPORT_ACLKS, 21, 0, 0),
	GATE(CLK_GOUT_G2D_AS_AXI_MSCL_ACLKS, "gout_g2d_as_axi_mscl_aclks", "mout_cmu_g2d_mscl_user",
	     GOUT_BLK_G2D_UID_AS_AXI_MSCL_IPCLKPORT_ACLKS, 21, 0, 0),
	GATE(CLK_GOUT_G2D_AS_AXI_MSCL_ACLKM, "gout_g2d_as_axi_mscl_aclkm", "mout_cmu_g2d_g2d_user",
	     GOUT_BLK_G2D_UID_AS_AXI_MSCL_IPCLKPORT_ACLKM, 21, 0, 0),
	GATE(CLK_GOUT_G2D_LHS_ACEL_D_G2D_I_CLK, "gout_g2d_lhs_acel_d_g2d_i_clk",
	     "mout_cmu_g2d_g2d_user",
	     GOUT_BLK_G2D_UID_LHS_ACEL_D_G2D_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G2D_PPMU_G2D_ACLK, "gout_g2d_ppmu_g2d_aclk", "mout_cmu_g2d_g2d_user",
	     GOUT_BLK_G2D_UID_PPMU_G2D_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_G2D_SYSMMU_G2D_CLK, "gout_g2d_sysmmu_g2d_clk", "mout_cmu_g2d_g2d_user",
	     GOUT_BLK_G2D_UID_SYSMMU_G2D_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G2D_XIU_D_MSCL_ACLK, "gout_g2d_xiu_d_mscl_aclk", "mout_cmu_g2d_g2d_user",
	     GOUT_BLK_G2D_UID_XIU_D_MSCL_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_G2D_G2D_ACLK, "gout_g2d_g2d_aclk", "mout_cmu_g2d_g2d_user",
	     GOUT_BLK_G2D_UID_G2D_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_G2D_PGEN100_LITE_G2D_CLK, "gout_g2d_pgen100_lite_g2d_clk", "dout_g2d_busp",
	     GOUT_BLK_G2D_UID_PGEN100_LITE_G2D_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G2D_BLK_G2D_CLK_G2D_MSCL, "gout_g2d_blk_g2d_clk_g2d_mscl",
	     "mout_cmu_g2d_mscl_user",
	     GOUT_BLK_G2D_UID_BLK_G2D_IPCLKPORT_CLK_G2D_MSCL, 21, 0, 0),
	GATE(CLK_GOUT_G2D_BLK_G2D_CLK_G2D_G2D, "gout_g2d_blk_g2d_clk_g2d_g2d",
	     "mout_cmu_g2d_g2d_user",
	     GOUT_BLK_G2D_UID_BLK_G2D_IPCLKPORT_CLK_G2D_G2D, 21, 0, 0),
	GATE(CLK_GOUT_G2D_BTM_G2D_I_ACLK, "gout_g2d_btm_g2d_i_aclk", "mout_cmu_g2d_g2d_user",
	     GOUT_BLK_G2D_UID_BTM_G2D_IPCLKPORT_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_G2D_BTM_G2D_I_PCLK, "gout_g2d_btm_g2d_i_pclk", "dout_g2d_busp",
	     GOUT_BLK_G2D_UID_BTM_G2D_IPCLKPORT_I_PCLK, 21, 0, 0),
};

static const struct samsung_cmu_info g2d_cmu_info __initconst = {
	.mux_clks		= g2d_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(g2d_mux_clks),
	.div_clks		= g2d_div_clks,
	.nr_div_clks		= ARRAY_SIZE(g2d_div_clks),
	.gate_clks		= g2d_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(g2d_gate_clks),
	.nr_clk_ids		= CLKS_NR_G2D,
	.clk_regs		= g2d_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(g2d_clk_regs),
	.qch_regs		= g2d_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(g2d_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9610_cmu_g2d_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &g2d_cmu_info);
}

CLK_OF_DECLARE(exynos9610_cmu_g2d, "samsung,exynos9610-cmu-g2d",
	       exynos9610_cmu_g2d_init);

/* ---- CMU_G3D ---------------------------------------------------------*/

/* Register Offset definitions for CMU_G3D (0x11430000) */
#define PLL_LOCKTIME_PLL_G3D_PLL_LOCK_TIME			0x0000
#define MUX_CLKCMU_G3D_SWITCH_USER			0x0100
#define PLL_CON0_PLL_G3D_ENABLE			0x0120
#define MUX_CLK_G3D_BUSD			0x1004
#define DIV_CLK_G3D_BUSP			0x1804
#define CLK_BLK_G3D_UID_G3D_CMU_G3D_IPCLKPORT_PCLK	0x2000
#define CLK_BLK_G3D_UID_G3D_IPCLKPORT_CLK			0x2004
#define CLK_BLK_G3D_UID_HPM_G3D_IPCLKPORT_hpm_targetclk_c	0x2008
#define CLK_BLK_G3D_UID_RSTnSYNC_CLK_G3D_OSCCLK_IPCLKPORT_CLK	0x200c
#define GOUT_BLK_G3D_UID_BTM_G3D_IPCLKPORT_I_ACLK	0x2014
#define GOUT_BLK_G3D_UID_BTM_G3D_IPCLKPORT_I_PCLK	0x2018
#define GOUT_BLK_G3D_UID_BUSIF_HPMG3D_IPCLKPORT_PCLK	0x201c
#define GOUT_BLK_G3D_UID_GRAY2BIN_G3D_IPCLKPORT_CLK	0x2020
#define GOUT_BLK_G3D_UID_LHM_AXI_G3DSFR_IPCLKPORT_I_CLK	0x2024
#define GOUT_BLK_G3D_UID_LHM_AXI_P_G3D_IPCLKPORT_I_CLK	0x2028
#define GOUT_BLK_G3D_UID_LHS_AXI_D_G3D_IPCLKPORT_I_CLK	0x202c
#define GOUT_BLK_G3D_UID_LHS_AXI_G3DSFR_IPCLKPORT_I_CLK	0x2030
#define GOUT_BLK_G3D_UID_PGEN_LITE_G3D_IPCLKPORT_CLK	0x2034
#define GOUT_BLK_G3D_UID_RSTnSYNC_CLK_G3D_BUSD_IPCLKPORT_CLK	0x2038
#define GOUT_BLK_G3D_UID_RSTnSYNC_CLK_G3D_BUSP_IPCLKPORT_CLK	0x203c
#define GOUT_BLK_G3D_UID_SYSREG_G3D_IPCLKPORT_PCLK	0x2040

static const unsigned long g3d_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_G3D_PLL_LOCK_TIME,
	MUX_CLKCMU_G3D_SWITCH_USER,
	PLL_CON0_PLL_G3D_ENABLE,
	MUX_CLK_G3D_BUSD,
	DIV_CLK_G3D_BUSP,
	CLK_BLK_G3D_UID_G3D_CMU_G3D_IPCLKPORT_PCLK,
	CLK_BLK_G3D_UID_G3D_IPCLKPORT_CLK,
	CLK_BLK_G3D_UID_HPM_G3D_IPCLKPORT_hpm_targetclk_c,
	CLK_BLK_G3D_UID_RSTnSYNC_CLK_G3D_OSCCLK_IPCLKPORT_CLK,
	GOUT_BLK_G3D_UID_BTM_G3D_IPCLKPORT_I_ACLK,
	GOUT_BLK_G3D_UID_BTM_G3D_IPCLKPORT_I_PCLK,
	GOUT_BLK_G3D_UID_BUSIF_HPMG3D_IPCLKPORT_PCLK,
	GOUT_BLK_G3D_UID_GRAY2BIN_G3D_IPCLKPORT_CLK,
	GOUT_BLK_G3D_UID_LHM_AXI_G3DSFR_IPCLKPORT_I_CLK,
	GOUT_BLK_G3D_UID_LHM_AXI_P_G3D_IPCLKPORT_I_CLK,
	GOUT_BLK_G3D_UID_LHS_AXI_D_G3D_IPCLKPORT_I_CLK,
	GOUT_BLK_G3D_UID_LHS_AXI_G3DSFR_IPCLKPORT_I_CLK,
	GOUT_BLK_G3D_UID_PGEN_LITE_G3D_IPCLKPORT_CLK,
	GOUT_BLK_G3D_UID_RSTnSYNC_CLK_G3D_BUSD_IPCLKPORT_CLK,
	GOUT_BLK_G3D_UID_RSTnSYNC_CLK_G3D_BUSP_IPCLKPORT_CLK,
	GOUT_BLK_G3D_UID_SYSREG_G3D_IPCLKPORT_PCLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long g3d_qch_regs[] __initconst = {
	0x3010,	/* BTM_G3D_QCH */
	0x3014,	/* BUSIF_HPMG3D_QCH */
	0x3018,	/* G3D_CMU_G3D_QCH */
	0x301c,	/* G3D_QCH */
	0x3020,	/* LHM_AXI_G3DSFR_QCH */
	0x3024,	/* LHM_AXI_P_G3D_QCH */
	0x3028,	/* LHS_AXI_D_G3D_QCH */
	0x302c,	/* LHS_AXI_G3DSFR_QCH */
	0x3030,	/* PGEN_LITE_G3D_QCH */
	0x3034,	/* SYSREG_G3D_QCH */
};

static const struct samsung_pll_rate_table fout_g3d_rate_table[] __initconst = {
	PLL_35XX_RATE(26 * MHZ, 1200000000U, 600, 13, 0),
	PLL_35XX_RATE(26 * MHZ, 1000000000U, 500, 13, 0),
	PLL_35XX_RATE(26 * MHZ, 750000000U, 375, 13, 0),
	PLL_35XX_RATE(26 * MHZ, 550000000U, 550, 13, 1),
	PLL_35XX_RATE(26 * MHZ, 300000000U, 600, 13, 2),
};

static const struct samsung_pll_clock g3d_pll_clks[] __initconst = {
	PLL(pll_1052x, CLK_FOUT_G3D, "fout_g3d", "oscclk",
	    PLL_LOCKTIME_PLL_G3D_PLL_LOCK_TIME, PLL_CON0_PLL_G3D_ENABLE, fout_g3d_rate_table),
};

/* List of parent clocks for Muxes in CMU_G3D */
PNAME(mout_g3d_busd_p) = { "fout_g3d", "mout_cmu_g3d_switch_user" };
PNAME(mout_cmu_g3d_switch_user_p) = { "oscclk", "dout_clkcmu_g3d_switch" };

static const struct samsung_mux_clock g3d_mux_clks[] __initconst = {
	MUX(CLK_MOUT_G3D_BUSD, "mout_g3d_busd", mout_g3d_busd_p,
	    MUX_CLK_G3D_BUSD, 0, 1),
	MUX(CLK_MOUT_CMU_G3D_SWITCH_USER, "mout_cmu_g3d_switch_user", mout_cmu_g3d_switch_user_p,
	    MUX_CLKCMU_G3D_SWITCH_USER, 4, 1),
};

static const struct samsung_div_clock g3d_div_clks[] __initconst = {
	DIV(CLK_DOUT_G3D_BUSP, "dout_g3d_busp", "mout_g3d_busd",
	    DIV_CLK_G3D_BUSP, 0, 3),
};

static const struct samsung_gate_clock g3d_gate_clks[] __initconst = {
	GATE(CLK_GOUT_G3D_LHS_AXI_G3DSFR_I_CLK, "gout_g3d_lhs_axi_g3dsfr_i_clk", "dout_g3d_busp",
	     GOUT_BLK_G3D_UID_LHS_AXI_G3DSFR_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_LHM_AXI_P_G3D_I_CLK, "gout_g3d_lhm_axi_p_g3d_i_clk", "dout_g3d_busp",
	     GOUT_BLK_G3D_UID_LHM_AXI_P_G3D_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_SYSREG_G3D_PCLK, "gout_g3d_sysreg_g3d_pclk", "dout_g3d_busp",
	     GOUT_BLK_G3D_UID_SYSREG_G3D_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_RSTnSYNC_CLK_G3D_BUSP_CLK, "gout_g3d_rstnsync_clk_g3d_busp_clk",
	     "dout_g3d_busp",
	     GOUT_BLK_G3D_UID_RSTnSYNC_CLK_G3D_BUSP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_RSTnSYNC_CLK_G3D_OSCCLK_CLK, "gout_g3d_rstnsync_clk_g3d_oscclk_clk",
	     "oscclk",
	     CLK_BLK_G3D_UID_RSTnSYNC_CLK_G3D_OSCCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_RSTnSYNC_CLK_G3D_BUSD_CLK, "gout_g3d_rstnsync_clk_g3d_busd_clk",
	     "UNRESOLVED_DIV_CLK_G3D_BUSD",
	     GOUT_BLK_G3D_UID_RSTnSYNC_CLK_G3D_BUSD_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_LHM_AXI_G3DSFR_I_CLK, "gout_g3d_lhm_axi_g3dsfr_i_clk",
	     "UNRESOLVED_DIV_CLK_G3D_BUSD",
	     GOUT_BLK_G3D_UID_LHM_AXI_G3DSFR_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_G3D_CMU_G3D_PCLK, "gout_g3d_g3d_cmu_g3d_pclk", "dout_g3d_busp",
	     CLK_BLK_G3D_UID_G3D_CMU_G3D_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_GRAY2BIN_G3D_CLK, "gout_g3d_gray2bin_g3d_clk",
	     "UNRESOLVED_DIV_CLK_G3D_BUSD",
	     GOUT_BLK_G3D_UID_GRAY2BIN_G3D_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_PGEN_LITE_G3D_CLK, "gout_g3d_pgen_lite_g3d_clk", "dout_g3d_busp",
	     GOUT_BLK_G3D_UID_PGEN_LITE_G3D_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_HPM_G3D_hpm_targetclk_c, "gout_g3d_hpm_g3d_hpm_targetclk_c",
	     "dout_clkcmu_hpm",
	     CLK_BLK_G3D_UID_HPM_G3D_IPCLKPORT_hpm_targetclk_c, 21, 0, 0),
	GATE(CLK_GOUT_G3D_BUSIF_HPMG3D_PCLK, "gout_g3d_busif_hpmg3d_pclk", "dout_g3d_busp",
	     GOUT_BLK_G3D_UID_BUSIF_HPMG3D_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_BTM_G3D_I_ACLK, "gout_g3d_btm_g3d_i_aclk", "UNRESOLVED_DIV_CLK_G3D_BUSD",
	     GOUT_BLK_G3D_UID_BTM_G3D_IPCLKPORT_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_BTM_G3D_I_PCLK, "gout_g3d_btm_g3d_i_pclk", "dout_g3d_busp",
	     GOUT_BLK_G3D_UID_BTM_G3D_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_G3D_CLK, "gout_g3d_g3d_clk", "UNRESOLVED_DIV_CLK_G3D_BUSD",
	     CLK_BLK_G3D_UID_G3D_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_LHS_AXI_D_G3D_I_CLK, "gout_g3d_lhs_axi_d_g3d_i_clk",
	     "UNRESOLVED_DIV_CLK_G3D_BUSD",
	     GOUT_BLK_G3D_UID_LHS_AXI_D_G3D_IPCLKPORT_I_CLK, 21, 0, 0),
};

static const struct samsung_cmu_info g3d_cmu_info __initconst = {
	.pll_clks		= g3d_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(g3d_pll_clks),
	.mux_clks		= g3d_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(g3d_mux_clks),
	.div_clks		= g3d_div_clks,
	.nr_div_clks		= ARRAY_SIZE(g3d_div_clks),
	.gate_clks		= g3d_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(g3d_gate_clks),
	.nr_clk_ids		= CLKS_NR_G3D,
	.clk_regs		= g3d_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(g3d_clk_regs),
	.qch_regs		= g3d_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(g3d_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9610_cmu_g3d_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &g3d_cmu_info);
}

CLK_OF_DECLARE(exynos9610_cmu_g3d, "samsung,exynos9610-cmu-g3d",
	       exynos9610_cmu_g3d_init);

/* ---- CMU_ISP ---------------------------------------------------------*/

/* Register Offset definitions for CMU_ISP (0x14700000) */
#define MUX_CLKCMU_ISP_BUS_USER			0x0100
#define MUX_CLKCMU_ISP_GDC_USER			0x0120
#define MUX_CLKCMU_ISP_VRA_USER			0x0140
#define DIV_CLK_ISP_BUSP			0x1800
#define CLK_BLK_ISP_UID_ISP_CMU_ISP_IPCLKPORT_PCLK	0x2000
#define CLK_BLK_ISP_UID_RSTnSYNC_CLK_ISP_OSCCLK_IPCLKPORT_CLK	0x2004
#define GOUT_BLK_ISP_UID_BLK_ISP_IPCLKPORT_CLK_ISP_BUSD	0x2008
#define GOUT_BLK_ISP_UID_BLK_ISP_IPCLKPORT_CLK_ISP_GDC	0x200c
#define GOUT_BLK_ISP_UID_BLK_ISP_IPCLKPORT_CLK_ISP_VRA	0x2010
#define GOUT_BLK_ISP_UID_BTM_ISP0_IPCLKPORT_I_ACLK	0x2014
#define GOUT_BLK_ISP_UID_BTM_ISP0_IPCLKPORT_I_PCLK	0x2018
#define GOUT_BLK_ISP_UID_BTM_ISP1_IPCLKPORT_I_ACLK	0x201c
#define GOUT_BLK_ISP_UID_BTM_ISP1_IPCLKPORT_I_PCLK	0x2020
#define GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_GDC	0x2024
#define GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_ISP	0x2028
#define GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_MCSC	0x202c
#define GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_PPMU_ISP0	0x2030
#define GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_PPMU_ISP1	0x2034
#define GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_SMMU_ISP0	0x2038
#define GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_SMMU_ISP1	0x203c
#define GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_VRA	0x2040
#define GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_XIU_ASYNCM_GDC	0x2044
#define GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_XIU_ASYNCM_VRA	0x2048
#define GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_XIU_ASYNCS_GDC	0x204c
#define GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_XIU_ASYNCS_VRA	0x2050
#define GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_XIU_D_ISP	0x2054
#define GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_PCLK_PPMU_ISP0	0x2058
#define GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_PCLK_PPMU_ISP1	0x205c
#define GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_PGEN_LITE_ISP_PCLK	0x2060
#define GOUT_BLK_ISP_UID_LHM_ATB_CAMISP_IPCLKPORT_I_CLK	0x2064
#define GOUT_BLK_ISP_UID_LHM_AXI_P_ISP_IPCLKPORT_I_CLK	0x2068
#define GOUT_BLK_ISP_UID_LHS_ACEL_D0_ISP_IPCLKPORT_I_CLK	0x206c
#define GOUT_BLK_ISP_UID_LHS_ACEL_D1_ISP_IPCLKPORT_I_CLK	0x2070
#define GOUT_BLK_ISP_UID_RSTnSYNC_CLK_ISP_BUSD_IPCLKPORT_CLK	0x2074
#define GOUT_BLK_ISP_UID_RSTnSYNC_CLK_ISP_BUSP_IPCLKPORT_CLK	0x2078
#define GOUT_BLK_ISP_UID_RSTnSYNC_CLK_ISP_GDC_IPCLKPORT_CLK	0x207c
#define GOUT_BLK_ISP_UID_RSTnSYNC_CLK_ISP_VRA_IPCLKPORT_CLK	0x2080
#define GOUT_BLK_ISP_UID_SYSREG_ISP_IPCLKPORT_PCLK	0x2084

static const unsigned long isp_clk_regs[] __initconst = {
	MUX_CLKCMU_ISP_BUS_USER,
	MUX_CLKCMU_ISP_GDC_USER,
	MUX_CLKCMU_ISP_VRA_USER,
	DIV_CLK_ISP_BUSP,
	CLK_BLK_ISP_UID_ISP_CMU_ISP_IPCLKPORT_PCLK,
	CLK_BLK_ISP_UID_RSTnSYNC_CLK_ISP_OSCCLK_IPCLKPORT_CLK,
	GOUT_BLK_ISP_UID_BLK_ISP_IPCLKPORT_CLK_ISP_BUSD,
	GOUT_BLK_ISP_UID_BLK_ISP_IPCLKPORT_CLK_ISP_GDC,
	GOUT_BLK_ISP_UID_BLK_ISP_IPCLKPORT_CLK_ISP_VRA,
	GOUT_BLK_ISP_UID_BTM_ISP0_IPCLKPORT_I_ACLK,
	GOUT_BLK_ISP_UID_BTM_ISP0_IPCLKPORT_I_PCLK,
	GOUT_BLK_ISP_UID_BTM_ISP1_IPCLKPORT_I_ACLK,
	GOUT_BLK_ISP_UID_BTM_ISP1_IPCLKPORT_I_PCLK,
	GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_GDC,
	GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_ISP,
	GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_MCSC,
	GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_PPMU_ISP0,
	GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_PPMU_ISP1,
	GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_SMMU_ISP0,
	GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_SMMU_ISP1,
	GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_VRA,
	GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_XIU_ASYNCM_GDC,
	GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_XIU_ASYNCM_VRA,
	GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_XIU_ASYNCS_GDC,
	GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_XIU_ASYNCS_VRA,
	GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_XIU_D_ISP,
	GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_PCLK_PPMU_ISP0,
	GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_PCLK_PPMU_ISP1,
	GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_PGEN_LITE_ISP_PCLK,
	GOUT_BLK_ISP_UID_LHM_ATB_CAMISP_IPCLKPORT_I_CLK,
	GOUT_BLK_ISP_UID_LHM_AXI_P_ISP_IPCLKPORT_I_CLK,
	GOUT_BLK_ISP_UID_LHS_ACEL_D0_ISP_IPCLKPORT_I_CLK,
	GOUT_BLK_ISP_UID_LHS_ACEL_D1_ISP_IPCLKPORT_I_CLK,
	GOUT_BLK_ISP_UID_RSTnSYNC_CLK_ISP_BUSD_IPCLKPORT_CLK,
	GOUT_BLK_ISP_UID_RSTnSYNC_CLK_ISP_BUSP_IPCLKPORT_CLK,
	GOUT_BLK_ISP_UID_RSTnSYNC_CLK_ISP_GDC_IPCLKPORT_CLK,
	GOUT_BLK_ISP_UID_RSTnSYNC_CLK_ISP_VRA_IPCLKPORT_CLK,
	GOUT_BLK_ISP_UID_SYSREG_ISP_IPCLKPORT_PCLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long isp_qch_regs[] __initconst = {
	0x3010,	/* BTM_ISP0_QCH */
	0x3014,	/* BTM_ISP1_QCH */
	0x3018,	/* IS6P10P0_ISP_QCH_S_ISP_GDC */
	0x301c,	/* IS6P10P0_ISP_QCH_S_ISP_ISP */
	0x3020,	/* IS6P10P0_ISP_QCH_S_ISP_MCSC */
	0x3024,	/* IS6P10P0_ISP_QCH_S_ISP_PGEN_LITE_ISP */
	0x3028,	/* IS6P10P0_ISP_QCH_S_ISP_PPMU_ISP0 */
	0x302c,	/* IS6P10P0_ISP_QCH_S_ISP_PPMU_ISP1 */
	0x3030,	/* IS6P10P0_ISP_QCH_S_ISP_SMMU_ISP0 */
	0x3034,	/* IS6P10P0_ISP_QCH_S_ISP_SMMU_ISP1 */
	0x3038,	/* IS6P10P0_ISP_QCH_S_ISP_VRA */
	0x303c,	/* ISP_CMU_ISP_QCH */
	0x3040,	/* LHM_ATB_CAMISP_QCH */
	0x3044,	/* LHM_AXI_P_ISP_QCH */
	0x3048,	/* LHS_ACEL_D0_ISP_QCH */
	0x304c,	/* LHS_ACEL_D1_ISP_QCH */
	0x3050,	/* SYSREG_ISP_QCH */
};

/* List of parent clocks for Muxes in CMU_ISP */
PNAME(mout_cmu_isp_bus_user_p) = { "oscclk", "dout_clkcmu_isp_bus" };
PNAME(mout_cmu_isp_vra_user_p) = { "oscclk", "dout_clkcmu_isp_vra" };
PNAME(mout_cmu_isp_gdc_user_p) = { "oscclk", "dout_clkcmu_isp_gdc" };

static const struct samsung_mux_clock isp_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_ISP_BUS_USER, "mout_cmu_isp_bus_user", mout_cmu_isp_bus_user_p,
	    MUX_CLKCMU_ISP_BUS_USER, 4, 1),
	MUX(CLK_MOUT_CMU_ISP_VRA_USER, "mout_cmu_isp_vra_user", mout_cmu_isp_vra_user_p,
	    MUX_CLKCMU_ISP_VRA_USER, 4, 1),
	MUX(CLK_MOUT_CMU_ISP_GDC_USER, "mout_cmu_isp_gdc_user", mout_cmu_isp_gdc_user_p,
	    MUX_CLKCMU_ISP_GDC_USER, 4, 1),
};

static const struct samsung_div_clock isp_div_clks[] __initconst = {
	DIV(CLK_DOUT_ISP_BUSP, "dout_isp_busp", "mout_cmu_isp_bus_user",
	    DIV_CLK_ISP_BUSP, 0, 2),
};

static const struct samsung_gate_clock isp_gate_clks[] __initconst = {
	GATE(CLK_GOUT_ISP_RSTnSYNC_CLK_ISP_OSCCLK_CLK, "gout_isp_rstnsync_clk_isp_oscclk_clk",
	     "oscclk",
	     CLK_BLK_ISP_UID_RSTnSYNC_CLK_ISP_OSCCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_ISP_ISP_CMU_ISP_PCLK, "gout_isp_isp_cmu_isp_pclk", "dout_isp_busp",
	     CLK_BLK_ISP_UID_ISP_CMU_ISP_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_ISP_RSTnSYNC_CLK_ISP_BUSP_CLK, "gout_isp_rstnsync_clk_isp_busp_clk",
	     "dout_isp_busp",
	     GOUT_BLK_ISP_UID_RSTnSYNC_CLK_ISP_BUSP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_ISP_RSTnSYNC_CLK_ISP_BUSD_CLK, "gout_isp_rstnsync_clk_isp_busd_clk",
	     "mout_cmu_isp_bus_user",
	     GOUT_BLK_ISP_UID_RSTnSYNC_CLK_ISP_BUSD_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_ISP_RSTnSYNC_CLK_ISP_GDC_CLK, "gout_isp_rstnsync_clk_isp_gdc_clk",
	     "mout_cmu_isp_gdc_user",
	     GOUT_BLK_ISP_UID_RSTnSYNC_CLK_ISP_GDC_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_ISP_RSTnSYNC_CLK_ISP_VRA_CLK, "gout_isp_rstnsync_clk_isp_vra_clk",
	     "mout_cmu_isp_vra_user",
	     GOUT_BLK_ISP_UID_RSTnSYNC_CLK_ISP_VRA_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_ISP_SYSREG_ISP_PCLK, "gout_isp_sysreg_isp_pclk", "dout_isp_busp",
	     GOUT_BLK_ISP_UID_SYSREG_ISP_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_ISP_LHM_AXI_P_ISP_I_CLK, "gout_isp_lhm_axi_p_isp_i_clk", "dout_isp_busp",
	     GOUT_BLK_ISP_UID_LHM_AXI_P_ISP_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_ISP_LHS_ACEL_D1_ISP_I_CLK, "gout_isp_lhs_acel_d1_isp_i_clk",
	     "mout_cmu_isp_bus_user",
	     GOUT_BLK_ISP_UID_LHS_ACEL_D1_ISP_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_ISP_LHS_ACEL_D0_ISP_I_CLK, "gout_isp_lhs_acel_d0_isp_i_clk",
	     "mout_cmu_isp_bus_user",
	     GOUT_BLK_ISP_UID_LHS_ACEL_D0_ISP_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_ISP_is6p10p0_ISP_PCLK_PPMU_ISP1, "gout_isp_is6p10p0_isp_pclk_ppmu_isp1",
	     "dout_isp_busp",
	     GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_PCLK_PPMU_ISP1, 21, 0, 0),
	GATE(CLK_GOUT_ISP_is6p10p0_ISP_ACLK_XIU_ASYNCM_VRA, "gout_isp_is6p10p0_isp_aclk_xiu_asyncm_vra",
	     "mout_cmu_isp_bus_user",
	     GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_XIU_ASYNCM_VRA, 21, 0, 0),
	GATE(CLK_GOUT_ISP_is6p10p0_ISP_ACLK_XIU_ASYNCS_VRA, "gout_isp_is6p10p0_isp_aclk_xiu_asyncs_vra",
	     "mout_cmu_isp_vra_user",
	     GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_XIU_ASYNCS_VRA, 21, 0, 0),
	GATE(CLK_GOUT_ISP_is6p10p0_ISP_ACLK_XIU_ASYNCS_GDC, "gout_isp_is6p10p0_isp_aclk_xiu_asyncs_gdc",
	     "mout_cmu_isp_gdc_user",
	     GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_XIU_ASYNCS_GDC, 21, 0, 0),
	GATE(CLK_GOUT_ISP_is6p10p0_ISP_ACLK_XIU_ASYNCM_GDC, "gout_isp_is6p10p0_isp_aclk_xiu_asyncm_gdc",
	     "mout_cmu_isp_bus_user",
	     GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_XIU_ASYNCM_GDC, 21, 0, 0),
	GATE(CLK_GOUT_ISP_is6p10p0_ISP_ACLK_PPMU_ISP0, "gout_isp_is6p10p0_isp_aclk_ppmu_isp0",
	     "mout_cmu_isp_bus_user",
	     GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_PPMU_ISP0, 21, 0, 0),
	GATE(CLK_GOUT_ISP_is6p10p0_ISP_PCLK_PPMU_ISP0, "gout_isp_is6p10p0_isp_pclk_ppmu_isp0",
	     "dout_isp_busp",
	     GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_PCLK_PPMU_ISP0, 21, 0, 0),
	GATE(CLK_GOUT_ISP_is6p10p0_ISP_ACLK_SMMU_ISP0, "gout_isp_is6p10p0_isp_aclk_smmu_isp0",
	     "mout_cmu_isp_bus_user",
	     GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_SMMU_ISP0, 21, 0, 0),
	GATE(CLK_GOUT_ISP_is6p10p0_ISP_ACLK_GDC, "gout_isp_is6p10p0_isp_aclk_gdc",
	     "mout_cmu_isp_gdc_user",
	     GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_GDC, 21, 0, 0),
	GATE(CLK_GOUT_ISP_is6p10p0_ISP_ACLK_ISP, "gout_isp_is6p10p0_isp_aclk_isp",
	     "mout_cmu_isp_bus_user",
	     GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_ISP, 21, 0, 0),
	GATE(CLK_GOUT_ISP_is6p10p0_ISP_ACLK_VRA, "gout_isp_is6p10p0_isp_aclk_vra",
	     "mout_cmu_isp_vra_user",
	     GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_VRA, 21, 0, 0),
	GATE(CLK_GOUT_ISP_LHM_ATB_CAMISP_I_CLK, "gout_isp_lhm_atb_camisp_i_clk",
	     "mout_cmu_isp_bus_user",
	     GOUT_BLK_ISP_UID_LHM_ATB_CAMISP_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_ISP_BLK_ISP_CLK_ISP_BUSD, "gout_isp_blk_isp_clk_isp_busd",
	     "mout_cmu_isp_bus_user",
	     GOUT_BLK_ISP_UID_BLK_ISP_IPCLKPORT_CLK_ISP_BUSD, 21, 0, 0),
	GATE(CLK_GOUT_ISP_BLK_ISP_CLK_ISP_GDC, "gout_isp_blk_isp_clk_isp_gdc",
	     "mout_cmu_isp_gdc_user",
	     GOUT_BLK_ISP_UID_BLK_ISP_IPCLKPORT_CLK_ISP_GDC, 21, 0, 0),
	GATE(CLK_GOUT_ISP_BLK_ISP_CLK_ISP_VRA, "gout_isp_blk_isp_clk_isp_vra",
	     "mout_cmu_isp_vra_user",
	     GOUT_BLK_ISP_UID_BLK_ISP_IPCLKPORT_CLK_ISP_VRA, 21, 0, 0),
	GATE(CLK_GOUT_ISP_is6p10p0_ISP_ACLK_PPMU_ISP1, "gout_isp_is6p10p0_isp_aclk_ppmu_isp1",
	     "mout_cmu_isp_bus_user",
	     GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_PPMU_ISP1, 21, 0, 0),
	GATE(CLK_GOUT_ISP_is6p10p0_ISP_ACLK_SMMU_ISP1, "gout_isp_is6p10p0_isp_aclk_smmu_isp1",
	     "mout_cmu_isp_bus_user",
	     GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_SMMU_ISP1, 21, 0, 0),
	GATE(CLK_GOUT_ISP_is6p10p0_ISP_ACLK_XIU_D_ISP, "gout_isp_is6p10p0_isp_aclk_xiu_d_isp",
	     "mout_cmu_isp_bus_user",
	     GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_XIU_D_ISP, 21, 0, 0),
	GATE(CLK_GOUT_ISP_is6p10p0_ISP_ACLK_MCSC, "gout_isp_is6p10p0_isp_aclk_mcsc",
	     "mout_cmu_isp_bus_user",
	     GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_ACLK_MCSC, 21, 0, 0),
	GATE(CLK_GOUT_ISP_is6p10p0_ISP_PGEN_LITE_ISP_PCLK, "gout_isp_is6p10p0_isp_pgen_lite_isp_pclk",
	     "dout_isp_busp",
	     GOUT_BLK_ISP_UID_is6p10p0_ISP_IPCLKPORT_PGEN_LITE_ISP_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_ISP_BTM_ISP0_I_ACLK, "gout_isp_btm_isp0_i_aclk", "mout_cmu_isp_bus_user",
	     GOUT_BLK_ISP_UID_BTM_ISP0_IPCLKPORT_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_ISP_BTM_ISP0_I_PCLK, "gout_isp_btm_isp0_i_pclk", "dout_isp_busp",
	     GOUT_BLK_ISP_UID_BTM_ISP0_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_ISP_BTM_ISP1_I_ACLK, "gout_isp_btm_isp1_i_aclk", "mout_cmu_isp_bus_user",
	     GOUT_BLK_ISP_UID_BTM_ISP1_IPCLKPORT_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_ISP_BTM_ISP1_I_PCLK, "gout_isp_btm_isp1_i_pclk", "dout_isp_busp",
	     GOUT_BLK_ISP_UID_BTM_ISP1_IPCLKPORT_I_PCLK, 21, 0, 0),
};

static const struct samsung_cmu_info isp_cmu_info __initconst = {
	.mux_clks		= isp_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(isp_mux_clks),
	.div_clks		= isp_div_clks,
	.nr_div_clks		= ARRAY_SIZE(isp_div_clks),
	.gate_clks		= isp_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(isp_gate_clks),
	.nr_clk_ids		= CLKS_NR_ISP,
	.clk_regs		= isp_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(isp_clk_regs),
	.qch_regs		= isp_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(isp_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9610_cmu_isp_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &isp_cmu_info);
}

CLK_OF_DECLARE(exynos9610_cmu_isp, "samsung,exynos9610-cmu-isp",
	       exynos9610_cmu_isp_init);

/* ---- CMU_MFC ---------------------------------------------------------*/

/* Register Offset definitions for CMU_MFC (0x12c00000) */
#define MUX_CLKCMU_MFC_MFC_USER			0x0100
#define MUX_CLKCMU_MFC_WFD_USER			0x0120
#define DIV_CLK_MFC_BUSP			0x1800
#define CLK_BLK_MFC_UID_MFC_CMU_MFC_IPCLKPORT_PCLK	0x2000
#define CLK_BLK_MFC_UID_RSTnSYNC_CLK_MFC_OSCCLK_IPCLKPORT_CLK	0x2004
#define GOUT_BLK_MFC_UID_AS_AXI_WFD_IPCLKPORT_ACLKM	0x2008
#define GOUT_BLK_MFC_UID_AS_AXI_WFD_IPCLKPORT_ACLKS	0x200c
#define GOUT_BLK_MFC_UID_BLK_MFC_IPCLKPORT_CLK_MFC_MFC	0x2010
#define GOUT_BLK_MFC_UID_BLK_MFC_IPCLKPORT_CLK_MFC_WFD	0x2014
#define GOUT_BLK_MFC_UID_BTM_MFCD0_IPCLKPORT_I_ACLK	0x2018
#define GOUT_BLK_MFC_UID_BTM_MFCD0_IPCLKPORT_I_PCLK	0x201c
#define GOUT_BLK_MFC_UID_BTM_MFCD1_IPCLKPORT_I_ACLK	0x2020
#define GOUT_BLK_MFC_UID_BTM_MFCD1_IPCLKPORT_I_PCLK	0x2024
#define GOUT_BLK_MFC_UID_LHM_AXI_P_MFC_IPCLKPORT_I_CLK	0x2028
#define GOUT_BLK_MFC_UID_LHS_ACEL_D0_MFC_IPCLKPORT_I_CLK	0x202c
#define GOUT_BLK_MFC_UID_LHS_ACEL_D1_MFC_IPCLKPORT_I_CLK	0x2030
#define GOUT_BLK_MFC_UID_LH_ATB_MFC_IPCLKPORT_I_CLK_MI	0x2034
#define GOUT_BLK_MFC_UID_LH_ATB_MFC_IPCLKPORT_I_CLK_SI	0x2038
#define GOUT_BLK_MFC_UID_MFC_IPCLKPORT_ACLK			0x203c
#define GOUT_BLK_MFC_UID_PGEN100_LITE_MFC_IPCLKPORT_CLK	0x2040
#define GOUT_BLK_MFC_UID_PPMU_MFCD0_IPCLKPORT_ACLK	0x2044
#define GOUT_BLK_MFC_UID_PPMU_MFCD0_IPCLKPORT_PCLK	0x2048
#define GOUT_BLK_MFC_UID_PPMU_MFCD1_IPCLKPORT_ACLK	0x204c
#define GOUT_BLK_MFC_UID_PPMU_MFCD1_IPCLKPORT_PCLK	0x2050
#define GOUT_BLK_MFC_UID_RSTnSYNC_CLK_MFC_BUSP_IPCLKPORT_CLK	0x2054
#define GOUT_BLK_MFC_UID_RSTnSYNC_CLK_MFC_LH_ATB_MFC_MI_SW_RESET_IPCLKPORT_CLK	0x2058
#define GOUT_BLK_MFC_UID_RSTnSYNC_CLK_MFC_LH_ATB_MFC_SI_SW_RESET_IPCLKPORT_CLK	0x205c
#define GOUT_BLK_MFC_UID_RSTnSYNC_CLK_MFC_MFC_IPCLKPORT_CLK	0x2060
#define GOUT_BLK_MFC_UID_RSTnSYNC_CLK_MFC_MFC_SW_RESET_IPCLKPORT_CLK	0x2064
#define GOUT_BLK_MFC_UID_RSTnSYNC_CLK_MFC_WFD_IPCLKPORT_CLK	0x2068
#define GOUT_BLK_MFC_UID_RSTnSYNC_CLK_MFC_WFD_SW_RESET_IPCLKPORT_CLK	0x206c
#define GOUT_BLK_MFC_UID_SYSMMU_MFCD0_IPCLKPORT_CLK	0x2070
#define GOUT_BLK_MFC_UID_SYSMMU_MFCD1_IPCLKPORT_CLK	0x2074
#define GOUT_BLK_MFC_UID_SYSREG_MFC_IPCLKPORT_PCLK	0x2078
#define GOUT_BLK_MFC_UID_WFD_IPCLKPORT_ACLK			0x207c
#define GOUT_BLK_MFC_UID_XIU_D_MFC_IPCLKPORT_ACLK	0x2080

static const unsigned long mfc_clk_regs[] __initconst = {
	MUX_CLKCMU_MFC_MFC_USER,
	MUX_CLKCMU_MFC_WFD_USER,
	DIV_CLK_MFC_BUSP,
	CLK_BLK_MFC_UID_MFC_CMU_MFC_IPCLKPORT_PCLK,
	CLK_BLK_MFC_UID_RSTnSYNC_CLK_MFC_OSCCLK_IPCLKPORT_CLK,
	GOUT_BLK_MFC_UID_AS_AXI_WFD_IPCLKPORT_ACLKM,
	GOUT_BLK_MFC_UID_AS_AXI_WFD_IPCLKPORT_ACLKS,
	GOUT_BLK_MFC_UID_BLK_MFC_IPCLKPORT_CLK_MFC_MFC,
	GOUT_BLK_MFC_UID_BLK_MFC_IPCLKPORT_CLK_MFC_WFD,
	GOUT_BLK_MFC_UID_BTM_MFCD0_IPCLKPORT_I_ACLK,
	GOUT_BLK_MFC_UID_BTM_MFCD0_IPCLKPORT_I_PCLK,
	GOUT_BLK_MFC_UID_BTM_MFCD1_IPCLKPORT_I_ACLK,
	GOUT_BLK_MFC_UID_BTM_MFCD1_IPCLKPORT_I_PCLK,
	GOUT_BLK_MFC_UID_LHM_AXI_P_MFC_IPCLKPORT_I_CLK,
	GOUT_BLK_MFC_UID_LHS_ACEL_D0_MFC_IPCLKPORT_I_CLK,
	GOUT_BLK_MFC_UID_LHS_ACEL_D1_MFC_IPCLKPORT_I_CLK,
	GOUT_BLK_MFC_UID_LH_ATB_MFC_IPCLKPORT_I_CLK_MI,
	GOUT_BLK_MFC_UID_LH_ATB_MFC_IPCLKPORT_I_CLK_SI,
	GOUT_BLK_MFC_UID_MFC_IPCLKPORT_ACLK,
	GOUT_BLK_MFC_UID_PGEN100_LITE_MFC_IPCLKPORT_CLK,
	GOUT_BLK_MFC_UID_PPMU_MFCD0_IPCLKPORT_ACLK,
	GOUT_BLK_MFC_UID_PPMU_MFCD0_IPCLKPORT_PCLK,
	GOUT_BLK_MFC_UID_PPMU_MFCD1_IPCLKPORT_ACLK,
	GOUT_BLK_MFC_UID_PPMU_MFCD1_IPCLKPORT_PCLK,
	GOUT_BLK_MFC_UID_RSTnSYNC_CLK_MFC_BUSP_IPCLKPORT_CLK,
	GOUT_BLK_MFC_UID_RSTnSYNC_CLK_MFC_LH_ATB_MFC_MI_SW_RESET_IPCLKPORT_CLK,
	GOUT_BLK_MFC_UID_RSTnSYNC_CLK_MFC_LH_ATB_MFC_SI_SW_RESET_IPCLKPORT_CLK,
	GOUT_BLK_MFC_UID_RSTnSYNC_CLK_MFC_MFC_IPCLKPORT_CLK,
	GOUT_BLK_MFC_UID_RSTnSYNC_CLK_MFC_MFC_SW_RESET_IPCLKPORT_CLK,
	GOUT_BLK_MFC_UID_RSTnSYNC_CLK_MFC_WFD_IPCLKPORT_CLK,
	GOUT_BLK_MFC_UID_RSTnSYNC_CLK_MFC_WFD_SW_RESET_IPCLKPORT_CLK,
	GOUT_BLK_MFC_UID_SYSMMU_MFCD0_IPCLKPORT_CLK,
	GOUT_BLK_MFC_UID_SYSMMU_MFCD1_IPCLKPORT_CLK,
	GOUT_BLK_MFC_UID_SYSREG_MFC_IPCLKPORT_PCLK,
	GOUT_BLK_MFC_UID_WFD_IPCLKPORT_ACLK,
	GOUT_BLK_MFC_UID_XIU_D_MFC_IPCLKPORT_ACLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long mfc_qch_regs[] __initconst = {
	0x300c,	/* BTM_MFCD0_QCH */
	0x3010,	/* BTM_MFCD1_QCH */
	0x3014,	/* LHM_AXI_P_MFC_QCH */
	0x3018,	/* LHS_ACEL_D0_MFC_QCH */
	0x301c,	/* LHS_ACEL_D1_MFC_QCH */
	0x3020,	/* LH_ATB_MFC_QCH_S_MI */
	0x3024,	/* LH_ATB_MFC_QCH_S_SI */
	0x3028,	/* MFC_CMU_MFC_QCH */
	0x302c,	/* MFC_QCH */
	0x3030,	/* PGEN100_LITE_MFC_QCH */
	0x3034,	/* PPMU_MFCD0_QCH */
	0x3038,	/* PPMU_MFCD1_QCH */
	0x303c,	/* RSTNSYNC_CLK_MFC_LH_ATB_MFC_MI_SW_RESET_QCH */
	0x3040,	/* RSTNSYNC_CLK_MFC_LH_ATB_MFC_SI_SW_RESET_QCH */
	0x3044,	/* RSTNSYNC_CLK_MFC_MFC_SW_RESET_QCH */
	0x3048,	/* RSTNSYNC_CLK_MFC_WFD_SW_RESET_QCH */
	0x304c,	/* SYSMMU_MFCD0_QCH */
	0x3050,	/* SYSMMU_MFCD1_QCH */
	0x3054,	/* SYSREG_MFC_QCH */
	0x3058,	/* WFD_QCH */
};

/* List of parent clocks for Muxes in CMU_MFC */
PNAME(mout_cmu_mfc_wfd_user_p) = { "oscclk", "dout_clkcmu_mfc_wfd" };
PNAME(mout_cmu_mfc_mfc_user_p) = { "oscclk", "dout_clkcmu_mfc_mfc" };

static const struct samsung_mux_clock mfc_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_MFC_WFD_USER, "mout_cmu_mfc_wfd_user", mout_cmu_mfc_wfd_user_p,
	    MUX_CLKCMU_MFC_WFD_USER, 4, 1),
	MUX(CLK_MOUT_CMU_MFC_MFC_USER, "mout_cmu_mfc_mfc_user", mout_cmu_mfc_mfc_user_p,
	    MUX_CLKCMU_MFC_MFC_USER, 4, 1),
};

static const struct samsung_div_clock mfc_div_clks[] __initconst = {
	DIV(CLK_DOUT_MFC_BUSP, "dout_mfc_busp", "mout_cmu_mfc_mfc_user",
	    DIV_CLK_MFC_BUSP, 0, 3),
};

static const struct samsung_gate_clock mfc_gate_clks[] __initconst = {
	GATE(CLK_GOUT_MFC_MFC_CMU_MFC_PCLK, "gout_mfc_mfc_cmu_mfc_pclk", "dout_mfc_busp",
	     CLK_BLK_MFC_UID_MFC_CMU_MFC_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_AS_AXI_WFD_ACLKS, "gout_mfc_as_axi_wfd_aclks", "mout_cmu_mfc_wfd_user",
	     GOUT_BLK_MFC_UID_AS_AXI_WFD_IPCLKPORT_ACLKS, 21, 0, 0),
	GATE(CLK_GOUT_MFC_AS_AXI_WFD_ACLKM, "gout_mfc_as_axi_wfd_aclkm", "mout_cmu_mfc_mfc_user",
	     GOUT_BLK_MFC_UID_AS_AXI_WFD_IPCLKPORT_ACLKM, 21, 0, 0),
	GATE(CLK_GOUT_MFC_LH_ATB_MFC_I_CLK_SI, "gout_mfc_lh_atb_mfc_i_clk_si",
	     "mout_cmu_mfc_mfc_user",
	     GOUT_BLK_MFC_UID_LH_ATB_MFC_IPCLKPORT_I_CLK_SI, 21, 0, 0),
	GATE(CLK_GOUT_MFC_LH_ATB_MFC_I_CLK_MI, "gout_mfc_lh_atb_mfc_i_clk_mi",
	     "mout_cmu_mfc_wfd_user",
	     GOUT_BLK_MFC_UID_LH_ATB_MFC_IPCLKPORT_I_CLK_MI, 21, 0, 0),
	GATE(CLK_GOUT_MFC_MFC_ACLK, "gout_mfc_mfc_aclk", "mout_cmu_mfc_mfc_user",
	     GOUT_BLK_MFC_UID_MFC_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_PGEN100_LITE_MFC_CLK, "gout_mfc_pgen100_lite_mfc_clk", "dout_mfc_busp",
	     GOUT_BLK_MFC_UID_PGEN100_LITE_MFC_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_PPMU_MFCD0_ACLK, "gout_mfc_ppmu_mfcd0_aclk", "mout_cmu_mfc_mfc_user",
	     GOUT_BLK_MFC_UID_PPMU_MFCD0_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_PPMU_MFCD0_PCLK, "gout_mfc_ppmu_mfcd0_pclk", "dout_mfc_busp",
	     GOUT_BLK_MFC_UID_PPMU_MFCD0_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_PPMU_MFCD1_ACLK, "gout_mfc_ppmu_mfcd1_aclk", "mout_cmu_mfc_mfc_user",
	     GOUT_BLK_MFC_UID_PPMU_MFCD1_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_PPMU_MFCD1_PCLK, "gout_mfc_ppmu_mfcd1_pclk", "dout_mfc_busp",
	     GOUT_BLK_MFC_UID_PPMU_MFCD1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_SYSMMU_MFCD0_CLK, "gout_mfc_sysmmu_mfcd0_clk", "mout_cmu_mfc_mfc_user",
	     GOUT_BLK_MFC_UID_SYSMMU_MFCD0_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_SYSMMU_MFCD1_CLK, "gout_mfc_sysmmu_mfcd1_clk", "mout_cmu_mfc_mfc_user",
	     GOUT_BLK_MFC_UID_SYSMMU_MFCD1_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_SYSREG_MFC_PCLK, "gout_mfc_sysreg_mfc_pclk", "dout_mfc_busp",
	     GOUT_BLK_MFC_UID_SYSREG_MFC_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_WFD_ACLK, "gout_mfc_wfd_aclk", "mout_cmu_mfc_wfd_user",
	     GOUT_BLK_MFC_UID_WFD_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_XIU_D_MFC_ACLK, "gout_mfc_xiu_d_mfc_aclk", "mout_cmu_mfc_mfc_user",
	     GOUT_BLK_MFC_UID_XIU_D_MFC_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_BLK_MFC_CLK_MFC_MFC, "gout_mfc_blk_mfc_clk_mfc_mfc",
	     "mout_cmu_mfc_mfc_user",
	     GOUT_BLK_MFC_UID_BLK_MFC_IPCLKPORT_CLK_MFC_MFC, 21, 0, 0),
	GATE(CLK_GOUT_MFC_BLK_MFC_CLK_MFC_WFD, "gout_mfc_blk_mfc_clk_mfc_wfd",
	     "mout_cmu_mfc_wfd_user",
	     GOUT_BLK_MFC_UID_BLK_MFC_IPCLKPORT_CLK_MFC_WFD, 21, 0, 0),
	GATE(CLK_GOUT_MFC_RSTnSYNC_CLK_MFC_MFC_CLK, "gout_mfc_rstnsync_clk_mfc_mfc_clk",
	     "mout_cmu_mfc_mfc_user",
	     GOUT_BLK_MFC_UID_RSTnSYNC_CLK_MFC_MFC_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_RSTnSYNC_CLK_MFC_WFD_CLK, "gout_mfc_rstnsync_clk_mfc_wfd_clk",
	     "mout_cmu_mfc_wfd_user",
	     GOUT_BLK_MFC_UID_RSTnSYNC_CLK_MFC_WFD_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_RSTnSYNC_CLK_MFC_OSCCLK_CLK, "gout_mfc_rstnsync_clk_mfc_oscclk_clk",
	     "oscclk",
	     CLK_BLK_MFC_UID_RSTnSYNC_CLK_MFC_OSCCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_RSTnSYNC_CLK_MFC_BUSP_CLK, "gout_mfc_rstnsync_clk_mfc_busp_clk",
	     "dout_mfc_busp",
	     GOUT_BLK_MFC_UID_RSTnSYNC_CLK_MFC_BUSP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_BTM_MFCD0_I_PCLK, "gout_mfc_btm_mfcd0_i_pclk", "dout_mfc_busp",
	     GOUT_BLK_MFC_UID_BTM_MFCD0_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_BTM_MFCD0_I_ACLK, "gout_mfc_btm_mfcd0_i_aclk", "mout_cmu_mfc_mfc_user",
	     GOUT_BLK_MFC_UID_BTM_MFCD0_IPCLKPORT_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_BTM_MFCD1_I_ACLK, "gout_mfc_btm_mfcd1_i_aclk", "mout_cmu_mfc_mfc_user",
	     GOUT_BLK_MFC_UID_BTM_MFCD1_IPCLKPORT_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_BTM_MFCD1_I_PCLK, "gout_mfc_btm_mfcd1_i_pclk", "dout_mfc_busp",
	     GOUT_BLK_MFC_UID_BTM_MFCD1_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_LHM_AXI_P_MFC_I_CLK, "gout_mfc_lhm_axi_p_mfc_i_clk", "dout_mfc_busp",
	     GOUT_BLK_MFC_UID_LHM_AXI_P_MFC_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_LHS_ACEL_D0_MFC_I_CLK, "gout_mfc_lhs_acel_d0_mfc_i_clk",
	     "mout_cmu_mfc_mfc_user",
	     GOUT_BLK_MFC_UID_LHS_ACEL_D0_MFC_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_LHS_ACEL_D1_MFC_I_CLK, "gout_mfc_lhs_acel_d1_mfc_i_clk",
	     "mout_cmu_mfc_mfc_user",
	     GOUT_BLK_MFC_UID_LHS_ACEL_D1_MFC_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_RSTnSYNC_CLK_MFC_LH_ATB_MFC_MI_SW_RESET_CLK, "gout_mfc_rstnsync_clk_mfc_lh_atb_mfc_mi_sw_reset_clk",
	     "mout_cmu_mfc_wfd_user",
	     GOUT_BLK_MFC_UID_RSTnSYNC_CLK_MFC_LH_ATB_MFC_MI_SW_RESET_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_RSTnSYNC_CLK_MFC_LH_ATB_MFC_SI_SW_RESET_CLK, "gout_mfc_rstnsync_clk_mfc_lh_atb_mfc_si_sw_reset_clk",
	     "mout_cmu_mfc_mfc_user",
	     GOUT_BLK_MFC_UID_RSTnSYNC_CLK_MFC_LH_ATB_MFC_SI_SW_RESET_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_RSTnSYNC_CLK_MFC_MFC_SW_RESET_CLK, "gout_mfc_rstnsync_clk_mfc_mfc_sw_reset_clk",
	     "mout_cmu_mfc_mfc_user",
	     GOUT_BLK_MFC_UID_RSTnSYNC_CLK_MFC_MFC_SW_RESET_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MFC_RSTnSYNC_CLK_MFC_WFD_SW_RESET_CLK, "gout_mfc_rstnsync_clk_mfc_wfd_sw_reset_clk",
	     "mout_cmu_mfc_wfd_user",
	     GOUT_BLK_MFC_UID_RSTnSYNC_CLK_MFC_WFD_SW_RESET_IPCLKPORT_CLK, 21, 0, 0),
};

static const struct samsung_cmu_info mfc_cmu_info __initconst = {
	.mux_clks		= mfc_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(mfc_mux_clks),
	.div_clks		= mfc_div_clks,
	.nr_div_clks		= ARRAY_SIZE(mfc_div_clks),
	.gate_clks		= mfc_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(mfc_gate_clks),
	.nr_clk_ids		= CLKS_NR_MFC,
	.clk_regs		= mfc_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(mfc_clk_regs),
	.qch_regs		= mfc_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(mfc_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9610_cmu_mfc_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &mfc_cmu_info);
}

CLK_OF_DECLARE(exynos9610_cmu_mfc, "samsung,exynos9610-cmu-mfc",
	       exynos9610_cmu_mfc_init);

/* ---- CMU_MIF ---------------------------------------------------------*/

/* Register Offset definitions for CMU_MIF (0x10400000) */
#define PLL_LOCKTIME_PLL_MIF_PLL_LOCK_TIME			0x0004
#define MUX_CLKCMU_MIF_BUSP_USER			0x0100
#define PLL_CON0_PLL_MIF_ENABLE			0x0120
#define MUX_CLK_MIF_DDRPHY_CLK2X			0x1008
#define MUX_MIF_CMUREF			0x100c
#define CLK_BLK_MIF_UID_HPM_MIF_IPCLKPORT_hpm_targetclk_c	0x2000
#define CLK_BLK_MIF_UID_LHM_AXI_D_MIF_CPU_IPCLKPORT_I_CLK	0x2004
#define CLK_BLK_MIF_UID_LHM_AXI_D_MIF_CP_IPCLKPORT_I_CLK	0x2008
#define CLK_BLK_MIF_UID_LHM_AXI_D_MIF_NRT_IPCLKPORT_I_CLK	0x200c
#define CLK_BLK_MIF_UID_LHM_AXI_D_MIF_RT_IPCLKPORT_I_CLK	0x2010
#define CLK_BLK_MIF_UID_MIF_CMU_MIF_IPCLKPORT_PCLK	0x2014
#define CLK_BLK_MIF_UID_PPMU_DMC_CPU_IPCLKPORT_ACLK	0x2018
#define CLK_BLK_MIF_UID_RSTnSYNC_CLK_MIF_OSCCLK_IPCLKPORT_CLK	0x201c
#define GOUT_BLK_MIF_UID_BUSIF_HPMMIF_IPCLKPORT_PCLK	0x2020
#define GOUT_BLK_MIF_UID_DDR_PHY_IPCLKPORT_PCLK			0x2024
#define GOUT_BLK_MIF_UID_DMC_IPCLKPORT_ACLK			0x2028
#define GOUT_BLK_MIF_UID_DMC_IPCLKPORT_PCLK			0x202c
#define GOUT_BLK_MIF_UID_DMC_IPCLKPORT_PCLK_PF			0x2030
#define GOUT_BLK_MIF_UID_DMC_IPCLKPORT_PCLK_PPMPU	0x2034
#define GOUT_BLK_MIF_UID_DMC_IPCLKPORT_PCLK_SECURE	0x2038
#define GOUT_BLK_MIF_UID_LHM_AXI_P_MIF_IPCLKPORT_I_CLK	0x203c
#define GOUT_BLK_MIF_UID_PPMU_DMC_CPU_IPCLKPORT_PCLK	0x2040
#define GOUT_BLK_MIF_UID_QE_DMC_CPU_IPCLKPORT_ACLK	0x2044
#define GOUT_BLK_MIF_UID_QE_DMC_CPU_IPCLKPORT_PCLK	0x2048
#define GOUT_BLK_MIF_UID_RSTnSYNC_CLK_MIF_BUSD_IPCLKPORT_CLK	0x204c
#define GOUT_BLK_MIF_UID_RSTnSYNC_CLK_MIF_BUSP_IPCLKPORT_CLK	0x2050
#define GOUT_BLK_MIF_UID_SFRAPB_BRIDGE_DDR_PHY_IPCLKPORT_PCLK	0x2054
#define GOUT_BLK_MIF_UID_SFRAPB_BRIDGE_DMC_IPCLKPORT_PCLK	0x2058
#define GOUT_BLK_MIF_UID_SFRAPB_BRIDGE_DMC_PF_IPCLKPORT_PCLK	0x205c
#define GOUT_BLK_MIF_UID_SFRAPB_BRIDGE_DMC_PPMPU_IPCLKPORT_PCLK	0x2060
#define GOUT_BLK_MIF_UID_SFRAPB_BRIDGE_DMC_SECURE_IPCLKPORT_PCLK	0x2064
#define GOUT_BLK_MIF_UID_SYSREG_MIF_IPCLKPORT_PCLK	0x2068

static const unsigned long mif_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_MIF_PLL_LOCK_TIME,
	MUX_CLKCMU_MIF_BUSP_USER,
	PLL_CON0_PLL_MIF_ENABLE,
	MUX_CLK_MIF_DDRPHY_CLK2X,
	MUX_MIF_CMUREF,
	CLK_BLK_MIF_UID_HPM_MIF_IPCLKPORT_hpm_targetclk_c,
	CLK_BLK_MIF_UID_LHM_AXI_D_MIF_CPU_IPCLKPORT_I_CLK,
	CLK_BLK_MIF_UID_LHM_AXI_D_MIF_CP_IPCLKPORT_I_CLK,
	CLK_BLK_MIF_UID_LHM_AXI_D_MIF_NRT_IPCLKPORT_I_CLK,
	CLK_BLK_MIF_UID_LHM_AXI_D_MIF_RT_IPCLKPORT_I_CLK,
	CLK_BLK_MIF_UID_MIF_CMU_MIF_IPCLKPORT_PCLK,
	CLK_BLK_MIF_UID_PPMU_DMC_CPU_IPCLKPORT_ACLK,
	CLK_BLK_MIF_UID_RSTnSYNC_CLK_MIF_OSCCLK_IPCLKPORT_CLK,
	GOUT_BLK_MIF_UID_BUSIF_HPMMIF_IPCLKPORT_PCLK,
	GOUT_BLK_MIF_UID_DDR_PHY_IPCLKPORT_PCLK,
	GOUT_BLK_MIF_UID_DMC_IPCLKPORT_ACLK,
	GOUT_BLK_MIF_UID_DMC_IPCLKPORT_PCLK,
	GOUT_BLK_MIF_UID_DMC_IPCLKPORT_PCLK_PF,
	GOUT_BLK_MIF_UID_DMC_IPCLKPORT_PCLK_PPMPU,
	GOUT_BLK_MIF_UID_DMC_IPCLKPORT_PCLK_SECURE,
	GOUT_BLK_MIF_UID_LHM_AXI_P_MIF_IPCLKPORT_I_CLK,
	GOUT_BLK_MIF_UID_PPMU_DMC_CPU_IPCLKPORT_PCLK,
	GOUT_BLK_MIF_UID_QE_DMC_CPU_IPCLKPORT_ACLK,
	GOUT_BLK_MIF_UID_QE_DMC_CPU_IPCLKPORT_PCLK,
	GOUT_BLK_MIF_UID_RSTnSYNC_CLK_MIF_BUSD_IPCLKPORT_CLK,
	GOUT_BLK_MIF_UID_RSTnSYNC_CLK_MIF_BUSP_IPCLKPORT_CLK,
	GOUT_BLK_MIF_UID_SFRAPB_BRIDGE_DDR_PHY_IPCLKPORT_PCLK,
	GOUT_BLK_MIF_UID_SFRAPB_BRIDGE_DMC_IPCLKPORT_PCLK,
	GOUT_BLK_MIF_UID_SFRAPB_BRIDGE_DMC_PF_IPCLKPORT_PCLK,
	GOUT_BLK_MIF_UID_SFRAPB_BRIDGE_DMC_PPMPU_IPCLKPORT_PCLK,
	GOUT_BLK_MIF_UID_SFRAPB_BRIDGE_DMC_SECURE_IPCLKPORT_PCLK,
	GOUT_BLK_MIF_UID_SYSREG_MIF_IPCLKPORT_PCLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long mif_qch_regs[] __initconst = {
	0x3000,	/* CMU_MIF_CMUREF_QCH */
	0x3018,	/* BUSIF_HPMMIF_QCH */
	0x301c,	/* DMC_QCH */
	0x3020,	/* LHM_AXI_D_MIF_CPU_QCH */
	0x3024,	/* LHM_AXI_D_MIF_CP_QCH */
	0x3028,	/* LHM_AXI_D_MIF_NRT_QCH */
	0x302c,	/* LHM_AXI_D_MIF_RT_QCH */
	0x3030,	/* LHM_AXI_P_MIF_QCH */
	0x3034,	/* MIF_CMU_MIF_QCH */
	0x3038,	/* PPMU_DMC_CPU_QCH */
	0x303c,	/* QE_DMC_CPU_QCH */
	0x3040,	/* SFRAPB_BRIDGE_DDR_PHY_QCH */
	0x3044,	/* SFRAPB_BRIDGE_DMC_PF_QCH */
	0x3048,	/* SFRAPB_BRIDGE_DMC_PPMPU_QCH */
	0x304c,	/* SFRAPB_BRIDGE_DMC_QCH */
	0x3050,	/* SFRAPB_BRIDGE_DMC_SECURE_QCH */
	0x3054,	/* SYSREG_MIF_QCH */
};

static const struct samsung_pll_rate_table fout_mif_rate_table[] __initconst = {
	PLL_35XX_RATE(26 * MHZ, 4264000000U, 492, 3, 0),
	PLL_35XX_RATE(26 * MHZ, 1399666666U, 323, 3, 1),
	PLL_35XX_RATE(26 * MHZ, 1332500000U, 410, 4, 1),
};

static const struct samsung_pll_clock mif_pll_clks[] __initconst = {
	PLL(pll_1050x, CLK_FOUT_MIF, "fout_mif", "oscclk",
	    PLL_LOCKTIME_PLL_MIF_PLL_LOCK_TIME, PLL_CON0_PLL_MIF_ENABLE, fout_mif_rate_table),
};

/* List of parent clocks for Muxes in CMU_MIF */
PNAME(mout_mif_ddrphy_clk2x_p) = { "fout_mif", "gout_clkcmu_mif_switch" };
PNAME(mout_mif_cmuref_p) = { "oscclk", "mout_cmu_mif_busp_user" };
PNAME(mout_cmu_mif_busp_user_p) = { "oscclk", "dout_clkcmu_mif_busp" };

static const struct samsung_mux_clock mif_mux_clks[] __initconst = {
	MUX(CLK_MOUT_MIF_DDRPHY_CLK2X, "mout_mif_ddrphy_clk2x", mout_mif_ddrphy_clk2x_p,
	    MUX_CLK_MIF_DDRPHY_CLK2X, 0, 1),
	MUX(CLK_MOUT_MIF_CMUREF, "mout_mif_cmuref", mout_mif_cmuref_p,
	    MUX_MIF_CMUREF, 0, 1),
	MUX(CLK_MOUT_CMU_MIF_BUSP_USER, "mout_cmu_mif_busp_user", mout_cmu_mif_busp_user_p,
	    MUX_CLKCMU_MIF_BUSP_USER, 4, 1),
};

static const struct samsung_gate_clock mif_gate_clks[] __initconst = {
	GATE(CLK_GOUT_MIF_RSTnSYNC_CLK_MIF_OSCCLK_CLK, "gout_mif_rstnsync_clk_mif_oscclk_clk",
	     "oscclk",
	     CLK_BLK_MIF_UID_RSTnSYNC_CLK_MIF_OSCCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF_RSTnSYNC_CLK_MIF_BUSD_CLK, "gout_mif_rstnsync_clk_mif_busd_clk",
	     "UNRESOLVED_CLK_MIF_BUSD",
	     GOUT_BLK_MIF_UID_RSTnSYNC_CLK_MIF_BUSD_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF_QE_DMC_CPU_ACLK, "gout_mif_qe_dmc_cpu_aclk", "UNRESOLVED_CLK_MIF_BUSD",
	     GOUT_BLK_MIF_UID_QE_DMC_CPU_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF_DDR_PHY_PCLK, "gout_mif_ddr_phy_pclk", "mout_cmu_mif_busp_user",
	     GOUT_BLK_MIF_UID_DDR_PHY_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF_DMC_PCLK_PPMPU, "gout_mif_dmc_pclk_ppmpu", "mout_cmu_mif_busp_user",
	     GOUT_BLK_MIF_UID_DMC_IPCLKPORT_PCLK_PPMPU, 21, 0, 0),
	GATE(CLK_GOUT_MIF_LHM_AXI_P_MIF_I_CLK, "gout_mif_lhm_axi_p_mif_i_clk",
	     "mout_cmu_mif_busp_user",
	     GOUT_BLK_MIF_UID_LHM_AXI_P_MIF_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF_PPMU_DMC_CPU_PCLK, "gout_mif_ppmu_dmc_cpu_pclk", "mout_cmu_mif_busp_user",
	     GOUT_BLK_MIF_UID_PPMU_DMC_CPU_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF_QE_DMC_CPU_PCLK, "gout_mif_qe_dmc_cpu_pclk", "mout_cmu_mif_busp_user",
	     GOUT_BLK_MIF_UID_QE_DMC_CPU_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF_RSTnSYNC_CLK_MIF_BUSP_CLK, "gout_mif_rstnsync_clk_mif_busp_clk",
	     "mout_cmu_mif_busp_user",
	     GOUT_BLK_MIF_UID_RSTnSYNC_CLK_MIF_BUSP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF_SFRAPB_BRIDGE_DDR_PHY_PCLK, "gout_mif_sfrapb_bridge_ddr_phy_pclk",
	     "mout_cmu_mif_busp_user",
	     GOUT_BLK_MIF_UID_SFRAPB_BRIDGE_DDR_PHY_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF_SFRAPB_BRIDGE_DMC_PCLK, "gout_mif_sfrapb_bridge_dmc_pclk",
	     "mout_cmu_mif_busp_user",
	     GOUT_BLK_MIF_UID_SFRAPB_BRIDGE_DMC_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF_SFRAPB_BRIDGE_DMC_PPMPU_PCLK, "gout_mif_sfrapb_bridge_dmc_ppmpu_pclk",
	     "mout_cmu_mif_busp_user",
	     GOUT_BLK_MIF_UID_SFRAPB_BRIDGE_DMC_PPMPU_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF_SFRAPB_BRIDGE_DMC_SECURE_PCLK, "gout_mif_sfrapb_bridge_dmc_secure_pclk",
	     "mout_cmu_mif_busp_user",
	     GOUT_BLK_MIF_UID_SFRAPB_BRIDGE_DMC_SECURE_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF_SYSREG_MIF_PCLK, "gout_mif_sysreg_mif_pclk", "mout_cmu_mif_busp_user",
	     GOUT_BLK_MIF_UID_SYSREG_MIF_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF_MIF_CMU_MIF_PCLK, "gout_mif_mif_cmu_mif_pclk", "mout_cmu_mif_busp_user",
	     CLK_BLK_MIF_UID_MIF_CMU_MIF_IPCLKPORT_PCLK, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_MIF_PPMU_DMC_CPU_ACLK, "gout_mif_ppmu_dmc_cpu_aclk",
	     "UNRESOLVED_CLK_MIF_BUSD",
	     CLK_BLK_MIF_UID_PPMU_DMC_CPU_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF_SFRAPB_BRIDGE_DMC_PF_PCLK, "gout_mif_sfrapb_bridge_dmc_pf_pclk",
	     "mout_cmu_mif_busp_user",
	     GOUT_BLK_MIF_UID_SFRAPB_BRIDGE_DMC_PF_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF_DMC_PCLK_PF, "gout_mif_dmc_pclk_pf", "mout_cmu_mif_busp_user",
	     GOUT_BLK_MIF_UID_DMC_IPCLKPORT_PCLK_PF, 21, 0, 0),
	GATE(CLK_GOUT_MIF_DMC_PCLK_SECURE, "gout_mif_dmc_pclk_secure", "mout_cmu_mif_busp_user",
	     GOUT_BLK_MIF_UID_DMC_IPCLKPORT_PCLK_SECURE, 21, 0, 0),
	GATE(CLK_GOUT_MIF_DMC_PCLK, "gout_mif_dmc_pclk", "mout_cmu_mif_busp_user",
	     GOUT_BLK_MIF_UID_DMC_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF_HPM_MIF_hpm_targetclk_c, "gout_mif_hpm_mif_hpm_targetclk_c",
	     "dout_clkcmu_hpm",
	     CLK_BLK_MIF_UID_HPM_MIF_IPCLKPORT_hpm_targetclk_c, 21, 0, 0),
	GATE(CLK_GOUT_MIF_BUSIF_HPMMIF_PCLK, "gout_mif_busif_hpmmif_pclk", "mout_cmu_mif_busp_user",
	     GOUT_BLK_MIF_UID_BUSIF_HPMMIF_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF_DMC_ACLK, "gout_mif_dmc_aclk", "UNRESOLVED_CLK_MIF_BUSD",
	     GOUT_BLK_MIF_UID_DMC_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF_LHM_AXI_D_MIF_CP_I_CLK, "gout_mif_lhm_axi_d_mif_cp_i_clk",
	     "UNRESOLVED_CLK_MIF_BUSD",
	     CLK_BLK_MIF_UID_LHM_AXI_D_MIF_CP_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF_LHM_AXI_D_MIF_CPU_I_CLK, "gout_mif_lhm_axi_d_mif_cpu_i_clk",
	     "UNRESOLVED_CLK_MIF_BUSD",
	     CLK_BLK_MIF_UID_LHM_AXI_D_MIF_CPU_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF_LHM_AXI_D_MIF_NRT_I_CLK, "gout_mif_lhm_axi_d_mif_nrt_i_clk",
	     "UNRESOLVED_CLK_MIF_BUSD",
	     CLK_BLK_MIF_UID_LHM_AXI_D_MIF_NRT_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF_LHM_AXI_D_MIF_RT_I_CLK, "gout_mif_lhm_axi_d_mif_rt_i_clk",
	     "UNRESOLVED_CLK_MIF_BUSD",
	     CLK_BLK_MIF_UID_LHM_AXI_D_MIF_RT_IPCLKPORT_I_CLK, 21, 0, 0),
};

static const struct samsung_cmu_info mif_cmu_info __initconst = {
	.pll_clks		= mif_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(mif_pll_clks),
	.mux_clks		= mif_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(mif_mux_clks),
	.gate_clks		= mif_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(mif_gate_clks),
	.nr_clk_ids		= CLKS_NR_MIF,
	.clk_regs		= mif_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(mif_clk_regs),
	.qch_regs		= mif_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(mif_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9610_cmu_mif_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &mif_cmu_info);
}

CLK_OF_DECLARE(exynos9610_cmu_mif, "samsung,exynos9610-cmu-mif",
	       exynos9610_cmu_mif_init);

/* ---- CMU_MIF1 --------------------------------------------------------*/

/* Register Offset definitions for CMU_MIF1 (0x10500000) */
#define PLL_LOCKTIME_PLL_MIF1_PLL_LOCK_TIME			0x0000
#define MUX_CLKCMU_MIF1_BUSP_USER			0x0100
#define PLL_CON0_PLL_MIF1_ENABLE			0x0120
#define MUX_CLK_MIF1_DDRPHY_CLK2X			0x1004
#define MUX_MIF1_CMUREF			0x1008
#define CLK_BLK_MIF1_UID_MIF1_CMU_MIF1_IPCLKPORT_PCLK	0x2000
#define GOUT_BLK_MIF1_UID_BUSIF_HPMMIF1_IPCLKPORT_PCLK	0x2004
#define GOUT_BLK_MIF1_UID_DMC1_IPCLKPORT_ACLK			0x200c
#define GOUT_BLK_MIF1_UID_DMC1_IPCLKPORT_PCLK			0x2010
#define GOUT_BLK_MIF1_UID_DMC1_IPCLKPORT_PCLK_PF	0x2014
#define GOUT_BLK_MIF1_UID_DMC1_IPCLKPORT_PCLK_PPMPU	0x2018
#define GOUT_BLK_MIF1_UID_DMC1_IPCLKPORT_PCLK_SECURE	0x201c
#define GOUT_BLK_MIF1_UID_HPM_MIF1_IPCLKPORT_hpm_targetclk_c	0x2020
#define GOUT_BLK_MIF1_UID_LHM_AXI_D_MIF1_CPU_IPCLKPORT_I_CLK	0x2024
#define GOUT_BLK_MIF1_UID_LHM_AXI_D_MIF1_CP_IPCLKPORT_I_CLK	0x2028
#define GOUT_BLK_MIF1_UID_LHM_AXI_D_MIF1_NRT_IPCLKPORT_I_CLK	0x202c
#define GOUT_BLK_MIF1_UID_LHM_AXI_D_MIF1_RT_IPCLKPORT_I_CLK	0x2030

static const unsigned long mif1_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_MIF1_PLL_LOCK_TIME,
	MUX_CLKCMU_MIF1_BUSP_USER,
	PLL_CON0_PLL_MIF1_ENABLE,
	MUX_CLK_MIF1_DDRPHY_CLK2X,
	MUX_MIF1_CMUREF,
	CLK_BLK_MIF1_UID_MIF1_CMU_MIF1_IPCLKPORT_PCLK,
	GOUT_BLK_MIF1_UID_BUSIF_HPMMIF1_IPCLKPORT_PCLK,
	GOUT_BLK_MIF1_UID_DMC1_IPCLKPORT_ACLK,
	GOUT_BLK_MIF1_UID_DMC1_IPCLKPORT_PCLK,
	GOUT_BLK_MIF1_UID_DMC1_IPCLKPORT_PCLK_PF,
	GOUT_BLK_MIF1_UID_DMC1_IPCLKPORT_PCLK_PPMPU,
	GOUT_BLK_MIF1_UID_DMC1_IPCLKPORT_PCLK_SECURE,
	GOUT_BLK_MIF1_UID_HPM_MIF1_IPCLKPORT_hpm_targetclk_c,
	GOUT_BLK_MIF1_UID_LHM_AXI_D_MIF1_CPU_IPCLKPORT_I_CLK,
	GOUT_BLK_MIF1_UID_LHM_AXI_D_MIF1_CP_IPCLKPORT_I_CLK,
	GOUT_BLK_MIF1_UID_LHM_AXI_D_MIF1_NRT_IPCLKPORT_I_CLK,
	GOUT_BLK_MIF1_UID_LHM_AXI_D_MIF1_RT_IPCLKPORT_I_CLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long mif1_qch_regs[] __initconst = {
	0x3000,	/* CMU_MIF1_CMUREF_QCH */
	0x3014,	/* BUSIF_HPMMIF1_QCH */
	0x3018,	/* DMC1_QCH */
	0x301c,	/* LHM_AXI_D_MIF1_CPU_QCH */
	0x3020,	/* LHM_AXI_D_MIF1_CP_QCH */
	0x3024,	/* LHM_AXI_D_MIF1_NRT_QCH */
	0x3028,	/* LHM_AXI_D_MIF1_RT_QCH */
	0x302c,	/* MIF1_CMU_MIF1_QCH */
};

static const struct samsung_pll_clock mif1_pll_clks[] __initconst = {
	PLL(pll_1050x, CLK_FOUT_MIF1, "fout_mif1", "oscclk",
	    PLL_LOCKTIME_PLL_MIF1_PLL_LOCK_TIME, PLL_CON0_PLL_MIF1_ENABLE, NULL),
};

/* List of parent clocks for Muxes in CMU_MIF1 */
PNAME(mout_mif1_ddrphy_clk2x_p) = { "fout_mif1", "gout_clkcmu_mif_switch" };
PNAME(mout_mif1_cmuref_p) = { "oscclk", "mout_cmu_mif1_busp_user" };
PNAME(mout_cmu_mif1_busp_user_p) = { "oscclk", "dout_clkcmu_mif_busp" };

static const struct samsung_mux_clock mif1_mux_clks[] __initconst = {
	MUX(CLK_MOUT_MIF1_DDRPHY_CLK2X, "mout_mif1_ddrphy_clk2x", mout_mif1_ddrphy_clk2x_p,
	    MUX_CLK_MIF1_DDRPHY_CLK2X, 0, 1),
	MUX(CLK_MOUT_MIF1_CMUREF, "mout_mif1_cmuref", mout_mif1_cmuref_p,
	    MUX_MIF1_CMUREF, 0, 1),
	MUX(CLK_MOUT_CMU_MIF1_BUSP_USER, "mout_cmu_mif1_busp_user", mout_cmu_mif1_busp_user_p,
	    MUX_CLKCMU_MIF1_BUSP_USER, 4, 1),
};

static const struct samsung_gate_clock mif1_gate_clks[] __initconst = {
	GATE(CLK_GOUT_MIF1_MIF1_CMU_MIF1_PCLK, "gout_mif1_mif1_cmu_mif1_pclk",
	     "mout_cmu_mif1_busp_user",
	     CLK_BLK_MIF1_UID_MIF1_CMU_MIF1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF1_DMC1_ACLK, "gout_mif1_dmc1_aclk", "UNRESOLVED_CLK_MIF1_BUSD",
	     GOUT_BLK_MIF1_UID_DMC1_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF1_HPM_MIF1_hpm_targetclk_c, "gout_mif1_hpm_mif1_hpm_targetclk_c",
	     "dout_clkcmu_hpm",
	     GOUT_BLK_MIF1_UID_HPM_MIF1_IPCLKPORT_hpm_targetclk_c, 21, 0, 0),
	GATE(CLK_GOUT_MIF1_BUSIF_HPMMIF1_PCLK, "gout_mif1_busif_hpmmif1_pclk",
	     "mout_cmu_mif1_busp_user",
	     GOUT_BLK_MIF1_UID_BUSIF_HPMMIF1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF1_DMC1_PCLK, "gout_mif1_dmc1_pclk", "mout_cmu_mif1_busp_user",
	     GOUT_BLK_MIF1_UID_DMC1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF1_DMC1_PCLK_PF, "gout_mif1_dmc1_pclk_pf", "mout_cmu_mif1_busp_user",
	     GOUT_BLK_MIF1_UID_DMC1_IPCLKPORT_PCLK_PF, 21, 0, 0),
	GATE(CLK_GOUT_MIF1_DMC1_PCLK_PPMPU, "gout_mif1_dmc1_pclk_ppmpu", "mout_cmu_mif1_busp_user",
	     GOUT_BLK_MIF1_UID_DMC1_IPCLKPORT_PCLK_PPMPU, 21, 0, 0),
	GATE(CLK_GOUT_MIF1_DMC1_PCLK_SECURE, "gout_mif1_dmc1_pclk_secure",
	     "mout_cmu_mif1_busp_user",
	     GOUT_BLK_MIF1_UID_DMC1_IPCLKPORT_PCLK_SECURE, 21, 0, 0),
	GATE(CLK_GOUT_MIF1_LHM_AXI_D_MIF1_CP_I_CLK, "gout_mif1_lhm_axi_d_mif1_cp_i_clk",
	     "mout_cmu_mif1_busp_user",
	     GOUT_BLK_MIF1_UID_LHM_AXI_D_MIF1_CP_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF1_LHM_AXI_D_MIF1_CPU_I_CLK, "gout_mif1_lhm_axi_d_mif1_cpu_i_clk",
	     "mout_cmu_mif1_busp_user",
	     GOUT_BLK_MIF1_UID_LHM_AXI_D_MIF1_CPU_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF1_LHM_AXI_D_MIF1_NRT_I_CLK, "gout_mif1_lhm_axi_d_mif1_nrt_i_clk",
	     "mout_cmu_mif1_busp_user",
	     GOUT_BLK_MIF1_UID_LHM_AXI_D_MIF1_NRT_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MIF1_LHM_AXI_D_MIF1_RT_I_CLK, "gout_mif1_lhm_axi_d_mif1_rt_i_clk",
	     "mout_cmu_mif1_busp_user",
	     GOUT_BLK_MIF1_UID_LHM_AXI_D_MIF1_RT_IPCLKPORT_I_CLK, 21, 0, 0),
};

static const struct samsung_cmu_info mif1_cmu_info __initconst = {
	.pll_clks		= mif1_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(mif1_pll_clks),
	.mux_clks		= mif1_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(mif1_mux_clks),
	.gate_clks		= mif1_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(mif1_gate_clks),
	.nr_clk_ids		= CLKS_NR_MIF1,
	.clk_regs		= mif1_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(mif1_clk_regs),
	.qch_regs		= mif1_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(mif1_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9610_cmu_mif1_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &mif1_cmu_info);
}

CLK_OF_DECLARE(exynos9610_cmu_mif1, "samsung,exynos9610-cmu-mif1",
	       exynos9610_cmu_mif1_init);

/* ---- CMU_PERI --------------------------------------------------------*/

/* Register Offset definitions for CMU_PERI (0x10030000) */
#define MUX_CLKCMU_PERI_BUS_USER			0x0100
#define MUX_CLKCMU_PERI_IP_USER			0x0120
#define MUX_CLKCMU_PERI_UART_USER			0x0140
#define DIV_CLK_PERI_I2C			0x1800
#define DIV_CLK_PERI_SPI0			0x1804
#define DIV_CLK_PERI_SPI1			0x1808
#define DIV_CLK_PERI_SPI2			0x180c
#define DIV_CLK_PERI_USI_I2C			0x1814
#define DIV_CLK_PERI_USI_USI			0x1818
#define CLK_BLK_PERI_UID_PERI_CMU_PERI_IPCLKPORT_PCLK	0x2000
#define CLK_BLK_PERI_UID_RSTnSYNC_CLK_PERI_OSCCLK_IPCLKPORT_CLK	0x2004
#define GATE_CLK_PERI_I2C			0x2008
#define GATE_CLK_PERI_SPI0			0x200c
#define GATE_CLK_PERI_SPI1			0x2010
#define GATE_CLK_PERI_SPI2			0x2014
#define GATE_CLK_PERI_USI_I2C			0x2018
#define GATE_CLK_PERI_USI_USI			0x201c
#define GOUT_BLK_PERI_UID_AXI2AHB_MSD32_PERI_IPCLKPORT_aclk	0x2020
#define GOUT_BLK_PERI_UID_BUSIF_TMU_IPCLKPORT_PCLK	0x2024
#define GOUT_BLK_PERI_UID_CAMI2C_0_IPCLKPORT_IPCLK	0x2028
#define GOUT_BLK_PERI_UID_CAMI2C_0_IPCLKPORT_PCLK	0x202c
#define GOUT_BLK_PERI_UID_CAMI2C_1_IPCLKPORT_IPCLK	0x2030
#define GOUT_BLK_PERI_UID_CAMI2C_1_IPCLKPORT_PCLK	0x2034
#define GOUT_BLK_PERI_UID_CAMI2C_2_IPCLKPORT_IPCLK	0x2038
#define GOUT_BLK_PERI_UID_CAMI2C_2_IPCLKPORT_PCLK	0x203c
#define GOUT_BLK_PERI_UID_CAMI2C_3_IPCLKPORT_IPCLK	0x2040
#define GOUT_BLK_PERI_UID_CAMI2C_3_IPCLKPORT_PCLK	0x2044
#define GOUT_BLK_PERI_UID_GPIO_PERI_IPCLKPORT_PCLK	0x2048
#define GOUT_BLK_PERI_UID_I2C_0_IPCLKPORT_PCLK			0x204c
#define GOUT_BLK_PERI_UID_I2C_1_IPCLKPORT_PCLK			0x2050
#define GOUT_BLK_PERI_UID_I2C_2_IPCLKPORT_PCLK			0x2054
#define GOUT_BLK_PERI_UID_I2C_3_IPCLKPORT_PCLK			0x2058
#define GOUT_BLK_PERI_UID_I2C_4_IPCLKPORT_PCLK			0x205c
#define GOUT_BLK_PERI_UID_I2C_5_IPCLKPORT_PCLK			0x2060
#define GOUT_BLK_PERI_UID_I2C_6_IPCLKPORT_PCLK			0x2064
#define GOUT_BLK_PERI_UID_LHM_AXI_P_PERI_IPCLKPORT_I_CLK	0x2068
#define GOUT_BLK_PERI_UID_MCT_IPCLKPORT_PCLK			0x206c
#define GOUT_BLK_PERI_UID_OTP_CON_TOP_IPCLKPORT_PCLK	0x2070
#define GOUT_BLK_PERI_UID_PWM_MOTOR_IPCLKPORT_i_PCLK_S0	0x2074
#define GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_BUS_IPCLKPORT_CLK	0x2078
#define GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_I2C_IPCLKPORT_CLK	0x207c
#define GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_SPI_0_IPCLKPORT_CLK	0x2080
#define GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_SPI_1_IPCLKPORT_CLK	0x2084
#define GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_SPI_2_IPCLKPORT_CLK	0x2088
#define GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_UART_IPCLKPORT_CLK	0x208c
#define GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_USI00_I2C_IPCLKPORT_CLK	0x2090
#define GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_USI00_USI_IPCLKPORT_CLK	0x2094
#define GOUT_BLK_PERI_UID_SPI_0_IPCLKPORT_IPCLK			0x2098
#define GOUT_BLK_PERI_UID_SPI_0_IPCLKPORT_PCLK			0x209c
#define GOUT_BLK_PERI_UID_SPI_1_IPCLKPORT_IPCLK			0x20a0
#define GOUT_BLK_PERI_UID_SPI_1_IPCLKPORT_PCLK			0x20a4
#define GOUT_BLK_PERI_UID_SPI_2_IPCLKPORT_IPCLK			0x20a8
#define GOUT_BLK_PERI_UID_SPI_2_IPCLKPORT_PCLK			0x20ac
#define GOUT_BLK_PERI_UID_SYSREG_PERI_IPCLKPORT_PCLK	0x20b0
#define GOUT_BLK_PERI_UID_UART_IPCLKPORT_IPCLK			0x20b4
#define GOUT_BLK_PERI_UID_UART_IPCLKPORT_PCLK			0x20b8
#define GOUT_BLK_PERI_UID_USI00_I2C_IPCLKPORT_IPCLK	0x20bc
#define GOUT_BLK_PERI_UID_USI00_I2C_IPCLKPORT_PCLK	0x20c0
#define GOUT_BLK_PERI_UID_USI00_USI_IPCLKPORT_IPCLK	0x20c4
#define GOUT_BLK_PERI_UID_USI00_USI_IPCLKPORT_PCLK	0x20c8
#define GOUT_BLK_PERI_UID_WDT_CLUSTER0_IPCLKPORT_PCLK	0x20cc
#define GOUT_BLK_PERI_UID_WDT_CLUSTER1_IPCLKPORT_PCLK	0x20d0

static const unsigned long peri_clk_regs[] __initconst = {
	MUX_CLKCMU_PERI_BUS_USER,
	MUX_CLKCMU_PERI_IP_USER,
	MUX_CLKCMU_PERI_UART_USER,
	DIV_CLK_PERI_I2C,
	DIV_CLK_PERI_SPI0,
	DIV_CLK_PERI_SPI1,
	DIV_CLK_PERI_SPI2,
	DIV_CLK_PERI_USI_I2C,
	DIV_CLK_PERI_USI_USI,
	CLK_BLK_PERI_UID_PERI_CMU_PERI_IPCLKPORT_PCLK,
	CLK_BLK_PERI_UID_RSTnSYNC_CLK_PERI_OSCCLK_IPCLKPORT_CLK,
	GATE_CLK_PERI_I2C,
	GATE_CLK_PERI_SPI0,
	GATE_CLK_PERI_SPI1,
	GATE_CLK_PERI_SPI2,
	GATE_CLK_PERI_USI_I2C,
	GATE_CLK_PERI_USI_USI,
	GOUT_BLK_PERI_UID_AXI2AHB_MSD32_PERI_IPCLKPORT_aclk,
	GOUT_BLK_PERI_UID_BUSIF_TMU_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_CAMI2C_0_IPCLKPORT_IPCLK,
	GOUT_BLK_PERI_UID_CAMI2C_0_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_CAMI2C_1_IPCLKPORT_IPCLK,
	GOUT_BLK_PERI_UID_CAMI2C_1_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_CAMI2C_2_IPCLKPORT_IPCLK,
	GOUT_BLK_PERI_UID_CAMI2C_2_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_CAMI2C_3_IPCLKPORT_IPCLK,
	GOUT_BLK_PERI_UID_CAMI2C_3_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_GPIO_PERI_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_I2C_0_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_I2C_1_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_I2C_2_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_I2C_3_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_I2C_4_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_I2C_5_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_I2C_6_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_LHM_AXI_P_PERI_IPCLKPORT_I_CLK,
	GOUT_BLK_PERI_UID_MCT_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_OTP_CON_TOP_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_PWM_MOTOR_IPCLKPORT_i_PCLK_S0,
	GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_BUS_IPCLKPORT_CLK,
	GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_I2C_IPCLKPORT_CLK,
	GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_SPI_0_IPCLKPORT_CLK,
	GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_SPI_1_IPCLKPORT_CLK,
	GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_SPI_2_IPCLKPORT_CLK,
	GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_UART_IPCLKPORT_CLK,
	GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_USI00_I2C_IPCLKPORT_CLK,
	GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_USI00_USI_IPCLKPORT_CLK,
	GOUT_BLK_PERI_UID_SPI_0_IPCLKPORT_IPCLK,
	GOUT_BLK_PERI_UID_SPI_0_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_SPI_1_IPCLKPORT_IPCLK,
	GOUT_BLK_PERI_UID_SPI_1_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_SPI_2_IPCLKPORT_IPCLK,
	GOUT_BLK_PERI_UID_SPI_2_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_SYSREG_PERI_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_UART_IPCLKPORT_IPCLK,
	GOUT_BLK_PERI_UID_UART_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_USI00_I2C_IPCLKPORT_IPCLK,
	GOUT_BLK_PERI_UID_USI00_I2C_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_USI00_USI_IPCLKPORT_IPCLK,
	GOUT_BLK_PERI_UID_USI00_USI_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_WDT_CLUSTER0_IPCLKPORT_PCLK,
	GOUT_BLK_PERI_UID_WDT_CLUSTER1_IPCLKPORT_PCLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long peri_qch_regs[] __initconst = {
	0x3004,	/* BUSIF_TMU_QCH */
	0x3008,	/* CAMI2C_0_QCH */
	0x300c,	/* CAMI2C_1_QCH */
	0x3010,	/* CAMI2C_2_QCH */
	0x3014,	/* CAMI2C_3_QCH */
	0x3018,	/* GPIO_PERI_QCH */
	0x301c,	/* I2C_0_QCH */
	0x3020,	/* I2C_1_QCH */
	0x3024,	/* I2C_2_QCH */
	0x3028,	/* I2C_3_QCH */
	0x302c,	/* I2C_4_QCH */
	0x3030,	/* I2C_5_QCH */
	0x3034,	/* I2C_6_QCH */
	0x3038,	/* LHM_AXI_P_PERI_QCH */
	0x303c,	/* MCT_QCH */
	0x3040,	/* OTP_CON_TOP_QCH */
	0x3044,	/* PERI_CMU_PERI_QCH */
	0x3048,	/* PWM_MOTOR_QCH */
	0x304c,	/* SPI_0_QCH */
	0x3050,	/* SPI_1_QCH */
	0x3054,	/* SPI_2_QCH */
	0x3058,	/* SYSREG_PERI_QCH */
	0x305c,	/* UART_QCH */
	0x3060,	/* USI00_I2C_QCH */
	0x3064,	/* USI00_USI_QCH */
	0x3068,	/* WDT_CLUSTER0_QCH */
	0x306c,	/* WDT_CLUSTER1_QCH */
};

/* List of parent clocks for Muxes in CMU_PERI */
PNAME(mout_cmu_peri_bus_user_p) = { "oscclk", "dout_clkcmu_peri_bus" };
PNAME(mout_cmu_peri_ip_user_p) = { "oscclk", "dout_clkcmu_peri_ip" };
PNAME(mout_cmu_peri_uart_user_p) = { "oscclk", "dout_clkcmu_peri_uart" };

static const struct samsung_mux_clock peri_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_PERI_BUS_USER, "mout_cmu_peri_bus_user", mout_cmu_peri_bus_user_p,
	    MUX_CLKCMU_PERI_BUS_USER, 4, 1),
	MUX(CLK_MOUT_CMU_PERI_IP_USER, "mout_cmu_peri_ip_user", mout_cmu_peri_ip_user_p,
	    MUX_CLKCMU_PERI_IP_USER, 4, 1),
	MUX(CLK_MOUT_CMU_PERI_UART_USER, "mout_cmu_peri_uart_user", mout_cmu_peri_uart_user_p,
	    MUX_CLKCMU_PERI_UART_USER, 4, 1),
};

static const struct samsung_div_clock peri_div_clks[] __initconst = {
	DIV(CLK_DOUT_PERI_I2C, "dout_peri_i2c", "gout_peri_i2c",
	    DIV_CLK_PERI_I2C, 0, 4),
	DIV(CLK_DOUT_PERI_SPI0, "dout_peri_spi0", "gout_peri_spi0",
	    DIV_CLK_PERI_SPI0, 0, 8),
	DIV(CLK_DOUT_PERI_SPI1, "dout_peri_spi1", "gout_peri_spi1",
	    DIV_CLK_PERI_SPI1, 0, 8),
	DIV(CLK_DOUT_PERI_USI_I2C, "dout_peri_usi_i2c", "gout_peri_usi_i2c",
	    DIV_CLK_PERI_USI_I2C, 0, 4),
	DIV(CLK_DOUT_PERI_USI_USI, "dout_peri_usi_usi", "gout_peri_usi_usi",
	    DIV_CLK_PERI_USI_USI, 0, 8),
	DIV(CLK_DOUT_PERI_SPI2, "dout_peri_spi2", "gout_peri_spi2",
	    DIV_CLK_PERI_SPI2, 0, 8),
};

static const struct samsung_gate_clock peri_gate_clks[] __initconst = {
	GATE(CLK_GOUT_PERI_RSTnSYNC_CLK_PERI_OSCCLK_CLK, "gout_peri_rstnsync_clk_peri_oscclk_clk",
	     "oscclk",
	     CLK_BLK_PERI_UID_RSTnSYNC_CLK_PERI_OSCCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_AXI2AHB_MSD32_PERI_aclk, "gout_peri_axi2ahb_msd32_peri_aclk",
	     "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_AXI2AHB_MSD32_PERI_IPCLKPORT_aclk, 21, 0, 0),
	GATE(CLK_GOUT_PERI_BUSIF_TMU_PCLK, "gout_peri_busif_tmu_pclk", "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_BUSIF_TMU_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_LHM_AXI_P_PERI_I_CLK, "gout_peri_lhm_axi_p_peri_i_clk",
	     "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_LHM_AXI_P_PERI_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_OTP_CON_TOP_PCLK, "gout_peri_otp_con_top_pclk", "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_OTP_CON_TOP_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_RSTnSYNC_CLK_PERI_BUS_CLK, "gout_peri_rstnsync_clk_peri_bus_clk",
	     "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_BUS_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_SYSREG_PERI_PCLK, "gout_peri_sysreg_peri_pclk", "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_SYSREG_PERI_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_WDT_CLUSTER0_PCLK, "gout_peri_wdt_cluster0_pclk",
	     "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_WDT_CLUSTER0_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_WDT_CLUSTER1_PCLK, "gout_peri_wdt_cluster1_pclk",
	     "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_WDT_CLUSTER1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_MCT_PCLK, "gout_peri_mct_pclk", "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_MCT_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_PWM_MOTOR_i_PCLK_S0, "gout_peri_pwm_motor_i_pclk_s0",
	     "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_PWM_MOTOR_IPCLKPORT_i_PCLK_S0, 21, 0, 0),
	GATE(CLK_GOUT_PERI_GPIO_PERI_PCLK, "gout_peri_gpio_peri_pclk", "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_GPIO_PERI_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_SPI_0_PCLK, "gout_peri_spi_0_pclk", "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_SPI_0_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_SPI_1_PCLK, "gout_peri_spi_1_pclk", "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_SPI_1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_UART_PCLK, "gout_peri_uart_pclk", "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_UART_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_CAMI2C_0_PCLK, "gout_peri_cami2c_0_pclk", "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_CAMI2C_0_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_CAMI2C_1_PCLK, "gout_peri_cami2c_1_pclk", "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_CAMI2C_1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_CAMI2C_2_PCLK, "gout_peri_cami2c_2_pclk", "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_CAMI2C_2_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_CAMI2C_3_PCLK, "gout_peri_cami2c_3_pclk", "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_CAMI2C_3_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_USI00_I2C_PCLK, "gout_peri_usi00_i2c_pclk", "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_USI00_I2C_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_USI00_USI_PCLK, "gout_peri_usi00_usi_pclk", "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_USI00_USI_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_SPI_2_PCLK, "gout_peri_spi_2_pclk", "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_SPI_2_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_RSTnSYNC_CLK_PERI_UART_CLK, "gout_peri_rstnsync_clk_peri_uart_clk",
	     "mout_cmu_peri_uart_user",
	     GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_UART_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C, "gout_peri_i2c", "mout_cmu_peri_ip_user",
	     GATE_CLK_PERI_I2C, 21, 0, 0),
	GATE(CLK_GOUT_PERI_SPI0, "gout_peri_spi0", "mout_cmu_peri_ip_user",
	     GATE_CLK_PERI_SPI0, 21, 0, 0),
	GATE(CLK_GOUT_PERI_SPI1, "gout_peri_spi1", "mout_cmu_peri_ip_user",
	     GATE_CLK_PERI_SPI1, 21, 0, 0),
	GATE(CLK_GOUT_PERI_USI_USI, "gout_peri_usi_usi", "mout_cmu_peri_ip_user",
	     GATE_CLK_PERI_USI_USI, 21, 0, 0),
	GATE(CLK_GOUT_PERI_USI_I2C, "gout_peri_usi_i2c", "mout_cmu_peri_ip_user",
	     GATE_CLK_PERI_USI_I2C, 21, 0, 0),
	GATE(CLK_GOUT_PERI_SPI2, "gout_peri_spi2", "mout_cmu_peri_ip_user",
	     GATE_CLK_PERI_SPI2, 21, 0, 0),
	GATE(CLK_GOUT_PERI_RSTnSYNC_CLK_PERI_I2C_CLK, "gout_peri_rstnsync_clk_peri_i2c_clk",
	     "dout_peri_i2c",
	     GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_I2C_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_RSTnSYNC_CLK_PERI_SPI_0_CLK, "gout_peri_rstnsync_clk_peri_spi_0_clk",
	     "dout_peri_spi0",
	     GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_SPI_0_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_RSTnSYNC_CLK_PERI_SPI_1_CLK, "gout_peri_rstnsync_clk_peri_spi_1_clk",
	     "dout_peri_spi1",
	     GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_SPI_1_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_RSTnSYNC_CLK_PERI_USI00_I2C_CLK, "gout_peri_rstnsync_clk_peri_usi00_i2c_clk",
	     "dout_peri_usi_i2c",
	     GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_USI00_I2C_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_RSTnSYNC_CLK_PERI_USI00_USI_CLK, "gout_peri_rstnsync_clk_peri_usi00_usi_clk",
	     "dout_peri_usi_usi",
	     GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_USI00_USI_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_RSTnSYNC_CLK_PERI_SPI_2_CLK, "gout_peri_rstnsync_clk_peri_spi_2_clk",
	     "dout_peri_spi2",
	     GOUT_BLK_PERI_UID_RSTnSYNC_CLK_PERI_SPI_2_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_PERI_CMU_PERI_PCLK, "gout_peri_peri_cmu_peri_pclk",
	     "mout_cmu_peri_bus_user",
	     CLK_BLK_PERI_UID_PERI_CMU_PERI_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_UART_IPCLK, "gout_peri_uart_ipclk", "mout_cmu_peri_uart_user",
	     GOUT_BLK_PERI_UID_UART_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_CAMI2C_0_IPCLK, "gout_peri_cami2c_0_ipclk", "dout_peri_i2c",
	     GOUT_BLK_PERI_UID_CAMI2C_0_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_CAMI2C_1_IPCLK, "gout_peri_cami2c_1_ipclk", "dout_peri_i2c",
	     GOUT_BLK_PERI_UID_CAMI2C_1_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_CAMI2C_2_IPCLK, "gout_peri_cami2c_2_ipclk", "dout_peri_i2c",
	     GOUT_BLK_PERI_UID_CAMI2C_2_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_CAMI2C_3_IPCLK, "gout_peri_cami2c_3_ipclk", "dout_peri_i2c",
	     GOUT_BLK_PERI_UID_CAMI2C_3_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C_0_PCLK, "gout_peri_i2c_0_pclk", "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_I2C_0_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C_1_PCLK, "gout_peri_i2c_1_pclk", "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_I2C_1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C_2_PCLK, "gout_peri_i2c_2_pclk", "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_I2C_2_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C_3_PCLK, "gout_peri_i2c_3_pclk", "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_I2C_3_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C_4_PCLK, "gout_peri_i2c_4_pclk", "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_I2C_4_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C_5_PCLK, "gout_peri_i2c_5_pclk", "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_I2C_5_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C_6_PCLK, "gout_peri_i2c_6_pclk", "mout_cmu_peri_bus_user",
	     GOUT_BLK_PERI_UID_I2C_6_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_SPI_0_IPCLK, "gout_peri_spi_0_ipclk", "dout_peri_spi0",
	     GOUT_BLK_PERI_UID_SPI_0_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_SPI_1_IPCLK, "gout_peri_spi_1_ipclk", "dout_peri_spi1",
	     GOUT_BLK_PERI_UID_SPI_1_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_SPI_2_IPCLK, "gout_peri_spi_2_ipclk", "dout_peri_spi2",
	     GOUT_BLK_PERI_UID_SPI_2_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_USI00_I2C_IPCLK, "gout_peri_usi00_i2c_ipclk", "dout_peri_usi_i2c",
	     GOUT_BLK_PERI_UID_USI00_I2C_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERI_USI00_USI_IPCLK, "gout_peri_usi00_usi_ipclk", "dout_peri_usi_usi",
	     GOUT_BLK_PERI_UID_USI00_USI_IPCLKPORT_IPCLK, 21, 0, 0),
};

static const struct samsung_cmu_info peri_cmu_info __initconst = {
	.mux_clks		= peri_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(peri_mux_clks),
	.div_clks		= peri_div_clks,
	.nr_div_clks		= ARRAY_SIZE(peri_div_clks),
	.gate_clks		= peri_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(peri_gate_clks),
	.nr_clk_ids		= CLKS_NR_PERI,
	.clk_regs		= peri_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(peri_clk_regs),
	.qch_regs		= peri_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(peri_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9610_cmu_peri_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &peri_cmu_info);
}

CLK_OF_DECLARE(exynos9610_cmu_peri, "samsung,exynos9610-cmu-peri",
	       exynos9610_cmu_peri_init);

/* ---- CMU_SHUB --------------------------------------------------------*/

/* Register Offset definitions for CMU_SHUB (0x11000000) */
#define MUX_CLKCMU_SHUB_BUS_USER			0x0100
#define MUX_CLK_SHUB_I2C			0x1000
#define MUX_CLK_SHUB_USI00			0x1004
#define MUX_CLK_SHUB_USI01			0x1008
#define DIV_CLK_SHUB_I2C			0x1800
#define DIV_CLK_SHUB_USI00			0x1804
#define DIV_CLK_SHUB_USI01			0x1808
#define CLK_BLK_SHUB_UID_RSTnSYNC_CLK_SHUB_OSCCLK_IPCLKPORT_CLK	0x2000
#define CLK_BLK_SHUB_UID_RSTnSYNC_CLK_SHUB_RTCCLK_IPCLKPORT_CLK	0x2004
#define CLK_BLK_SHUB_UID_SHUB_CMU_SHUB_IPCLKPORT_PCLK	0x2008
#define GOUT_BLK_SHUB_UID_BAAW_D_SHUB_IPCLKPORT_I_PCLK	0x200c
#define GOUT_BLK_SHUB_UID_BAAW_P_APM_SHUB_IPCLKPORT_I_PCLK	0x2010
#define GOUT_BLK_SHUB_UID_CM4_SHUB_IPCLKPORT_FCLK	0x2014
#define GOUT_BLK_SHUB_UID_GPIO_SHUB_IPCLKPORT_PCLK	0x2018
#define GOUT_BLK_SHUB_UID_I2C_SHUB00_IPCLKPORT_IPCLK	0x201c
#define GOUT_BLK_SHUB_UID_I2C_SHUB00_IPCLKPORT_PCLK	0x2020
#define GOUT_BLK_SHUB_UID_LHM_AXI_LP_SHUB_IPCLKPORT_I_CLK	0x2024
#define GOUT_BLK_SHUB_UID_LHM_AXI_P_SHUB_IPCLKPORT_I_CLK	0x2028
#define GOUT_BLK_SHUB_UID_LHS_AXI_D_SHUB_IPCLKPORT_I_CLK	0x202c
#define GOUT_BLK_SHUB_UID_LHS_AXI_P_APM_SHUB_IPCLKPORT_I_CLK	0x2030
#define GOUT_BLK_SHUB_UID_PDMA_SHUB_IPCLKPORT_ACLK	0x2034
#define GOUT_BLK_SHUB_UID_PWM_SHUB_IPCLKPORT_i_PCLK_S0	0x2038
#define GOUT_BLK_SHUB_UID_RSTnSYNC_CLK_SHUB_BUS_IPCLKPORT_CLK	0x203c
#define GOUT_BLK_SHUB_UID_RSTnSYNC_CLK_SHUB_I2C_IPCLKPORT_CLK	0x2040
#define GOUT_BLK_SHUB_UID_RSTnSYNC_CLK_SHUB_USI00_IPCLKPORT_CLK	0x2044
#define GOUT_BLK_SHUB_UID_SWEEPER_D_SHUB_IPCLKPORT_ACLK	0x204c
#define GOUT_BLK_SHUB_UID_SWEEPER_P_APM_SHUB_IPCLKPORT_ACLK	0x2050
#define GOUT_BLK_SHUB_UID_SYSREG_SHUB_IPCLKPORT_PCLK	0x2054
#define GOUT_BLK_SHUB_UID_TIMER_SHUB_IPCLKPORT_PCLK	0x2058
#define GOUT_BLK_SHUB_UID_USI_SHUB00_IPCLKPORT_IPCLK	0x205c
#define GOUT_BLK_SHUB_UID_USI_SHUB00_IPCLKPORT_PCLK	0x2060
#define GOUT_BLK_SHUB_UID_WDT_SHUB_IPCLKPORT_PCLK	0x2064
#define GOUT_BLK_SHUB_UID_XIU_DP_SHUB_IPCLKPORT_ACLK	0x2068

static const unsigned long shub_clk_regs[] __initconst = {
	MUX_CLKCMU_SHUB_BUS_USER,
	MUX_CLK_SHUB_I2C,
	MUX_CLK_SHUB_USI00,
	MUX_CLK_SHUB_USI01,
	DIV_CLK_SHUB_I2C,
	DIV_CLK_SHUB_USI00,
	DIV_CLK_SHUB_USI01,
	CLK_BLK_SHUB_UID_RSTnSYNC_CLK_SHUB_OSCCLK_IPCLKPORT_CLK,
	CLK_BLK_SHUB_UID_RSTnSYNC_CLK_SHUB_RTCCLK_IPCLKPORT_CLK,
	CLK_BLK_SHUB_UID_SHUB_CMU_SHUB_IPCLKPORT_PCLK,
	GOUT_BLK_SHUB_UID_BAAW_D_SHUB_IPCLKPORT_I_PCLK,
	GOUT_BLK_SHUB_UID_BAAW_P_APM_SHUB_IPCLKPORT_I_PCLK,
	GOUT_BLK_SHUB_UID_CM4_SHUB_IPCLKPORT_FCLK,
	GOUT_BLK_SHUB_UID_GPIO_SHUB_IPCLKPORT_PCLK,
	GOUT_BLK_SHUB_UID_I2C_SHUB00_IPCLKPORT_IPCLK,
	GOUT_BLK_SHUB_UID_I2C_SHUB00_IPCLKPORT_PCLK,
	GOUT_BLK_SHUB_UID_LHM_AXI_LP_SHUB_IPCLKPORT_I_CLK,
	GOUT_BLK_SHUB_UID_LHM_AXI_P_SHUB_IPCLKPORT_I_CLK,
	GOUT_BLK_SHUB_UID_LHS_AXI_D_SHUB_IPCLKPORT_I_CLK,
	GOUT_BLK_SHUB_UID_LHS_AXI_P_APM_SHUB_IPCLKPORT_I_CLK,
	GOUT_BLK_SHUB_UID_PDMA_SHUB_IPCLKPORT_ACLK,
	GOUT_BLK_SHUB_UID_PWM_SHUB_IPCLKPORT_i_PCLK_S0,
	GOUT_BLK_SHUB_UID_RSTnSYNC_CLK_SHUB_BUS_IPCLKPORT_CLK,
	GOUT_BLK_SHUB_UID_RSTnSYNC_CLK_SHUB_I2C_IPCLKPORT_CLK,
	GOUT_BLK_SHUB_UID_RSTnSYNC_CLK_SHUB_USI00_IPCLKPORT_CLK,
	GOUT_BLK_SHUB_UID_SWEEPER_D_SHUB_IPCLKPORT_ACLK,
	GOUT_BLK_SHUB_UID_SWEEPER_P_APM_SHUB_IPCLKPORT_ACLK,
	GOUT_BLK_SHUB_UID_SYSREG_SHUB_IPCLKPORT_PCLK,
	GOUT_BLK_SHUB_UID_TIMER_SHUB_IPCLKPORT_PCLK,
	GOUT_BLK_SHUB_UID_USI_SHUB00_IPCLKPORT_IPCLK,
	GOUT_BLK_SHUB_UID_USI_SHUB00_IPCLKPORT_PCLK,
	GOUT_BLK_SHUB_UID_WDT_SHUB_IPCLKPORT_PCLK,
	GOUT_BLK_SHUB_UID_XIU_DP_SHUB_IPCLKPORT_ACLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long shub_qch_regs[] __initconst = {
	0x3010,	/* BAAW_D_SHUB_QCH */
	0x3014,	/* BAAW_P_APM_SHUB_QCH */
	0x3018,	/* CM4_SHUB_QCH */
	0x301c,	/* GPIO_SHUB_QCH */
	0x3020,	/* I2C_SHUB00_QCH */
	0x3024,	/* LHM_AXI_LP_SHUB_QCH */
	0x3028,	/* LHM_AXI_P_SHUB_QCH */
	0x302c,	/* LHS_AXI_D_SHUB_QCH */
	0x3030,	/* LHS_AXI_P_APM_SHUB_QCH */
	0x3034,	/* PDMA_SHUB_QCH */
	0x3038,	/* PWM_SHUB_QCH */
	0x303c,	/* SHUB_CMU_SHUB_QCH */
	0x3040,	/* SWEEPER_D_SHUB_QCH */
	0x3044,	/* SWEEPER_P_APM_SHUB_QCH */
	0x3048,	/* SYSREG_SHUB_QCH */
	0x304c,	/* TIMER_SHUB_QCH */
	0x3050,	/* USI_SHUB00_QCH */
	0x3054,	/* WDT_SHUB_QCH */
};

static const struct samsung_fixed_rate_clock shub_fixed_clks[] __initconst = {
	FRATE(CLK_RTCCLK_SHUB__ALV, "rtcclk_shub__alv", NULL, 0, 26000000),
};

/* List of parent clocks for Muxes in CMU_SHUB */
PNAME(mout_shub_usi00_p) = { "oscclk", "mout_cmu_shub_bus_user" };
PNAME(mout_shub_usi01_p) = { "oscclk", "mout_cmu_shub_bus_user" };
PNAME(mout_shub_i2c_p) = { "oscclk", "mout_cmu_shub_bus_user" };
PNAME(mout_cmu_shub_bus_user_p) = { "oscclk", "dout_clkcmu_shub_bus" };

static const struct samsung_mux_clock shub_mux_clks[] __initconst = {
	MUX(CLK_MOUT_SHUB_USI00, "mout_shub_usi00", mout_shub_usi00_p,
	    MUX_CLK_SHUB_USI00, 0, 1),
	MUX(CLK_MOUT_SHUB_USI01, "mout_shub_usi01", mout_shub_usi01_p,
	    MUX_CLK_SHUB_USI01, 0, 1),
	MUX(CLK_MOUT_SHUB_I2C, "mout_shub_i2c", mout_shub_i2c_p,
	    MUX_CLK_SHUB_I2C, 0, 1),
	MUX(CLK_MOUT_CMU_SHUB_BUS_USER, "mout_cmu_shub_bus_user", mout_cmu_shub_bus_user_p,
	    MUX_CLKCMU_SHUB_BUS_USER, 4, 1),
};

static const struct samsung_div_clock shub_div_clks[] __initconst = {
	DIV(CLK_DOUT_SHUB_USI01, "dout_shub_usi01", "mout_shub_usi01",
	    DIV_CLK_SHUB_USI01, 0, 4),
	DIV(CLK_DOUT_SHUB_I2C, "dout_shub_i2c", "mout_shub_i2c",
	    DIV_CLK_SHUB_I2C, 0, 4),
	DIV(CLK_DOUT_SHUB_USI00, "dout_shub_usi00", "mout_shub_usi00",
	    DIV_CLK_SHUB_USI00, 0, 4),
};

static const struct samsung_gate_clock shub_gate_clks[] __initconst = {
	GATE(CLK_GOUT_SHUB_SHUB_CMU_SHUB_PCLK, "gout_shub_shub_cmu_shub_pclk",
	     "mout_cmu_shub_bus_user",
	     CLK_BLK_SHUB_UID_SHUB_CMU_SHUB_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_BAAW_D_SHUB_I_PCLK, "gout_shub_baaw_d_shub_i_pclk",
	     "mout_cmu_shub_bus_user",
	     GOUT_BLK_SHUB_UID_BAAW_D_SHUB_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_BAAW_P_APM_SHUB_I_PCLK, "gout_shub_baaw_p_apm_shub_i_pclk",
	     "mout_cmu_shub_bus_user",
	     GOUT_BLK_SHUB_UID_BAAW_P_APM_SHUB_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_GPIO_SHUB_PCLK, "gout_shub_gpio_shub_pclk", "mout_cmu_shub_bus_user",
	     GOUT_BLK_SHUB_UID_GPIO_SHUB_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_I2C_SHUB00_PCLK, "gout_shub_i2c_shub00_pclk", "mout_cmu_shub_bus_user",
	     GOUT_BLK_SHUB_UID_I2C_SHUB00_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_LHM_AXI_LP_SHUB_I_CLK, "gout_shub_lhm_axi_lp_shub_i_clk",
	     "mout_cmu_shub_bus_user",
	     GOUT_BLK_SHUB_UID_LHM_AXI_LP_SHUB_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_LHM_AXI_P_SHUB_I_CLK, "gout_shub_lhm_axi_p_shub_i_clk",
	     "mout_cmu_shub_bus_user",
	     GOUT_BLK_SHUB_UID_LHM_AXI_P_SHUB_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_LHS_AXI_D_SHUB_I_CLK, "gout_shub_lhs_axi_d_shub_i_clk",
	     "mout_cmu_shub_bus_user",
	     GOUT_BLK_SHUB_UID_LHS_AXI_D_SHUB_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_LHS_AXI_P_APM_SHUB_I_CLK, "gout_shub_lhs_axi_p_apm_shub_i_clk",
	     "mout_cmu_shub_bus_user",
	     GOUT_BLK_SHUB_UID_LHS_AXI_P_APM_SHUB_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_PWM_SHUB_i_PCLK_S0, "gout_shub_pwm_shub_i_pclk_s0",
	     "mout_cmu_shub_bus_user",
	     GOUT_BLK_SHUB_UID_PWM_SHUB_IPCLKPORT_i_PCLK_S0, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_SWEEPER_D_SHUB_ACLK, "gout_shub_sweeper_d_shub_aclk",
	     "mout_cmu_shub_bus_user",
	     GOUT_BLK_SHUB_UID_SWEEPER_D_SHUB_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_SWEEPER_P_APM_SHUB_ACLK, "gout_shub_sweeper_p_apm_shub_aclk",
	     "mout_cmu_shub_bus_user",
	     GOUT_BLK_SHUB_UID_SWEEPER_P_APM_SHUB_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_SYSREG_SHUB_PCLK, "gout_shub_sysreg_shub_pclk", "mout_cmu_shub_bus_user",
	     GOUT_BLK_SHUB_UID_SYSREG_SHUB_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_TIMER_SHUB_PCLK, "gout_shub_timer_shub_pclk", "mout_cmu_shub_bus_user",
	     GOUT_BLK_SHUB_UID_TIMER_SHUB_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_USI_SHUB00_PCLK, "gout_shub_usi_shub00_pclk", "mout_cmu_shub_bus_user",
	     GOUT_BLK_SHUB_UID_USI_SHUB00_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_WDT_SHUB_PCLK, "gout_shub_wdt_shub_pclk", "mout_cmu_shub_bus_user",
	     GOUT_BLK_SHUB_UID_WDT_SHUB_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_RSTnSYNC_CLK_SHUB_BUS_CLK, "gout_shub_rstnsync_clk_shub_bus_clk",
	     "mout_cmu_shub_bus_user",
	     GOUT_BLK_SHUB_UID_RSTnSYNC_CLK_SHUB_BUS_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_RSTnSYNC_CLK_SHUB_I2C_CLK, "gout_shub_rstnsync_clk_shub_i2c_clk",
	     "dout_shub_i2c",
	     GOUT_BLK_SHUB_UID_RSTnSYNC_CLK_SHUB_I2C_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_RSTnSYNC_CLK_SHUB_OSCCLK_CLK, "gout_shub_rstnsync_clk_shub_oscclk_clk",
	     "oscclk",
	     CLK_BLK_SHUB_UID_RSTnSYNC_CLK_SHUB_OSCCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_RSTnSYNC_CLK_SHUB_RTCCLK_CLK, "gout_shub_rstnsync_clk_shub_rtcclk_clk",
	     "rtcclk_shub__alv",
	     CLK_BLK_SHUB_UID_RSTnSYNC_CLK_SHUB_RTCCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_RSTnSYNC_CLK_SHUB_USI00_CLK, "gout_shub_rstnsync_clk_shub_usi00_clk",
	     "dout_shub_usi00",
	     GOUT_BLK_SHUB_UID_RSTnSYNC_CLK_SHUB_USI00_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_CM4_SHUB_FCLK, "gout_shub_cm4_shub_fclk", "mout_cmu_shub_bus_user",
	     GOUT_BLK_SHUB_UID_CM4_SHUB_IPCLKPORT_FCLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_I2C_SHUB00_IPCLK, "gout_shub_i2c_shub00_ipclk", "dout_shub_i2c",
	     GOUT_BLK_SHUB_UID_I2C_SHUB00_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_PDMA_SHUB_ACLK, "gout_shub_pdma_shub_aclk", "mout_cmu_shub_bus_user",
	     GOUT_BLK_SHUB_UID_PDMA_SHUB_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_USI_SHUB00_IPCLK, "gout_shub_usi_shub00_ipclk", "dout_shub_usi00",
	     GOUT_BLK_SHUB_UID_USI_SHUB00_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_SHUB_XIU_DP_SHUB_ACLK, "gout_shub_xiu_dp_shub_aclk", "mout_cmu_shub_bus_user",
	     GOUT_BLK_SHUB_UID_XIU_DP_SHUB_IPCLKPORT_ACLK, 21, 0, 0),
};

static const struct samsung_cmu_info shub_cmu_info __initconst = {
	.mux_clks		= shub_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(shub_mux_clks),
	.div_clks		= shub_div_clks,
	.nr_div_clks		= ARRAY_SIZE(shub_div_clks),
	.gate_clks		= shub_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(shub_gate_clks),
	.fixed_clks		= shub_fixed_clks,
	.nr_fixed_clks		= ARRAY_SIZE(shub_fixed_clks),
	.nr_clk_ids		= CLKS_NR_SHUB,
	.clk_regs		= shub_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(shub_clk_regs),
	.qch_regs		= shub_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(shub_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9610_cmu_shub_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &shub_cmu_info);
}

CLK_OF_DECLARE(exynos9610_cmu_shub, "samsung,exynos9610-cmu-shub",
	       exynos9610_cmu_shub_init);

/* ---- CMU_USB ---------------------------------------------------------*/

/* Register Offset definitions for CMU_USB (0x13030000) */
#define MUX_CLKCMU_USB_BUS_USER			0x0100
#define MUX_CLKCMU_USB_DPGTC_USER			0x0120
#define MUX_CLKCMU_USB_USB30DRD_USER			0x0140
#define CLK_BLK_USB_UID_RSTnSYNC_CLK_USB_OSCCLK_IPCLKPORT_CLK	0x2000
#define CLK_BLK_USB_UID_USB_CMU_USB_IPCLKPORT_PCLK	0x2004
#define GOUT_BLK_USB_UID_BTM_USB_IPCLKPORT_I_ACLK	0x2008
#define GOUT_BLK_USB_UID_BTM_USB_IPCLKPORT_I_PCLK	0x200c
#define GOUT_BLK_USB_UID_DP_LINK_IPCLKPORT_DPTX_LINK_I_DP_GTC_CLK	0x2010
#define GOUT_BLK_USB_UID_DP_LINK_IPCLKPORT_DPTX_LINK_I_PCLK	0x2014
#define GOUT_BLK_USB_UID_LHM_AXI_P_USB_IPCLKPORT_I_CLK	0x2018
#define GOUT_BLK_USB_UID_LHS_ACEL_D_USB_IPCLKPORT_I_CLK	0x201c
#define GOUT_BLK_USB_UID_PGEN_LITE_USB_IPCLKPORT_CLK	0x2020
#define GOUT_BLK_USB_UID_PPMU_USB_IPCLKPORT_ACLK	0x2024
#define GOUT_BLK_USB_UID_PPMU_USB_IPCLKPORT_PCLK	0x2028
#define GOUT_BLK_USB_UID_RSTnSYNC_CLK_USB_BUS_IPCLKPORT_CLK	0x202c
#define GOUT_BLK_USB_UID_SYSREG_USB_IPCLKPORT_PCLK	0x2030
#define GOUT_BLK_USB_UID_USB30DRD_IPCLKPORT_ACLK_PHYCTRL_20	0x2034
#define GOUT_BLK_USB_UID_USB30DRD_IPCLKPORT_ACLK_PHYCTRL_30_0	0x2038
#define GOUT_BLK_USB_UID_USB30DRD_IPCLKPORT_ACLK_PHYCTRL_30_1	0x203c
#define GOUT_BLK_USB_UID_USB30DRD_IPCLKPORT_bus_clk_early	0x2040
#define GOUT_BLK_USB_UID_USB30DRD_IPCLKPORT_USB30DRD_ref_clk	0x2044
#define GOUT_BLK_USB_UID_US_D_USB_IPCLKPORT_aclk	0x2048

static const unsigned long usb_clk_regs[] __initconst = {
	MUX_CLKCMU_USB_BUS_USER,
	MUX_CLKCMU_USB_DPGTC_USER,
	MUX_CLKCMU_USB_USB30DRD_USER,
	CLK_BLK_USB_UID_RSTnSYNC_CLK_USB_OSCCLK_IPCLKPORT_CLK,
	CLK_BLK_USB_UID_USB_CMU_USB_IPCLKPORT_PCLK,
	GOUT_BLK_USB_UID_BTM_USB_IPCLKPORT_I_ACLK,
	GOUT_BLK_USB_UID_BTM_USB_IPCLKPORT_I_PCLK,
	GOUT_BLK_USB_UID_DP_LINK_IPCLKPORT_DPTX_LINK_I_DP_GTC_CLK,
	GOUT_BLK_USB_UID_DP_LINK_IPCLKPORT_DPTX_LINK_I_PCLK,
	GOUT_BLK_USB_UID_LHM_AXI_P_USB_IPCLKPORT_I_CLK,
	GOUT_BLK_USB_UID_LHS_ACEL_D_USB_IPCLKPORT_I_CLK,
	GOUT_BLK_USB_UID_PGEN_LITE_USB_IPCLKPORT_CLK,
	GOUT_BLK_USB_UID_PPMU_USB_IPCLKPORT_ACLK,
	GOUT_BLK_USB_UID_PPMU_USB_IPCLKPORT_PCLK,
	GOUT_BLK_USB_UID_RSTnSYNC_CLK_USB_BUS_IPCLKPORT_CLK,
	GOUT_BLK_USB_UID_SYSREG_USB_IPCLKPORT_PCLK,
	GOUT_BLK_USB_UID_USB30DRD_IPCLKPORT_ACLK_PHYCTRL_20,
	GOUT_BLK_USB_UID_USB30DRD_IPCLKPORT_ACLK_PHYCTRL_30_0,
	GOUT_BLK_USB_UID_USB30DRD_IPCLKPORT_ACLK_PHYCTRL_30_1,
	GOUT_BLK_USB_UID_USB30DRD_IPCLKPORT_bus_clk_early,
	GOUT_BLK_USB_UID_USB30DRD_IPCLKPORT_USB30DRD_ref_clk,
	GOUT_BLK_USB_UID_US_D_USB_IPCLKPORT_aclk,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long usb_qch_regs[] __initconst = {
	0x3008,	/* BTM_USB_QCH */
	0x300c,	/* DP_LINK_QCH_DP */
	0x3010,	/* DP_LINK_QCH_GTC */
	0x3014,	/* LHM_AXI_P_USB_QCH */
	0x3018,	/* LHS_ACEL_D_USB_QCH */
	0x301c,	/* PGEN_LITE_USB_QCH */
	0x3020,	/* PPMU_USB_QCH */
	0x3024,	/* SYSREG_USB_QCH */
	0x3028,	/* USB30DRD_QCH_USB30 */
	0x302c,	/* USB30DRD_QCH_USBPHY_20CTRL */
	0x3030,	/* USB30DRD_QCH_USBPHY_30CTRL_0 */
	0x3034,	/* USB30DRD_QCH_USBPHY_30CTRL_1 */
	0x3038,	/* USB_CMU_USB_QCH */
};

/* List of parent clocks for Muxes in CMU_USB */
PNAME(mout_cmu_usb_bus_user_p) = { "oscclk", "dout_clkcmu_usb_bus" };
PNAME(mout_cmu_usb_usb30drd_user_p) = { "oscclk", "dout_clkcmu_usb_usb30drd" };
PNAME(mout_cmu_usb_dpgtc_user_p) = { "oscclk", "dout_clkcmu_usb_dpgtc" };

static const struct samsung_mux_clock usb_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_USB_BUS_USER, "mout_cmu_usb_bus_user", mout_cmu_usb_bus_user_p,
	    MUX_CLKCMU_USB_BUS_USER, 4, 1),
	MUX(CLK_MOUT_CMU_USB_USB30DRD_USER, "mout_cmu_usb_usb30drd_user",
	    mout_cmu_usb_usb30drd_user_p,
	    MUX_CLKCMU_USB_USB30DRD_USER, 4, 1),
	MUX(CLK_MOUT_CMU_USB_DPGTC_USER, "mout_cmu_usb_dpgtc_user", mout_cmu_usb_dpgtc_user_p,
	    MUX_CLKCMU_USB_DPGTC_USER, 4, 1),
};

static const struct samsung_gate_clock usb_gate_clks[] __initconst = {
	GATE(CLK_GOUT_USB_USB_CMU_USB_PCLK, "gout_usb_usb_cmu_usb_pclk", "mout_cmu_usb_bus_user",
	     CLK_BLK_USB_UID_USB_CMU_USB_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_USB_LHM_AXI_P_USB_I_CLK, "gout_usb_lhm_axi_p_usb_i_clk",
	     "mout_cmu_usb_bus_user",
	     GOUT_BLK_USB_UID_LHM_AXI_P_USB_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_USB_PPMU_USB_ACLK, "gout_usb_ppmu_usb_aclk", "mout_cmu_usb_bus_user",
	     GOUT_BLK_USB_UID_PPMU_USB_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_USB_PPMU_USB_PCLK, "gout_usb_ppmu_usb_pclk", "mout_cmu_usb_bus_user",
	     GOUT_BLK_USB_UID_PPMU_USB_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_USB_SYSREG_USB_PCLK, "gout_usb_sysreg_usb_pclk", "mout_cmu_usb_bus_user",
	     GOUT_BLK_USB_UID_SYSREG_USB_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_USB_USB30DRD_bus_clk_early, "gout_usb_usb30drd_bus_clk_early",
	     "mout_cmu_usb_bus_user",
	     GOUT_BLK_USB_UID_USB30DRD_IPCLKPORT_bus_clk_early, 21, 0, 0),
	GATE(CLK_GOUT_USB_DP_LINK_DPTX_LINK_I_DP_GTC_CLK, "gout_usb_dp_link_dptx_link_i_dp_gtc_clk",
	     "mout_cmu_usb_dpgtc_user",
	     GOUT_BLK_USB_UID_DP_LINK_IPCLKPORT_DPTX_LINK_I_DP_GTC_CLK, 21, 0, 0),
	GATE(CLK_GOUT_USB_RSTnSYNC_CLK_USB_BUS_CLK, "gout_usb_rstnsync_clk_usb_bus_clk",
	     "mout_cmu_usb_bus_user",
	     GOUT_BLK_USB_UID_RSTnSYNC_CLK_USB_BUS_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_USB_RSTnSYNC_CLK_USB_OSCCLK_CLK, "gout_usb_rstnsync_clk_usb_oscclk_clk",
	     "oscclk",
	     CLK_BLK_USB_UID_RSTnSYNC_CLK_USB_OSCCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_USB_USB30DRD_ACLK_PHYCTRL_20, "gout_usb_usb30drd_aclk_phyctrl_20",
	     "mout_cmu_usb_bus_user",
	     GOUT_BLK_USB_UID_USB30DRD_IPCLKPORT_ACLK_PHYCTRL_20, 21, 0, 0),
	GATE(CLK_GOUT_USB_PGEN_LITE_USB_CLK, "gout_usb_pgen_lite_usb_clk", "mout_cmu_usb_bus_user",
	     GOUT_BLK_USB_UID_PGEN_LITE_USB_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_USB_USB30DRD_ACLK_PHYCTRL_30_1, "gout_usb_usb30drd_aclk_phyctrl_30_1",
	     "mout_cmu_usb_bus_user",
	     GOUT_BLK_USB_UID_USB30DRD_IPCLKPORT_ACLK_PHYCTRL_30_1, 21, 0, 0),
	GATE(CLK_GOUT_USB_BTM_USB_I_ACLK, "gout_usb_btm_usb_i_aclk", "mout_cmu_usb_bus_user",
	     GOUT_BLK_USB_UID_BTM_USB_IPCLKPORT_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_USB_BTM_USB_I_PCLK, "gout_usb_btm_usb_i_pclk", "mout_cmu_usb_bus_user",
	     GOUT_BLK_USB_UID_BTM_USB_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_USB_US_D_USB_aclk, "gout_usb_us_d_usb_aclk", "mout_cmu_usb_bus_user",
	     GOUT_BLK_USB_UID_US_D_USB_IPCLKPORT_aclk, 21, 0, 0),
	GATE(CLK_GOUT_USB_DP_LINK_DPTX_LINK_I_PCLK, "gout_usb_dp_link_dptx_link_i_pclk",
	     "mout_cmu_usb_bus_user",
	     GOUT_BLK_USB_UID_DP_LINK_IPCLKPORT_DPTX_LINK_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_USB_USB30DRD_ACLK_PHYCTRL_30_0, "gout_usb_usb30drd_aclk_phyctrl_30_0",
	     "mout_cmu_usb_bus_user",
	     GOUT_BLK_USB_UID_USB30DRD_IPCLKPORT_ACLK_PHYCTRL_30_0, 21, 0, 0),
	GATE(CLK_GOUT_USB_USB30DRD_USB30DRD_ref_clk, "gout_usb_usb30drd_usb30drd_ref_clk",
	     "mout_cmu_usb_usb30drd_user",
	     GOUT_BLK_USB_UID_USB30DRD_IPCLKPORT_USB30DRD_ref_clk, 21, 0, 0),
	GATE(CLK_GOUT_USB_LHS_ACEL_D_USB_I_CLK, "gout_usb_lhs_acel_d_usb_i_clk",
	     "mout_cmu_usb_bus_user",
	     GOUT_BLK_USB_UID_LHS_ACEL_D_USB_IPCLKPORT_I_CLK, 21, 0, 0),
};

static const struct samsung_cmu_info usb_cmu_info __initconst = {
	.mux_clks		= usb_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(usb_mux_clks),
	.gate_clks		= usb_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(usb_gate_clks),
	.nr_clk_ids		= CLKS_NR_USB,
	.clk_regs		= usb_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(usb_clk_regs),
	.qch_regs		= usb_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(usb_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9610_cmu_usb_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &usb_cmu_info);
}

CLK_OF_DECLARE(exynos9610_cmu_usb, "samsung,exynos9610-cmu-usb",
	       exynos9610_cmu_usb_init);

/* ---- CMU_VIPX1 -------------------------------------------------------*/

/* Register Offset definitions for CMU_VIPX1 (0x10c90000) */
#define MUX_CLKCMU_VIPX1_BUS_USER			0x0100
#define DIV_CLK_VIPX1_BUSP			0x1800
#define CLK_BLK_VIPX1_UID_RSTnSYNC_CLK_VIPX1_OSCCLK_IPCLKPORT_CLK	0x2000
#define CLK_BLK_VIPX1_UID_VIPX1_CMU_VIPX1_IPCLKPORT_PCLK	0x2004
#define GOUT_BLK_VIPX1_UID_BLK_VIPX1_IPCLKPORT_CLK_VIPX1_BUSD	0x2008
#define GOUT_BLK_VIPX1_UID_BTM_D_VIPX1_IPCLKPORT_I_ACLK	0x200c
#define GOUT_BLK_VIPX1_UID_BTM_D_VIPX1_IPCLKPORT_I_PCLK	0x2010
#define GOUT_BLK_VIPX1_UID_LHM_ATB_VIPX1_IPCLKPORT_I_CLK	0x2014
#define GOUT_BLK_VIPX1_UID_LHM_AXI_P_VIPX1_IPCLKPORT_I_CLK	0x2018
#define GOUT_BLK_VIPX1_UID_LHS_ACEL_D_VIPX1_IPCLKPORT_I_CLK	0x201c
#define GOUT_BLK_VIPX1_UID_LHS_ATB_VIPX1_IPCLKPORT_I_CLK	0x2020
#define GOUT_BLK_VIPX1_UID_LHS_AXI_P_VIPX1_LOCAL_IPCLKPORT_I_CLK	0x2024
#define GOUT_BLK_VIPX1_UID_PGEN_LITE_VIPX1_IPCLKPORT_CLK	0x2028
#define GOUT_BLK_VIPX1_UID_PPMU_D_VIPX1_IPCLKPORT_ACLK	0x202c
#define GOUT_BLK_VIPX1_UID_PPMU_D_VIPX1_IPCLKPORT_PCLK	0x2030
#define GOUT_BLK_VIPX1_UID_RSTnSYNC_CLK_VIPX1_BUSD_IPCLKPORT_CLK	0x2034
#define GOUT_BLK_VIPX1_UID_RSTnSYNC_CLK_VIPX1_BUSP_IPCLKPORT_CLK	0x2038
#define GOUT_BLK_VIPX1_UID_SMMU_D_VIPX1_IPCLKPORT_CLK	0x203c
#define GOUT_BLK_VIPX1_UID_SYSREG_VIPX1_IPCLKPORT_PCLK	0x2040
#define GOUT_BLK_VIPX1_UID_VIPX1_IPCLKPORT_CLK			0x2044
#define GOUT_BLK_VIPX1_UID_XIU_D_VIPX1_IPCLKPORT_ACLK	0x2050

static const unsigned long vipx1_clk_regs[] __initconst = {
	MUX_CLKCMU_VIPX1_BUS_USER,
	DIV_CLK_VIPX1_BUSP,
	CLK_BLK_VIPX1_UID_RSTnSYNC_CLK_VIPX1_OSCCLK_IPCLKPORT_CLK,
	CLK_BLK_VIPX1_UID_VIPX1_CMU_VIPX1_IPCLKPORT_PCLK,
	GOUT_BLK_VIPX1_UID_BLK_VIPX1_IPCLKPORT_CLK_VIPX1_BUSD,
	GOUT_BLK_VIPX1_UID_BTM_D_VIPX1_IPCLKPORT_I_ACLK,
	GOUT_BLK_VIPX1_UID_BTM_D_VIPX1_IPCLKPORT_I_PCLK,
	GOUT_BLK_VIPX1_UID_LHM_ATB_VIPX1_IPCLKPORT_I_CLK,
	GOUT_BLK_VIPX1_UID_LHM_AXI_P_VIPX1_IPCLKPORT_I_CLK,
	GOUT_BLK_VIPX1_UID_LHS_ACEL_D_VIPX1_IPCLKPORT_I_CLK,
	GOUT_BLK_VIPX1_UID_LHS_ATB_VIPX1_IPCLKPORT_I_CLK,
	GOUT_BLK_VIPX1_UID_LHS_AXI_P_VIPX1_LOCAL_IPCLKPORT_I_CLK,
	GOUT_BLK_VIPX1_UID_PGEN_LITE_VIPX1_IPCLKPORT_CLK,
	GOUT_BLK_VIPX1_UID_PPMU_D_VIPX1_IPCLKPORT_ACLK,
	GOUT_BLK_VIPX1_UID_PPMU_D_VIPX1_IPCLKPORT_PCLK,
	GOUT_BLK_VIPX1_UID_RSTnSYNC_CLK_VIPX1_BUSD_IPCLKPORT_CLK,
	GOUT_BLK_VIPX1_UID_RSTnSYNC_CLK_VIPX1_BUSP_IPCLKPORT_CLK,
	GOUT_BLK_VIPX1_UID_SMMU_D_VIPX1_IPCLKPORT_CLK,
	GOUT_BLK_VIPX1_UID_SYSREG_VIPX1_IPCLKPORT_PCLK,
	GOUT_BLK_VIPX1_UID_VIPX1_IPCLKPORT_CLK,
	GOUT_BLK_VIPX1_UID_XIU_D_VIPX1_IPCLKPORT_ACLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long vipx1_qch_regs[] __initconst = {
	0x3014,	/* BTM_D_VIPX1_QCH */
	0x3018,	/* LHM_ATB_VIPX1_QCH */
	0x301c,	/* LHM_AXI_P_VIPX1_QCH */
	0x3020,	/* LHS_ACEL_D_VIPX1_QCH */
	0x3024,	/* LHS_ATB_VIPX1_QCH */
	0x3028,	/* LHS_AXI_P_VIPX1_LOCAL_QCH */
	0x302c,	/* PGEN_LITE_VIPX1_QCH */
	0x3030,	/* PPMU_D_VIPX1_QCH */
	0x3034,	/* SMMU_D_VIPX1_QCH */
	0x3038,	/* SYSREG_VIPX1_QCH */
	0x303c,	/* VIPX1_CMU_VIPX1_QCH */
	0x3040,	/* VIPX1_QCH */
};

/* List of parent clocks for Muxes in CMU_VIPX1 */
PNAME(mout_cmu_vipx1_bus_user_p) = { "oscclk", "dout_clkcmu_vipx1_bus" };

static const struct samsung_mux_clock vipx1_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_VIPX1_BUS_USER, "mout_cmu_vipx1_bus_user", mout_cmu_vipx1_bus_user_p,
	    MUX_CLKCMU_VIPX1_BUS_USER, 4, 1),
};

static const struct samsung_div_clock vipx1_div_clks[] __initconst = {
	DIV(CLK_DOUT_VIPX1_BUSP, "dout_vipx1_busp", "mout_cmu_vipx1_bus_user",
	    DIV_CLK_VIPX1_BUSP, 0, 2),
};

static const struct samsung_gate_clock vipx1_gate_clks[] __initconst = {
	GATE(CLK_GOUT_VIPX1_LHS_ACEL_D_VIPX1_I_CLK, "gout_vipx1_lhs_acel_d_vipx1_i_clk",
	     "mout_cmu_vipx1_bus_user",
	     GOUT_BLK_VIPX1_UID_LHS_ACEL_D_VIPX1_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX1_RSTnSYNC_CLK_VIPX1_BUSD_CLK, "gout_vipx1_rstnsync_clk_vipx1_busd_clk",
	     "mout_cmu_vipx1_bus_user",
	     GOUT_BLK_VIPX1_UID_RSTnSYNC_CLK_VIPX1_BUSD_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX1_RSTnSYNC_CLK_VIPX1_BUSP_CLK, "gout_vipx1_rstnsync_clk_vipx1_busp_clk",
	     "dout_vipx1_busp",
	     GOUT_BLK_VIPX1_UID_RSTnSYNC_CLK_VIPX1_BUSP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX1_RSTnSYNC_CLK_VIPX1_OSCCLK_CLK, "gout_vipx1_rstnsync_clk_vipx1_oscclk_clk",
	     "oscclk",
	     CLK_BLK_VIPX1_UID_RSTnSYNC_CLK_VIPX1_OSCCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX1_SYSREG_VIPX1_PCLK, "gout_vipx1_sysreg_vipx1_pclk", "dout_vipx1_busp",
	     GOUT_BLK_VIPX1_UID_SYSREG_VIPX1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX1_VIPX1_CMU_VIPX1_PCLK, "gout_vipx1_vipx1_cmu_vipx1_pclk",
	     "dout_vipx1_busp",
	     CLK_BLK_VIPX1_UID_VIPX1_CMU_VIPX1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX1_LHS_ATB_VIPX1_I_CLK, "gout_vipx1_lhs_atb_vipx1_i_clk",
	     "mout_cmu_vipx1_bus_user",
	     GOUT_BLK_VIPX1_UID_LHS_ATB_VIPX1_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX1_BLK_VIPX1_CLK_VIPX1_BUSD, "gout_vipx1_blk_vipx1_clk_vipx1_busd",
	     "mout_cmu_vipx1_bus_user",
	     GOUT_BLK_VIPX1_UID_BLK_VIPX1_IPCLKPORT_CLK_VIPX1_BUSD, 21, 0, 0),
	GATE(CLK_GOUT_VIPX1_PPMU_D_VIPX1_ACLK, "gout_vipx1_ppmu_d_vipx1_aclk",
	     "mout_cmu_vipx1_bus_user",
	     GOUT_BLK_VIPX1_UID_PPMU_D_VIPX1_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX1_PPMU_D_VIPX1_PCLK, "gout_vipx1_ppmu_d_vipx1_pclk", "dout_vipx1_busp",
	     GOUT_BLK_VIPX1_UID_PPMU_D_VIPX1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX1_SMMU_D_VIPX1_CLK, "gout_vipx1_smmu_d_vipx1_clk",
	     "mout_cmu_vipx1_bus_user",
	     GOUT_BLK_VIPX1_UID_SMMU_D_VIPX1_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX1_XIU_D_VIPX1_ACLK, "gout_vipx1_xiu_d_vipx1_aclk",
	     "mout_cmu_vipx1_bus_user",
	     GOUT_BLK_VIPX1_UID_XIU_D_VIPX1_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX1_PGEN_LITE_VIPX1_CLK, "gout_vipx1_pgen_lite_vipx1_clk",
	     "dout_vipx1_busp",
	     GOUT_BLK_VIPX1_UID_PGEN_LITE_VIPX1_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX1_VIPX1_CLK, "gout_vipx1_vipx1_clk", "mout_cmu_vipx1_bus_user",
	     GOUT_BLK_VIPX1_UID_VIPX1_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX1_BTM_D_VIPX1_I_ACLK, "gout_vipx1_btm_d_vipx1_i_aclk",
	     "mout_cmu_vipx1_bus_user",
	     GOUT_BLK_VIPX1_UID_BTM_D_VIPX1_IPCLKPORT_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX1_BTM_D_VIPX1_I_PCLK, "gout_vipx1_btm_d_vipx1_i_pclk", "dout_vipx1_busp",
	     GOUT_BLK_VIPX1_UID_BTM_D_VIPX1_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX1_LHM_AXI_P_VIPX1_I_CLK, "gout_vipx1_lhm_axi_p_vipx1_i_clk",
	     "dout_vipx1_busp",
	     GOUT_BLK_VIPX1_UID_LHM_AXI_P_VIPX1_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX1_LHM_ATB_VIPX1_I_CLK, "gout_vipx1_lhm_atb_vipx1_i_clk",
	     "mout_cmu_vipx1_bus_user",
	     GOUT_BLK_VIPX1_UID_LHM_ATB_VIPX1_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX1_LHS_AXI_P_VIPX1_LOCAL_I_CLK, "gout_vipx1_lhs_axi_p_vipx1_local_i_clk",
	     "mout_cmu_vipx1_bus_user",
	     GOUT_BLK_VIPX1_UID_LHS_AXI_P_VIPX1_LOCAL_IPCLKPORT_I_CLK, 21, 0, 0),
};

static const struct samsung_cmu_info vipx1_cmu_info __initconst = {
	.mux_clks		= vipx1_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(vipx1_mux_clks),
	.div_clks		= vipx1_div_clks,
	.nr_div_clks		= ARRAY_SIZE(vipx1_div_clks),
	.gate_clks		= vipx1_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(vipx1_gate_clks),
	.nr_clk_ids		= CLKS_NR_VIPX1,
	.clk_regs		= vipx1_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(vipx1_clk_regs),
	.qch_regs		= vipx1_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(vipx1_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9610_cmu_vipx1_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &vipx1_cmu_info);
}

CLK_OF_DECLARE(exynos9610_cmu_vipx1, "samsung,exynos9610-cmu-vipx1",
	       exynos9610_cmu_vipx1_init);

/* ---- CMU_VIPX2 -------------------------------------------------------*/

/* Register Offset definitions for CMU_VIPX2 (0x10e90000) */
#define MUX_CLKCMU_VIPX2_BUS_USER			0x0100
#define DIV_CLK_VIPX2_BUSP			0x1800
#define CLK_BLK_VIPX2_UID_RSTnSYNC_CLK_VIPX2_OSCCLK_IPCLKPORT_CLK	0x2000
#define CLK_BLK_VIPX2_UID_VIPX2_CMU_VIPX2_IPCLKPORT_PCLK	0x2004
#define GOUT_BLK_VIPX2_UID_BLK_VIPX2_IPCLKPORT_CLK_VIPX2_BUSD	0x2008
#define GOUT_BLK_VIPX2_UID_BTM_D_VIPX2_IPCLKPORT_I_ACLK	0x200c
#define GOUT_BLK_VIPX2_UID_BTM_D_VIPX2_IPCLKPORT_I_PCLK	0x2010
#define GOUT_BLK_VIPX2_UID_LHM_ATB_VIPX2_IPCLKPORT_I_CLK	0x2014
#define GOUT_BLK_VIPX2_UID_LHM_AXI_P_VIPX2_IPCLKPORT_I_CLK	0x2018
#define GOUT_BLK_VIPX2_UID_LHM_AXI_P_VIPX2_LOCAL_IPCLKPORT_I_CLK	0x201c
#define GOUT_BLK_VIPX2_UID_LHS_ACEL_D_VIPX2_IPCLKPORT_I_CLK	0x2020
#define GOUT_BLK_VIPX2_UID_LHS_ATB_VIPX2_IPCLKPORT_I_CLK	0x2024
#define GOUT_BLK_VIPX2_UID_PGEN_LITE_VIPX2_IPCLKPORT_CLK	0x2028
#define GOUT_BLK_VIPX2_UID_PPMU_D_VIPX2_IPCLKPORT_ACLK	0x202c
#define GOUT_BLK_VIPX2_UID_PPMU_D_VIPX2_IPCLKPORT_PCLK	0x2030
#define GOUT_BLK_VIPX2_UID_RSTnSYNC_CLK_VIPX2_BUSD_IPCLKPORT_CLK	0x2034
#define GOUT_BLK_VIPX2_UID_RSTnSYNC_CLK_VIPX2_BUSP_IPCLKPORT_CLK	0x2038
#define GOUT_BLK_VIPX2_UID_SMMU_D_VIPX2_IPCLKPORT_CLK	0x203c
#define GOUT_BLK_VIPX2_UID_SYSREG_VIPX2_IPCLKPORT_PCLK	0x2040
#define GOUT_BLK_VIPX2_UID_VIPX2_IPCLKPORT_CLK			0x2044

static const unsigned long vipx2_clk_regs[] __initconst = {
	MUX_CLKCMU_VIPX2_BUS_USER,
	DIV_CLK_VIPX2_BUSP,
	CLK_BLK_VIPX2_UID_RSTnSYNC_CLK_VIPX2_OSCCLK_IPCLKPORT_CLK,
	CLK_BLK_VIPX2_UID_VIPX2_CMU_VIPX2_IPCLKPORT_PCLK,
	GOUT_BLK_VIPX2_UID_BLK_VIPX2_IPCLKPORT_CLK_VIPX2_BUSD,
	GOUT_BLK_VIPX2_UID_BTM_D_VIPX2_IPCLKPORT_I_ACLK,
	GOUT_BLK_VIPX2_UID_BTM_D_VIPX2_IPCLKPORT_I_PCLK,
	GOUT_BLK_VIPX2_UID_LHM_ATB_VIPX2_IPCLKPORT_I_CLK,
	GOUT_BLK_VIPX2_UID_LHM_AXI_P_VIPX2_IPCLKPORT_I_CLK,
	GOUT_BLK_VIPX2_UID_LHM_AXI_P_VIPX2_LOCAL_IPCLKPORT_I_CLK,
	GOUT_BLK_VIPX2_UID_LHS_ACEL_D_VIPX2_IPCLKPORT_I_CLK,
	GOUT_BLK_VIPX2_UID_LHS_ATB_VIPX2_IPCLKPORT_I_CLK,
	GOUT_BLK_VIPX2_UID_PGEN_LITE_VIPX2_IPCLKPORT_CLK,
	GOUT_BLK_VIPX2_UID_PPMU_D_VIPX2_IPCLKPORT_ACLK,
	GOUT_BLK_VIPX2_UID_PPMU_D_VIPX2_IPCLKPORT_PCLK,
	GOUT_BLK_VIPX2_UID_RSTnSYNC_CLK_VIPX2_BUSD_IPCLKPORT_CLK,
	GOUT_BLK_VIPX2_UID_RSTnSYNC_CLK_VIPX2_BUSP_IPCLKPORT_CLK,
	GOUT_BLK_VIPX2_UID_SMMU_D_VIPX2_IPCLKPORT_CLK,
	GOUT_BLK_VIPX2_UID_SYSREG_VIPX2_IPCLKPORT_PCLK,
	GOUT_BLK_VIPX2_UID_VIPX2_IPCLKPORT_CLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long vipx2_qch_regs[] __initconst = {
	0x3014,	/* BTM_D_VIPX2_QCH */
	0x3018,	/* LHM_ATB_VIPX2_QCH */
	0x301c,	/* LHM_AXI_P_VIPX2_LOCAL_QCH */
	0x3020,	/* LHM_AXI_P_VIPX2_QCH */
	0x3024,	/* LHS_ACEL_D_VIPX2_QCH */
	0x3028,	/* LHS_ATB_VIPX2_QCH */
	0x302c,	/* PGEN_LITE_VIPX2_QCH */
	0x3030,	/* PPMU_D_VIPX2_QCH */
	0x3034,	/* SMMU_D_VIPX2_QCH */
	0x3038,	/* SYSREG_VIPX2_QCH */
	0x303c,	/* VIPX2_CMU_VIPX2_QCH */
	0x3040,	/* VIPX2_QCH */
	0x3044,	/* VIPX2_QCH_LOCAL */
};

/* List of parent clocks for Muxes in CMU_VIPX2 */
PNAME(mout_cmu_vipx2_bus_user_p) = { "oscclk", "dout_clkcmu_vipx2_bus" };

static const struct samsung_mux_clock vipx2_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_VIPX2_BUS_USER, "mout_cmu_vipx2_bus_user", mout_cmu_vipx2_bus_user_p,
	    MUX_CLKCMU_VIPX2_BUS_USER, 4, 1),
};

static const struct samsung_div_clock vipx2_div_clks[] __initconst = {
	DIV(CLK_DOUT_VIPX2_BUSP, "dout_vipx2_busp", "mout_cmu_vipx2_bus_user",
	    DIV_CLK_VIPX2_BUSP, 0, 2),
};

static const struct samsung_gate_clock vipx2_gate_clks[] __initconst = {
	GATE(CLK_GOUT_VIPX2_VIPX2_CMU_VIPX2_PCLK, "gout_vipx2_vipx2_cmu_vipx2_pclk",
	     "dout_vipx2_busp",
	     CLK_BLK_VIPX2_UID_VIPX2_CMU_VIPX2_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX2_BTM_D_VIPX2_I_ACLK, "gout_vipx2_btm_d_vipx2_i_aclk",
	     "mout_cmu_vipx2_bus_user",
	     GOUT_BLK_VIPX2_UID_BTM_D_VIPX2_IPCLKPORT_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX2_BTM_D_VIPX2_I_PCLK, "gout_vipx2_btm_d_vipx2_i_pclk", "dout_vipx2_busp",
	     GOUT_BLK_VIPX2_UID_BTM_D_VIPX2_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX2_LHM_ATB_VIPX2_I_CLK, "gout_vipx2_lhm_atb_vipx2_i_clk",
	     "mout_cmu_vipx2_bus_user",
	     GOUT_BLK_VIPX2_UID_LHM_ATB_VIPX2_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX2_LHM_AXI_P_VIPX2_I_CLK, "gout_vipx2_lhm_axi_p_vipx2_i_clk",
	     "dout_vipx2_busp",
	     GOUT_BLK_VIPX2_UID_LHM_AXI_P_VIPX2_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX2_LHS_ACEL_D_VIPX2_I_CLK, "gout_vipx2_lhs_acel_d_vipx2_i_clk",
	     "mout_cmu_vipx2_bus_user",
	     GOUT_BLK_VIPX2_UID_LHS_ACEL_D_VIPX2_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX2_LHS_ATB_VIPX2_I_CLK, "gout_vipx2_lhs_atb_vipx2_i_clk",
	     "mout_cmu_vipx2_bus_user",
	     GOUT_BLK_VIPX2_UID_LHS_ATB_VIPX2_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX2_LHM_AXI_P_VIPX2_LOCAL_I_CLK, "gout_vipx2_lhm_axi_p_vipx2_local_i_clk",
	     "mout_cmu_vipx2_bus_user",
	     GOUT_BLK_VIPX2_UID_LHM_AXI_P_VIPX2_LOCAL_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX2_PGEN_LITE_VIPX2_CLK, "gout_vipx2_pgen_lite_vipx2_clk",
	     "dout_vipx2_busp",
	     GOUT_BLK_VIPX2_UID_PGEN_LITE_VIPX2_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX2_PPMU_D_VIPX2_ACLK, "gout_vipx2_ppmu_d_vipx2_aclk",
	     "mout_cmu_vipx2_bus_user",
	     GOUT_BLK_VIPX2_UID_PPMU_D_VIPX2_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX2_PPMU_D_VIPX2_PCLK, "gout_vipx2_ppmu_d_vipx2_pclk", "dout_vipx2_busp",
	     GOUT_BLK_VIPX2_UID_PPMU_D_VIPX2_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX2_SMMU_D_VIPX2_CLK, "gout_vipx2_smmu_d_vipx2_clk",
	     "mout_cmu_vipx2_bus_user",
	     GOUT_BLK_VIPX2_UID_SMMU_D_VIPX2_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX2_SYSREG_VIPX2_PCLK, "gout_vipx2_sysreg_vipx2_pclk", "dout_vipx2_busp",
	     GOUT_BLK_VIPX2_UID_SYSREG_VIPX2_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX2_BLK_VIPX2_CLK_VIPX2_BUSD, "gout_vipx2_blk_vipx2_clk_vipx2_busd",
	     "mout_cmu_vipx2_bus_user",
	     GOUT_BLK_VIPX2_UID_BLK_VIPX2_IPCLKPORT_CLK_VIPX2_BUSD, 21, 0, 0),
	GATE(CLK_GOUT_VIPX2_VIPX2_CLK, "gout_vipx2_vipx2_clk", "mout_cmu_vipx2_bus_user",
	     GOUT_BLK_VIPX2_UID_VIPX2_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX2_RSTnSYNC_CLK_VIPX2_BUSD_CLK, "gout_vipx2_rstnsync_clk_vipx2_busd_clk",
	     "mout_cmu_vipx2_bus_user",
	     GOUT_BLK_VIPX2_UID_RSTnSYNC_CLK_VIPX2_BUSD_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX2_RSTnSYNC_CLK_VIPX2_BUSP_CLK, "gout_vipx2_rstnsync_clk_vipx2_busp_clk",
	     "dout_vipx2_busp",
	     GOUT_BLK_VIPX2_UID_RSTnSYNC_CLK_VIPX2_BUSP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VIPX2_RSTnSYNC_CLK_VIPX2_OSCCLK_CLK, "gout_vipx2_rstnsync_clk_vipx2_oscclk_clk",
	     "oscclk",
	     CLK_BLK_VIPX2_UID_RSTnSYNC_CLK_VIPX2_OSCCLK_IPCLKPORT_CLK, 21, 0, 0),
};

static const struct samsung_cmu_info vipx2_cmu_info __initconst = {
	.mux_clks		= vipx2_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(vipx2_mux_clks),
	.div_clks		= vipx2_div_clks,
	.nr_div_clks		= ARRAY_SIZE(vipx2_div_clks),
	.gate_clks		= vipx2_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(vipx2_gate_clks),
	.nr_clk_ids		= CLKS_NR_VIPX2,
	.clk_regs		= vipx2_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(vipx2_clk_regs),
	.qch_regs		= vipx2_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(vipx2_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9610_cmu_vipx2_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &vipx2_cmu_info);
}

CLK_OF_DECLARE(exynos9610_cmu_vipx2, "samsung,exynos9610-cmu-vipx2",
	       exynos9610_cmu_vipx2_init);

