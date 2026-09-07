// SPDX-License-Identifier: GPL-2.0-only
/*
 * Common Clock Framework support for Exynos9810 SoC.
 *
 * Register data mechanically extracted from the downstream CMUCAL tables
 * (LineageOS android_kernel_samsung_universal9810, lineage-17.1,
 * drivers/soc/samsung/cal-if/exynos9810/{cmucal-sfr,cmucal-node,cmucal-qch}.c),
 * covering every CMU domain on the SoC. Cross-checked against a second
 * independent downstream tree (universal9810/android_kernel_samsung_exynos9810,
 * 4.9.312) for the QCH register layout and PERIS/PERIC0/PERIC1 offsets.
 *
 * NONE of this has been verified against real hardware yet (no clk_summary or
 * register read-back from a running Exynos9810 device). Gate/mux/div bit
 * positions for PERIS/PERIC0/PERIC1 are corroborated across two independent
 * downstream source trees agreeing exactly; the remaining domains rely on this
 * single LineageOS tree only. Gate flags (CLK_IS_CRITICAL vs 0) are a
 * conservative first pass: only CMU self-access PCLKs and the GIC clock are
 * marked critical, matching the pattern used for Exynos8895/990 in this tree.
 *
 * PLL types pll_1016x/1018x/1019x/1050x were added to clk-pll.c/clk-pll.h in a
 * companion change after confirming their P/M/S/ENABLE/STABLE bit layout is
 * identical to the already-supported pll_1017x on every instance checked.
 * PLL rate tables for CPUCL0/CPUCL1/G3D are genuine multi-point downstream
 * DVFS tables (5 operating points each); all other PLLs only had a single
 * downstream entry (they run at one fixed rate). The PLL_35XX_RATE() macro's
 * build-time PLL_VALID_RATE check independently re-derives each rate from its
 * m/p/s divider and fails the build if it doesn't match what's written here --
 * this caught nothing during generation, i.e. every extracted rate is at least
 * internally consistent with the PLL math, though still not hardware-verified.
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/samsung,exynos9810.h>

#include "clk.h"
#include "clk-pll.h"
#include "clk-exynos-arm64.h"

/* NOTE: Must be equal to the last clock ID for this CMU, increased by one. */
#define CLKS_NR_APM	(CLK_CLK_DLL_DCO + 1)
#define CLKS_NR_AUD	(CLK_CLKIO_AUD_UAIF3 + 1)
#define CLKS_NR_BUS1	(CLK_GOUT_BUS1_XIU_D_BUS1_ACLK + 1)
#define CLKS_NR_BUSC	(CLK_GOUT_BUSC_HPM_BUSC_hpm_targetclk_c + 1)
#define CLKS_NR_CHUB	(CLK_RTCCLK_CHUB + 1)
#define CLKS_NR_CMGP	(CLK_GOUT_CMGP_SYSREG_CMGP2PMU_CHUB_PCLK + 1)
#define CLKS_NR_CMU	(CLK_RTCCLK_VTS + 1)
#define CLKS_NR_CORE	(CLK_GOUT_CORE_LHS_AXI_P_APM_I_CLK + 1)
#define CLKS_NR_CPUCL0	(CLK_ACLKP_OUT + 1)
#define CLKS_NR_CPUCL1	(CLK_GOUT_CPUCL1_HPM_CPUCL1_2_hpm_targetclk_c + 1)
#define CLKS_NR_DCF	(CLK_DOUT_DCF_BUSP + 1)
#define CLKS_NR_DCPOST	(CLK_GOUT_DCPOST_IS_DCPOST_AXI2APB_DCPOST_ACLK + 1)
#define CLKS_NR_DCRD	(CLK_DOUT_DCRD_BUSD_HALF + 1)
#define CLKS_NR_DPU	(CLK_GOUT_DPU_AD_APB_DPU_DMA_PGEN_PCLKS + 1)
#define CLKS_NR_DSPM	(CLK_GOUT_DSPM_SCORE_MASTER_i_CLK + 1)
#define CLKS_NR_DSPS	(CLK_GOUT_DSPS_RSTnSYNC_CLK_DSPS_BUSP_CLK + 1)
#define CLKS_NR_FSYS0	(CLK_GOUT_FSYS0_DP_LINK_I_DP_GTC_CLK + 1)
#define CLKS_NR_FSYS1	(CLK_GOUT_FSYS1_PCIE_IA_GEN3_i_CLK + 1)
#define CLKS_NR_G2D	(CLK_DOUT_G2D_BUSP + 1)
#define CLKS_NR_G3D	(CLK_GOUT_G3D_GRAY2BIN_G3D_CLK + 1)
#define CLKS_NR_ISPHQ	(CLK_DOUT_ISPHQ_BUSP + 1)
#define CLKS_NR_ISPLP	(CLK_DOUT_ISPLP_BUSP + 1)
#define CLKS_NR_ISPPRE	(CLK_DOUT_ISPPRE_BUSP + 1)
#define CLKS_NR_IVA	(CLK_GOUT_IVA_IVA_dap_clk + 1)
#define CLKS_NR_MFC	(CLK_DOUT_MFC_BUSP + 1)
#define CLKS_NR_MIF	(CLK_GOUT_MIF_HPM_MIF_hpm_targetclk_c + 1)
#define CLKS_NR_PERIC0	(CLK_GOUT_PERIC0_USI14_I2C_IPCLK + 1)
#define CLKS_NR_PERIC1	(CLK_GOUT_PERIC1_RSTnSYNC_CLK_PERIC1_USI11_I2C_CLK + 1)
#define CLKS_NR_PERIS	(CLK_GOUT_PERIS_OTP_CON_TOP_PCLK + 1)
#define CLKS_NR_S2D	(CLK_GOUT_S2D_RSTnSYNC_CLK_S2D_CORE_CLK + 1)
#define CLKS_NR_VTS	(CLK_CLK_RCO_VTS + 1)

/* ---- CMU_APM ---------------------------------------------------------*/

/* Register Offset definitions for CMU_APM (0x14000000) */
#define MUX_CLKCMU_APM_BUS_USER			0x0100
#define MUX_DLL_USER			0x0120
#define MUX_CLK_APM_BUS			0x1000
#define CLKCMU_APM_DLL_CMGP			0x1800
#define CLKCMU_APM_DLL_VTS			0x1804
#define DIV_CLK_APM_BUS			0x1808
#define CLKCMU_APM_DLL_CHUB			0x2000
#define CLK_BLK_APM_UID_APM_CMU_APM_IPCLKPORT_PCLK	0x2004
#define GATE_CLKCMU_APM_DLL_CMGP			0x2010
#define GATE_CLKCMU_APM_DLL_VTS			0x2014
#define GOUT_BLK_APM_UID_APBIF_GPIO_ALIVE_IPCLKPORT_PCLK	0x2018
#define GOUT_BLK_APM_UID_APBIF_PMU_ALIVE_IPCLKPORT_PCLK	0x201c
#define GOUT_BLK_APM_UID_APBIF_RTC_IPCLKPORT_PCLK	0x2020
#define GOUT_BLK_APM_UID_APBIF_TOP_RTC_IPCLKPORT_PCLK	0x2024
#define GOUT_BLK_APM_UID_GREBEINTEGRATION_IPCLKPORT_HCLK	0x2028
#define GOUT_BLK_APM_UID_INTMEM_IPCLKPORT_ACLK			0x202c
#define GOUT_BLK_APM_UID_INTMEM_IPCLKPORT_PCLK			0x2030
#define GOUT_BLK_APM_UID_LHM_AXI_P_APM_CHUB_IPCLKPORT_I_CLK	0x2034
#define GOUT_BLK_APM_UID_LHM_AXI_P_APM_CP_IPCLKPORT_I_CLK	0x2038
#define GOUT_BLK_APM_UID_LHM_AXI_P_APM_GNSS_IPCLKPORT_I_CLK	0x203c
#define GOUT_BLK_APM_UID_LHM_AXI_P_APM_IPCLKPORT_I_CLK	0x2040
#define GOUT_BLK_APM_UID_LHS_AXI_D_APM_IPCLKPORT_I_CLK	0x2044
#define GOUT_BLK_APM_UID_LHS_AXI_G_SCAN2DRAM_IPCLKPORT_I_CLK	0x2048
#define GOUT_BLK_APM_UID_LHS_AXI_LP_CHUB_IPCLKPORT_I_CLK	0x204c
#define GOUT_BLK_APM_UID_LHS_AXI_P_APM2CMGP_IPCLKPORT_I_CLK	0x2050
#define GOUT_BLK_APM_UID_MAILBOX_AP2CHUB_IPCLKPORT_PCLK	0x2054
#define GOUT_BLK_APM_UID_MAILBOX_AP2CP_IPCLKPORT_PCLK	0x2058
#define GOUT_BLK_APM_UID_MAILBOX_AP2CP_S_IPCLKPORT_PCLK	0x205c
#define GOUT_BLK_APM_UID_MAILBOX_AP2GNSS_IPCLKPORT_PCLK	0x2060
#define GOUT_BLK_APM_UID_MAILBOX_AP2VTS_IPCLKPORT_PCLK	0x2064
#define GOUT_BLK_APM_UID_MAILBOX_APM2AP_IPCLKPORT_PCLK	0x2068
#define GOUT_BLK_APM_UID_MAILBOX_APM2CHUB_IPCLKPORT_PCLK	0x206c
#define GOUT_BLK_APM_UID_MAILBOX_APM2CP_IPCLKPORT_PCLK	0x2070
#define GOUT_BLK_APM_UID_MAILBOX_APM2GNSS_IPCLKPORT_PCLK	0x2074
#define GOUT_BLK_APM_UID_MAILBOX_CHUB2CP_IPCLKPORT_PCLK	0x2078
#define GOUT_BLK_APM_UID_MAILBOX_GNSS2CHUB_IPCLKPORT_PCLK	0x207c
#define GOUT_BLK_APM_UID_MAILBOX_GNSS2CP_IPCLKPORT_PCLK	0x2080
#define GOUT_BLK_APM_UID_PEM_IPCLKPORT_I_CLK			0x2084
#define GOUT_BLK_APM_UID_PGEN_APM_IPCLKPORT_CLK			0x2088
#define GOUT_BLK_APM_UID_PMU_INTR_GEN_IPCLKPORT_PCLK	0x208c
#define GOUT_BLK_APM_UID_RSTnSYNC_CLK_APM_BUS_IPCLKPORT_CLK	0x2090
#define GOUT_BLK_APM_UID_SPEEDY_APM_IPCLKPORT_PCLK	0x2094
#define GOUT_BLK_APM_UID_SPEEDY_SUB_APM_IPCLKPORT_PCLK	0x2098
#define GOUT_BLK_APM_UID_SYSREG_APM_IPCLKPORT_PCLK	0x209c
#define GOUT_BLK_APM_UID_WDT_APM_IPCLKPORT_PCLK			0x20a0
#define GOUT_BLK_APM_UID_XIU_DP_APM_IPCLKPORT_ACLK	0x20a4

static const unsigned long apm_clk_regs[] __initconst = {
	MUX_CLKCMU_APM_BUS_USER,
	MUX_DLL_USER,
	MUX_CLK_APM_BUS,
	CLKCMU_APM_DLL_CMGP,
	CLKCMU_APM_DLL_VTS,
	DIV_CLK_APM_BUS,
	CLKCMU_APM_DLL_CHUB,
	CLK_BLK_APM_UID_APM_CMU_APM_IPCLKPORT_PCLK,
	GATE_CLKCMU_APM_DLL_CMGP,
	GATE_CLKCMU_APM_DLL_VTS,
	GOUT_BLK_APM_UID_APBIF_GPIO_ALIVE_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_APBIF_PMU_ALIVE_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_APBIF_RTC_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_APBIF_TOP_RTC_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_GREBEINTEGRATION_IPCLKPORT_HCLK,
	GOUT_BLK_APM_UID_INTMEM_IPCLKPORT_ACLK,
	GOUT_BLK_APM_UID_INTMEM_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_LHM_AXI_P_APM_CHUB_IPCLKPORT_I_CLK,
	GOUT_BLK_APM_UID_LHM_AXI_P_APM_CP_IPCLKPORT_I_CLK,
	GOUT_BLK_APM_UID_LHM_AXI_P_APM_GNSS_IPCLKPORT_I_CLK,
	GOUT_BLK_APM_UID_LHM_AXI_P_APM_IPCLKPORT_I_CLK,
	GOUT_BLK_APM_UID_LHS_AXI_D_APM_IPCLKPORT_I_CLK,
	GOUT_BLK_APM_UID_LHS_AXI_G_SCAN2DRAM_IPCLKPORT_I_CLK,
	GOUT_BLK_APM_UID_LHS_AXI_LP_CHUB_IPCLKPORT_I_CLK,
	GOUT_BLK_APM_UID_LHS_AXI_P_APM2CMGP_IPCLKPORT_I_CLK,
	GOUT_BLK_APM_UID_MAILBOX_AP2CHUB_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_AP2CP_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_AP2CP_S_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_AP2GNSS_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_AP2VTS_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_APM2AP_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_APM2CHUB_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_APM2CP_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_APM2GNSS_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_CHUB2CP_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_GNSS2CHUB_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_MAILBOX_GNSS2CP_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_PEM_IPCLKPORT_I_CLK,
	GOUT_BLK_APM_UID_PGEN_APM_IPCLKPORT_CLK,
	GOUT_BLK_APM_UID_PMU_INTR_GEN_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_RSTnSYNC_CLK_APM_BUS_IPCLKPORT_CLK,
	GOUT_BLK_APM_UID_SPEEDY_APM_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_SPEEDY_SUB_APM_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_SYSREG_APM_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_WDT_APM_IPCLKPORT_PCLK,
	GOUT_BLK_APM_UID_XIU_DP_APM_IPCLKPORT_ACLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long apm_qch_regs[] __initconst = {
	0x3020,	/* APBIF_GPIO_ALIVE_QCH */
	0x3024,	/* APBIF_PMU_ALIVE_QCH */
	0x3028,	/* APBIF_RTC_QCH */
	0x302c,	/* APBIF_TOP_RTC_QCH */
	0x3030,	/* APM_CMU_APM_QCH */
	0x3034,	/* GREBEINTEGRATION_QCH_DBG */
	0x3038,	/* GREBEINTEGRATION_QCH_GREBE */
	0x303c,	/* INTMEM_QCH */
	0x3040,	/* LHM_AXI_P_APM_CHUB_QCH */
	0x3044,	/* LHM_AXI_P_APM_CP_QCH */
	0x3048,	/* LHM_AXI_P_APM_GNSS_QCH */
	0x304c,	/* LHM_AXI_P_APM_QCH */
	0x3050,	/* LHS_AXI_D_APM_QCH */
	0x3054,	/* LHS_AXI_G_SCAN2DRAM_QCH */
	0x3058,	/* LHS_AXI_LP_CHUB_QCH */
	0x305c,	/* LHS_AXI_P_APM2CMGP_QCH */
	0x3060,	/* MAILBOX_AP2CHUB_QCH */
	0x3064,	/* MAILBOX_AP2CP_QCH */
	0x3068,	/* MAILBOX_AP2CP_S_QCH */
	0x306c,	/* MAILBOX_AP2GNSS_QCH */
	0x3070,	/* MAILBOX_AP2VTS_QCH */
	0x3074,	/* MAILBOX_APM2AP_QCH */
	0x3078,	/* MAILBOX_APM2CHUB_QCH */
	0x307c,	/* MAILBOX_APM2CP_QCH */
	0x3080,	/* MAILBOX_APM2GNSS_QCH */
	0x3084,	/* MAILBOX_CHUB2CP_QCH */
	0x3088,	/* MAILBOX_GNSS2CHUB_QCH */
	0x308c,	/* MAILBOX_GNSS2CP_QCH */
	0x3090,	/* PEM_QCH */
	0x3094,	/* PGEN_APM_QCH */
	0x3098,	/* PMU_INTR_GEN_QCH */
	0x309c,	/* RSTNSYNC_CLK_APM_BUS_QCH */
	0x30a0,	/* SPEEDY_APM_QCH */
	0x30a4,	/* SPEEDY_SUB_APM_QCH */
	0x30a8,	/* SYSREG_APM_QCH */
	0x30ac,	/* WDT_APM_QCH */
};

static const struct samsung_fixed_rate_clock apm_fixed_clks[] __initconst = {
	FRATE(CLK_CLK_DLL_DCO, "clk_dll_dco", NULL, 0, 400000000),
};

/* List of parent clocks for Muxes in CMU_APM */
PNAME(mout_apm_bus_p) = { "mout_cmu_apm_bus_user", "mout_dll_user" };
PNAME(mout_cmu_apm_bus_user_p) = { "oscclk", "dout_clkcmu_apm_bus" };
PNAME(mout_dll_user_p) = { "oscclk", "clk_dll_dco" };

static const struct samsung_mux_clock apm_mux_clks[] __initconst = {
	MUX(CLK_MOUT_APM_BUS, "mout_apm_bus", mout_apm_bus_p,
	    MUX_CLK_APM_BUS, 0, 1),
	MUX(CLK_MOUT_CMU_APM_BUS_USER, "mout_cmu_apm_bus_user", mout_cmu_apm_bus_user_p,
	    MUX_CLKCMU_APM_BUS_USER, 4, 1),
	MUX(CLK_MOUT_DLL_USER, "mout_dll_user", mout_dll_user_p,
	    MUX_DLL_USER, 4, 1),
};

static const struct samsung_div_clock apm_div_clks[] __initconst = {
	DIV(CLK_DOUT_CLKCMU_APM_DLL_CMGP, "dout_clkcmu_apm_dll_cmgp", "gout_clkcmu_apm_dll_cmgp",
	    CLKCMU_APM_DLL_CMGP, 0, 3),
	DIV(CLK_DOUT_CLKCMU_APM_DLL_VTS, "dout_clkcmu_apm_dll_vts", "gout_clkcmu_apm_dll_vts",
	    CLKCMU_APM_DLL_VTS, 0, 3),
	DIV(CLK_DOUT_APM_BUS, "dout_apm_bus", "mout_apm_bus",
	    DIV_CLK_APM_BUS, 0, 3),
};

static const struct samsung_gate_clock apm_gate_clks[] __initconst = {
	GATE(CLK_GOUT_APM_LHS_AXI_D_APM_I_CLK, "gout_apm_lhs_axi_d_apm_i_clk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_LHS_AXI_D_APM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_LHM_AXI_P_APM_I_CLK, "gout_apm_lhm_axi_p_apm_i_clk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_LHM_AXI_P_APM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_RSTnSYNC_CLK_APM_BUS_CLK, "gout_apm_rstnsync_clk_apm_bus_clk",
	     "dout_apm_bus",
	     GOUT_BLK_APM_UID_RSTnSYNC_CLK_APM_BUS_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_WDT_APM_PCLK, "gout_apm_wdt_apm_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_WDT_APM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_SYSREG_APM_PCLK, "gout_apm_sysreg_apm_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_SYSREG_APM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_APM2AP_PCLK, "gout_apm_mailbox_apm2ap_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_APM2AP_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_APM2CP_PCLK, "gout_apm_mailbox_apm2cp_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_APM2CP_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_APM2GNSS_PCLK, "gout_apm_mailbox_apm2gnss_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_APM2GNSS_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_APM_DLL_CHUB, "gout_clkcmu_apm_dll_chub", "mout_dll_user",
	     CLKCMU_APM_DLL_CHUB, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_APM_DLL_VTS, "gout_clkcmu_apm_dll_vts", "mout_dll_user",
	     GATE_CLKCMU_APM_DLL_VTS, 21, 0, 0),
	GATE(CLK_GOUT_APM_APBIF_PMU_ALIVE_PCLK, "gout_apm_apbif_pmu_alive_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_APBIF_PMU_ALIVE_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_INTMEM_ACLK, "gout_apm_intmem_aclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_INTMEM_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_INTMEM_PCLK, "gout_apm_intmem_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_INTMEM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_LHM_AXI_P_APM_CHUB_I_CLK, "gout_apm_lhm_axi_p_apm_chub_i_clk",
	     "dout_apm_bus",
	     GOUT_BLK_APM_UID_LHM_AXI_P_APM_CHUB_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_LHM_AXI_P_APM_GNSS_I_CLK, "gout_apm_lhm_axi_p_apm_gnss_i_clk",
	     "dout_apm_bus",
	     GOUT_BLK_APM_UID_LHM_AXI_P_APM_GNSS_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_LHM_AXI_P_APM_CP_I_CLK, "gout_apm_lhm_axi_p_apm_cp_i_clk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_LHM_AXI_P_APM_CP_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_LHS_AXI_G_SCAN2DRAM_I_CLK, "gout_apm_lhs_axi_g_scan2dram_i_clk",
	     "dout_apm_bus",
	     GOUT_BLK_APM_UID_LHS_AXI_G_SCAN2DRAM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_APM2CHUB_PCLK, "gout_apm_mailbox_apm2chub_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_APM2CHUB_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_CHUB2CP_PCLK, "gout_apm_mailbox_chub2cp_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_CHUB2CP_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_GNSS2CHUB_PCLK, "gout_apm_mailbox_gnss2chub_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_GNSS2CHUB_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_GNSS2CP_PCLK, "gout_apm_mailbox_gnss2cp_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_GNSS2CP_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_PMU_INTR_GEN_PCLK, "gout_apm_pmu_intr_gen_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_PMU_INTR_GEN_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_PEM_I_CLK, "gout_apm_pem_i_clk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_PEM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_SPEEDY_APM_PCLK, "gout_apm_speedy_apm_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_SPEEDY_APM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_XIU_DP_APM_ACLK, "gout_apm_xiu_dp_apm_aclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_XIU_DP_APM_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_APM_CMU_APM_PCLK, "gout_apm_apm_cmu_apm_pclk", "dout_apm_bus",
	     CLK_BLK_APM_UID_APM_CMU_APM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_LHS_AXI_P_APM2CMGP_I_CLK, "gout_apm_lhs_axi_p_apm2cmgp_i_clk",
	     "dout_apm_bus",
	     GOUT_BLK_APM_UID_LHS_AXI_P_APM2CMGP_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_APM_DLL_CMGP, "gout_clkcmu_apm_dll_cmgp", "mout_dll_user",
	     GATE_CLKCMU_APM_DLL_CMGP, 21, 0, 0),
	GATE(CLK_GOUT_APM_PGEN_APM_CLK, "gout_apm_pgen_apm_clk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_PGEN_APM_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_LHS_AXI_LP_CHUB_I_CLK, "gout_apm_lhs_axi_lp_chub_i_clk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_LHS_AXI_LP_CHUB_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_GREBEINTEGRATION_HCLK, "gout_apm_grebeintegration_hclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_GREBEINTEGRATION_IPCLKPORT_HCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_APBIF_GPIO_ALIVE_PCLK, "gout_apm_apbif_gpio_alive_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_APBIF_GPIO_ALIVE_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_APBIF_RTC_PCLK, "gout_apm_apbif_rtc_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_APBIF_RTC_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_APBIF_TOP_RTC_PCLK, "gout_apm_apbif_top_rtc_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_APBIF_TOP_RTC_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_AP2CP_PCLK, "gout_apm_mailbox_ap2cp_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_AP2CP_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_AP2CP_S_PCLK, "gout_apm_mailbox_ap2cp_s_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_AP2CP_S_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_AP2GNSS_PCLK, "gout_apm_mailbox_ap2gnss_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_AP2GNSS_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_AP2CHUB_PCLK, "gout_apm_mailbox_ap2chub_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_AP2CHUB_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_AP2VTS_PCLK, "gout_apm_mailbox_ap2vts_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_MAILBOX_AP2VTS_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_SPEEDY_SUB_APM_PCLK, "gout_apm_speedy_sub_apm_pclk", "dout_apm_bus",
	     GOUT_BLK_APM_UID_SPEEDY_SUB_APM_IPCLKPORT_PCLK, 21, 0, 0),
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

static void __init exynos9810_cmu_apm_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &apm_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_apm, "samsung,exynos9810-cmu-apm",
	       exynos9810_cmu_apm_init);

/* ---- CMU_AUD ---------------------------------------------------------*/

/* Register Offset definitions for CMU_AUD (0x17c00000) */
#define PLL_LOCKTIME_PLL_AUD_PLL_LOCK_TIME			0x0000
#define MUX_CLKCMU_AUD_CPU_USER			0x0100
#define PLL_CON0_PLL_AUD_ENABLE			0x0120
#define MUX_CLK_AUD_CPU			0x1000
#define MUX_CLK_AUD_UAIF0			0x1004
#define MUX_CLK_AUD_UAIF1			0x1008
#define MUX_CLK_AUD_UAIF2			0x100c
#define MUX_CLK_AUD_UAIF3			0x1010
#define MUX_HCHGEN_CLK_AUD_CPU			0x1014
#define DIV_CLK_AUD_AUDIF			0x1800
#define DIV_CLK_AUD_BUS			0x1804
#define DIV_CLK_AUD_BUSP			0x1808
#define DIV_CLK_AUD_CPU_ACLK			0x180c
#define DIV_CLK_AUD_CPU_ATCLK			0x1810
#define DIV_CLK_AUD_CPU_PCLKDBG			0x1814
#define DIV_CLK_AUD_DMIC			0x1818
#define DIV_CLK_AUD_DSIF			0x181c
#define DIV_CLK_AUD_PLL			0x1820
#define DIV_CLK_AUD_UAIF0			0x1824
#define DIV_CLK_AUD_UAIF1			0x1828
#define DIV_CLK_AUD_UAIF2			0x182c
#define DIV_CLK_AUD_UAIF3			0x1830
#define CLK_BLK_AUD_UID_DFTMUX_AUD_IPCLKPORT_AUD_CODEC_MCLK	0x2004
#define CLK_BLK_AUD_UID_DMIC_IPCLKPORT_CLK			0x2008
#define GOUT_BLK_AUD_UID_ABOX_DAP_IPCLKPORT_dapclk	0x2010
#define GOUT_BLK_AUD_UID_ABOX_IPCLKPORT_BCLK_DSIF	0x2018
#define GOUT_BLK_AUD_UID_ABOX_IPCLKPORT_BCLK_UAIF0	0x201c
#define GOUT_BLK_AUD_UID_ABOX_IPCLKPORT_BCLK_UAIF1	0x2020
#define GOUT_BLK_AUD_UID_ABOX_IPCLKPORT_BCLK_UAIF2	0x2024
#define GOUT_BLK_AUD_UID_ABOX_IPCLKPORT_BCLK_UAIF3	0x2028
#define GOUT_BLK_AUD_UID_ABOX_IPCLKPORT_CCLK_ATB	0x2030
#define GOUT_BLK_AUD_UID_AD_APB_SYSMMU_AUD_IPCLKPORT_PCLKS	0x203c
#define GOUT_BLK_AUD_UID_AXI2APB_AUD_IPCLKPORT_ACLK	0x2040
#define GOUT_BLK_AUD_UID_LHM_AXI_P_AUD_IPCLKPORT_I_CLK	0x2054
#define GOUT_BLK_AUD_UID_LHS_ATB_AUD_IPCLKPORT_I_CLK	0x2058
#define GOUT_BLK_AUD_UID_PERI_AXI_ASB_IPCLKPORT_ACLKS	0x2064
#define GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_BUSP_IPCLKPORT_CLK	0x2074
#define GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_CPU_ATCLK_IPCLKPORT_CLK	0x2080
#define GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_CPU_PCLKDBG_IPCLKPORT_CLK	0x2088
#define GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_DSIF_IPCLKPORT_CLK	0x208c
#define GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_UAIF0_IPCLKPORT_CLK	0x2090
#define GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_UAIF1_IPCLKPORT_CLK	0x2094
#define GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_UAIF2_IPCLKPORT_CLK	0x2098
#define GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_UAIF3_IPCLKPORT_CLK	0x209c
#define GOUT_BLK_AUD_UID_XIU_P_AUD_IPCLKPORT_ACLK	0x20b4

static const unsigned long aud_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_AUD_PLL_LOCK_TIME,
	MUX_CLKCMU_AUD_CPU_USER,
	PLL_CON0_PLL_AUD_ENABLE,
	MUX_CLK_AUD_CPU,
	MUX_CLK_AUD_UAIF0,
	MUX_CLK_AUD_UAIF1,
	MUX_CLK_AUD_UAIF2,
	MUX_CLK_AUD_UAIF3,
	MUX_HCHGEN_CLK_AUD_CPU,
	DIV_CLK_AUD_AUDIF,
	DIV_CLK_AUD_BUS,
	DIV_CLK_AUD_BUSP,
	DIV_CLK_AUD_CPU_ACLK,
	DIV_CLK_AUD_CPU_ATCLK,
	DIV_CLK_AUD_CPU_PCLKDBG,
	DIV_CLK_AUD_DMIC,
	DIV_CLK_AUD_DSIF,
	DIV_CLK_AUD_PLL,
	DIV_CLK_AUD_UAIF0,
	DIV_CLK_AUD_UAIF1,
	DIV_CLK_AUD_UAIF2,
	DIV_CLK_AUD_UAIF3,
	CLK_BLK_AUD_UID_DFTMUX_AUD_IPCLKPORT_AUD_CODEC_MCLK,
	CLK_BLK_AUD_UID_DMIC_IPCLKPORT_CLK,
	GOUT_BLK_AUD_UID_ABOX_DAP_IPCLKPORT_dapclk,
	GOUT_BLK_AUD_UID_ABOX_IPCLKPORT_BCLK_DSIF,
	GOUT_BLK_AUD_UID_ABOX_IPCLKPORT_BCLK_UAIF0,
	GOUT_BLK_AUD_UID_ABOX_IPCLKPORT_BCLK_UAIF1,
	GOUT_BLK_AUD_UID_ABOX_IPCLKPORT_BCLK_UAIF2,
	GOUT_BLK_AUD_UID_ABOX_IPCLKPORT_BCLK_UAIF3,
	GOUT_BLK_AUD_UID_ABOX_IPCLKPORT_CCLK_ATB,
	GOUT_BLK_AUD_UID_AD_APB_SYSMMU_AUD_IPCLKPORT_PCLKS,
	GOUT_BLK_AUD_UID_AXI2APB_AUD_IPCLKPORT_ACLK,
	GOUT_BLK_AUD_UID_LHM_AXI_P_AUD_IPCLKPORT_I_CLK,
	GOUT_BLK_AUD_UID_LHS_ATB_AUD_IPCLKPORT_I_CLK,
	GOUT_BLK_AUD_UID_PERI_AXI_ASB_IPCLKPORT_ACLKS,
	GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_BUSP_IPCLKPORT_CLK,
	GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_CPU_ATCLK_IPCLKPORT_CLK,
	GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_CPU_PCLKDBG_IPCLKPORT_CLK,
	GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_DSIF_IPCLKPORT_CLK,
	GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_UAIF0_IPCLKPORT_CLK,
	GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_UAIF1_IPCLKPORT_CLK,
	GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_UAIF2_IPCLKPORT_CLK,
	GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_UAIF3_IPCLKPORT_CLK,
	GOUT_BLK_AUD_UID_XIU_P_AUD_IPCLKPORT_ACLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long aud_qch_regs[] __initconst = {
	0x3000,	/* ABOX_QCH_DUMMY */
	0x3004,	/* DFTMUX_AUD_QCH */
	0x3008,	/* DMIC_QCH */
	0x3024,	/* ABOX_QCH_ACLK */
	0x3028,	/* ABOX_QCH_BCLK0 */
	0x302c,	/* ABOX_QCH_BCLK1 */
	0x3030,	/* ABOX_QCH_BCLK2 */
	0x3034,	/* ABOX_QCH_BCLK3 */
	0x3038,	/* ABOX_QCH_BCLK_DSIF */
	0x303c,	/* ABOX_QCH_CCLK_ASB */
	0x3040,	/* ABOX_QCH_CCLK_ATB */
	0x3044,	/* AUD_CMU_AUD_QCH */
	0x3048,	/* BTM_AUD_QCH */
	0x304c,	/* GPIO_AUD_QCH */
	0x3050,	/* LHM_AXI_P_AUD_QCH */
	0x3054,	/* LHS_ATB_AUD_QCH */
	0x3058,	/* LHS_AXI_D_AUD_QCH */
	0x305c,	/* PPMU_AUD_QCH */
	0x3060,	/* SYSMMU_AUD_QCH */
	0x3064,	/* SYSREG_AUD_QCH */
	0x3068,	/* TREX_AUD_QCH */
	0x306c,	/* WDT_AUD_QCH */
};

static const struct samsung_fixed_rate_clock aud_fixed_clks[] __initconst = {
	FRATE(CLK_CLKIO_AUD_UAIF0, "clkio_aud_uaif0", NULL, 0, 10000000),
	FRATE(CLK_CLKIO_AUD_UAIF1, "clkio_aud_uaif1", NULL, 0, 10000000),
	FRATE(CLK_CLKIO_AUD_UAIF2, "clkio_aud_uaif2", NULL, 0, 10000000),
	FRATE(CLK_CLKIO_AUD_UAIF3, "clkio_aud_uaif3", NULL, 0, 100000000),
};

static const struct samsung_pll_clock aud_pll_clks[] __initconst = {
	PLL(pll_1031x, CLK_FOUT_AUD, "fout_aud", "oscclk",
	    PLL_LOCKTIME_PLL_AUD_PLL_LOCK_TIME, PLL_CON0_PLL_AUD_ENABLE, NULL),
};

/* List of parent clocks for Muxes in CMU_AUD */
PNAME(mout_aud_uaif3_p) = { "dout_aud_uaif3", "clkio_aud_uaif3" };
PNAME(mout_aud_uaif2_p) = { "dout_aud_uaif2", "clkio_aud_uaif2" };
PNAME(mout_aud_uaif1_p) = { "dout_aud_uaif1", "clkio_aud_uaif1" };
PNAME(mout_aud_uaif0_p) = { "dout_aud_uaif0", "clkio_aud_uaif0" };
PNAME(mout_aud_cpu_p) = { "dout_aud_pll", "mout_cmu_aud_cpu_user" };
PNAME(mout_hchgen_clk_aud_cpu_p) = { "mout_aud_cpu", "oscclk" };
PNAME(mout_cmu_aud_cpu_user_p) = { "oscclk", "dout_clkcmu_aud_cpu" };

static const struct samsung_mux_clock aud_mux_clks[] __initconst = {
	MUX(CLK_MOUT_AUD_UAIF3, "mout_aud_uaif3", mout_aud_uaif3_p,
	    MUX_CLK_AUD_UAIF3, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF2, "mout_aud_uaif2", mout_aud_uaif2_p,
	    MUX_CLK_AUD_UAIF2, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF1, "mout_aud_uaif1", mout_aud_uaif1_p,
	    MUX_CLK_AUD_UAIF1, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF0, "mout_aud_uaif0", mout_aud_uaif0_p,
	    MUX_CLK_AUD_UAIF0, 0, 1),
	MUX(CLK_MOUT_AUD_CPU, "mout_aud_cpu", mout_aud_cpu_p,
	    MUX_CLK_AUD_CPU, 0, 1),
	MUX(CLK_MOUT_HCHGEN_CLK_AUD_CPU, "mout_hchgen_clk_aud_cpu", mout_hchgen_clk_aud_cpu_p,
	    MUX_HCHGEN_CLK_AUD_CPU, 0, 5),
	MUX(CLK_MOUT_CMU_AUD_CPU_USER, "mout_cmu_aud_cpu_user", mout_cmu_aud_cpu_user_p,
	    MUX_CLKCMU_AUD_CPU_USER, 4, 1),
};

static const struct samsung_div_clock aud_div_clks[] __initconst = {
	DIV(CLK_DOUT_AUD_PLL, "dout_aud_pll", "fout_aud",
	    DIV_CLK_AUD_PLL, 0, 4),
	DIV(CLK_DOUT_AUD_AUDIF, "dout_aud_audif", "fout_aud",
	    DIV_CLK_AUD_AUDIF, 0, 9),
	DIV(CLK_DOUT_AUD_CPU_ATCLK, "dout_aud_cpu_atclk", "mout_hchgen_clk_aud_cpu",
	    DIV_CLK_AUD_CPU_ATCLK, 0, 3),
	DIV(CLK_DOUT_AUD_CPU_PCLKDBG, "dout_aud_cpu_pclkdbg", "mout_hchgen_clk_aud_cpu",
	    DIV_CLK_AUD_CPU_PCLKDBG, 0, 3),
	DIV(CLK_DOUT_AUD_DSIF, "dout_aud_dsif", "dout_aud_audif",
	    DIV_CLK_AUD_DSIF, 0, 5),
	DIV(CLK_DOUT_AUD_UAIF0, "dout_aud_uaif0", "dout_aud_audif",
	    DIV_CLK_AUD_UAIF0, 0, 9),
	DIV(CLK_DOUT_AUD_UAIF1, "dout_aud_uaif1", "dout_aud_audif",
	    DIV_CLK_AUD_UAIF1, 0, 9),
	DIV(CLK_DOUT_AUD_UAIF2, "dout_aud_uaif2", "dout_aud_audif",
	    DIV_CLK_AUD_UAIF2, 0, 9),
	DIV(CLK_DOUT_AUD_UAIF3, "dout_aud_uaif3", "dout_aud_audif",
	    DIV_CLK_AUD_UAIF3, 0, 9),
	DIV(CLK_DOUT_AUD_CPU_ACLK, "dout_aud_cpu_aclk", "mout_hchgen_clk_aud_cpu",
	    DIV_CLK_AUD_CPU_ACLK, 0, 3),
	DIV(CLK_DOUT_AUD_BUS, "dout_aud_bus", "fout_aud",
	    DIV_CLK_AUD_BUS, 0, 3),
	DIV(CLK_DOUT_AUD_BUSP, "dout_aud_busp", "dout_aud_bus",
	    DIV_CLK_AUD_BUSP, 0, 2),
	DIV(CLK_DOUT_AUD_DMIC, "dout_aud_dmic", "dout_aud_dsif",
	    DIV_CLK_AUD_DMIC, 0, 2),
};

static const struct samsung_gate_clock aud_gate_clks[] __initconst = {
	GATE(CLK_GOUT_AUD_ABOX_BCLK_UAIF0, "gout_aud_abox_bclk_uaif0", "mout_aud_uaif0",
	     GOUT_BLK_AUD_UID_ABOX_IPCLKPORT_BCLK_UAIF0, 21, 0, 0),
	GATE(CLK_GOUT_AUD_ABOX_BCLK_UAIF1, "gout_aud_abox_bclk_uaif1", "mout_aud_uaif1",
	     GOUT_BLK_AUD_UID_ABOX_IPCLKPORT_BCLK_UAIF1, 21, 0, 0),
	GATE(CLK_GOUT_AUD_ABOX_BCLK_UAIF3, "gout_aud_abox_bclk_uaif3", "mout_aud_uaif3",
	     GOUT_BLK_AUD_UID_ABOX_IPCLKPORT_BCLK_UAIF3, 21, 0, 0),
	GATE(CLK_GOUT_AUD_ABOX_BCLK_DSIF, "gout_aud_abox_bclk_dsif", "dout_aud_dsif",
	     GOUT_BLK_AUD_UID_ABOX_IPCLKPORT_BCLK_DSIF, 21, 0, 0),
	GATE(CLK_GOUT_AUD_LHS_ATB_AUD_I_CLK, "gout_aud_lhs_atb_aud_i_clk", "dout_aud_cpu_atclk",
	     GOUT_BLK_AUD_UID_LHS_ATB_AUD_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_AUD_RSTnSYNC_CLK_AUD_CPU_ATCLK_CLK, "gout_aud_rstnsync_clk_aud_cpu_atclk_clk",
	     "dout_aud_cpu_atclk",
	     GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_CPU_ATCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_AUD_RSTnSYNC_CLK_AUD_CPU_PCLKDBG_CLK, "gout_aud_rstnsync_clk_aud_cpu_pclkdbg_clk",
	     "dout_aud_cpu_pclkdbg",
	     GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_CPU_PCLKDBG_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_AUD_RSTnSYNC_CLK_AUD_DSIF_CLK, "gout_aud_rstnsync_clk_aud_dsif_clk",
	     "dout_aud_dsif",
	     GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_DSIF_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_AUD_RSTnSYNC_CLK_AUD_UAIF0_CLK, "gout_aud_rstnsync_clk_aud_uaif0_clk",
	     "mout_aud_uaif0",
	     GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_UAIF0_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_AUD_RSTnSYNC_CLK_AUD_UAIF1_CLK, "gout_aud_rstnsync_clk_aud_uaif1_clk",
	     "mout_aud_uaif1",
	     GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_UAIF1_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_AUD_RSTnSYNC_CLK_AUD_UAIF2_CLK, "gout_aud_rstnsync_clk_aud_uaif2_clk",
	     "mout_aud_uaif2",
	     GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_UAIF2_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_AUD_RSTnSYNC_CLK_AUD_UAIF3_CLK, "gout_aud_rstnsync_clk_aud_uaif3_clk",
	     "mout_aud_uaif3",
	     GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_UAIF3_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_AUD_ABOX_BCLK_UAIF2, "gout_aud_abox_bclk_uaif2", "mout_aud_uaif2",
	     GOUT_BLK_AUD_UID_ABOX_IPCLKPORT_BCLK_UAIF2, 21, 0, 0),
	GATE(CLK_GOUT_AUD_LHM_AXI_P_AUD_I_CLK, "gout_aud_lhm_axi_p_aud_i_clk", "dout_aud_busp",
	     GOUT_BLK_AUD_UID_LHM_AXI_P_AUD_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_AUD_PERI_AXI_ASB_ACLKS, "gout_aud_peri_axi_asb_aclks", "dout_aud_busp",
	     GOUT_BLK_AUD_UID_PERI_AXI_ASB_IPCLKPORT_ACLKS, 21, 0, 0),
	GATE(CLK_GOUT_AUD_RSTnSYNC_CLK_AUD_BUSP_CLK, "gout_aud_rstnsync_clk_aud_busp_clk",
	     "dout_aud_busp",
	     GOUT_BLK_AUD_UID_RSTnSYNC_CLK_AUD_BUSP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_AUD_DMIC_CLK, "gout_aud_dmic_clk", "dout_aud_dmic",
	     CLK_BLK_AUD_UID_DMIC_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_AUD_ABOX_CCLK_ATB, "gout_aud_abox_cclk_atb", "dout_aud_cpu_atclk",
	     GOUT_BLK_AUD_UID_ABOX_IPCLKPORT_CCLK_ATB, 21, 0, 0),
	GATE(CLK_GOUT_AUD_ABOX_DAP_dapclk, "gout_aud_abox_dap_dapclk", "dout_aud_cpu_pclkdbg",
	     GOUT_BLK_AUD_UID_ABOX_DAP_IPCLKPORT_dapclk, 21, 0, 0),
	GATE(CLK_GOUT_AUD_DFTMUX_AUD_AUD_CODEC_MCLK, "gout_aud_dftmux_aud_aud_codec_mclk",
	     "dout_aud_audif",
	     CLK_BLK_AUD_UID_DFTMUX_AUD_IPCLKPORT_AUD_CODEC_MCLK, 21, 0, 0),
	GATE(CLK_GOUT_AUD_XIU_P_AUD_ACLK, "gout_aud_xiu_p_aud_aclk", "dout_aud_busp",
	     GOUT_BLK_AUD_UID_XIU_P_AUD_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_AUD_AD_APB_SYSMMU_AUD_PCLKS, "gout_aud_ad_apb_sysmmu_aud_pclks",
	     "dout_aud_busp",
	     GOUT_BLK_AUD_UID_AD_APB_SYSMMU_AUD_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_AUD_AXI2APB_AUD_ACLK, "gout_aud_axi2apb_aud_aclk", "dout_aud_busp",
	     GOUT_BLK_AUD_UID_AXI2APB_AUD_IPCLKPORT_ACLK, 21, 0, 0),
};

static const struct samsung_cmu_info aud_cmu_info __initconst = {
	.pll_clks		= aud_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(aud_pll_clks),
	.mux_clks		= aud_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(aud_mux_clks),
	.div_clks		= aud_div_clks,
	.nr_div_clks		= ARRAY_SIZE(aud_div_clks),
	.gate_clks		= aud_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(aud_gate_clks),
	.fixed_clks		= aud_fixed_clks,
	.nr_fixed_clks		= ARRAY_SIZE(aud_fixed_clks),
	.nr_clk_ids		= CLKS_NR_AUD,
	.clk_regs		= aud_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(aud_clk_regs),
	.qch_regs		= aud_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(aud_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_aud_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &aud_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_aud, "samsung,exynos9810-cmu-aud",
	       exynos9810_cmu_aud_init);

/* ---- CMU_BUS1 --------------------------------------------------------*/

/* Register Offset definitions for CMU_BUS1 (0x1a400000) */
#define MUX_CLKCMU_BUS1_BUS_USER			0x0100
#define CLK_BLK_BUS1_UID_BUS1_CMU_BUS1_IPCLKPORT_PCLK	0x2000
#define GOUT_BLK_BUS1_UID_AXI2APB_BUS1_IPCLKPORT_ACLK	0x2004
#define GOUT_BLK_BUS1_UID_AXI2APB_BUS1_TREX_IPCLKPORT_ACLK	0x2008
#define GOUT_BLK_BUS1_UID_BAAW_P_CHUB_IPCLKPORT_I_PCLK	0x200c
#define GOUT_BLK_BUS1_UID_BAAW_P_GNSS_IPCLKPORT_I_PCLK	0x2010
#define GOUT_BLK_BUS1_UID_LHM_AXI_D_APM_IPCLKPORT_I_CLK	0x2014
#define GOUT_BLK_BUS1_UID_LHM_AXI_D_CHUB_IPCLKPORT_I_CLK	0x2018
#define GOUT_BLK_BUS1_UID_LHM_AXI_D_GNSS_IPCLKPORT_I_CLK	0x201c
#define GOUT_BLK_BUS1_UID_LHM_AXI_G_CSSYS_IPCLKPORT_I_CLK	0x2020
#define GOUT_BLK_BUS1_UID_LHS_AXI_D_BUS1_IPCLKPORT_I_CLK	0x2024
#define GOUT_BLK_BUS1_UID_LHS_AXI_P_CHUB_IPCLKPORT_I_CLK	0x202c
#define GOUT_BLK_BUS1_UID_LHS_AXI_P_CSSYS_IPCLKPORT_I_CLK	0x2030
#define GOUT_BLK_BUS1_UID_LHS_AXI_P_GNSS_IPCLKPORT_I_CLK	0x2034
#define GOUT_BLK_BUS1_UID_RSTnSYNC_CLK_BUS1_BUS_IPCLKPORT_CLK	0x2038
#define GOUT_BLK_BUS1_UID_SYSREG_BUS1_IPCLKPORT_PCLK	0x203c
#define GOUT_BLK_BUS1_UID_TREX_P_BUS1_IPCLKPORT_pclk	0x2040
#define GOUT_BLK_BUS1_UID_TREX_P_BUS1_IPCLKPORT_PCLK_BUS1	0x2044
#define GOUT_BLK_BUS1_UID_XIU_D_BUS1_IPCLKPORT_ACLK	0x2048

static const unsigned long bus1_clk_regs[] __initconst = {
	MUX_CLKCMU_BUS1_BUS_USER,
	CLK_BLK_BUS1_UID_BUS1_CMU_BUS1_IPCLKPORT_PCLK,
	GOUT_BLK_BUS1_UID_AXI2APB_BUS1_IPCLKPORT_ACLK,
	GOUT_BLK_BUS1_UID_AXI2APB_BUS1_TREX_IPCLKPORT_ACLK,
	GOUT_BLK_BUS1_UID_BAAW_P_CHUB_IPCLKPORT_I_PCLK,
	GOUT_BLK_BUS1_UID_BAAW_P_GNSS_IPCLKPORT_I_PCLK,
	GOUT_BLK_BUS1_UID_LHM_AXI_D_APM_IPCLKPORT_I_CLK,
	GOUT_BLK_BUS1_UID_LHM_AXI_D_CHUB_IPCLKPORT_I_CLK,
	GOUT_BLK_BUS1_UID_LHM_AXI_D_GNSS_IPCLKPORT_I_CLK,
	GOUT_BLK_BUS1_UID_LHM_AXI_G_CSSYS_IPCLKPORT_I_CLK,
	GOUT_BLK_BUS1_UID_LHS_AXI_D_BUS1_IPCLKPORT_I_CLK,
	GOUT_BLK_BUS1_UID_LHS_AXI_P_CHUB_IPCLKPORT_I_CLK,
	GOUT_BLK_BUS1_UID_LHS_AXI_P_CSSYS_IPCLKPORT_I_CLK,
	GOUT_BLK_BUS1_UID_LHS_AXI_P_GNSS_IPCLKPORT_I_CLK,
	GOUT_BLK_BUS1_UID_RSTnSYNC_CLK_BUS1_BUS_IPCLKPORT_CLK,
	GOUT_BLK_BUS1_UID_SYSREG_BUS1_IPCLKPORT_PCLK,
	GOUT_BLK_BUS1_UID_TREX_P_BUS1_IPCLKPORT_pclk,
	GOUT_BLK_BUS1_UID_TREX_P_BUS1_IPCLKPORT_PCLK_BUS1,
	GOUT_BLK_BUS1_UID_XIU_D_BUS1_IPCLKPORT_ACLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long bus1_qch_regs[] __initconst = {
	0x3028,	/* BAAW_P_CHUB_QCH */
	0x302c,	/* BAAW_P_GNSS_QCH */
	0x3030,	/* BUS1_CMU_BUS1_QCH */
	0x3034,	/* LHM_AXI_D_APM_QCH */
	0x3038,	/* LHM_AXI_D_CHUB_QCH */
	0x303c,	/* LHM_AXI_D_GNSS_QCH */
	0x3040,	/* LHM_AXI_G_CSSYS_QCH */
	0x3044,	/* LHS_AXI_D_BUS1_QCH */
	0x304c,	/* LHS_AXI_P_CHUB_QCH */
	0x3050,	/* LHS_AXI_P_CSSYS_QCH */
	0x3054,	/* LHS_AXI_P_GNSS_QCH */
	0x3058,	/* SYSREG_BUS1_QCH */
	0x305c,	/* TREX_P_BUS1_QCH */
};

/* List of parent clocks for Muxes in CMU_BUS1 */
PNAME(mout_cmu_bus1_bus_user_p) = { "oscclk", "dout_clkcmu_bus1_bus" };

static const struct samsung_mux_clock bus1_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_BUS1_BUS_USER, "mout_cmu_bus1_bus_user", mout_cmu_bus1_bus_user_p,
	    MUX_CLKCMU_BUS1_BUS_USER, 4, 1),
};

static const struct samsung_gate_clock bus1_gate_clks[] __initconst = {
	GATE(CLK_GOUT_BUS1_LHM_AXI_D_GNSS_I_CLK, "gout_bus1_lhm_axi_d_gnss_i_clk",
	     "mout_cmu_bus1_bus_user",
	     GOUT_BLK_BUS1_UID_LHM_AXI_D_GNSS_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_BUS1_LHM_AXI_D_APM_I_CLK, "gout_bus1_lhm_axi_d_apm_i_clk",
	     "mout_cmu_bus1_bus_user",
	     GOUT_BLK_BUS1_UID_LHM_AXI_D_APM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_BUS1_RSTnSYNC_CLK_BUS1_BUS_CLK, "gout_bus1_rstnsync_clk_bus1_bus_clk",
	     "mout_cmu_bus1_bus_user",
	     GOUT_BLK_BUS1_UID_RSTnSYNC_CLK_BUS1_BUS_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_BUS1_AXI2APB_BUS1_ACLK, "gout_bus1_axi2apb_bus1_aclk",
	     "mout_cmu_bus1_bus_user",
	     GOUT_BLK_BUS1_UID_AXI2APB_BUS1_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_BUS1_AXI2APB_BUS1_TREX_ACLK, "gout_bus1_axi2apb_bus1_trex_aclk",
	     "mout_cmu_bus1_bus_user",
	     GOUT_BLK_BUS1_UID_AXI2APB_BUS1_TREX_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_BUS1_LHM_AXI_D_CHUB_I_CLK, "gout_bus1_lhm_axi_d_chub_i_clk",
	     "mout_cmu_bus1_bus_user",
	     GOUT_BLK_BUS1_UID_LHM_AXI_D_CHUB_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_BUS1_LHM_AXI_G_CSSYS_I_CLK, "gout_bus1_lhm_axi_g_cssys_i_clk",
	     "mout_cmu_bus1_bus_user",
	     GOUT_BLK_BUS1_UID_LHM_AXI_G_CSSYS_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_BUS1_LHS_AXI_D_BUS1_I_CLK, "gout_bus1_lhs_axi_d_bus1_i_clk",
	     "mout_cmu_bus1_bus_user",
	     GOUT_BLK_BUS1_UID_LHS_AXI_D_BUS1_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_BUS1_LHS_AXI_P_CHUB_I_CLK, "gout_bus1_lhs_axi_p_chub_i_clk",
	     "mout_cmu_bus1_bus_user",
	     GOUT_BLK_BUS1_UID_LHS_AXI_P_CHUB_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_BUS1_LHS_AXI_P_CSSYS_I_CLK, "gout_bus1_lhs_axi_p_cssys_i_clk",
	     "mout_cmu_bus1_bus_user",
	     GOUT_BLK_BUS1_UID_LHS_AXI_P_CSSYS_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_BUS1_LHS_AXI_P_GNSS_I_CLK, "gout_bus1_lhs_axi_p_gnss_i_clk",
	     "mout_cmu_bus1_bus_user",
	     GOUT_BLK_BUS1_UID_LHS_AXI_P_GNSS_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_BUS1_SYSREG_BUS1_PCLK, "gout_bus1_sysreg_bus1_pclk", "mout_cmu_bus1_bus_user",
	     GOUT_BLK_BUS1_UID_SYSREG_BUS1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_BUS1_TREX_P_BUS1_pclk, "gout_bus1_trex_p_bus1_pclk", "mout_cmu_bus1_bus_user",
	     GOUT_BLK_BUS1_UID_TREX_P_BUS1_IPCLKPORT_pclk, 21, 0, 0),
	GATE(CLK_GOUT_BUS1_BUS1_CMU_BUS1_PCLK, "gout_bus1_bus1_cmu_bus1_pclk",
	     "mout_cmu_bus1_bus_user",
	     CLK_BLK_BUS1_UID_BUS1_CMU_BUS1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_BUS1_BAAW_P_GNSS_I_PCLK, "gout_bus1_baaw_p_gnss_i_pclk",
	     "mout_cmu_bus1_bus_user",
	     GOUT_BLK_BUS1_UID_BAAW_P_GNSS_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_BUS1_BAAW_P_CHUB_I_PCLK, "gout_bus1_baaw_p_chub_i_pclk",
	     "mout_cmu_bus1_bus_user",
	     GOUT_BLK_BUS1_UID_BAAW_P_CHUB_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_BUS1_TREX_P_BUS1_PCLK_BUS1, "gout_bus1_trex_p_bus1_pclk_bus1",
	     "mout_cmu_bus1_bus_user",
	     GOUT_BLK_BUS1_UID_TREX_P_BUS1_IPCLKPORT_PCLK_BUS1, 21, 0, 0),
	GATE(CLK_GOUT_BUS1_XIU_D_BUS1_ACLK, "gout_bus1_xiu_d_bus1_aclk", "mout_cmu_bus1_bus_user",
	     GOUT_BLK_BUS1_UID_XIU_D_BUS1_IPCLKPORT_ACLK, 21, 0, 0),
};

static const struct samsung_cmu_info bus1_cmu_info __initconst = {
	.mux_clks		= bus1_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(bus1_mux_clks),
	.gate_clks		= bus1_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(bus1_gate_clks),
	.nr_clk_ids		= CLKS_NR_BUS1,
	.clk_regs		= bus1_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(bus1_clk_regs),
	.qch_regs		= bus1_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(bus1_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_bus1_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &bus1_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_bus1, "samsung,exynos9810-cmu-bus1",
	       exynos9810_cmu_bus1_init);

/* ---- CMU_BUSC --------------------------------------------------------*/

/* Register Offset definitions for CMU_BUSC (0x1a200000) */
#define MUX_CLKCMU_BUSC_BUS_USER			0x0140
#define DIV_CLK_BUSC_BUSP			0x1800
#define CLK_BLK_BUSC_UID_HPM_BUSC_IPCLKPORT_hpm_targetclk_c	0x2004

static const unsigned long busc_clk_regs[] __initconst = {
	MUX_CLKCMU_BUSC_BUS_USER,
	DIV_CLK_BUSC_BUSP,
	CLK_BLK_BUSC_UID_HPM_BUSC_IPCLKPORT_hpm_targetclk_c,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long busc_qch_regs[] __initconst = {
	0x30b4,	/* BUSC_CMU_BUSC_QCH */
	0x30b8,	/* BUSIF_CMUTOPC_QCH */
	0x30bc,	/* BUSIF_HPMBUSC_QCH */
	0x30c0,	/* LHM_ACEL_D0_DSPM_QCH */
	0x30c4,	/* LHM_ACEL_D0_G2D_QCH */
	0x30c8,	/* LHM_ACEL_D1_DSPM_QCH */
	0x30cc,	/* LHM_ACEL_D1_G2D_QCH */
	0x30d0,	/* LHM_ACEL_D2_DSPM_QCH */
	0x30d4,	/* LHM_ACEL_D2_G2D_QCH */
	0x30d8,	/* LHM_ACEL_D_FSYS0_QCH */
	0x30dc,	/* LHM_ACEL_D_FSYS1_QCH */
	0x30e0,	/* LHM_ACEL_D_IVA_QCH */
	0x30e4,	/* LHM_AXI_D0_DPU_QCH */
	0x30e8,	/* LHM_AXI_D0_ISPLP_QCH */
	0x30ec,	/* LHM_AXI_D0_MFC_QCH */
	0x30f0,	/* LHM_AXI_D1_DPU_QCH */
	0x30f4,	/* LHM_AXI_D1_ISPLP_QCH */
	0x30f8,	/* LHM_AXI_D1_MFC_QCH */
	0x30fc,	/* LHM_AXI_D2_DPU_QCH */
	0x3100,	/* LHM_AXI_D_AUD_QCH */
	0x3104,	/* LHM_AXI_D_BUS1_QCH */
	0x3108,	/* LHM_AXI_D_DCF_QCH */
	0x310c,	/* LHM_AXI_D_DCRD_QCH */
	0x3110,	/* LHM_AXI_D_ISPHQ_QCH */
	0x3114,	/* LHM_AXI_D_ISPPRE_QCH */
	0x3118,	/* LHS_AXI_D_IVASC_QCH */
	0x311c,	/* LHS_AXI_P_AUD_QCH */
	0x3120,	/* LHS_AXI_P_DCF_QCH */
	0x3124,	/* LHS_AXI_P_DCRD_QCH */
	0x3128,	/* LHS_AXI_P_DPU_QCH */
	0x312c,	/* LHS_AXI_P_DSPM_QCH */
	0x3130,	/* LHS_AXI_P_FSYS0_QCH */
	0x3134,	/* LHS_AXI_P_FSYS1_QCH */
	0x3138,	/* LHS_AXI_P_G2D_QCH */
	0x313c,	/* LHS_AXI_P_ISPHQ_QCH */
	0x3140,	/* LHS_AXI_P_ISPLP_QCH */
	0x3144,	/* LHS_AXI_P_ISPPRE_QCH */
	0x3148,	/* LHS_AXI_P_IVA_QCH */
	0x314c,	/* LHS_AXI_P_MFC_QCH */
	0x3150,	/* LHS_AXI_P_MIF0_QCH */
	0x3154,	/* LHS_AXI_P_MIF1_QCH */
	0x3158,	/* LHS_AXI_P_MIF2_QCH */
	0x315c,	/* LHS_AXI_P_MIF3_QCH */
	0x3160,	/* LHS_AXI_P_PERIC0_QCH */
	0x3164,	/* LHS_AXI_P_PERIC1_QCH */
	0x3168,	/* LHS_AXI_P_PERIS_QCH */
	0x316c,	/* PDMA0_QCH */
	0x3170,	/* PGEN_LITE_BUSC_QCH */
	0x3174,	/* PGEN_PDMA0_QCH */
	0x3178,	/* PPFW_QCH */
	0x317c,	/* SBIC_QCH */
	0x3180,	/* SIREX_QCH */
	0x3184,	/* SPDMA_QCH */
	0x3188,	/* SYSREG_BUSC_QCH */
	0x318c,	/* TREX_D_BUSC_QCH */
	0x3190,	/* TREX_P_BUSC_QCH */
	0x3194,	/* TREX_RB_BUSC_QCH */
};

/* List of parent clocks for Muxes in CMU_BUSC */
PNAME(mout_cmu_busc_bus_user_p) = { "oscclk", "dout_clkcmu_busc_bus" };

static const struct samsung_mux_clock busc_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_BUSC_BUS_USER, "mout_cmu_busc_bus_user", mout_cmu_busc_bus_user_p,
	    MUX_CLKCMU_BUSC_BUS_USER, 4, 1),
};

static const struct samsung_div_clock busc_div_clks[] __initconst = {
	DIV(CLK_DOUT_BUSC_BUSP, "dout_busc_busp", "mout_cmu_busc_bus_user",
	    DIV_CLK_BUSC_BUSP, 0, 3),
};

static const struct samsung_gate_clock busc_gate_clks[] __initconst = {
	GATE(CLK_GOUT_BUSC_HPM_BUSC_hpm_targetclk_c, "gout_busc_hpm_busc_hpm_targetclk_c",
	     "dout_clkcmu_hpm",
	     CLK_BLK_BUSC_UID_HPM_BUSC_IPCLKPORT_hpm_targetclk_c, 21, 0, 0),
};

static const struct samsung_cmu_info busc_cmu_info __initconst = {
	.mux_clks		= busc_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(busc_mux_clks),
	.div_clks		= busc_div_clks,
	.nr_div_clks		= ARRAY_SIZE(busc_div_clks),
	.gate_clks		= busc_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(busc_gate_clks),
	.nr_clk_ids		= CLKS_NR_BUSC,
	.clk_regs		= busc_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(busc_clk_regs),
	.qch_regs		= busc_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(busc_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_busc_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &busc_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_busc, "samsung,exynos9810-cmu-busc",
	       exynos9810_cmu_busc_init);

/* ---- CMU_CHUB --------------------------------------------------------*/

/* Register Offset definitions for CMU_CHUB (0x13a00000) */
#define MUX_CLKCMU_CHUB_BUS_USER			0x0100
#define MUX_CLKCMU_CHUB_DLL_BUS_USER			0x0120
#define CLK_CHUB_TIMER_FCLK			0x1000
#define MUX_CLK_CHUB_BUS			0x1004
#define MUX_CLK_CHUB_I2C			0x1008
#define MUX_CLK_CHUB_USI00			0x100c
#define MUX_CLK_CHUB_USI01			0x1010
#define DIV_CLK_CHUB_BUS			0x1800
#define DIV_CLK_CHUB_I2C			0x1804
#define DIV_CLK_CHUB_USI00			0x1808
#define DIV_CLK_CHUB_USI01			0x180c
#define GATE_CLK_CHUB_I2C			0x200c
#define GATE_CLK_CHUB_USI00			0x2010
#define GATE_CLK_CHUB_USI01			0x2014
#define GOUT_BLK_CHUB_UID_I2C_CHUB00_IPCLKPORT_IPCLK	0x2058
#define GOUT_BLK_CHUB_UID_I2C_CHUB01_IPCLKPORT_IPCLK	0x2060
#define GOUT_BLK_CHUB_UID_RSTnSYNC_CLK_CHUB_I2C_IPCLKPORT_CLK	0x208c
#define GOUT_BLK_CHUB_UID_RSTnSYNC_CLK_CHUB_USI00_IPCLKPORT_CLK	0x2090
#define GOUT_BLK_CHUB_UID_RSTnSYNC_CLK_CHUB_USI01_IPCLKPORT_CLK	0x2094
#define GOUT_BLK_CHUB_UID_USI_CHUB00_IPCLKPORT_IPCLK	0x20a8
#define GOUT_BLK_CHUB_UID_USI_CHUB01_IPCLKPORT_IPCLK	0x20b0

static const unsigned long chub_clk_regs[] __initconst = {
	MUX_CLKCMU_CHUB_BUS_USER,
	MUX_CLKCMU_CHUB_DLL_BUS_USER,
	CLK_CHUB_TIMER_FCLK,
	MUX_CLK_CHUB_BUS,
	MUX_CLK_CHUB_I2C,
	MUX_CLK_CHUB_USI00,
	MUX_CLK_CHUB_USI01,
	DIV_CLK_CHUB_BUS,
	DIV_CLK_CHUB_I2C,
	DIV_CLK_CHUB_USI00,
	DIV_CLK_CHUB_USI01,
	GATE_CLK_CHUB_I2C,
	GATE_CLK_CHUB_USI00,
	GATE_CLK_CHUB_USI01,
	GOUT_BLK_CHUB_UID_I2C_CHUB00_IPCLKPORT_IPCLK,
	GOUT_BLK_CHUB_UID_I2C_CHUB01_IPCLKPORT_IPCLK,
	GOUT_BLK_CHUB_UID_RSTnSYNC_CLK_CHUB_I2C_IPCLKPORT_CLK,
	GOUT_BLK_CHUB_UID_RSTnSYNC_CLK_CHUB_USI00_IPCLKPORT_CLK,
	GOUT_BLK_CHUB_UID_RSTnSYNC_CLK_CHUB_USI01_IPCLKPORT_CLK,
	GOUT_BLK_CHUB_UID_USI_CHUB00_IPCLKPORT_IPCLK,
	GOUT_BLK_CHUB_UID_USI_CHUB01_IPCLKPORT_IPCLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long chub_qch_regs[] __initconst = {
	0x3010,	/* ASYNCAHBM_CHUB_QCH */
	0x3014,	/* BAAW_D_CHUB_QCH */
	0x3018,	/* BAAW_P_APM_CHUB_QCH */
	0x301c,	/* BAAW_S_CHUB_QCH */
	0x3020,	/* CHUB_CMU_CHUB_QCH */
	0x3024,	/* CM4_CHUB_QCH */
	0x3028,	/* GPIO_CHUB_QCH */
	0x302c,	/* I2C_CHUB00_QCH */
	0x3030,	/* I2C_CHUB01_QCH */
	0x3034,	/* LHM_AXI_LP_CHUB_QCH */
	0x3038,	/* LHM_AXI_P_CHUB_QCH */
	0x303c,	/* LHS_AXI_D_CHUB_QCH */
	0x3040,	/* LHS_AXI_P_APM_CHUB_QCH */
	0x3044,	/* PDMA_CHUB_QCH */
	0x3048,	/* PWM_CHUB_QCH */
	0x304c,	/* SWEEPER_D_CHUB_QCH */
	0x3050,	/* SWEEPER_P_APM_CHUB_QCH */
	0x3054,	/* SYSREG_CHUB_QCH */
	0x3058,	/* TIMER_CHUB_QCH */
	0x305c,	/* USI_CHUB00_QCH */
	0x3060,	/* USI_CHUB01_QCH */
	0x3064,	/* WDT_CHUB_QCH */
};

static const struct samsung_fixed_rate_clock chub_fixed_clks[] __initconst = {
	FRATE(CLK_RTCCLK_CHUB, "rtcclk_chub", NULL, 0, 100000000),
};

/* List of parent clocks for Muxes in CMU_CHUB */
PNAME(mout_chub_bus_p) = { "mout_cmu_chub_bus_user", "mout_cmu_chub_dll_bus_user" };
PNAME(mout_chub_i2c_p) = { "oscclk", "mout_chub_bus" };
PNAME(mout_chub_usi00_p) = { "oscclk", "mout_chub_bus" };
PNAME(mout_chub_usi01_p) = { "oscclk", "mout_chub_bus" };
PNAME(mout_chub_timer_fclk_p) = { "oscclk", "rtcclk_chub" };
PNAME(mout_cmu_chub_bus_user_p) = { "oscclk", "dout_clkcmu_chub_bus" };
PNAME(mout_cmu_chub_dll_bus_user_p) = { "oscclk", "gout_clkcmu_apm_dll_chub" };

static const struct samsung_mux_clock chub_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CHUB_BUS, "mout_chub_bus", mout_chub_bus_p,
	    MUX_CLK_CHUB_BUS, 0, 1),
	MUX(CLK_MOUT_CHUB_I2C, "mout_chub_i2c", mout_chub_i2c_p,
	    MUX_CLK_CHUB_I2C, 0, 1),
	MUX(CLK_MOUT_CHUB_USI00, "mout_chub_usi00", mout_chub_usi00_p,
	    MUX_CLK_CHUB_USI00, 0, 1),
	MUX(CLK_MOUT_CHUB_USI01, "mout_chub_usi01", mout_chub_usi01_p,
	    MUX_CLK_CHUB_USI01, 0, 1),
	MUX(CLK_MOUT_CHUB_TIMER_FCLK, "mout_chub_timer_fclk", mout_chub_timer_fclk_p,
	    CLK_CHUB_TIMER_FCLK, 0, 1),
	MUX(CLK_MOUT_CMU_CHUB_BUS_USER, "mout_cmu_chub_bus_user", mout_cmu_chub_bus_user_p,
	    MUX_CLKCMU_CHUB_BUS_USER, 4, 1),
	MUX(CLK_MOUT_CMU_CHUB_DLL_BUS_USER, "mout_cmu_chub_dll_bus_user",
	    mout_cmu_chub_dll_bus_user_p,
	    MUX_CLKCMU_CHUB_DLL_BUS_USER, 4, 1),
};

static const struct samsung_div_clock chub_div_clks[] __initconst = {
	DIV(CLK_DOUT_CHUB_USI01, "dout_chub_usi01", "gout_chub_usi01",
	    DIV_CLK_CHUB_USI01, 0, 4),
	DIV(CLK_DOUT_CHUB_I2C, "dout_chub_i2c", "gout_chub_i2c",
	    DIV_CLK_CHUB_I2C, 0, 4),
	DIV(CLK_DOUT_CHUB_USI00, "dout_chub_usi00", "gout_chub_usi00",
	    DIV_CLK_CHUB_USI00, 0, 4),
	DIV(CLK_DOUT_CHUB_BUS, "dout_chub_bus", "mout_chub_bus",
	    DIV_CLK_CHUB_BUS, 0, 4),
};

static const struct samsung_gate_clock chub_gate_clks[] __initconst = {
	GATE(CLK_GOUT_CHUB_RSTnSYNC_CLK_CHUB_I2C_CLK, "gout_chub_rstnsync_clk_chub_i2c_clk",
	     "dout_chub_i2c",
	     GOUT_BLK_CHUB_UID_RSTnSYNC_CLK_CHUB_I2C_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CHUB_RSTnSYNC_CLK_CHUB_USI00_CLK, "gout_chub_rstnsync_clk_chub_usi00_clk",
	     "dout_chub_usi00",
	     GOUT_BLK_CHUB_UID_RSTnSYNC_CLK_CHUB_USI00_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CHUB_RSTnSYNC_CLK_CHUB_USI01_CLK, "gout_chub_rstnsync_clk_chub_usi01_clk",
	     "dout_chub_usi01",
	     GOUT_BLK_CHUB_UID_RSTnSYNC_CLK_CHUB_USI01_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CHUB_I2C, "gout_chub_i2c", "mout_chub_i2c",
	     GATE_CLK_CHUB_I2C, 21, 0, 0),
	GATE(CLK_GOUT_CHUB_USI00, "gout_chub_usi00", "mout_chub_usi00",
	     GATE_CLK_CHUB_USI00, 21, 0, 0),
	GATE(CLK_GOUT_CHUB_USI01, "gout_chub_usi01", "mout_chub_usi01",
	     GATE_CLK_CHUB_USI01, 21, 0, 0),
	GATE(CLK_GOUT_CHUB_I2C_CHUB00_IPCLK, "gout_chub_i2c_chub00_ipclk", "dout_chub_i2c",
	     GOUT_BLK_CHUB_UID_I2C_CHUB00_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_CHUB_I2C_CHUB01_IPCLK, "gout_chub_i2c_chub01_ipclk", "dout_chub_i2c",
	     GOUT_BLK_CHUB_UID_I2C_CHUB01_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_CHUB_USI_CHUB00_IPCLK, "gout_chub_usi_chub00_ipclk", "dout_chub_usi00",
	     GOUT_BLK_CHUB_UID_USI_CHUB00_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_CHUB_USI_CHUB01_IPCLK, "gout_chub_usi_chub01_ipclk", "dout_chub_usi01",
	     GOUT_BLK_CHUB_UID_USI_CHUB01_IPCLKPORT_IPCLK, 21, 0, 0),
};

static const struct samsung_cmu_info chub_cmu_info __initconst = {
	.mux_clks		= chub_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(chub_mux_clks),
	.div_clks		= chub_div_clks,
	.nr_div_clks		= ARRAY_SIZE(chub_div_clks),
	.gate_clks		= chub_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(chub_gate_clks),
	.fixed_clks		= chub_fixed_clks,
	.nr_fixed_clks		= ARRAY_SIZE(chub_fixed_clks),
	.nr_clk_ids		= CLKS_NR_CHUB,
	.clk_regs		= chub_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(chub_clk_regs),
	.qch_regs		= chub_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(chub_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_chub_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &chub_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_chub, "samsung,exynos9810-cmu-chub",
	       exynos9810_cmu_chub_init);

/* ---- CMU_CMGP --------------------------------------------------------*/

/* Register Offset definitions for CMU_CMGP (0x14200000) */
#define MUX_CLKCMU_CMGP_BUS_USER			0x0100
#define MUX_CLKCMU_CMGP_DLL_USER			0x0120
#define CLK_CMGP_ADC			0x1000
#define MUX_CLK_CMGP_BUS			0x1004
#define MUX_CLK_I2C_CMGP			0x1008
#define MUX_CLK_USI_CMGP00			0x100c
#define MUX_CLK_USI_CMGP01			0x1010
#define MUX_CLK_USI_CMGP02			0x1014
#define MUX_CLK_USI_CMGP03			0x1018
#define DIV_CLK_CMGP_ADC			0x1800
#define DIV_CLK_I2C_CMGP			0x1804
#define DIV_CLK_USI_CMGP00			0x1808
#define DIV_CLK_USI_CMGP01			0x180c
#define DIV_CLK_USI_CMGP02			0x1810
#define DIV_CLK_USI_CMGP03			0x1814
#define CLK_BLK_CMGP_UID_CMGP_CMU_CMGP_IPCLKPORT_PCLK	0x2000
#define GATE_CLK_I2C_CMGP			0x2004
#define GATE_CLK_USI_CMGP00			0x2008
#define GATE_CLK_USI_CMGP01			0x200c
#define GATE_CLK_USI_CMGP02			0x2010
#define GATE_CLK_USI_CMGP03			0x2014
#define GOUT_BLK_CMGP_UID_ADC_CMGP_IPCLKPORT_PCLK_S0	0x2018
#define GOUT_BLK_CMGP_UID_ADC_CMGP_IPCLKPORT_PCLK_S1	0x201c
#define GOUT_BLK_CMGP_UID_AXI2APB_CMGP0_IPCLKPORT_ACLK	0x2020
#define GOUT_BLK_CMGP_UID_AXI2APB_CMGP1_IPCLKPORT_ACLK	0x2024
#define GOUT_BLK_CMGP_UID_GPIO_CMGP_IPCLKPORT_PCLK	0x2028
#define GOUT_BLK_CMGP_UID_I2C_CMGP00_IPCLKPORT_IPCLK	0x202c
#define GOUT_BLK_CMGP_UID_I2C_CMGP00_IPCLKPORT_PCLK	0x2030
#define GOUT_BLK_CMGP_UID_I2C_CMGP01_IPCLKPORT_IPCLK	0x2034
#define GOUT_BLK_CMGP_UID_I2C_CMGP01_IPCLKPORT_PCLK	0x2038
#define GOUT_BLK_CMGP_UID_I2C_CMGP02_IPCLKPORT_IPCLK	0x203c
#define GOUT_BLK_CMGP_UID_I2C_CMGP02_IPCLKPORT_PCLK	0x2040
#define GOUT_BLK_CMGP_UID_I2C_CMGP03_IPCLKPORT_IPCLK	0x2044
#define GOUT_BLK_CMGP_UID_I2C_CMGP03_IPCLKPORT_PCLK	0x2048
#define GOUT_BLK_CMGP_UID_LHM_AXI_P_APM2CMGP_IPCLKPORT_I_CLK	0x204c
#define GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_BUS_IPCLKPORT_CLK	0x2050
#define GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_I2C_CMGP00_IPCLKPORT_CLK	0x2054
#define GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_I2C_CMGP01_IPCLKPORT_CLK	0x2058
#define GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_I2C_CMGP02_IPCLKPORT_CLK	0x205c
#define GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_I2C_CMGP03_IPCLKPORT_CLK	0x2060
#define GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI_CMGP00_IPCLKPORT_CLK	0x2068
#define GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI_CMGP01_IPCLKPORT_CLK	0x206c
#define GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI_CMGP02_IPCLKPORT_CLK	0x2070
#define GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI_CMGP03_IPCLKPORT_CLK	0x2074
#define GOUT_BLK_CMGP_UID_SYSREG_CMGP2CHUB_IPCLKPORT_PCLK	0x2078
#define GOUT_BLK_CMGP_UID_SYSREG_CMGP2CP_IPCLKPORT_PCLK	0x207c
#define GOUT_BLK_CMGP_UID_SYSREG_CMGP2GNSS_IPCLKPORT_PCLK	0x2080
#define GOUT_BLK_CMGP_UID_SYSREG_CMGP2PMU_AP_IPCLKPORT_PCLK	0x2084
#define GOUT_BLK_CMGP_UID_SYSREG_CMGP2PMU_CHUB_IPCLKPORT_PCLK	0x2088
#define GOUT_BLK_CMGP_UID_SYSREG_CMGP_IPCLKPORT_PCLK	0x208c
#define GOUT_BLK_CMGP_UID_USI_CMGP00_IPCLKPORT_IPCLK	0x2090
#define GOUT_BLK_CMGP_UID_USI_CMGP00_IPCLKPORT_PCLK	0x2094
#define GOUT_BLK_CMGP_UID_USI_CMGP01_IPCLKPORT_IPCLK	0x2098
#define GOUT_BLK_CMGP_UID_USI_CMGP01_IPCLKPORT_PCLK	0x209c
#define GOUT_BLK_CMGP_UID_USI_CMGP02_IPCLKPORT_IPCLK	0x20a0
#define GOUT_BLK_CMGP_UID_USI_CMGP02_IPCLKPORT_PCLK	0x20a4
#define GOUT_BLK_CMGP_UID_USI_CMGP03_IPCLKPORT_IPCLK	0x20a8
#define GOUT_BLK_CMGP_UID_USI_CMGP03_IPCLKPORT_PCLK	0x20ac
#define GOUT_BLK_CMGP_UID_XIU_P_CMGP_IPCLKPORT_ACLK	0x20b0

static const unsigned long cmgp_clk_regs[] __initconst = {
	MUX_CLKCMU_CMGP_BUS_USER,
	MUX_CLKCMU_CMGP_DLL_USER,
	CLK_CMGP_ADC,
	MUX_CLK_CMGP_BUS,
	MUX_CLK_I2C_CMGP,
	MUX_CLK_USI_CMGP00,
	MUX_CLK_USI_CMGP01,
	MUX_CLK_USI_CMGP02,
	MUX_CLK_USI_CMGP03,
	DIV_CLK_CMGP_ADC,
	DIV_CLK_I2C_CMGP,
	DIV_CLK_USI_CMGP00,
	DIV_CLK_USI_CMGP01,
	DIV_CLK_USI_CMGP02,
	DIV_CLK_USI_CMGP03,
	CLK_BLK_CMGP_UID_CMGP_CMU_CMGP_IPCLKPORT_PCLK,
	GATE_CLK_I2C_CMGP,
	GATE_CLK_USI_CMGP00,
	GATE_CLK_USI_CMGP01,
	GATE_CLK_USI_CMGP02,
	GATE_CLK_USI_CMGP03,
	GOUT_BLK_CMGP_UID_ADC_CMGP_IPCLKPORT_PCLK_S0,
	GOUT_BLK_CMGP_UID_ADC_CMGP_IPCLKPORT_PCLK_S1,
	GOUT_BLK_CMGP_UID_AXI2APB_CMGP0_IPCLKPORT_ACLK,
	GOUT_BLK_CMGP_UID_AXI2APB_CMGP1_IPCLKPORT_ACLK,
	GOUT_BLK_CMGP_UID_GPIO_CMGP_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_I2C_CMGP00_IPCLKPORT_IPCLK,
	GOUT_BLK_CMGP_UID_I2C_CMGP00_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_I2C_CMGP01_IPCLKPORT_IPCLK,
	GOUT_BLK_CMGP_UID_I2C_CMGP01_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_I2C_CMGP02_IPCLKPORT_IPCLK,
	GOUT_BLK_CMGP_UID_I2C_CMGP02_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_I2C_CMGP03_IPCLKPORT_IPCLK,
	GOUT_BLK_CMGP_UID_I2C_CMGP03_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_LHM_AXI_P_APM2CMGP_IPCLKPORT_I_CLK,
	GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_BUS_IPCLKPORT_CLK,
	GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_I2C_CMGP00_IPCLKPORT_CLK,
	GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_I2C_CMGP01_IPCLKPORT_CLK,
	GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_I2C_CMGP02_IPCLKPORT_CLK,
	GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_I2C_CMGP03_IPCLKPORT_CLK,
	GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI_CMGP00_IPCLKPORT_CLK,
	GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI_CMGP01_IPCLKPORT_CLK,
	GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI_CMGP02_IPCLKPORT_CLK,
	GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI_CMGP03_IPCLKPORT_CLK,
	GOUT_BLK_CMGP_UID_SYSREG_CMGP2CHUB_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_SYSREG_CMGP2CP_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_SYSREG_CMGP2GNSS_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_SYSREG_CMGP2PMU_AP_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_SYSREG_CMGP2PMU_CHUB_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_SYSREG_CMGP_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_USI_CMGP00_IPCLKPORT_IPCLK,
	GOUT_BLK_CMGP_UID_USI_CMGP00_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_USI_CMGP01_IPCLKPORT_IPCLK,
	GOUT_BLK_CMGP_UID_USI_CMGP01_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_USI_CMGP02_IPCLKPORT_IPCLK,
	GOUT_BLK_CMGP_UID_USI_CMGP02_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_USI_CMGP03_IPCLKPORT_IPCLK,
	GOUT_BLK_CMGP_UID_USI_CMGP03_IPCLKPORT_PCLK,
	GOUT_BLK_CMGP_UID_XIU_P_CMGP_IPCLKPORT_ACLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long cmgp_qch_regs[] __initconst = {
	0x3000,	/* ADC_CMGP_QCH_ADC */
	0x3008,	/* ADC_CMGP_QCH_S0 */
	0x300c,	/* ADC_CMGP_QCH_S1 */
	0x3010,	/* CMGP_CMU_CMGP_QCH */
	0x3014,	/* GPIO_CMGP_QCH */
	0x3018,	/* I2C_CMGP00_QCH */
	0x301c,	/* I2C_CMGP01_QCH */
	0x3020,	/* I2C_CMGP02_QCH */
	0x3024,	/* I2C_CMGP03_QCH */
	0x3028,	/* LHM_AXI_P_APM2CMGP_QCH */
	0x302c,	/* SYSREG_CMGP2CHUB_QCH */
	0x3030,	/* SYSREG_CMGP2CP_QCH */
	0x3034,	/* SYSREG_CMGP2GNSS_QCH */
	0x3038,	/* SYSREG_CMGP2PMU_AP_QCH */
	0x303c,	/* SYSREG_CMGP2PMU_CHUB_QCH */
	0x3040,	/* SYSREG_CMGP_QCH */
	0x3044,	/* USI_CMGP00_QCH */
	0x3048,	/* USI_CMGP01_QCH */
	0x304c,	/* USI_CMGP02_QCH */
	0x3050,	/* USI_CMGP03_QCH */
};

/* List of parent clocks for Muxes in CMU_CMGP */
PNAME(mout_i2c_cmgp_p) = { "oscclk", "mout_cmgp_bus" };
PNAME(mout_usi_cmgp00_p) = { "oscclk", "mout_cmgp_bus" };
PNAME(mout_usi_cmgp01_p) = { "oscclk", "mout_cmgp_bus" };
PNAME(mout_usi_cmgp02_p) = { "oscclk", "mout_cmgp_bus" };
PNAME(mout_usi_cmgp03_p) = { "oscclk", "mout_cmgp_bus" };
PNAME(mout_cmgp_bus_p) = { "mout_cmu_cmgp_bus_user", "mout_cmu_cmgp_dll_user" };
PNAME(mout_cmgp_adc_p) = { "oscclk", "dout_cmgp_adc" };
PNAME(mout_cmu_cmgp_bus_user_p) = { "oscclk", "dout_clkcmu_cmgp_bus" };
PNAME(mout_cmu_cmgp_dll_user_p) = { "oscclk", "dout_clkcmu_apm_dll_cmgp" };

static const struct samsung_mux_clock cmgp_mux_clks[] __initconst = {
	MUX(CLK_MOUT_I2C_CMGP, "mout_i2c_cmgp", mout_i2c_cmgp_p,
	    MUX_CLK_I2C_CMGP, 0, 1),
	MUX(CLK_MOUT_USI_CMGP00, "mout_usi_cmgp00", mout_usi_cmgp00_p,
	    MUX_CLK_USI_CMGP00, 0, 1),
	MUX(CLK_MOUT_USI_CMGP01, "mout_usi_cmgp01", mout_usi_cmgp01_p,
	    MUX_CLK_USI_CMGP01, 0, 1),
	MUX(CLK_MOUT_USI_CMGP02, "mout_usi_cmgp02", mout_usi_cmgp02_p,
	    MUX_CLK_USI_CMGP02, 0, 1),
	MUX(CLK_MOUT_USI_CMGP03, "mout_usi_cmgp03", mout_usi_cmgp03_p,
	    MUX_CLK_USI_CMGP03, 0, 1),
	MUX(CLK_MOUT_CMGP_BUS, "mout_cmgp_bus", mout_cmgp_bus_p,
	    MUX_CLK_CMGP_BUS, 0, 1),
	MUX(CLK_MOUT_CMGP_ADC, "mout_cmgp_adc", mout_cmgp_adc_p,
	    CLK_CMGP_ADC, 0, 1),
	MUX(CLK_MOUT_CMU_CMGP_BUS_USER, "mout_cmu_cmgp_bus_user", mout_cmu_cmgp_bus_user_p,
	    MUX_CLKCMU_CMGP_BUS_USER, 4, 1),
	MUX(CLK_MOUT_CMU_CMGP_DLL_USER, "mout_cmu_cmgp_dll_user", mout_cmu_cmgp_dll_user_p,
	    MUX_CLKCMU_CMGP_DLL_USER, 4, 1),
};

static const struct samsung_div_clock cmgp_div_clks[] __initconst = {
	DIV(CLK_DOUT_I2C_CMGP, "dout_i2c_cmgp", "gout_i2c_cmgp",
	    DIV_CLK_I2C_CMGP, 0, 4),
	DIV(CLK_DOUT_USI_CMGP01, "dout_usi_cmgp01", "gout_usi_cmgp01",
	    DIV_CLK_USI_CMGP01, 0, 4),
	DIV(CLK_DOUT_USI_CMGP00, "dout_usi_cmgp00", "gout_usi_cmgp00",
	    DIV_CLK_USI_CMGP00, 0, 4),
	DIV(CLK_DOUT_USI_CMGP02, "dout_usi_cmgp02", "gout_usi_cmgp02",
	    DIV_CLK_USI_CMGP02, 0, 4),
	DIV(CLK_DOUT_USI_CMGP03, "dout_usi_cmgp03", "gout_usi_cmgp03",
	    DIV_CLK_USI_CMGP03, 0, 4),
	DIV(CLK_DOUT_CMGP_ADC, "dout_cmgp_adc", "mout_cmgp_bus",
	    DIV_CLK_CMGP_ADC, 0, 4),
};

static const struct samsung_gate_clock cmgp_gate_clks[] __initconst = {
	GATE(CLK_GOUT_CMGP_CMGP_CMU_CMGP_PCLK, "gout_cmgp_cmgp_cmu_cmgp_pclk", "mout_cmgp_bus",
	     CLK_BLK_CMGP_UID_CMGP_CMU_CMGP_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_ADC_CMGP_PCLK_S0, "gout_cmgp_adc_cmgp_pclk_s0", "mout_cmgp_bus",
	     GOUT_BLK_CMGP_UID_ADC_CMGP_IPCLKPORT_PCLK_S0, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_ADC_CMGP_PCLK_S1, "gout_cmgp_adc_cmgp_pclk_s1", "mout_cmgp_bus",
	     GOUT_BLK_CMGP_UID_ADC_CMGP_IPCLKPORT_PCLK_S1, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_AXI2APB_CMGP0_ACLK, "gout_cmgp_axi2apb_cmgp0_aclk", "mout_cmgp_bus",
	     GOUT_BLK_CMGP_UID_AXI2APB_CMGP0_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_AXI2APB_CMGP1_ACLK, "gout_cmgp_axi2apb_cmgp1_aclk", "mout_cmgp_bus",
	     GOUT_BLK_CMGP_UID_AXI2APB_CMGP1_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_GPIO_CMGP_PCLK, "gout_cmgp_gpio_cmgp_pclk", "mout_cmgp_bus",
	     GOUT_BLK_CMGP_UID_GPIO_CMGP_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_I2C_CMGP00_PCLK, "gout_cmgp_i2c_cmgp00_pclk", "mout_cmgp_bus",
	     GOUT_BLK_CMGP_UID_I2C_CMGP00_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_I2C_CMGP01_PCLK, "gout_cmgp_i2c_cmgp01_pclk", "mout_cmgp_bus",
	     GOUT_BLK_CMGP_UID_I2C_CMGP01_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_I2C_CMGP02_PCLK, "gout_cmgp_i2c_cmgp02_pclk", "mout_cmgp_bus",
	     GOUT_BLK_CMGP_UID_I2C_CMGP02_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_I2C_CMGP03_PCLK, "gout_cmgp_i2c_cmgp03_pclk", "mout_cmgp_bus",
	     GOUT_BLK_CMGP_UID_I2C_CMGP03_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_SYSREG_CMGP_PCLK, "gout_cmgp_sysreg_cmgp_pclk", "mout_cmgp_bus",
	     GOUT_BLK_CMGP_UID_SYSREG_CMGP_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_USI_CMGP00_PCLK, "gout_cmgp_usi_cmgp00_pclk", "mout_cmgp_bus",
	     GOUT_BLK_CMGP_UID_USI_CMGP00_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_USI_CMGP01_PCLK, "gout_cmgp_usi_cmgp01_pclk", "mout_cmgp_bus",
	     GOUT_BLK_CMGP_UID_USI_CMGP01_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_USI_CMGP02_PCLK, "gout_cmgp_usi_cmgp02_pclk", "mout_cmgp_bus",
	     GOUT_BLK_CMGP_UID_USI_CMGP02_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_USI_CMGP03_PCLK, "gout_cmgp_usi_cmgp03_pclk", "mout_cmgp_bus",
	     GOUT_BLK_CMGP_UID_USI_CMGP03_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_RSTnSYNC_CLK_CMGP_I2C_CMGP00_CLK, "gout_cmgp_rstnsync_clk_cmgp_i2c_cmgp00_clk",
	     "dout_i2c_cmgp",
	     GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_I2C_CMGP00_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_RSTnSYNC_CLK_CMGP_BUS_CLK, "gout_cmgp_rstnsync_clk_cmgp_bus_clk",
	     "mout_cmgp_bus",
	     GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_BUS_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_RSTnSYNC_CLK_CMGP_USI_CMGP00_CLK, "gout_cmgp_rstnsync_clk_cmgp_usi_cmgp00_clk",
	     "dout_usi_cmgp00",
	     GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI_CMGP00_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_RSTnSYNC_CLK_CMGP_USI_CMGP01_CLK, "gout_cmgp_rstnsync_clk_cmgp_usi_cmgp01_clk",
	     "dout_usi_cmgp01",
	     GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI_CMGP01_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_RSTnSYNC_CLK_CMGP_USI_CMGP02_CLK, "gout_cmgp_rstnsync_clk_cmgp_usi_cmgp02_clk",
	     "dout_usi_cmgp02",
	     GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI_CMGP02_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_RSTnSYNC_CLK_CMGP_USI_CMGP03_CLK, "gout_cmgp_rstnsync_clk_cmgp_usi_cmgp03_clk",
	     "dout_usi_cmgp03",
	     GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_USI_CMGP03_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_SYSREG_CMGP2CP_PCLK, "gout_cmgp_sysreg_cmgp2cp_pclk", "mout_cmgp_bus",
	     GOUT_BLK_CMGP_UID_SYSREG_CMGP2CP_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_SYSREG_CMGP2GNSS_PCLK, "gout_cmgp_sysreg_cmgp2gnss_pclk",
	     "mout_cmgp_bus",
	     GOUT_BLK_CMGP_UID_SYSREG_CMGP2GNSS_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_SYSREG_CMGP2CHUB_PCLK, "gout_cmgp_sysreg_cmgp2chub_pclk",
	     "mout_cmgp_bus",
	     GOUT_BLK_CMGP_UID_SYSREG_CMGP2CHUB_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_RSTnSYNC_CLK_CMGP_I2C_CMGP01_CLK, "gout_cmgp_rstnsync_clk_cmgp_i2c_cmgp01_clk",
	     "dout_i2c_cmgp",
	     GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_I2C_CMGP01_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_RSTnSYNC_CLK_CMGP_I2C_CMGP02_CLK, "gout_cmgp_rstnsync_clk_cmgp_i2c_cmgp02_clk",
	     "dout_i2c_cmgp",
	     GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_I2C_CMGP02_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_RSTnSYNC_CLK_CMGP_I2C_CMGP03_CLK, "gout_cmgp_rstnsync_clk_cmgp_i2c_cmgp03_clk",
	     "dout_i2c_cmgp",
	     GOUT_BLK_CMGP_UID_RSTnSYNC_CLK_CMGP_I2C_CMGP03_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_XIU_P_CMGP_ACLK, "gout_cmgp_xiu_p_cmgp_aclk", "mout_cmgp_bus",
	     GOUT_BLK_CMGP_UID_XIU_P_CMGP_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_I2C_CMGP00_IPCLK, "gout_cmgp_i2c_cmgp00_ipclk", "dout_i2c_cmgp",
	     GOUT_BLK_CMGP_UID_I2C_CMGP00_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_I2C_CMGP01_IPCLK, "gout_cmgp_i2c_cmgp01_ipclk", "dout_i2c_cmgp",
	     GOUT_BLK_CMGP_UID_I2C_CMGP01_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_I2C_CMGP02_IPCLK, "gout_cmgp_i2c_cmgp02_ipclk", "dout_i2c_cmgp",
	     GOUT_BLK_CMGP_UID_I2C_CMGP02_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_I2C_CMGP03_IPCLK, "gout_cmgp_i2c_cmgp03_ipclk", "dout_i2c_cmgp",
	     GOUT_BLK_CMGP_UID_I2C_CMGP03_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_LHM_AXI_P_APM2CMGP_I_CLK, "gout_cmgp_lhm_axi_p_apm2cmgp_i_clk",
	     "mout_cmgp_bus",
	     GOUT_BLK_CMGP_UID_LHM_AXI_P_APM2CMGP_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_USI_CMGP00_IPCLK, "gout_cmgp_usi_cmgp00_ipclk", "dout_usi_cmgp00",
	     GOUT_BLK_CMGP_UID_USI_CMGP00_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_USI_CMGP02_IPCLK, "gout_cmgp_usi_cmgp02_ipclk", "dout_usi_cmgp02",
	     GOUT_BLK_CMGP_UID_USI_CMGP02_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_USI_CMGP03_IPCLK, "gout_cmgp_usi_cmgp03_ipclk", "dout_usi_cmgp03",
	     GOUT_BLK_CMGP_UID_USI_CMGP03_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_USI_CMGP01_IPCLK, "gout_cmgp_usi_cmgp01_ipclk", "dout_usi_cmgp01",
	     GOUT_BLK_CMGP_UID_USI_CMGP01_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_I2C_CMGP, "gout_i2c_cmgp", "mout_i2c_cmgp",
	     GATE_CLK_I2C_CMGP, 21, 0, 0),
	GATE(CLK_GOUT_USI_CMGP00, "gout_usi_cmgp00", "mout_usi_cmgp00",
	     GATE_CLK_USI_CMGP00, 21, 0, 0),
	GATE(CLK_GOUT_USI_CMGP01, "gout_usi_cmgp01", "mout_usi_cmgp01",
	     GATE_CLK_USI_CMGP01, 21, 0, 0),
	GATE(CLK_GOUT_USI_CMGP02, "gout_usi_cmgp02", "mout_usi_cmgp02",
	     GATE_CLK_USI_CMGP02, 21, 0, 0),
	GATE(CLK_GOUT_USI_CMGP03, "gout_usi_cmgp03", "mout_usi_cmgp03",
	     GATE_CLK_USI_CMGP03, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_SYSREG_CMGP2PMU_AP_PCLK, "gout_cmgp_sysreg_cmgp2pmu_ap_pclk",
	     "mout_cmgp_bus",
	     GOUT_BLK_CMGP_UID_SYSREG_CMGP2PMU_AP_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_SYSREG_CMGP2PMU_CHUB_PCLK, "gout_cmgp_sysreg_cmgp2pmu_chub_pclk",
	     "mout_cmgp_bus",
	     GOUT_BLK_CMGP_UID_SYSREG_CMGP2PMU_CHUB_IPCLKPORT_PCLK, 21, 0, 0),
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

static void __init exynos9810_cmu_cmgp_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &cmgp_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_cmgp, "samsung,exynos9810-cmu-cmgp",
	       exynos9810_cmu_cmgp_init);

/* ---- CMU_CMU ---------------------------------------------------------*/

/* Register Offset definitions for CMU_CMU (0x1a240000) */
#define PLL_LOCKTIME_PLL_MMC_PLL_LOCK_TIME			0x0000
#define PLL_LOCKTIME_PLL_SHARED0_PLL_LOCK_TIME			0x0004
#define PLL_LOCKTIME_PLL_SHARED1_PLL_LOCK_TIME			0x0008
#define PLL_LOCKTIME_PLL_SHARED2_PLL_LOCK_TIME			0x000c
#define PLL_LOCKTIME_PLL_SHARED3_PLL_LOCK_TIME			0x0010
#define PLL_LOCKTIME_PLL_SHARED4_PLL_LOCK_TIME			0x0014
#define PLL_CON0_PLL_MMC_ENABLE			0x0100
#define PLL_CON0_PLL_SHARED0_ENABLE			0x0120
#define PLL_CON0_PLL_SHARED1_ENABLE			0x01a0
#define PLL_CON0_PLL_SHARED2_ENABLE			0x0220
#define PLL_CON0_PLL_SHARED3_ENABLE			0x02a0
#define PLL_CON0_PLL_SHARED4_ENABLE			0x0320
#define CLKCMU_DPU_BUS			0x1000
#define MUX_CLKCMU_APM_BUS			0x1004
#define MUX_CLKCMU_AUD_CPU			0x1008
#define MUX_CLKCMU_BUS1_BUS			0x100c
#define MUX_CLKCMU_BUSC_BUS			0x1010
#define MUX_CLKCMU_CHUB_BUS			0x1014
#define MUX_CLKCMU_CIS_CLK0			0x1018
#define MUX_CLKCMU_CIS_CLK1			0x101c
#define MUX_CLKCMU_CIS_CLK2			0x1020
#define MUX_CLKCMU_CIS_CLK3			0x1024
#define MUX_CLKCMU_CMGP_BUS			0x1028
#define MUX_CLKCMU_CORE_BUS			0x102c
#define MUX_CLKCMU_CPUCL0_DBG_BUS			0x1030
#define MUX_CLKCMU_CPUCL0_SWITCH			0x1034
#define MUX_CLKCMU_CPUCL1_SWITCH			0x1038
#define MUX_CLKCMU_DCF_BUS			0x103c
#define MUX_CLKCMU_DCPOST_BUS			0x1040
#define MUX_CLKCMU_DCRD_BUS			0x1044
#define MUX_CLKCMU_DPU_BUS			0x1048
#define MUX_CLKCMU_DSPM_BUS			0x104c
#define MUX_CLKCMU_DSPS_AUD			0x1050
#define MUX_CLKCMU_FSYS0_BUS			0x1054
#define MUX_CLKCMU_FSYS0_DPGTC			0x1058
#define MUX_CLKCMU_FSYS0_UFS_EMBD			0x105c
#define MUX_CLKCMU_FSYS0_USB30DRD			0x1060
#define MUX_CLKCMU_FSYS0_USBDP_DEBUG			0x1064
#define MUX_CLKCMU_FSYS1_BUS			0x1068
#define MUX_CLKCMU_FSYS1_MMC_CARD			0x106c
#define MUX_CLKCMU_FSYS1_PCIE			0x1070
#define MUX_CLKCMU_FSYS1_UFS_CARD			0x1074
#define MUX_CLKCMU_G2D_G2D			0x1078
#define MUX_CLKCMU_G2D_MSCL			0x107c
#define MUX_CLKCMU_HPM			0x1080
#define MUX_CLKCMU_ISPHQ_BUS			0x1084
#define MUX_CLKCMU_ISPLP_BUS			0x1088
#define MUX_CLKCMU_ISPLP_GDC			0x108c
#define MUX_CLKCMU_ISPLP_VRA			0x1090
#define MUX_CLKCMU_ISPPRE_BUS			0x1094
#define MUX_CLKCMU_IVA_BUS			0x1098
#define MUX_CLKCMU_MFC_BUS			0x109c
#define MUX_CLKCMU_MFC_WFD			0x10a0
#define MUX_CLKCMU_MIF_BUSP			0x10a4
#define MUX_CLKCMU_MIF_SWITCH			0x10a8
#define MUX_CLKCMU_PERIC0_BUS			0x10ac
#define MUX_CLKCMU_PERIC0_IP			0x10b0
#define MUX_CLKCMU_PERIC1_BUS			0x10b4
#define MUX_CLKCMU_PERIC1_IP			0x10b8
#define MUX_CLKCMU_PERIS_BUS			0x10bc
#define MUX_CLKCMU_VTS_BUS			0x10c0
#define MUX_CLK_CMU_CMUREF			0x10c4
#define MUX_CMU_CMUREF			0x10c8
#define CLKCMU_APM_BUS			0x1800
#define CLKCMU_AUD_CPU			0x1804
#define CLKCMU_BUS1_BUS			0x1808
#define CLKCMU_BUSC_BUS			0x180c
#define CLKCMU_CHUB_BUS			0x1810
#define CLKCMU_CIS_CLK0			0x1814
#define CLKCMU_CIS_CLK1			0x1818
#define CLKCMU_CIS_CLK2			0x181c
#define CLKCMU_CIS_CLK3			0x1820
#define CLKCMU_CMGP_BUS			0x1824
#define CLKCMU_CORE_BUS			0x1828
#define CLKCMU_CPUCL0_DBG_BUS			0x182c
#define CLKCMU_CPUCL0_SWITCH			0x1830
#define CLKCMU_CPUCL1_SWITCH			0x1834
#define CLKCMU_DCF_BUS			0x1838
#define CLKCMU_DCPOST_BUS			0x183c
#define CLKCMU_DCRD_BUS			0x1840
#define CLKCMU_DSPM_BUS			0x1844
#define CLKCMU_DSPS_AUD			0x1848
#define CLKCMU_FSYS0_BUS			0x184c
#define CLKCMU_FSYS0_DPGTC			0x1850
#define CLKCMU_FSYS0_UFS_EMBD			0x1854
#define CLKCMU_FSYS0_USB30DRD			0x1858
#define CLKCMU_FSYS1_BUS			0x1860
#define CLKCMU_FSYS1_MMC_CARD			0x1864
#define CLKCMU_FSYS1_UFS_CARD			0x186c
#define CLKCMU_G2D_G2D			0x1870
#define CLKCMU_G2D_MSCL			0x1874
#define CLKCMU_G3D_SWITCH			0x1878
#define CLKCMU_HPM			0x187c
#define CLKCMU_ISPHQ_BUS			0x1880
#define CLKCMU_ISPLP_BUS			0x1884
#define CLKCMU_ISPLP_GDC			0x1888
#define CLKCMU_ISPLP_VRA			0x188c
#define CLKCMU_ISPPRE_BUS			0x1890
#define CLKCMU_IVA_BUS			0x1894
#define CLKCMU_MFC_BUS			0x1898
#define CLKCMU_MFC_WFD			0x189c
#define CLKCMU_MIF_BUSP			0x18a0
#define CLKCMU_MODEM_SHARED0			0x18a4
#define CLKCMU_MODEM_SHARED1			0x18a8
#define CLKCMU_PERIC0_BUS			0x18b0
#define CLKCMU_PERIC0_IP			0x18b4
#define CLKCMU_PERIC1_BUS			0x18b8
#define CLKCMU_PERIC1_IP			0x18bc
#define CLKCMU_PERIS_BUS			0x18c0
#define CLKCMU_VTS_BUS			0x18c4
#define DIV_CLKCMU_DPU			0x18c8
#define DIV_CLKCMU_DPU_BUS			0x18cc
#define DIV_CLK_CMU_CMUREF			0x18d0
#define DIV_PLL_SHARED0_DIV2			0x18d4
#define DIV_PLL_SHARED0_DIV3			0x18d8
#define DIV_PLL_SHARED0_DIV4			0x18dc
#define DIV_PLL_SHARED1_DIV2			0x18e0
#define DIV_PLL_SHARED1_DIV3			0x18e4
#define DIV_PLL_SHARED1_DIV4			0x18e8
#define DIV_PLL_SHARED2_DIV2			0x18ec
#define DIV_PLL_SHARED3_DIV2			0x18f0
#define DIV_PLL_SHARED4_DIV2			0x18f4
#define CLKCMU_MIF_SWITCH			0x2000
#define GATE_CLKCMU_APM_BUS			0x2004
#define GATE_CLKCMU_AUD_CPU			0x2008
#define GATE_CLKCMU_BUS1_BUS			0x200c
#define GATE_CLKCMU_BUSC_BUS			0x2010
#define GATE_CLKCMU_CHUB_BUS			0x2014
#define GATE_CLKCMU_CIS_CLK0			0x2018
#define GATE_CLKCMU_CIS_CLK1			0x201c
#define GATE_CLKCMU_CIS_CLK2			0x2020
#define GATE_CLKCMU_CIS_CLK3			0x2024
#define GATE_CLKCMU_CMGP_BUS			0x2028
#define GATE_CLKCMU_CORE_BUS			0x202c
#define GATE_CLKCMU_CPUCL0_DBG_BUS			0x2030
#define GATE_CLKCMU_CPUCL0_SWITCH			0x2034
#define GATE_CLKCMU_CPUCL1_SWITCH			0x2038
#define GATE_CLKCMU_DCF_BUS			0x203c
#define GATE_CLKCMU_DCPOST_BUS			0x2040
#define GATE_CLKCMU_DCRD_BUS			0x2044
#define GATE_CLKCMU_DPU			0x2048
#define GATE_CLKCMU_DPU_BUS			0x204c
#define GATE_CLKCMU_DSPM_BUS			0x2050
#define GATE_CLKCMU_DSPS_AUD			0x2054
#define GATE_CLKCMU_FSYS0_BUS			0x2058
#define GATE_CLKCMU_FSYS0_DPGTC			0x205c
#define GATE_CLKCMU_FSYS0_UFS_EMBD			0x2060
#define GATE_CLKCMU_FSYS0_USB30DRD			0x2064
#define GATE_CLKCMU_FSYS0_USBDP_DEBUG			0x2068
#define GATE_CLKCMU_FSYS1_BUS			0x206c
#define GATE_CLKCMU_FSYS1_MMC_CARD			0x2070
#define GATE_CLKCMU_FSYS1_PCIE			0x2074
#define GATE_CLKCMU_FSYS1_UFS_CARD			0x2078
#define GATE_CLKCMU_G2D_G2D			0x207c
#define GATE_CLKCMU_G2D_MSCL			0x2080
#define GATE_CLKCMU_G3D_SWITCH			0x2084
#define GATE_CLKCMU_HPM			0x2088
#define GATE_CLKCMU_ISPHQ_BUS			0x208c
#define GATE_CLKCMU_ISPLP_BUS			0x2090
#define GATE_CLKCMU_ISPLP_GDC			0x2094
#define GATE_CLKCMU_ISPLP_VRA			0x2098
#define GATE_CLKCMU_ISPPRE_BUS			0x209c
#define GATE_CLKCMU_IVA_BUS			0x20a0
#define GATE_CLKCMU_MFC_BUS			0x20a4
#define GATE_CLKCMU_MFC_WFD			0x20a8
#define GATE_CLKCMU_MIF_BUSP			0x20ac
#define GATE_CLKCMU_MODEM_SHARED0			0x20b0
#define GATE_CLKCMU_MODEM_SHARED1			0x20b4
#define GATE_CLKCMU_PERIC0_BUS			0x20b8
#define GATE_CLKCMU_PERIC0_IP			0x20bc
#define GATE_CLKCMU_PERIC1_BUS			0x20c0
#define GATE_CLKCMU_PERIC1_IP			0x20c4
#define GATE_CLKCMU_PERIS_BUS			0x20c8
#define GATE_CLKCMU_VTS_BUS			0x20cc

static const unsigned long cmu_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_MMC_PLL_LOCK_TIME,
	PLL_LOCKTIME_PLL_SHARED0_PLL_LOCK_TIME,
	PLL_LOCKTIME_PLL_SHARED1_PLL_LOCK_TIME,
	PLL_LOCKTIME_PLL_SHARED2_PLL_LOCK_TIME,
	PLL_LOCKTIME_PLL_SHARED3_PLL_LOCK_TIME,
	PLL_LOCKTIME_PLL_SHARED4_PLL_LOCK_TIME,
	PLL_CON0_PLL_MMC_ENABLE,
	PLL_CON0_PLL_SHARED0_ENABLE,
	PLL_CON0_PLL_SHARED1_ENABLE,
	PLL_CON0_PLL_SHARED2_ENABLE,
	PLL_CON0_PLL_SHARED3_ENABLE,
	PLL_CON0_PLL_SHARED4_ENABLE,
	CLKCMU_DPU_BUS,
	MUX_CLKCMU_APM_BUS,
	MUX_CLKCMU_AUD_CPU,
	MUX_CLKCMU_BUS1_BUS,
	MUX_CLKCMU_BUSC_BUS,
	MUX_CLKCMU_CHUB_BUS,
	MUX_CLKCMU_CIS_CLK0,
	MUX_CLKCMU_CIS_CLK1,
	MUX_CLKCMU_CIS_CLK2,
	MUX_CLKCMU_CIS_CLK3,
	MUX_CLKCMU_CMGP_BUS,
	MUX_CLKCMU_CORE_BUS,
	MUX_CLKCMU_CPUCL0_DBG_BUS,
	MUX_CLKCMU_CPUCL0_SWITCH,
	MUX_CLKCMU_CPUCL1_SWITCH,
	MUX_CLKCMU_DCF_BUS,
	MUX_CLKCMU_DCPOST_BUS,
	MUX_CLKCMU_DCRD_BUS,
	MUX_CLKCMU_DPU_BUS,
	MUX_CLKCMU_DSPM_BUS,
	MUX_CLKCMU_DSPS_AUD,
	MUX_CLKCMU_FSYS0_BUS,
	MUX_CLKCMU_FSYS0_DPGTC,
	MUX_CLKCMU_FSYS0_UFS_EMBD,
	MUX_CLKCMU_FSYS0_USB30DRD,
	MUX_CLKCMU_FSYS0_USBDP_DEBUG,
	MUX_CLKCMU_FSYS1_BUS,
	MUX_CLKCMU_FSYS1_MMC_CARD,
	MUX_CLKCMU_FSYS1_PCIE,
	MUX_CLKCMU_FSYS1_UFS_CARD,
	MUX_CLKCMU_G2D_G2D,
	MUX_CLKCMU_G2D_MSCL,
	MUX_CLKCMU_HPM,
	MUX_CLKCMU_ISPHQ_BUS,
	MUX_CLKCMU_ISPLP_BUS,
	MUX_CLKCMU_ISPLP_GDC,
	MUX_CLKCMU_ISPLP_VRA,
	MUX_CLKCMU_ISPPRE_BUS,
	MUX_CLKCMU_IVA_BUS,
	MUX_CLKCMU_MFC_BUS,
	MUX_CLKCMU_MFC_WFD,
	MUX_CLKCMU_MIF_BUSP,
	MUX_CLKCMU_MIF_SWITCH,
	MUX_CLKCMU_PERIC0_BUS,
	MUX_CLKCMU_PERIC0_IP,
	MUX_CLKCMU_PERIC1_BUS,
	MUX_CLKCMU_PERIC1_IP,
	MUX_CLKCMU_PERIS_BUS,
	MUX_CLKCMU_VTS_BUS,
	MUX_CLK_CMU_CMUREF,
	MUX_CMU_CMUREF,
	CLKCMU_APM_BUS,
	CLKCMU_AUD_CPU,
	CLKCMU_BUS1_BUS,
	CLKCMU_BUSC_BUS,
	CLKCMU_CHUB_BUS,
	CLKCMU_CIS_CLK0,
	CLKCMU_CIS_CLK1,
	CLKCMU_CIS_CLK2,
	CLKCMU_CIS_CLK3,
	CLKCMU_CMGP_BUS,
	CLKCMU_CORE_BUS,
	CLKCMU_CPUCL0_DBG_BUS,
	CLKCMU_CPUCL0_SWITCH,
	CLKCMU_CPUCL1_SWITCH,
	CLKCMU_DCF_BUS,
	CLKCMU_DCPOST_BUS,
	CLKCMU_DCRD_BUS,
	CLKCMU_DSPM_BUS,
	CLKCMU_DSPS_AUD,
	CLKCMU_FSYS0_BUS,
	CLKCMU_FSYS0_DPGTC,
	CLKCMU_FSYS0_UFS_EMBD,
	CLKCMU_FSYS0_USB30DRD,
	CLKCMU_FSYS1_BUS,
	CLKCMU_FSYS1_MMC_CARD,
	CLKCMU_FSYS1_UFS_CARD,
	CLKCMU_G2D_G2D,
	CLKCMU_G2D_MSCL,
	CLKCMU_G3D_SWITCH,
	CLKCMU_HPM,
	CLKCMU_ISPHQ_BUS,
	CLKCMU_ISPLP_BUS,
	CLKCMU_ISPLP_GDC,
	CLKCMU_ISPLP_VRA,
	CLKCMU_ISPPRE_BUS,
	CLKCMU_IVA_BUS,
	CLKCMU_MFC_BUS,
	CLKCMU_MFC_WFD,
	CLKCMU_MIF_BUSP,
	CLKCMU_MODEM_SHARED0,
	CLKCMU_MODEM_SHARED1,
	CLKCMU_PERIC0_BUS,
	CLKCMU_PERIC0_IP,
	CLKCMU_PERIC1_BUS,
	CLKCMU_PERIC1_IP,
	CLKCMU_PERIS_BUS,
	CLKCMU_VTS_BUS,
	DIV_CLKCMU_DPU,
	DIV_CLKCMU_DPU_BUS,
	DIV_CLK_CMU_CMUREF,
	DIV_PLL_SHARED0_DIV2,
	DIV_PLL_SHARED0_DIV3,
	DIV_PLL_SHARED0_DIV4,
	DIV_PLL_SHARED1_DIV2,
	DIV_PLL_SHARED1_DIV3,
	DIV_PLL_SHARED1_DIV4,
	DIV_PLL_SHARED2_DIV2,
	DIV_PLL_SHARED3_DIV2,
	DIV_PLL_SHARED4_DIV2,
	CLKCMU_MIF_SWITCH,
	GATE_CLKCMU_APM_BUS,
	GATE_CLKCMU_AUD_CPU,
	GATE_CLKCMU_BUS1_BUS,
	GATE_CLKCMU_BUSC_BUS,
	GATE_CLKCMU_CHUB_BUS,
	GATE_CLKCMU_CIS_CLK0,
	GATE_CLKCMU_CIS_CLK1,
	GATE_CLKCMU_CIS_CLK2,
	GATE_CLKCMU_CIS_CLK3,
	GATE_CLKCMU_CMGP_BUS,
	GATE_CLKCMU_CORE_BUS,
	GATE_CLKCMU_CPUCL0_DBG_BUS,
	GATE_CLKCMU_CPUCL0_SWITCH,
	GATE_CLKCMU_CPUCL1_SWITCH,
	GATE_CLKCMU_DCF_BUS,
	GATE_CLKCMU_DCPOST_BUS,
	GATE_CLKCMU_DCRD_BUS,
	GATE_CLKCMU_DPU,
	GATE_CLKCMU_DPU_BUS,
	GATE_CLKCMU_DSPM_BUS,
	GATE_CLKCMU_DSPS_AUD,
	GATE_CLKCMU_FSYS0_BUS,
	GATE_CLKCMU_FSYS0_DPGTC,
	GATE_CLKCMU_FSYS0_UFS_EMBD,
	GATE_CLKCMU_FSYS0_USB30DRD,
	GATE_CLKCMU_FSYS0_USBDP_DEBUG,
	GATE_CLKCMU_FSYS1_BUS,
	GATE_CLKCMU_FSYS1_MMC_CARD,
	GATE_CLKCMU_FSYS1_PCIE,
	GATE_CLKCMU_FSYS1_UFS_CARD,
	GATE_CLKCMU_G2D_G2D,
	GATE_CLKCMU_G2D_MSCL,
	GATE_CLKCMU_G3D_SWITCH,
	GATE_CLKCMU_HPM,
	GATE_CLKCMU_ISPHQ_BUS,
	GATE_CLKCMU_ISPLP_BUS,
	GATE_CLKCMU_ISPLP_GDC,
	GATE_CLKCMU_ISPLP_VRA,
	GATE_CLKCMU_ISPPRE_BUS,
	GATE_CLKCMU_IVA_BUS,
	GATE_CLKCMU_MFC_BUS,
	GATE_CLKCMU_MFC_WFD,
	GATE_CLKCMU_MIF_BUSP,
	GATE_CLKCMU_MODEM_SHARED0,
	GATE_CLKCMU_MODEM_SHARED1,
	GATE_CLKCMU_PERIC0_BUS,
	GATE_CLKCMU_PERIC0_IP,
	GATE_CLKCMU_PERIC1_BUS,
	GATE_CLKCMU_PERIC1_IP,
	GATE_CLKCMU_PERIS_BUS,
	GATE_CLKCMU_VTS_BUS,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long cmu_qch_regs[] __initconst = {
	0x3000,	/* CMU_CMU_CMUREF_QCH */
	0x3004,	/* DFTMUX_TOP_QCH_CIS_CLK0 */
	0x3008,	/* DFTMUX_TOP_QCH_CIS_CLK1 */
	0x300c,	/* DFTMUX_TOP_QCH_CIS_CLK2 */
	0x3010,	/* DFTMUX_TOP_QCH_CIS_CLK3 */
	0x3014,	/* OTP_QCH */
};

static const struct samsung_fixed_rate_clock cmu_fixed_clks[] __initconst = {
	FRATE(CLK_PAD_CLK_APM_BUS, "pad_clk_apm_bus", NULL, 0, 100000000),
	FRATE(CLK_PAD_CLK_BUS1_BUS, "pad_clk_bus1_bus", NULL, 0, 100000000),
	FRATE(CLK_PAD_CLK_CPUCL0_DBG_PCLKDBG, "pad_clk_cpucl0_dbg_pclkdbg", NULL, 0, 100000000),
	FRATE(CLK_CLK_CLUSTER1_DIV_ACLK, "clk_cluster1_div_aclk", NULL, 0, 100000000),
	FRATE(CLK_CLK_CLUSTER1_DIV_ATCLK, "clk_cluster1_div_atclk", NULL, 0, 100000000),
	FRATE(CLK_PAD_CLK_DCPOST_BUSD, "pad_clk_dcpost_busd", NULL, 0, 100000000),
	FRATE(CLK_CLK_DEBUG_DECON0, "clk_debug_decon0", NULL, 0, 100000000),
	FRATE(CLK_CLK_DEBUG_DECON1, "clk_debug_decon1", NULL, 0, 100000000),
	FRATE(CLK_CLK_DEBUG_DECON2, "clk_debug_decon2", NULL, 0, 100000000),
	FRATE(CLK_PAD_CLK_DSPM_BUSD, "pad_clk_dspm_busd", NULL, 0, 100000000),
	FRATE(CLK_PAD_CLK_DSPM_BUSP, "pad_clk_dspm_busp", NULL, 0, 100000000),
	FRATE(CLK_PAD_CLK_FSYS0_UFS_EMBD, "pad_clk_fsys0_ufs_embd", NULL, 0, 100000000),
	FRATE(CLK_USBDPPHY_TXCLK_CH0, "usbdpphy_txclk_ch0", NULL, 0, 100000000),
	FRATE(CLK_USBDPPHY_RXCLK_CH0, "usbdpphy_rxclk_ch0", NULL, 0, 100000000),
	FRATE(CLK_USBDPPHY_DP_TXCLK, "usbdpphy_dp_txclk", NULL, 0, 100000000),
	FRATE(CLK_USB20PHY_PHY_CLOCK, "usb20phy_phy_clock", NULL, 0, 100000000),
	FRATE(CLK_USBDPPHY_VCOCLK_DIV40_MON, "usbdpphy_vcoclk_div40_mon", NULL, 0, 100000000),
	FRATE(CLK_PAD_CLK_FSYS1_BUS, "pad_clk_fsys1_bus", NULL, 0, 100000000),
	FRATE(CLK_PAD_CLK_FSYS1_MMC_CARD, "pad_clk_fsys1_mmc_card", NULL, 0, 100000000),
	FRATE(CLK_PAD_CLK_FSYS1_UFS_CARD, "pad_clk_fsys1_ufs_card", NULL, 0, 100000000),
	FRATE(CLK_CLK_G3D_GPU_FEEDBACK, "clk_g3d_gpu_feedback", NULL, 0, 800000000),
	FRATE(CLK_RTCCLK_VTS, "rtcclk_vts", NULL, 0, 32767),
};

static const struct samsung_pll_rate_table fout_shared1_rate_table[] __initconst = {
	PLL_35XX_RATE(26 * MHZ, 1865500000U, 287, 4, 0),
};

static const struct samsung_pll_rate_table fout_shared4_rate_table[] __initconst = {
	PLL_35XX_RATE(26 * MHZ, 672000000U, 336, 13, 0),
};

static const struct samsung_pll_rate_table fout_shared3_rate_table[] __initconst = {
	PLL_35XX_RATE(26 * MHZ, 640000000U, 320, 13, 0),
};

static const struct samsung_pll_rate_table fout_shared2_rate_table[] __initconst = {
	PLL_35XX_RATE(26 * MHZ, 800000000U, 400, 13, 0),
};

static const struct samsung_pll_rate_table fout_shared0_rate_table[] __initconst = {
	PLL_35XX_RATE(26 * MHZ, 2132000000U, 328, 4, 0),
};

static const struct samsung_pll_clock cmu_pll_clks[] __initconst = {
	PLL(pll_1017x, CLK_FOUT_SHARED1, "fout_shared1", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED1_PLL_LOCK_TIME, PLL_CON0_PLL_SHARED1_ENABLE, fout_shared1_rate_table),
	PLL(pll_1018x, CLK_FOUT_SHARED4, "fout_shared4", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED4_PLL_LOCK_TIME, PLL_CON0_PLL_SHARED4_ENABLE, fout_shared4_rate_table),
	PLL(pll_1018x, CLK_FOUT_SHARED3, "fout_shared3", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED3_PLL_LOCK_TIME, PLL_CON0_PLL_SHARED3_ENABLE, fout_shared3_rate_table),
	PLL(pll_1018x, CLK_FOUT_SHARED2, "fout_shared2", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED2_PLL_LOCK_TIME, PLL_CON0_PLL_SHARED2_ENABLE, fout_shared2_rate_table),
	PLL(pll_1017x, CLK_FOUT_SHARED0, "fout_shared0", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED0_PLL_LOCK_TIME, PLL_CON0_PLL_SHARED0_ENABLE, fout_shared0_rate_table),
	PLL(pll_1031x, CLK_FOUT_MMC, "fout_mmc", "oscclk",
	    PLL_LOCKTIME_PLL_MMC_PLL_LOCK_TIME, PLL_CON0_PLL_MMC_ENABLE, NULL),
};

/* List of parent clocks for Muxes in CMU_CMU */
PNAME(mout_cmu_bus1_bus_p) = { "dout_pll_shared0_div4", "dout_pll_shared1_div4", "dout_pll_shared2_div2", "dout_pll_shared4_div2" };
PNAME(mout_cmu_mfc_bus_p) = { "dout_pll_shared0_div2", "fout_shared4", "dout_pll_shared0_div4", "dout_pll_shared1_div4", "dout_pll_shared2_div2", "oscclk", "oscclk", "oscclk" };
PNAME(mout_cmu_fsys0_usb30drd_p) = { "oscclk", "dout_pll_shared0_div4", "dout_pll_shared2_div2", "dout_pll_shared4_div2" };
PNAME(mout_cmu_fsys0_ufs_embd_p) = { "oscclk", "dout_pll_shared0_div4", "dout_pll_shared2_div2", "dout_pll_shared4_div2" };
PNAME(mout_cmu_cmgp_bus_p) = { "dout_pll_shared0_div4", "dout_pll_shared2_div2" };
PNAME(mout_cmu_busc_bus_p) = { "dout_pll_shared0_div4", "dout_pll_shared1_div4", "dout_pll_shared2_div2", "dout_pll_shared4_div2" };
PNAME(mout_cmu_g2d_g2d_p) = { "dout_pll_shared0_div2", "dout_pll_shared0_div3", "fout_shared4", "dout_pll_shared0_div4", "dout_pll_shared1_div4", "dout_pll_shared2_div2", "oscclk", "oscclk" };
PNAME(mout_cmu_fsys1_mmc_card_p) = { "oscclk", "fout_shared2", "fout_shared4", "dout_pll_shared0_div4", "fout_mmc", "oscclk", "oscclk", "oscclk" };
PNAME(mout_cmu_dspm_bus_p) = { "dout_pll_shared0_div4", "dout_pll_shared1_div4", "dout_pll_shared2_div2", "dout_pll_shared4_div2" };
PNAME(mout_cmu_cpucl0_switch_p) = { "dout_pll_shared0_div2", "dout_pll_shared1_div2", "fout_shared2", "fout_shared4" };
PNAME(mout_cmu_core_bus_p) = { "dout_pll_shared0_div2", "dout_pll_shared1_div2", "fout_shared2", "dout_pll_shared1_div3", "fout_shared4", "dout_pll_shared0_div4", "fout_shared3", "fout_mmc" };
PNAME(mout_cmu_mif_switch_p) = { "fout_shared0", "fout_shared1", "dout_pll_shared0_div2", "dout_pll_shared1_div2", "fout_shared2", "dout_pll_shared0_div4", "dout_pll_shared2_div2", "oscclk" };
PNAME(mout_cmu_isppre_bus_p) = { "dout_pll_shared0_div4", "dout_pll_shared1_div4", "dout_pll_shared2_div2", "dout_pll_shared4_div2" };
PNAME(mout_cmu_isplp_bus_p) = { "dout_pll_shared0_div4", "dout_pll_shared1_div4", "dout_pll_shared2_div2", "dout_pll_shared4_div2" };
PNAME(mout_cmu_isphq_bus_p) = { "dout_pll_shared0_div4", "dout_pll_shared1_div4", "dout_pll_shared2_div2", "dout_pll_shared4_div2" };
PNAME(mout_cmu_aud_cpu_p) = { "dout_pll_shared0_div2", "dout_pll_shared1_div2", "fout_shared2", "fout_shared4" };
PNAME(mout_cmu_g2d_mscl_p) = { "dout_pll_shared0_div4", "dout_pll_shared1_div4", "dout_pll_shared2_div2", "dout_pll_shared4_div2" };
PNAME(mout_cmu_hpm_p) = { "oscclk", "fout_shared2", "dout_pll_shared0_div4", "dout_pll_shared1_div4" };
PNAME(mout_cmu_cpucl0_dbg_bus_p) = { "fout_shared2", "dout_pll_shared0_div3", "fout_shared4", "dout_pll_shared0_div4" };
PNAME(mout_cmu_fsys0_bus_p) = { "dout_pll_shared1_div2", "fout_shared2", "fout_shared4", "dout_pll_shared0_div4" };
PNAME(mout_cmu_cis_clk0_p) = { "oscclk", "dout_pll_shared2_div2" };
PNAME(mout_cmu_cis_clk1_p) = { "oscclk", "dout_pll_shared2_div2" };
PNAME(mout_cmu_cis_clk2_p) = { "oscclk", "dout_pll_shared2_div2" };
PNAME(mout_cmu_cis_clk3_p) = { "oscclk", "dout_pll_shared2_div2" };
PNAME(mout_cmu_iva_bus_p) = { "dout_pll_shared0_div4", "dout_pll_shared1_div4", "dout_pll_shared2_div2", "dout_pll_shared4_div2" };
PNAME(mout_cmu_fsys1_ufs_card_p) = { "oscclk", "dout_pll_shared0_div4", "dout_pll_shared2_div2", "dout_pll_shared4_div2" };
PNAME(mout_cmu_cmuref_p) = { "oscclk", "dout_cmu_cmuref" };
PNAME(mout_cmu_peric0_bus_p) = { "dout_pll_shared0_div4", "dout_pll_shared2_div2" };
PNAME(mout_cmu_peric1_bus_p) = { "dout_pll_shared0_div4", "dout_pll_shared2_div2" };
PNAME(mout_cmu_peris_bus_p) = { "dout_pll_shared0_div4", "dout_pll_shared2_div2" };
PNAME(mout_cmu_dcrd_bus_p) = { "fout_shared4", "dout_pll_shared0_div4", "dout_pll_shared1_div4", "dout_pll_shared2_div2" };
PNAME(mout_cmu_fsys0_dpgtc_p) = { "oscclk", "dout_pll_shared0_div4", "dout_pll_shared2_div2", "dout_pll_shared4_div2" };
PNAME(mout_cmu_fsys1_pcie_p) = { "oscclk", "fout_shared2" };
PNAME(mout_cmu_chub_bus_p) = { "dout_pll_shared0_div3", "dout_pll_shared2_div2" };
PNAME(mout_cmu_dcf_bus_p) = { "dout_pll_shared0_div4", "dout_pll_shared1_div4", "dout_pll_shared2_div2", "dout_pll_shared4_div2" };
PNAME(mout_cmu_apm_bus_p) = { "dout_pll_shared0_div3", "dout_pll_shared2_div2" };
PNAME(mout_cmu_fsys1_bus_p) = { "dout_pll_shared1_div2", "fout_shared2", "fout_shared4", "dout_pll_shared0_div4" };
PNAME(mout_mux_clk_cmu_cmuref_p) = { "dout_pll_shared0_div4", "dout_pll_shared2_div2" };
PNAME(mout_cmu_cpucl1_switch_p) = { "dout_pll_shared0_div2", "dout_pll_shared1_div2", "fout_shared2", "fout_shared4" };
PNAME(mout_cmu_vts_bus_p) = { "dout_pll_shared0_div4", "dout_pll_shared2_div2" };
PNAME(mout_cmu_isplp_vra_p) = { "dout_pll_shared0_div4", "dout_pll_shared1_div4", "dout_pll_shared2_div2", "dout_pll_shared4_div2" };
PNAME(mout_cmu_mfc_wfd_p) = { "fout_shared4", "dout_pll_shared0_div4", "dout_pll_shared1_div4", "dout_pll_shared2_div2" };
PNAME(mout_cmu_mif_busp_p) = { "dout_pll_shared0_div4", "dout_pll_shared1_div4" };
PNAME(mout_cmu_peric0_ip_p) = { "dout_pll_shared0_div4", "dout_pll_shared2_div2" };
PNAME(mout_cmu_peric1_ip_p) = { "dout_pll_shared0_div4", "dout_pll_shared2_div2" };
PNAME(mout_cmu_dcpost_bus_p) = { "dout_pll_shared0_div4", "dout_pll_shared1_div4", "dout_pll_shared2_div2", "dout_pll_shared4_div2" };
PNAME(mout_cmu_fsys0_usbdp_debug_p) = { "oscclk", "fout_shared2" };
PNAME(mout_cmu_isplp_gdc_p) = { "dout_pll_shared0_div4", "dout_pll_shared1_div4", "dout_pll_shared2_div2", "dout_pll_shared4_div2" };
PNAME(mout_cmu_dsps_aud_p) = { "dout_pll_shared0_div4", "dout_pll_shared1_div4", "dout_pll_shared2_div2", "dout_pll_shared4_div2" };
PNAME(mout_cmu_dpu_bus_p) = { "dout_clkcmu_dpu", "dout_clkcmu_dpu_bus" };
PNAME(mout_mux_clkcmu_dpu_bus_p) = { "fout_shared3", "fout_shared4", "dout_pll_shared1_div3", "dout_pll_shared2_div2" };

static const struct samsung_mux_clock cmu_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_BUS1_BUS, "mout_cmu_bus1_bus", mout_cmu_bus1_bus_p,
	    MUX_CLKCMU_BUS1_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_MFC_BUS, "mout_cmu_mfc_bus", mout_cmu_mfc_bus_p,
	    MUX_CLKCMU_MFC_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_FSYS0_USB30DRD, "mout_cmu_fsys0_usb30drd", mout_cmu_fsys0_usb30drd_p,
	    MUX_CLKCMU_FSYS0_USB30DRD, 0, 2),
	MUX(CLK_MOUT_CMU_FSYS0_UFS_EMBD, "mout_cmu_fsys0_ufs_embd", mout_cmu_fsys0_ufs_embd_p,
	    MUX_CLKCMU_FSYS0_UFS_EMBD, 0, 2),
	MUX(CLK_MOUT_CMU_CMGP_BUS, "mout_cmu_cmgp_bus", mout_cmu_cmgp_bus_p,
	    MUX_CLKCMU_CMGP_BUS, 0, 1),
	MUX(CLK_MOUT_CMU_BUSC_BUS, "mout_cmu_busc_bus", mout_cmu_busc_bus_p,
	    MUX_CLKCMU_BUSC_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_G2D_G2D, "mout_cmu_g2d_g2d", mout_cmu_g2d_g2d_p,
	    MUX_CLKCMU_G2D_G2D, 0, 3),
	MUX(CLK_MOUT_CMU_FSYS1_MMC_CARD, "mout_cmu_fsys1_mmc_card", mout_cmu_fsys1_mmc_card_p,
	    MUX_CLKCMU_FSYS1_MMC_CARD, 0, 3),
	MUX(CLK_MOUT_CMU_DSPM_BUS, "mout_cmu_dspm_bus", mout_cmu_dspm_bus_p,
	    MUX_CLKCMU_DSPM_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_CPUCL0_SWITCH, "mout_cmu_cpucl0_switch", mout_cmu_cpucl0_switch_p,
	    MUX_CLKCMU_CPUCL0_SWITCH, 0, 2),
	MUX(CLK_MOUT_CMU_CORE_BUS, "mout_cmu_core_bus", mout_cmu_core_bus_p,
	    MUX_CLKCMU_CORE_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_MIF_SWITCH, "mout_cmu_mif_switch", mout_cmu_mif_switch_p,
	    MUX_CLKCMU_MIF_SWITCH, 0, 3),
	MUX(CLK_MOUT_CMU_ISPPRE_BUS, "mout_cmu_isppre_bus", mout_cmu_isppre_bus_p,
	    MUX_CLKCMU_ISPPRE_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_ISPLP_BUS, "mout_cmu_isplp_bus", mout_cmu_isplp_bus_p,
	    MUX_CLKCMU_ISPLP_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_ISPHQ_BUS, "mout_cmu_isphq_bus", mout_cmu_isphq_bus_p,
	    MUX_CLKCMU_ISPHQ_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_AUD_CPU, "mout_cmu_aud_cpu", mout_cmu_aud_cpu_p,
	    MUX_CLKCMU_AUD_CPU, 0, 2),
	MUX(CLK_MOUT_CMU_G2D_MSCL, "mout_cmu_g2d_mscl", mout_cmu_g2d_mscl_p,
	    MUX_CLKCMU_G2D_MSCL, 0, 2),
	MUX(CLK_MOUT_CMU_HPM, "mout_cmu_hpm", mout_cmu_hpm_p,
	    MUX_CLKCMU_HPM, 0, 2),
	MUX(CLK_MOUT_CMU_CPUCL0_DBG_BUS, "mout_cmu_cpucl0_dbg_bus", mout_cmu_cpucl0_dbg_bus_p,
	    MUX_CLKCMU_CPUCL0_DBG_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_FSYS0_BUS, "mout_cmu_fsys0_bus", mout_cmu_fsys0_bus_p,
	    MUX_CLKCMU_FSYS0_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_CIS_CLK0, "mout_cmu_cis_clk0", mout_cmu_cis_clk0_p,
	    MUX_CLKCMU_CIS_CLK0, 0, 1),
	MUX(CLK_MOUT_CMU_CIS_CLK1, "mout_cmu_cis_clk1", mout_cmu_cis_clk1_p,
	    MUX_CLKCMU_CIS_CLK1, 0, 1),
	MUX(CLK_MOUT_CMU_CIS_CLK2, "mout_cmu_cis_clk2", mout_cmu_cis_clk2_p,
	    MUX_CLKCMU_CIS_CLK2, 0, 1),
	MUX(CLK_MOUT_CMU_CIS_CLK3, "mout_cmu_cis_clk3", mout_cmu_cis_clk3_p,
	    MUX_CLKCMU_CIS_CLK3, 0, 1),
	MUX(CLK_MOUT_CMU_IVA_BUS, "mout_cmu_iva_bus", mout_cmu_iva_bus_p,
	    MUX_CLKCMU_IVA_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_FSYS1_UFS_CARD, "mout_cmu_fsys1_ufs_card", mout_cmu_fsys1_ufs_card_p,
	    MUX_CLKCMU_FSYS1_UFS_CARD, 0, 2),
	MUX(CLK_MOUT_CMU_CMUREF, "mout_cmu_cmuref", mout_cmu_cmuref_p,
	    MUX_CMU_CMUREF, 0, 1),
	MUX(CLK_MOUT_CMU_PERIC0_BUS, "mout_cmu_peric0_bus", mout_cmu_peric0_bus_p,
	    MUX_CLKCMU_PERIC0_BUS, 0, 1),
	MUX(CLK_MOUT_CMU_PERIC1_BUS, "mout_cmu_peric1_bus", mout_cmu_peric1_bus_p,
	    MUX_CLKCMU_PERIC1_BUS, 0, 1),
	MUX(CLK_MOUT_CMU_PERIS_BUS, "mout_cmu_peris_bus", mout_cmu_peris_bus_p,
	    MUX_CLKCMU_PERIS_BUS, 0, 1),
	MUX(CLK_MOUT_CMU_DCRD_BUS, "mout_cmu_dcrd_bus", mout_cmu_dcrd_bus_p,
	    MUX_CLKCMU_DCRD_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_FSYS0_DPGTC, "mout_cmu_fsys0_dpgtc", mout_cmu_fsys0_dpgtc_p,
	    MUX_CLKCMU_FSYS0_DPGTC, 0, 2),
	MUX(CLK_MOUT_CMU_FSYS1_PCIE, "mout_cmu_fsys1_pcie", mout_cmu_fsys1_pcie_p,
	    MUX_CLKCMU_FSYS1_PCIE, 0, 1),
	MUX(CLK_MOUT_CMU_CHUB_BUS, "mout_cmu_chub_bus", mout_cmu_chub_bus_p,
	    MUX_CLKCMU_CHUB_BUS, 0, 1),
	MUX(CLK_MOUT_CMU_DCF_BUS, "mout_cmu_dcf_bus", mout_cmu_dcf_bus_p,
	    MUX_CLKCMU_DCF_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_APM_BUS, "mout_cmu_apm_bus", mout_cmu_apm_bus_p,
	    MUX_CLKCMU_APM_BUS, 0, 1),
	MUX(CLK_MOUT_CMU_FSYS1_BUS, "mout_cmu_fsys1_bus", mout_cmu_fsys1_bus_p,
	    MUX_CLKCMU_FSYS1_BUS, 0, 2),
	MUX(CLK_MOUT_MUX_CLK_CMU_CMUREF, "mout_mux_clk_cmu_cmuref", mout_mux_clk_cmu_cmuref_p,
	    MUX_CLK_CMU_CMUREF, 0, 1),
	MUX(CLK_MOUT_CMU_CPUCL1_SWITCH, "mout_cmu_cpucl1_switch", mout_cmu_cpucl1_switch_p,
	    MUX_CLKCMU_CPUCL1_SWITCH, 0, 2),
	MUX(CLK_MOUT_CMU_VTS_BUS, "mout_cmu_vts_bus", mout_cmu_vts_bus_p,
	    MUX_CLKCMU_VTS_BUS, 0, 1),
	MUX(CLK_MOUT_CMU_ISPLP_VRA, "mout_cmu_isplp_vra", mout_cmu_isplp_vra_p,
	    MUX_CLKCMU_ISPLP_VRA, 0, 2),
	MUX(CLK_MOUT_CMU_MFC_WFD, "mout_cmu_mfc_wfd", mout_cmu_mfc_wfd_p,
	    MUX_CLKCMU_MFC_WFD, 0, 2),
	MUX(CLK_MOUT_CMU_MIF_BUSP, "mout_cmu_mif_busp", mout_cmu_mif_busp_p,
	    MUX_CLKCMU_MIF_BUSP, 0, 1),
	MUX(CLK_MOUT_CMU_PERIC0_IP, "mout_cmu_peric0_ip", mout_cmu_peric0_ip_p,
	    MUX_CLKCMU_PERIC0_IP, 0, 1),
	MUX(CLK_MOUT_CMU_PERIC1_IP, "mout_cmu_peric1_ip", mout_cmu_peric1_ip_p,
	    MUX_CLKCMU_PERIC1_IP, 0, 1),
	MUX(CLK_MOUT_CMU_DCPOST_BUS, "mout_cmu_dcpost_bus", mout_cmu_dcpost_bus_p,
	    MUX_CLKCMU_DCPOST_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_FSYS0_USBDP_DEBUG, "mout_cmu_fsys0_usbdp_debug",
	    mout_cmu_fsys0_usbdp_debug_p,
	    MUX_CLKCMU_FSYS0_USBDP_DEBUG, 0, 1),
	MUX(CLK_MOUT_CMU_ISPLP_GDC, "mout_cmu_isplp_gdc", mout_cmu_isplp_gdc_p,
	    MUX_CLKCMU_ISPLP_GDC, 0, 2),
	MUX(CLK_MOUT_CMU_DSPS_AUD, "mout_cmu_dsps_aud", mout_cmu_dsps_aud_p,
	    MUX_CLKCMU_DSPS_AUD, 0, 2),
	MUX(CLK_MOUT_CMU_DPU_BUS, "mout_cmu_dpu_bus", mout_cmu_dpu_bus_p,
	    CLKCMU_DPU_BUS, 0, 1),
	MUX(CLK_MOUT_MUX_CLKCMU_DPU_BUS, "mout_mux_clkcmu_dpu_bus", mout_mux_clkcmu_dpu_bus_p,
	    MUX_CLKCMU_DPU_BUS, 0, 2),
};

static const struct samsung_div_clock cmu_div_clks[] __initconst = {
	DIV(CLK_DOUT_CLKCMU_APM_BUS, "dout_clkcmu_apm_bus", "gout_clkcmu_apm_bus",
	    CLKCMU_APM_BUS, 0, 2),
	DIV(CLK_DOUT_PLL_SHARED0_DIV2, "dout_pll_shared0_div2", "fout_shared0",
	    DIV_PLL_SHARED0_DIV2, 0, 1),
	DIV(CLK_DOUT_CLKCMU_G3D_SWITCH, "dout_clkcmu_g3d_switch", "gout_clkcmu_g3d_switch",
	    CLKCMU_G3D_SWITCH, 0, 3),
	DIV(CLK_DOUT_CLKCMU_PERIC0_BUS, "dout_clkcmu_peric0_bus", "gout_clkcmu_peric0_bus",
	    CLKCMU_PERIC0_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_PERIS_BUS, "dout_clkcmu_peris_bus", "gout_clkcmu_peris_bus",
	    CLKCMU_PERIS_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_FSYS0_BUS, "dout_clkcmu_fsys0_bus", "gout_clkcmu_fsys0_bus",
	    CLKCMU_FSYS0_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_DPU_BUS, "dout_clkcmu_dpu_bus", "gout_clkcmu_dpu_bus",
	    DIV_CLKCMU_DPU_BUS, 0, 4),
	DIV(CLK_DOUT_PLL_SHARED1_DIV2, "dout_pll_shared1_div2", "fout_shared1",
	    DIV_PLL_SHARED1_DIV2, 0, 1),
	DIV(CLK_DOUT_CLKCMU_BUS1_BUS, "dout_clkcmu_bus1_bus", "gout_clkcmu_bus1_bus",
	    CLKCMU_BUS1_BUS, 0, 4),
	DIV(CLK_DOUT_PLL_SHARED2_DIV2, "dout_pll_shared2_div2", "fout_shared2",
	    DIV_PLL_SHARED2_DIV2, 0, 1),
	DIV(CLK_DOUT_PLL_SHARED3_DIV2, "dout_pll_shared3_div2", "fout_shared3",
	    DIV_PLL_SHARED3_DIV2, 0, 1),
	DIV(CLK_DOUT_PLL_SHARED4_DIV2, "dout_pll_shared4_div2", "fout_shared4",
	    DIV_PLL_SHARED4_DIV2, 0, 1),
	DIV(CLK_DOUT_PLL_SHARED0_DIV4, "dout_pll_shared0_div4", "dout_pll_shared0_div2",
	    DIV_PLL_SHARED0_DIV4, 0, 1),
	DIV(CLK_DOUT_CLKCMU_MFC_BUS, "dout_clkcmu_mfc_bus", "gout_clkcmu_mfc_bus",
	    CLKCMU_MFC_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_G2D_G2D, "dout_clkcmu_g2d_g2d", "gout_clkcmu_g2d_g2d",
	    CLKCMU_G2D_G2D, 0, 4),
	DIV(CLK_DOUT_CLKCMU_FSYS0_USB30DRD, "dout_clkcmu_fsys0_usb30drd",
	    "gout_clkcmu_fsys0_usb30drd",
	    CLKCMU_FSYS0_USB30DRD, 0, 4),
	DIV(CLK_DOUT_CLKCMU_FSYS0_UFS_EMBD, "dout_clkcmu_fsys0_ufs_embd",
	    "gout_clkcmu_fsys0_ufs_embd",
	    CLKCMU_FSYS0_UFS_EMBD, 0, 3),
	DIV(CLK_DOUT_CLKCMU_FSYS1_MMC_CARD, "dout_clkcmu_fsys1_mmc_card",
	    "gout_clkcmu_fsys1_mmc_card",
	    CLKCMU_FSYS1_MMC_CARD, 0, 9),
	DIV(CLK_DOUT_CLKCMU_FSYS1_BUS, "dout_clkcmu_fsys1_bus", "gout_clkcmu_fsys1_bus",
	    CLKCMU_FSYS1_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_CMGP_BUS, "dout_clkcmu_cmgp_bus", "gout_clkcmu_cmgp_bus",
	    CLKCMU_CMGP_BUS, 0, 3),
	DIV(CLK_DOUT_CLKCMU_DSPM_BUS, "dout_clkcmu_dspm_bus", "gout_clkcmu_dspm_bus",
	    CLKCMU_DSPM_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_PERIC1_BUS, "dout_clkcmu_peric1_bus", "gout_clkcmu_peric1_bus",
	    CLKCMU_PERIC1_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_BUSC_BUS, "dout_clkcmu_busc_bus", "gout_clkcmu_busc_bus",
	    CLKCMU_BUSC_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_CPUCL1_SWITCH, "dout_clkcmu_cpucl1_switch", "gout_clkcmu_cpucl1_switch",
	    CLKCMU_CPUCL1_SWITCH, 0, 3),
	DIV(CLK_DOUT_CLKCMU_CPUCL0_SWITCH, "dout_clkcmu_cpucl0_switch", "gout_clkcmu_cpucl0_switch",
	    CLKCMU_CPUCL0_SWITCH, 0, 3),
	DIV(CLK_DOUT_CLKCMU_CORE_BUS, "dout_clkcmu_core_bus", "gout_clkcmu_core_bus",
	    CLKCMU_CORE_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_ISPPRE_BUS, "dout_clkcmu_isppre_bus", "gout_clkcmu_isppre_bus",
	    CLKCMU_ISPPRE_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_ISPLP_BUS, "dout_clkcmu_isplp_bus", "gout_clkcmu_isplp_bus",
	    CLKCMU_ISPLP_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_ISPHQ_BUS, "dout_clkcmu_isphq_bus", "gout_clkcmu_isphq_bus",
	    CLKCMU_ISPHQ_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_AUD_CPU, "dout_clkcmu_aud_cpu", "gout_clkcmu_aud_cpu",
	    CLKCMU_AUD_CPU, 0, 3),
	DIV(CLK_DOUT_CLKCMU_G2D_MSCL, "dout_clkcmu_g2d_mscl", "gout_clkcmu_g2d_mscl",
	    CLKCMU_G2D_MSCL, 0, 4),
	DIV(CLK_DOUT_CLKCMU_HPM, "dout_clkcmu_hpm", "gout_clkcmu_hpm",
	    CLKCMU_HPM, 0, 2),
	DIV(CLK_DOUT_CLKCMU_CPUCL0_DBG_BUS, "dout_clkcmu_cpucl0_dbg_bus",
	    "gout_clkcmu_cpucl0_dbg_bus",
	    CLKCMU_CPUCL0_DBG_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_CIS_CLK0, "dout_clkcmu_cis_clk0", "gout_clkcmu_cis_clk0",
	    CLKCMU_CIS_CLK0, 0, 5),
	DIV(CLK_DOUT_CLKCMU_CIS_CLK1, "dout_clkcmu_cis_clk1", "gout_clkcmu_cis_clk1",
	    CLKCMU_CIS_CLK1, 0, 5),
	DIV(CLK_DOUT_CLKCMU_CIS_CLK2, "dout_clkcmu_cis_clk2", "gout_clkcmu_cis_clk2",
	    CLKCMU_CIS_CLK2, 0, 5),
	DIV(CLK_DOUT_CLKCMU_CIS_CLK3, "dout_clkcmu_cis_clk3", "gout_clkcmu_cis_clk3",
	    CLKCMU_CIS_CLK3, 0, 5),
	DIV(CLK_DOUT_CLKCMU_IVA_BUS, "dout_clkcmu_iva_bus", "gout_clkcmu_iva_bus",
	    CLKCMU_IVA_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_FSYS1_UFS_CARD, "dout_clkcmu_fsys1_ufs_card",
	    "gout_clkcmu_fsys1_ufs_card",
	    CLKCMU_FSYS1_UFS_CARD, 0, 4),
	DIV(CLK_DOUT_PLL_SHARED1_DIV4, "dout_pll_shared1_div4", "dout_pll_shared1_div2",
	    DIV_PLL_SHARED1_DIV4, 0, 1),
	DIV(CLK_DOUT_CLKCMU_FSYS0_DPGTC, "dout_clkcmu_fsys0_dpgtc", "gout_clkcmu_fsys0_dpgtc",
	    CLKCMU_FSYS0_DPGTC, 0, 3),
	DIV(CLK_DOUT_CLKCMU_MODEM_SHARED0, "dout_clkcmu_modem_shared0", "gout_clkcmu_modem_shared0",
	    CLKCMU_MODEM_SHARED0, 0, 3),
	DIV(CLK_DOUT_CLKCMU_MODEM_SHARED1, "dout_clkcmu_modem_shared1", "gout_clkcmu_modem_shared1",
	    CLKCMU_MODEM_SHARED1, 0, 3),
	DIV(CLK_DOUT_CLKCMU_DCRD_BUS, "dout_clkcmu_dcrd_bus", "gout_clkcmu_dcrd_bus",
	    CLKCMU_DCRD_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_CMUREF, "dout_cmu_cmuref", "mout_mux_clk_cmu_cmuref",
	    DIV_CLK_CMU_CMUREF, 0, 2),
	DIV(CLK_DOUT_CLKCMU_CHUB_BUS, "dout_clkcmu_chub_bus", "gout_clkcmu_chub_bus",
	    CLKCMU_CHUB_BUS, 0, 3),
	DIV(CLK_DOUT_CLKCMU_DCF_BUS, "dout_clkcmu_dcf_bus", "gout_clkcmu_dcf_bus",
	    CLKCMU_DCF_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_VTS_BUS, "dout_clkcmu_vts_bus", "gout_clkcmu_vts_bus",
	    CLKCMU_VTS_BUS, 0, 3),
	DIV(CLK_DOUT_CLKCMU_ISPLP_VRA, "dout_clkcmu_isplp_vra", "gout_clkcmu_isplp_vra",
	    CLKCMU_ISPLP_VRA, 0, 4),
	DIV(CLK_DOUT_CLKCMU_MFC_WFD, "dout_clkcmu_mfc_wfd", "gout_clkcmu_mfc_wfd",
	    CLKCMU_MFC_WFD, 0, 4),
	DIV(CLK_DOUT_CLKCMU_MIF_BUSP, "dout_clkcmu_mif_busp", "gout_clkcmu_mif_busp",
	    CLKCMU_MIF_BUSP, 0, 4),
	DIV(CLK_DOUT_CLKCMU_PERIC0_IP, "dout_clkcmu_peric0_ip", "gout_clkcmu_peric0_ip",
	    CLKCMU_PERIC0_IP, 0, 4),
	DIV(CLK_DOUT_CLKCMU_PERIC1_IP, "dout_clkcmu_peric1_ip", "gout_clkcmu_peric1_ip",
	    CLKCMU_PERIC1_IP, 0, 4),
	DIV(CLK_DOUT_CLKCMU_DCPOST_BUS, "dout_clkcmu_dcpost_bus", "gout_clkcmu_dcpost_bus",
	    CLKCMU_DCPOST_BUS, 0, 4),
	DIV(CLK_DOUT_CLKCMU_ISPLP_GDC, "dout_clkcmu_isplp_gdc", "gout_clkcmu_isplp_gdc",
	    CLKCMU_ISPLP_GDC, 0, 4),
	DIV(CLK_DOUT_CLKCMU_DSPS_AUD, "dout_clkcmu_dsps_aud", "gout_clkcmu_dsps_aud",
	    CLKCMU_DSPS_AUD, 0, 4),
	DIV(CLK_DOUT_PLL_SHARED1_DIV3, "dout_pll_shared1_div3", "fout_shared1",
	    DIV_PLL_SHARED1_DIV3, 0, 2),
	DIV(CLK_DOUT_PLL_SHARED0_DIV3, "dout_pll_shared0_div3", "fout_shared0",
	    DIV_PLL_SHARED0_DIV3, 0, 2),
	DIV(CLK_DOUT_CLKCMU_DPU, "dout_clkcmu_dpu", "gout_clkcmu_dpu",
	    DIV_CLKCMU_DPU, 0, 3),
};

static const struct samsung_gate_clock cmu_gate_clks[] __initconst = {
	GATE(CLK_GOUT_CLKCMU_APM_BUS, "gout_clkcmu_apm_bus", "mout_cmu_apm_bus",
	     GATE_CLKCMU_APM_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_FSYS0_BUS, "gout_clkcmu_fsys0_bus", "mout_cmu_fsys0_bus",
	     GATE_CLKCMU_FSYS0_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_MIF_SWITCH, "gout_clkcmu_mif_switch", "mout_cmu_mif_switch",
	     CLKCMU_MIF_SWITCH, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_MFC_BUS, "gout_clkcmu_mfc_bus", "mout_cmu_mfc_bus",
	     GATE_CLKCMU_MFC_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_G2D_G2D, "gout_clkcmu_g2d_g2d", "mout_cmu_g2d_g2d",
	     GATE_CLKCMU_G2D_G2D, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_FSYS0_USB30DRD, "gout_clkcmu_fsys0_usb30drd",
	     "mout_cmu_fsys0_usb30drd",
	     GATE_CLKCMU_FSYS0_USB30DRD, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_FSYS0_UFS_EMBD, "gout_clkcmu_fsys0_ufs_embd",
	     "mout_cmu_fsys0_ufs_embd",
	     GATE_CLKCMU_FSYS0_UFS_EMBD, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_FSYS1_BUS, "gout_clkcmu_fsys1_bus", "mout_cmu_fsys1_bus",
	     GATE_CLKCMU_FSYS1_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_FSYS1_MMC_CARD, "gout_clkcmu_fsys1_mmc_card",
	     "mout_cmu_fsys1_mmc_card",
	     GATE_CLKCMU_FSYS1_MMC_CARD, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_DPU_BUS, "gout_clkcmu_dpu_bus", "mout_mux_clkcmu_dpu_bus",
	     GATE_CLKCMU_DPU_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_G3D_SWITCH, "gout_clkcmu_g3d_switch", "fout_shared2",
	     GATE_CLKCMU_G3D_SWITCH, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_PERIS_BUS, "gout_clkcmu_peris_bus", "mout_cmu_peris_bus",
	     GATE_CLKCMU_PERIS_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_CMGP_BUS, "gout_clkcmu_cmgp_bus", "mout_cmu_cmgp_bus",
	     GATE_CLKCMU_CMGP_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_DSPM_BUS, "gout_clkcmu_dspm_bus", "mout_cmu_dspm_bus",
	     GATE_CLKCMU_DSPM_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_PERIC0_BUS, "gout_clkcmu_peric0_bus", "mout_cmu_peric0_bus",
	     GATE_CLKCMU_PERIC0_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_PERIC1_BUS, "gout_clkcmu_peric1_bus", "mout_cmu_peric1_bus",
	     GATE_CLKCMU_PERIC1_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_BUSC_BUS, "gout_clkcmu_busc_bus", "mout_cmu_busc_bus",
	     GATE_CLKCMU_BUSC_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_BUS1_BUS, "gout_clkcmu_bus1_bus", "mout_cmu_bus1_bus",
	     GATE_CLKCMU_BUS1_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_CPUCL1_SWITCH, "gout_clkcmu_cpucl1_switch", "mout_cmu_cpucl1_switch",
	     GATE_CLKCMU_CPUCL1_SWITCH, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_CPUCL0_SWITCH, "gout_clkcmu_cpucl0_switch", "mout_cmu_cpucl0_switch",
	     GATE_CLKCMU_CPUCL0_SWITCH, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_CORE_BUS, "gout_clkcmu_core_bus", "mout_cmu_core_bus",
	     GATE_CLKCMU_CORE_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_ISPPRE_BUS, "gout_clkcmu_isppre_bus", "mout_cmu_isppre_bus",
	     GATE_CLKCMU_ISPPRE_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_ISPLP_BUS, "gout_clkcmu_isplp_bus", "mout_cmu_isplp_bus",
	     GATE_CLKCMU_ISPLP_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_ISPHQ_BUS, "gout_clkcmu_isphq_bus", "mout_cmu_isphq_bus",
	     GATE_CLKCMU_ISPHQ_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_AUD_CPU, "gout_clkcmu_aud_cpu", "mout_cmu_aud_cpu",
	     GATE_CLKCMU_AUD_CPU, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_G2D_MSCL, "gout_clkcmu_g2d_mscl", "mout_cmu_g2d_mscl",
	     GATE_CLKCMU_G2D_MSCL, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_HPM, "gout_clkcmu_hpm", "mout_cmu_hpm",
	     GATE_CLKCMU_HPM, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_FSYS1_PCIE, "gout_clkcmu_fsys1_pcie", "mout_cmu_fsys1_pcie",
	     GATE_CLKCMU_FSYS1_PCIE, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_CPUCL0_DBG_BUS, "gout_clkcmu_cpucl0_dbg_bus",
	     "mout_cmu_cpucl0_dbg_bus",
	     GATE_CLKCMU_CPUCL0_DBG_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_CIS_CLK0, "gout_clkcmu_cis_clk0", "mout_cmu_cis_clk0",
	     GATE_CLKCMU_CIS_CLK0, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_CIS_CLK1, "gout_clkcmu_cis_clk1", "mout_cmu_cis_clk1",
	     GATE_CLKCMU_CIS_CLK1, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_CIS_CLK3, "gout_clkcmu_cis_clk3", "mout_cmu_cis_clk3",
	     GATE_CLKCMU_CIS_CLK3, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_CIS_CLK2, "gout_clkcmu_cis_clk2", "mout_cmu_cis_clk2",
	     GATE_CLKCMU_CIS_CLK2, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_IVA_BUS, "gout_clkcmu_iva_bus", "mout_cmu_iva_bus",
	     GATE_CLKCMU_IVA_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_FSYS1_UFS_CARD, "gout_clkcmu_fsys1_ufs_card",
	     "mout_cmu_fsys1_ufs_card",
	     GATE_CLKCMU_FSYS1_UFS_CARD, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_FSYS0_DPGTC, "gout_clkcmu_fsys0_dpgtc", "mout_cmu_fsys0_dpgtc",
	     GATE_CLKCMU_FSYS0_DPGTC, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_MODEM_SHARED0, "gout_clkcmu_modem_shared0", "dout_pll_shared0_div2",
	     GATE_CLKCMU_MODEM_SHARED0, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_MODEM_SHARED1, "gout_clkcmu_modem_shared1", "fout_shared2",
	     GATE_CLKCMU_MODEM_SHARED1, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_DCRD_BUS, "gout_clkcmu_dcrd_bus", "mout_cmu_dcrd_bus",
	     GATE_CLKCMU_DCRD_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_CHUB_BUS, "gout_clkcmu_chub_bus", "mout_cmu_chub_bus",
	     GATE_CLKCMU_CHUB_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_DCF_BUS, "gout_clkcmu_dcf_bus", "mout_cmu_dcf_bus",
	     GATE_CLKCMU_DCF_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_VTS_BUS, "gout_clkcmu_vts_bus", "mout_cmu_vts_bus",
	     GATE_CLKCMU_VTS_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_ISPLP_VRA, "gout_clkcmu_isplp_vra", "mout_cmu_isplp_vra",
	     GATE_CLKCMU_ISPLP_VRA, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_MFC_WFD, "gout_clkcmu_mfc_wfd", "mout_cmu_mfc_wfd",
	     GATE_CLKCMU_MFC_WFD, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_MIF_BUSP, "gout_clkcmu_mif_busp", "mout_cmu_mif_busp",
	     GATE_CLKCMU_MIF_BUSP, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_PERIC0_IP, "gout_clkcmu_peric0_ip", "mout_cmu_peric0_ip",
	     GATE_CLKCMU_PERIC0_IP, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_PERIC1_IP, "gout_clkcmu_peric1_ip", "mout_cmu_peric1_ip",
	     GATE_CLKCMU_PERIC1_IP, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_DCPOST_BUS, "gout_clkcmu_dcpost_bus", "mout_cmu_dcpost_bus",
	     GATE_CLKCMU_DCPOST_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_FSYS0_USBDP_DEBUG, "gout_clkcmu_fsys0_usbdp_debug",
	     "mout_cmu_fsys0_usbdp_debug",
	     GATE_CLKCMU_FSYS0_USBDP_DEBUG, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_ISPLP_GDC, "gout_clkcmu_isplp_gdc", "mout_cmu_isplp_gdc",
	     GATE_CLKCMU_ISPLP_GDC, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_DSPS_AUD, "gout_clkcmu_dsps_aud", "mout_cmu_dsps_aud",
	     GATE_CLKCMU_DSPS_AUD, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_DPU, "gout_clkcmu_dpu", "dout_pll_shared0_div2",
	     GATE_CLKCMU_DPU, 21, 0, 0),
};

static const struct samsung_cmu_info cmu_cmu_info __initconst = {
	.pll_clks		= cmu_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(cmu_pll_clks),
	.mux_clks		= cmu_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(cmu_mux_clks),
	.div_clks		= cmu_div_clks,
	.nr_div_clks		= ARRAY_SIZE(cmu_div_clks),
	.gate_clks		= cmu_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(cmu_gate_clks),
	.fixed_clks		= cmu_fixed_clks,
	.nr_fixed_clks		= ARRAY_SIZE(cmu_fixed_clks),
	.nr_clk_ids		= CLKS_NR_CMU,
	.clk_regs		= cmu_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cmu_clk_regs),
	.qch_regs		= cmu_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(cmu_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_cmu_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &cmu_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_cmu, "samsung,exynos9810-cmu-cmu",
	       exynos9810_cmu_cmu_init);

/* ---- CMU_CORE --------------------------------------------------------*/

/* Register Offset definitions for CMU_CORE (0x1a020000) */
#define MUX_CLKCMU_CORE_BUS_USER			0x0100
#define DIV_CLK_CORE_BUSP			0x1800
#define CLK_BLK_CORE_UID_CORE_CMU_CORE_IPCLKPORT_PCLK	0x2000
#define CLK_BLK_CORE_UID_HPM_CORE_IPCLKPORT_hpm_targetclk_c	0x2004
#define GOUT_BLK_CORE_UID_ADM_APB_G_BDU_IPCLKPORT_PCLKM	0x2020
#define GOUT_BLK_CORE_UID_APB_ASYNC_PPFW_DP_IPCLKPORT_PCLKS	0x2028
#define GOUT_BLK_CORE_UID_APB_ASYNC_PPFW_G3D_IPCLKPORT_PCLKS	0x2030
#define GOUT_BLK_CORE_UID_APB_ASYNC_PPFW_IO_IPCLKPORT_PCLKS	0x2038
#define GOUT_BLK_CORE_UID_AXI2APB_CORE_IPCLKPORT_ACLK	0x203c
#define GOUT_BLK_CORE_UID_AXI2APB_CORE_TP_IPCLKPORT_ACLK	0x2040
#define GOUT_BLK_CORE_UID_BAAW_CP_IPCLKPORT_I_PCLK	0x2044
#define GOUT_BLK_CORE_UID_BDU_IPCLKPORT_I_PCLK			0x204c
#define GOUT_BLK_CORE_UID_BPS_P_G3D_IPCLKPORT_I_CLK	0x2060
#define GOUT_BLK_CORE_UID_BUSIF_HPMCORE_IPCLKPORT_PCLK	0x2064
#define GOUT_BLK_CORE_UID_CCI_IPCLKPORT_PCLK			0x206c
#define GOUT_BLK_CORE_UID_LHS_AXI_P_APM_IPCLKPORT_I_CLK	0x20a0
#define GOUT_BLK_CORE_UID_LHS_AXI_P_CPUCL0_IPCLKPORT_I_CLK	0x20a4
#define GOUT_BLK_CORE_UID_LHS_AXI_P_CPUCL1_IPCLKPORT_I_CLK	0x20a8
#define GOUT_BLK_CORE_UID_LHS_AXI_P_CP_IPCLKPORT_I_CLK	0x20ac
#define GOUT_BLK_CORE_UID_LHS_AXI_P_G3D_IPCLKPORT_I_CLK	0x20b0
#define GOUT_BLK_CORE_UID_PPCFW_G3D_IPCLKPORT_PCLK	0x20d0
#define GOUT_BLK_CORE_UID_PPMUPPC_CCI_IPCLKPORT_PCLK	0x20e4
#define GOUT_BLK_CORE_UID_PPMU_CPUCL0_IPCLKPORT_PCLK	0x20ec
#define GOUT_BLK_CORE_UID_PPMU_CPUCL1_IPCLKPORT_PCLK	0x20f4
#define GOUT_BLK_CORE_UID_PPMU_G3D0_IPCLKPORT_PCLK	0x20fc
#define GOUT_BLK_CORE_UID_PPMU_G3D1_IPCLKPORT_PCLK	0x2104
#define GOUT_BLK_CORE_UID_PPMU_G3D2_IPCLKPORT_PCLK	0x210c
#define GOUT_BLK_CORE_UID_PPMU_G3D3_IPCLKPORT_PCLK	0x2114
#define GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_BUSP_IPCLKPORT_CLK	0x211c
#define GOUT_BLK_CORE_UID_SYSREG_CORE_IPCLKPORT_PCLK	0x2120
#define GOUT_BLK_CORE_UID_TREX_D_CORE_IPCLKPORT_pclk	0x2128
#define GOUT_BLK_CORE_UID_TREX_P0_CORE_IPCLKPORT_pclk	0x2130
#define GOUT_BLK_CORE_UID_TREX_P0_CORE_IPCLKPORT_PCLK_CORE	0x2134
#define GOUT_BLK_CORE_UID_TREX_P1_CORE_IPCLKPORT_pclk	0x2138
#define GOUT_BLK_CORE_UID_TREX_P1_CORE_IPCLKPORT_PCLK_CORE	0x213c

static const unsigned long core_clk_regs[] __initconst = {
	MUX_CLKCMU_CORE_BUS_USER,
	DIV_CLK_CORE_BUSP,
	CLK_BLK_CORE_UID_CORE_CMU_CORE_IPCLKPORT_PCLK,
	CLK_BLK_CORE_UID_HPM_CORE_IPCLKPORT_hpm_targetclk_c,
	GOUT_BLK_CORE_UID_ADM_APB_G_BDU_IPCLKPORT_PCLKM,
	GOUT_BLK_CORE_UID_APB_ASYNC_PPFW_DP_IPCLKPORT_PCLKS,
	GOUT_BLK_CORE_UID_APB_ASYNC_PPFW_G3D_IPCLKPORT_PCLKS,
	GOUT_BLK_CORE_UID_APB_ASYNC_PPFW_IO_IPCLKPORT_PCLKS,
	GOUT_BLK_CORE_UID_AXI2APB_CORE_IPCLKPORT_ACLK,
	GOUT_BLK_CORE_UID_AXI2APB_CORE_TP_IPCLKPORT_ACLK,
	GOUT_BLK_CORE_UID_BAAW_CP_IPCLKPORT_I_PCLK,
	GOUT_BLK_CORE_UID_BDU_IPCLKPORT_I_PCLK,
	GOUT_BLK_CORE_UID_BPS_P_G3D_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_BUSIF_HPMCORE_IPCLKPORT_PCLK,
	GOUT_BLK_CORE_UID_CCI_IPCLKPORT_PCLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_APM_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_CPUCL0_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_CPUCL1_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_CP_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_LHS_AXI_P_G3D_IPCLKPORT_I_CLK,
	GOUT_BLK_CORE_UID_PPCFW_G3D_IPCLKPORT_PCLK,
	GOUT_BLK_CORE_UID_PPMUPPC_CCI_IPCLKPORT_PCLK,
	GOUT_BLK_CORE_UID_PPMU_CPUCL0_IPCLKPORT_PCLK,
	GOUT_BLK_CORE_UID_PPMU_CPUCL1_IPCLKPORT_PCLK,
	GOUT_BLK_CORE_UID_PPMU_G3D0_IPCLKPORT_PCLK,
	GOUT_BLK_CORE_UID_PPMU_G3D1_IPCLKPORT_PCLK,
	GOUT_BLK_CORE_UID_PPMU_G3D2_IPCLKPORT_PCLK,
	GOUT_BLK_CORE_UID_PPMU_G3D3_IPCLKPORT_PCLK,
	GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_BUSP_IPCLKPORT_CLK,
	GOUT_BLK_CORE_UID_SYSREG_CORE_IPCLKPORT_PCLK,
	GOUT_BLK_CORE_UID_TREX_D_CORE_IPCLKPORT_pclk,
	GOUT_BLK_CORE_UID_TREX_P0_CORE_IPCLKPORT_pclk,
	GOUT_BLK_CORE_UID_TREX_P0_CORE_IPCLKPORT_PCLK_CORE,
	GOUT_BLK_CORE_UID_TREX_P1_CORE_IPCLKPORT_pclk,
	GOUT_BLK_CORE_UID_TREX_P1_CORE_IPCLKPORT_PCLK_CORE,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long core_qch_regs[] __initconst = {
	0x3000,	/* CCI_QCH */
	0x3044,	/* ACE_SLICE_G3D0_QCH */
	0x3048,	/* ACE_SLICE_G3D1_QCH */
	0x304c,	/* ACE_SLICE_G3D2_QCH */
	0x3050,	/* ACE_SLICE_G3D3_QCH */
	0x3054,	/* BAAW_CP_QCH */
	0x3058,	/* BDU_QCH */
	0x305c,	/* BUSIF_HPMCORE_QCH */
	0x3060,	/* CORE_CMU_CORE_QCH */
	0x3064,	/* LHM_ACE_D0_G3D_QCH */
	0x3068,	/* LHM_ACE_D1_G3D_QCH */
	0x306c,	/* LHM_ACE_D2_G3D_QCH */
	0x3070,	/* LHM_ACE_D3_G3D_QCH */
	0x3074,	/* LHM_ACE_D_CPUCL0_QCH */
	0x3078,	/* LHM_AXI_D_CP_QCH */
	0x307c,	/* LHM_AXI_P_CLUSTER0_QCH */
	0x3080,	/* LHS_ATB_T_BDU_QCH */
	0x3084,	/* LHS_AXI_P_APM_QCH */
	0x3088,	/* LHS_AXI_P_CPUCL0_QCH */
	0x308c,	/* LHS_AXI_P_CPUCL1_QCH */
	0x3090,	/* LHS_AXI_P_CP_QCH */
	0x3094,	/* LHS_AXI_P_G3D_QCH */
	0x3098,	/* PPCFW_G3D_QCH */
	0x309c,	/* PPFW_DP_QCH */
	0x30a0,	/* PPFW_G3D_QCH */
	0x30a4,	/* PPFW_IO_QCH */
	0x30a8,	/* PPMU_CPUCL0_QCH */
	0x30ac,	/* PPMU_CPUCL1_QCH */
	0x30b0,	/* PPMU_G3D0_QCH */
	0x30b4,	/* PPMU_G3D1_QCH */
	0x30b8,	/* PPMU_G3D2_QCH */
	0x30bc,	/* PPMU_G3D3_QCH */
	0x30c0,	/* SYSREG_CORE_QCH */
	0x30c4,	/* TREX_D_CORE_QCH */
	0x30c8,	/* TREX_P0_CORE_QCH */
	0x30cc,	/* TREX_P1_CORE_QCH */
};

/* List of parent clocks for Muxes in CMU_CORE */
PNAME(mout_cmu_core_bus_user_p) = { "oscclk", "dout_clkcmu_core_bus" };

static const struct samsung_mux_clock core_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_CORE_BUS_USER, "mout_cmu_core_bus_user", mout_cmu_core_bus_user_p,
	    MUX_CLKCMU_CORE_BUS_USER, 4, 1),
};

static const struct samsung_div_clock core_div_clks[] __initconst = {
	DIV(CLK_DOUT_CORE_BUSP, "dout_core_busp", "mout_cmu_core_bus_user",
	    DIV_CLK_CORE_BUSP, 0, 3),
};

static const struct samsung_gate_clock core_gate_clks[] __initconst = {
	GATE(CLK_GOUT_CORE_CORE_CMU_CORE_PCLK, "gout_core_core_cmu_core_pclk", "dout_core_busp",
	     CLK_BLK_CORE_UID_CORE_CMU_CORE_IPCLKPORT_PCLK, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_CORE_SYSREG_CORE_PCLK, "gout_core_sysreg_core_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_SYSREG_CORE_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_AXI2APB_CORE_ACLK, "gout_core_axi2apb_core_aclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_AXI2APB_CORE_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_PPMUPPC_CCI_PCLK, "gout_core_ppmuppc_cci_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_PPMUPPC_CCI_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_TREX_P0_CORE_pclk, "gout_core_trex_p0_core_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_TREX_P0_CORE_IPCLKPORT_pclk, 21, 0, 0),
	GATE(CLK_GOUT_CORE_CCI_PCLK, "gout_core_cci_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_CCI_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_PPMU_CPUCL0_PCLK, "gout_core_ppmu_cpucl0_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_PPMU_CPUCL0_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_PPMU_CPUCL1_PCLK, "gout_core_ppmu_cpucl1_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_PPMU_CPUCL1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_RSTnSYNC_CLK_CORE_BUSP_CLK, "gout_core_rstnsync_clk_core_busp_clk",
	     "dout_core_busp",
	     GOUT_BLK_CORE_UID_RSTnSYNC_CLK_CORE_BUSP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_ADM_APB_G_BDU_PCLKM, "gout_core_adm_apb_g_bdu_pclkm", "dout_core_busp",
	     GOUT_BLK_CORE_UID_ADM_APB_G_BDU_IPCLKPORT_PCLKM, 21, 0, 0),
	GATE(CLK_GOUT_CORE_BDU_I_PCLK, "gout_core_bdu_i_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_BDU_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_TREX_P1_CORE_pclk, "gout_core_trex_p1_core_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_TREX_P1_CORE_IPCLKPORT_pclk, 21, 0, 0),
	GATE(CLK_GOUT_CORE_AXI2APB_CORE_TP_ACLK, "gout_core_axi2apb_core_tp_aclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_AXI2APB_CORE_TP_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_PPMU_G3D0_PCLK, "gout_core_ppmu_g3d0_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_PPMU_G3D0_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_PPMU_G3D1_PCLK, "gout_core_ppmu_g3d1_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_PPMU_G3D1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_PPMU_G3D2_PCLK, "gout_core_ppmu_g3d2_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_PPMU_G3D2_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_PPMU_G3D3_PCLK, "gout_core_ppmu_g3d3_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_PPMU_G3D3_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_G3D_I_CLK, "gout_core_lhs_axi_p_g3d_i_clk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_G3D_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_CPUCL0_I_CLK, "gout_core_lhs_axi_p_cpucl0_i_clk",
	     "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_CPUCL0_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_CPUCL1_I_CLK, "gout_core_lhs_axi_p_cpucl1_i_clk",
	     "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_CPUCL1_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_TREX_D_CORE_pclk, "gout_core_trex_d_core_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_TREX_D_CORE_IPCLKPORT_pclk, 21, 0, 0),
	GATE(CLK_GOUT_CORE_HPM_CORE_hpm_targetclk_c, "gout_core_hpm_core_hpm_targetclk_c",
	     "dout_clkcmu_hpm",
	     CLK_BLK_CORE_UID_HPM_CORE_IPCLKPORT_hpm_targetclk_c, 21, 0, 0),
	GATE(CLK_GOUT_CORE_BUSIF_HPMCORE_PCLK, "gout_core_busif_hpmcore_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_BUSIF_HPMCORE_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_PPCFW_G3D_PCLK, "gout_core_ppcfw_g3d_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_PPCFW_G3D_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_CP_I_CLK, "gout_core_lhs_axi_p_cp_i_clk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_CP_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_APB_ASYNC_PPFW_G3D_PCLKS, "gout_core_apb_async_ppfw_g3d_pclks",
	     "dout_core_busp",
	     GOUT_BLK_CORE_UID_APB_ASYNC_PPFW_G3D_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_CORE_APB_ASYNC_PPFW_IO_PCLKS, "gout_core_apb_async_ppfw_io_pclks",
	     "dout_core_busp",
	     GOUT_BLK_CORE_UID_APB_ASYNC_PPFW_IO_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_CORE_BAAW_CP_I_PCLK, "gout_core_baaw_cp_i_pclk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_BAAW_CP_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_APB_ASYNC_PPFW_DP_PCLKS, "gout_core_apb_async_ppfw_dp_pclks",
	     "dout_core_busp",
	     GOUT_BLK_CORE_UID_APB_ASYNC_PPFW_DP_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_CORE_TREX_P0_CORE_PCLK_CORE, "gout_core_trex_p0_core_pclk_core",
	     "dout_core_busp",
	     GOUT_BLK_CORE_UID_TREX_P0_CORE_IPCLKPORT_PCLK_CORE, 21, 0, 0),
	GATE(CLK_GOUT_CORE_TREX_P1_CORE_PCLK_CORE, "gout_core_trex_p1_core_pclk_core",
	     "dout_core_busp",
	     GOUT_BLK_CORE_UID_TREX_P1_CORE_IPCLKPORT_PCLK_CORE, 21, 0, 0),
	GATE(CLK_GOUT_CORE_BPS_P_G3D_I_CLK, "gout_core_bps_p_g3d_i_clk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_BPS_P_G3D_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CORE_LHS_AXI_P_APM_I_CLK, "gout_core_lhs_axi_p_apm_i_clk", "dout_core_busp",
	     GOUT_BLK_CORE_UID_LHS_AXI_P_APM_IPCLKPORT_I_CLK, 21, 0, 0),
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

static void __init exynos9810_cmu_core_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &core_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_core, "samsung,exynos9810-cmu-core",
	       exynos9810_cmu_core_init);

/* ---- CMU_CPUCL0 ------------------------------------------------------*/

/* Register Offset definitions for CMU_CPUCL0 (0x1d000000) */
#define PLL_LOCKTIME_PLL_CPUCL0_PLL_LOCK_TIME			0x0000
#define MUX_CLKCMU_CPUCL0_DBG_BUS_USER			0x0100
#define MUX_CLKCMU_CPUCL0_SWITCH_USER			0x0120
#define PLL_CON0_PLL_CPUCL0_ENABLE			0x0140
#define MUX_CLK_CLUSTER0_ACLK			0x1000
#define MUX_CLK_CLUSTER0_ACLKP			0x1004
#define MUX_CLK_CLUSTER0_SCLK			0x1008
#define MUX_CLK_CPUCL0_PLL			0x100c
#define DIV_CLK_CLUSTER0_ACLK			0x1800
#define DIV_CLK_CLUSTER0_ACLKP			0x1804
#define DIV_CLK_CLUSTER0_ATCLK			0x1808
#define DIV_CLK_CLUSTER0_PCLKDBG			0x180c
#define DIV_CLK_CLUSTER0_PERIPHCLK			0x1810
#define DIV_CLK_CPUCL0_CMUREF			0x1814
#define DIV_CLK_CPUCL0_CPU			0x1818
#define DIV_CLK_CPUCL0_DBG_PCLKDBG			0x181c
#define DIV_CLK_CPUCL0_PCLK			0x1820
#define CLK_BLK_CPUCL0_UID_CLUSTER0_IPCLKPORT_ATCLK	0x2000
#define CLK_BLK_CPUCL0_UID_CLUSTER0_IPCLKPORT_PCLK	0x2004
#define CLK_BLK_CPUCL0_UID_CLUSTER0_IPCLKPORT_PERIPHCLK	0x2008
#define CLK_BLK_CPUCL0_UID_CLUSTER0_IPCLKPORT_SCLK	0x200c
#define CLK_BLK_CPUCL0_UID_CPUCL0_CMU_CPUCL0_IPCLKPORT_PCLK	0x2010
#define CLK_BLK_CPUCL0_UID_HPM_CPUCL0_IPCLKPORT_hpm_targetclk_c	0x2014
#define CLK_BLK_CPUCL0_UID_RSTnSYNC_CLK_CPUCL0_DBG_ATCLK_IPCLKPORT_CLK	0x2018
#define CLK_BLK_CPUCL0_UID_RSTnSYNC_CLK_CPUCL0_DBG_PCLKDBG_IPCLKPORT_CLK	0x201c
#define GATE_CLK_CPUCL0_CPU			0x202c
#define GOUT_BLK_CPUCL0_UID_ADM_APB_G_CLUSTER0_IPCLKPORT_PCLKM	0x2030
#define GOUT_BLK_CPUCL0_UID_ADS_AHB_G_SSS_IPCLKPORT_HCLKS	0x2034
#define GOUT_BLK_CPUCL0_UID_ADS_APB_G_BDU_IPCLKPORT_PCLKS	0x2038
#define GOUT_BLK_CPUCL0_UID_ADS_APB_G_CLUSTER0_IPCLKPORT_PCLKS	0x203c
#define GOUT_BLK_CPUCL0_UID_ADS_APB_G_CLUSTER1_IPCLKPORT_PCLKS	0x2040
#define GOUT_BLK_CPUCL0_UID_ADS_APB_G_DSPM_IPCLKPORT_PCLKS	0x2044
#define GOUT_BLK_CPUCL0_UID_ASB_APB_P_DUMPPC_CLUSTER0_IPCLKPORT_PCLKM	0x2048
#define GOUT_BLK_CPUCL0_UID_ASB_APB_P_DUMPPC_CLUSTER0_IPCLKPORT_PCLKS	0x204c
#define GOUT_BLK_CPUCL0_UID_ASB_APB_P_DUMPPC_CLUSTER1_IPCLKPORT_PCLKM	0x2050
#define GOUT_BLK_CPUCL0_UID_ASB_APB_P_DUMPPC_CLUSTER1_IPCLKPORT_PCLKS	0x2054
#define GOUT_BLK_CPUCL0_UID_AXI2APB_CPUCL0_IPCLKPORT_ACLK	0x2058
#define GOUT_BLK_CPUCL0_UID_AXI2APB_P_CSSYS_IPCLKPORT_ACLK	0x205c
#define GOUT_BLK_CPUCL0_UID_AXI_DS_64to32_G_CSSYS_IPCLKPORT_aclk	0x2060
#define GOUT_BLK_CPUCL0_UID_BUSIF_HPMCPUCL0_IPCLKPORT_PCLK	0x2064
#define GOUT_BLK_CPUCL0_UID_CSSYS_IPCLKPORT_ATCLK	0x2068
#define GOUT_BLK_CPUCL0_UID_CSSYS_IPCLKPORT_PCLKDBG	0x206c
#define GOUT_BLK_CPUCL0_UID_DUMPPC_CLUSTER0_IPCLKPORT_I_PCLK	0x2070
#define GOUT_BLK_CPUCL0_UID_DUMPPC_CLUSTER1_IPCLKPORT_I_PCLK	0x2074
#define GOUT_BLK_CPUCL0_UID_LHM_ATB_T0_CLUSTER0_IPCLKPORT_I_CLK	0x2078
#define GOUT_BLK_CPUCL0_UID_LHM_ATB_T0_CLUSTER1_IPCLKPORT_I_CLK	0x207c
#define GOUT_BLK_CPUCL0_UID_LHM_ATB_T1_CLUSTER0_IPCLKPORT_I_CLK	0x2080
#define GOUT_BLK_CPUCL0_UID_LHM_ATB_T1_CLUSTER1_IPCLKPORT_I_CLK	0x2084
#define GOUT_BLK_CPUCL0_UID_LHM_ATB_T2_CLUSTER0_IPCLKPORT_I_CLK	0x2088
#define GOUT_BLK_CPUCL0_UID_LHM_ATB_T2_CLUSTER1_IPCLKPORT_I_CLK	0x208c
#define GOUT_BLK_CPUCL0_UID_LHM_ATB_T3_CLUSTER0_IPCLKPORT_I_CLK	0x2090
#define GOUT_BLK_CPUCL0_UID_LHM_ATB_T3_CLUSTER1_IPCLKPORT_I_CLK	0x2094
#define GOUT_BLK_CPUCL0_UID_LHM_ATB_T_AUD_IPCLKPORT_I_CLK	0x2098
#define GOUT_BLK_CPUCL0_UID_LHM_ATB_T_BDU_IPCLKPORT_I_CLK	0x209c
#define GOUT_BLK_CPUCL0_UID_LHM_AXI_P_CPUCL0_IPCLKPORT_I_CLK	0x20a0
#define GOUT_BLK_CPUCL0_UID_LHM_AXI_P_CSSYS_IPCLKPORT_I_CLK	0x20a4
#define GOUT_BLK_CPUCL0_UID_LHS_ACE_D_CLUSTER0_IPCLKPORT_I_CLK	0x20a8
#define GOUT_BLK_CPUCL0_UID_LHS_ATB_T0_CLUSTER0_IPCLKPORT_I_CLK	0x20ac
#define GOUT_BLK_CPUCL0_UID_LHS_ATB_T1_CLUSTER0_IPCLKPORT_I_CLK	0x20b0
#define GOUT_BLK_CPUCL0_UID_LHS_ATB_T2_CLUSTER0_IPCLKPORT_I_CLK	0x20b4
#define GOUT_BLK_CPUCL0_UID_LHS_ATB_T3_CLUSTER0_IPCLKPORT_I_CLK	0x20b8
#define GOUT_BLK_CPUCL0_UID_LHS_AXI_G_CSSYS_IPCLKPORT_I_CLK	0x20bc
#define GOUT_BLK_CPUCL0_UID_LHS_AXI_G_ETR_IPCLKPORT_I_CLK	0x20c0
#define GOUT_BLK_CPUCL0_UID_LHS_AXI_P_CLUSTER0_IPCLKPORT_I_CLK	0x20c4
#define GOUT_BLK_CPUCL0_UID_RSTnSYNC_CLK_CLUSTER0_ACLKP_IPCLKPORT_CLK	0x20c8
#define GOUT_BLK_CPUCL0_UID_RSTnSYNC_CLK_CLUSTER0_ACLK_IPCLKPORT_CLK	0x20cc
#define GOUT_BLK_CPUCL0_UID_RSTnSYNC_CLK_CLUSTER0_ATCLK_IPCLKPORT_CLK	0x20d0
#define GOUT_BLK_CPUCL0_UID_RSTnSYNC_CLK_CLUSTER0_PCLKDBG_IPCLKPORT_CLK	0x20d4
#define GOUT_BLK_CPUCL0_UID_RSTnSYNC_CLK_CPUCL0_PCLK_IPCLKPORT_CLK	0x20d8
#define GOUT_BLK_CPUCL0_UID_SECJTAG_IPCLKPORT_i_clk	0x20dc
#define GOUT_BLK_CPUCL0_UID_SYSREG_CPUCL0_IPCLKPORT_PCLK	0x20e0

static const unsigned long cpucl0_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_CPUCL0_PLL_LOCK_TIME,
	MUX_CLKCMU_CPUCL0_DBG_BUS_USER,
	MUX_CLKCMU_CPUCL0_SWITCH_USER,
	PLL_CON0_PLL_CPUCL0_ENABLE,
	MUX_CLK_CLUSTER0_ACLK,
	MUX_CLK_CLUSTER0_ACLKP,
	MUX_CLK_CLUSTER0_SCLK,
	MUX_CLK_CPUCL0_PLL,
	DIV_CLK_CLUSTER0_ACLK,
	DIV_CLK_CLUSTER0_ACLKP,
	DIV_CLK_CLUSTER0_ATCLK,
	DIV_CLK_CLUSTER0_PCLKDBG,
	DIV_CLK_CLUSTER0_PERIPHCLK,
	DIV_CLK_CPUCL0_CMUREF,
	DIV_CLK_CPUCL0_CPU,
	DIV_CLK_CPUCL0_DBG_PCLKDBG,
	DIV_CLK_CPUCL0_PCLK,
	CLK_BLK_CPUCL0_UID_CLUSTER0_IPCLKPORT_ATCLK,
	CLK_BLK_CPUCL0_UID_CLUSTER0_IPCLKPORT_PCLK,
	CLK_BLK_CPUCL0_UID_CLUSTER0_IPCLKPORT_PERIPHCLK,
	CLK_BLK_CPUCL0_UID_CLUSTER0_IPCLKPORT_SCLK,
	CLK_BLK_CPUCL0_UID_CPUCL0_CMU_CPUCL0_IPCLKPORT_PCLK,
	CLK_BLK_CPUCL0_UID_HPM_CPUCL0_IPCLKPORT_hpm_targetclk_c,
	CLK_BLK_CPUCL0_UID_RSTnSYNC_CLK_CPUCL0_DBG_ATCLK_IPCLKPORT_CLK,
	CLK_BLK_CPUCL0_UID_RSTnSYNC_CLK_CPUCL0_DBG_PCLKDBG_IPCLKPORT_CLK,
	GATE_CLK_CPUCL0_CPU,
	GOUT_BLK_CPUCL0_UID_ADM_APB_G_CLUSTER0_IPCLKPORT_PCLKM,
	GOUT_BLK_CPUCL0_UID_ADS_AHB_G_SSS_IPCLKPORT_HCLKS,
	GOUT_BLK_CPUCL0_UID_ADS_APB_G_BDU_IPCLKPORT_PCLKS,
	GOUT_BLK_CPUCL0_UID_ADS_APB_G_CLUSTER0_IPCLKPORT_PCLKS,
	GOUT_BLK_CPUCL0_UID_ADS_APB_G_CLUSTER1_IPCLKPORT_PCLKS,
	GOUT_BLK_CPUCL0_UID_ADS_APB_G_DSPM_IPCLKPORT_PCLKS,
	GOUT_BLK_CPUCL0_UID_ASB_APB_P_DUMPPC_CLUSTER0_IPCLKPORT_PCLKM,
	GOUT_BLK_CPUCL0_UID_ASB_APB_P_DUMPPC_CLUSTER0_IPCLKPORT_PCLKS,
	GOUT_BLK_CPUCL0_UID_ASB_APB_P_DUMPPC_CLUSTER1_IPCLKPORT_PCLKM,
	GOUT_BLK_CPUCL0_UID_ASB_APB_P_DUMPPC_CLUSTER1_IPCLKPORT_PCLKS,
	GOUT_BLK_CPUCL0_UID_AXI2APB_CPUCL0_IPCLKPORT_ACLK,
	GOUT_BLK_CPUCL0_UID_AXI2APB_P_CSSYS_IPCLKPORT_ACLK,
	GOUT_BLK_CPUCL0_UID_AXI_DS_64to32_G_CSSYS_IPCLKPORT_aclk,
	GOUT_BLK_CPUCL0_UID_BUSIF_HPMCPUCL0_IPCLKPORT_PCLK,
	GOUT_BLK_CPUCL0_UID_CSSYS_IPCLKPORT_ATCLK,
	GOUT_BLK_CPUCL0_UID_CSSYS_IPCLKPORT_PCLKDBG,
	GOUT_BLK_CPUCL0_UID_DUMPPC_CLUSTER0_IPCLKPORT_I_PCLK,
	GOUT_BLK_CPUCL0_UID_DUMPPC_CLUSTER1_IPCLKPORT_I_PCLK,
	GOUT_BLK_CPUCL0_UID_LHM_ATB_T0_CLUSTER0_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL0_UID_LHM_ATB_T0_CLUSTER1_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL0_UID_LHM_ATB_T1_CLUSTER0_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL0_UID_LHM_ATB_T1_CLUSTER1_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL0_UID_LHM_ATB_T2_CLUSTER0_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL0_UID_LHM_ATB_T2_CLUSTER1_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL0_UID_LHM_ATB_T3_CLUSTER0_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL0_UID_LHM_ATB_T3_CLUSTER1_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL0_UID_LHM_ATB_T_AUD_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL0_UID_LHM_ATB_T_BDU_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL0_UID_LHM_AXI_P_CPUCL0_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL0_UID_LHM_AXI_P_CSSYS_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL0_UID_LHS_ACE_D_CLUSTER0_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL0_UID_LHS_ATB_T0_CLUSTER0_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL0_UID_LHS_ATB_T1_CLUSTER0_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL0_UID_LHS_ATB_T2_CLUSTER0_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL0_UID_LHS_ATB_T3_CLUSTER0_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL0_UID_LHS_AXI_G_CSSYS_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL0_UID_LHS_AXI_G_ETR_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL0_UID_LHS_AXI_P_CLUSTER0_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL0_UID_RSTnSYNC_CLK_CLUSTER0_ACLKP_IPCLKPORT_CLK,
	GOUT_BLK_CPUCL0_UID_RSTnSYNC_CLK_CLUSTER0_ACLK_IPCLKPORT_CLK,
	GOUT_BLK_CPUCL0_UID_RSTnSYNC_CLK_CLUSTER0_ATCLK_IPCLKPORT_CLK,
	GOUT_BLK_CPUCL0_UID_RSTnSYNC_CLK_CLUSTER0_PCLKDBG_IPCLKPORT_CLK,
	GOUT_BLK_CPUCL0_UID_RSTnSYNC_CLK_CPUCL0_PCLK_IPCLKPORT_CLK,
	GOUT_BLK_CPUCL0_UID_SECJTAG_IPCLKPORT_i_clk,
	GOUT_BLK_CPUCL0_UID_SYSREG_CPUCL0_IPCLKPORT_PCLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long cpucl0_qch_regs[] __initconst = {
	0x3000,	/* ADM_APB_G_CLUSTER0_QCH */
	0x3004,	/* CLUSTER0_QCH_PERIPHCLK */
	0x3070,	/* BUSIF_HPMCPUCL0_QCH */
	0x3074,	/* CLUSTER0_QCH_ATCLK */
	0x3078,	/* CLUSTER0_QCH_DBG_PD */
	0x307c,	/* CLUSTER0_QCH_GIC */
	0x3080,	/* CLUSTER0_QCH_PCLK */
	0x3084,	/* CLUSTER0_QCH_PDBGCLK */
	0x3088,	/* CLUSTER0_QCH_SCLK */
	0x308c,	/* CMU_CPUCL0_SHORTSTOP_QCH */
	0x3090,	/* CPUCL0_CMU_CPUCL0_QCH */
	0x3094,	/* CSSYS_QCH */
	0x3098,	/* DUMPPC_CLUSTER0_QCH */
	0x309c,	/* DUMPPC_CLUSTER1_QCH */
	0x30a0,	/* LHM_ATB_T0_CLUSTER0_QCH */
	0x30a4,	/* LHM_ATB_T0_CLUSTER1_QCH */
	0x30a8,	/* LHM_ATB_T1_CLUSTER0_QCH */
	0x30ac,	/* LHM_ATB_T1_CLUSTER1_QCH */
	0x30b0,	/* LHM_ATB_T2_CLUSTER0_QCH */
	0x30b4,	/* LHM_ATB_T2_CLUSTER1_QCH */
	0x30b8,	/* LHM_ATB_T3_CLUSTER0_QCH */
	0x30bc,	/* LHM_ATB_T3_CLUSTER1_QCH */
	0x30c0,	/* LHM_ATB_T_AUD_QCH */
	0x30c4,	/* LHM_ATB_T_BDU_QCH */
	0x30c8,	/* LHM_AXI_P_CPUCL0_QCH */
	0x30cc,	/* LHM_AXI_P_CSSYS_QCH */
	0x30d0,	/* LHS_ACE_D_CLUSTER0_QCH */
	0x30d4,	/* LHS_ATB_T0_CLUSTER0_QCH */
	0x30d8,	/* LHS_ATB_T1_CLUSTER0_QCH */
	0x30dc,	/* LHS_ATB_T2_CLUSTER0_QCH */
	0x30e0,	/* LHS_ATB_T3_CLUSTER0_QCH */
	0x30e4,	/* LHS_AXI_G_CSSYS_QCH */
	0x30e8,	/* LHS_AXI_G_ETR_QCH */
	0x30ec,	/* LHS_AXI_P_CLUSTER0_QCH */
	0x30f0,	/* SECJTAG_QCH */
	0x30f4,	/* SYSREG_CPUCL0_QCH */
};

static const struct samsung_fixed_rate_clock cpucl0_fixed_clks[] __initconst = {
	FRATE(CLK_PAD_CLK_CPUCL0_DBG_ATCLK, "pad_clk_cpucl0_dbg_atclk", NULL, 0, 100000000),
	FRATE(CLK_SCLK_OUT, "sclk_out", NULL, 0, 1150000000),
	FRATE(CLK_ACLK_OUT, "aclk_out", NULL, 0, 100000000),
	FRATE(CLK_ACLKP_OUT, "aclkp_out", NULL, 0, 100000000),
};

static const struct samsung_pll_rate_table fout_cpucl0_rate_table[] __initconst = {
	PLL_35XX_RATE(26 * MHZ, 1850333333U, 427, 6, 0),
	PLL_35XX_RATE(26 * MHZ, 1499333333U, 173, 3, 0),
	PLL_35XX_RATE(26 * MHZ, 1150500000U, 354, 4, 1),
	PLL_35XX_RATE(26 * MHZ, 650000000U, 200, 4, 1),
	PLL_35XX_RATE(26 * MHZ, 349916666U, 323, 6, 2),
};

static const struct samsung_pll_clock cpucl0_pll_clks[] __initconst = {
	PLL(pll_1050x, CLK_FOUT_CPUCL0, "fout_cpucl0", "oscclk",
	    PLL_LOCKTIME_PLL_CPUCL0_PLL_LOCK_TIME, PLL_CON0_PLL_CPUCL0_ENABLE, fout_cpucl0_rate_table),
};

/* List of parent clocks for Muxes in CMU_CPUCL0 */
PNAME(mout_cpucl0_pll_p) = { "fout_cpucl0", "mout_cmu_cpucl0_switch_user" };
PNAME(mout_cluster0_sclk_p) = { "gout_cpucl0_cpu", "sclk_out" };
PNAME(mout_cluster0_aclk_p) = { "dout_cluster0_aclk", "aclk_out" };
PNAME(mout_cluster0_aclkp_p) = { "dout_cluster0_aclkp", "aclkp_out" };
PNAME(mout_cmu_cpucl0_switch_user_p) = { "oscclk", "dout_clkcmu_cpucl0_switch" };
PNAME(mout_cmu_cpucl0_dbg_bus_user_p) = { "oscclk", "dout_clkcmu_cpucl0_dbg_bus" };

static const struct samsung_mux_clock cpucl0_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CPUCL0_PLL, "mout_cpucl0_pll", mout_cpucl0_pll_p,
	    MUX_CLK_CPUCL0_PLL, 0, 1),
	MUX(CLK_MOUT_CLUSTER0_SCLK, "mout_cluster0_sclk", mout_cluster0_sclk_p,
	    MUX_CLK_CLUSTER0_SCLK, 0, 1),
	MUX(CLK_MOUT_CLUSTER0_ACLK, "mout_cluster0_aclk", mout_cluster0_aclk_p,
	    MUX_CLK_CLUSTER0_ACLK, 0, 1),
	MUX(CLK_MOUT_CLUSTER0_ACLKP, "mout_cluster0_aclkp", mout_cluster0_aclkp_p,
	    MUX_CLK_CLUSTER0_ACLKP, 0, 1),
	MUX(CLK_MOUT_CMU_CPUCL0_SWITCH_USER, "mout_cmu_cpucl0_switch_user",
	    mout_cmu_cpucl0_switch_user_p,
	    MUX_CLKCMU_CPUCL0_SWITCH_USER, 4, 1),
	MUX(CLK_MOUT_CMU_CPUCL0_DBG_BUS_USER, "mout_cmu_cpucl0_dbg_bus_user",
	    mout_cmu_cpucl0_dbg_bus_user_p,
	    MUX_CLKCMU_CPUCL0_DBG_BUS_USER, 4, 1),
};

static const struct samsung_div_clock cpucl0_div_clks[] __initconst = {
	DIV(CLK_DOUT_CPUCL0_CMUREF, "dout_cpucl0_cmuref", "dout_cpucl0_cpu",
	    DIV_CLK_CPUCL0_CMUREF, 0, 3),
	DIV(CLK_DOUT_CLUSTER0_ACLK, "dout_cluster0_aclk", "gout_cpucl0_cpu",
	    DIV_CLK_CLUSTER0_ACLK, 0, 4),
	DIV(CLK_DOUT_CLUSTER0_ATCLK, "dout_cluster0_atclk", "gout_cpucl0_cpu",
	    DIV_CLK_CLUSTER0_ATCLK, 0, 4),
	DIV(CLK_DOUT_CLUSTER0_PCLKDBG, "dout_cluster0_pclkdbg", "gout_cpucl0_cpu",
	    DIV_CLK_CLUSTER0_PCLKDBG, 0, 4),
	DIV(CLK_DOUT_CPUCL0_CPU, "dout_cpucl0_cpu", "mout_cpucl0_pll",
	    DIV_CLK_CPUCL0_CPU, 0, 12),
	DIV(CLK_DOUT_CLUSTER0_PERIPHCLK, "dout_cluster0_periphclk", "gout_cpucl0_cpu",
	    DIV_CLK_CLUSTER0_PERIPHCLK, 0, 4),
	DIV(CLK_DOUT_CPUCL0_DBG_PCLKDBG, "dout_cpucl0_dbg_pclkdbg", "mout_cmu_cpucl0_dbg_bus_user",
	    DIV_CLK_CPUCL0_DBG_PCLKDBG, 0, 4),
	DIV(CLK_DOUT_CPUCL0_PCLK, "dout_cpucl0_pclk", "dout_cpucl0_cpu",
	    DIV_CLK_CPUCL0_PCLK, 0, 4),
	DIV(CLK_DOUT_CLUSTER0_ACLKP, "dout_cluster0_aclkp", "gout_cpucl0_cpu",
	    DIV_CLK_CLUSTER0_ACLKP, 0, 4),
};

static const struct samsung_gate_clock cpucl0_gate_clks[] __initconst = {
	GATE(CLK_GOUT_CPUCL0_AXI2APB_CPUCL0_ACLK, "gout_cpucl0_axi2apb_cpucl0_aclk",
	     "dout_cpucl0_pclk",
	     GOUT_BLK_CPUCL0_UID_AXI2APB_CPUCL0_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_SYSREG_CPUCL0_PCLK, "gout_cpucl0_sysreg_cpucl0_pclk",
	     "dout_cpucl0_pclk",
	     GOUT_BLK_CPUCL0_UID_SYSREG_CPUCL0_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_HPM_CPUCL0_hpm_targetclk_c, "gout_cpucl0_hpm_cpucl0_hpm_targetclk_c",
	     "dout_clkcmu_hpm",
	     CLK_BLK_CPUCL0_UID_HPM_CPUCL0_IPCLKPORT_hpm_targetclk_c, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_BUSIF_HPMCPUCL0_PCLK, "gout_cpucl0_busif_hpmcpucl0_pclk",
	     "dout_cpucl0_pclk",
	     GOUT_BLK_CPUCL0_UID_BUSIF_HPMCPUCL0_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_CPU, "gout_cpucl0_cpu", "dout_cpucl0_cpu",
	     GATE_CLK_CPUCL0_CPU, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_RSTnSYNC_CLK_CPUCL0_PCLK_CLK, "gout_cpucl0_rstnsync_clk_cpucl0_pclk_clk",
	     "dout_cpucl0_pclk",
	     GOUT_BLK_CPUCL0_UID_RSTnSYNC_CLK_CPUCL0_PCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_CSSYS_PCLKDBG, "gout_cpucl0_cssys_pclkdbg", "dout_cpucl0_dbg_pclkdbg",
	     GOUT_BLK_CPUCL0_UID_CSSYS_IPCLKPORT_PCLKDBG, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_ADS_APB_G_BDU_PCLKS, "gout_cpucl0_ads_apb_g_bdu_pclks",
	     "dout_cpucl0_dbg_pclkdbg",
	     GOUT_BLK_CPUCL0_UID_ADS_APB_G_BDU_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_DUMPPC_CLUSTER0_I_PCLK, "gout_cpucl0_dumppc_cluster0_i_pclk",
	     "dout_cpucl0_dbg_pclkdbg",
	     GOUT_BLK_CPUCL0_UID_DUMPPC_CLUSTER0_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_DUMPPC_CLUSTER1_I_PCLK, "gout_cpucl0_dumppc_cluster1_i_pclk",
	     "dout_cpucl0_dbg_pclkdbg",
	     GOUT_BLK_CPUCL0_UID_DUMPPC_CLUSTER1_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_LHM_ATB_T_AUD_I_CLK, "gout_cpucl0_lhm_atb_t_aud_i_clk",
	     "pad_clk_cpucl0_dbg_atclk",
	     GOUT_BLK_CPUCL0_UID_LHM_ATB_T_AUD_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_LHM_ATB_T_BDU_I_CLK, "gout_cpucl0_lhm_atb_t_bdu_i_clk",
	     "pad_clk_cpucl0_dbg_atclk",
	     GOUT_BLK_CPUCL0_UID_LHM_ATB_T_BDU_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_LHM_ATB_T0_CLUSTER0_I_CLK, "gout_cpucl0_lhm_atb_t0_cluster0_i_clk",
	     "pad_clk_cpucl0_dbg_atclk",
	     GOUT_BLK_CPUCL0_UID_LHM_ATB_T0_CLUSTER0_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_LHM_ATB_T0_CLUSTER1_I_CLK, "gout_cpucl0_lhm_atb_t0_cluster1_i_clk",
	     "pad_clk_cpucl0_dbg_atclk",
	     GOUT_BLK_CPUCL0_UID_LHM_ATB_T0_CLUSTER1_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_ADS_APB_G_CLUSTER0_PCLKS, "gout_cpucl0_ads_apb_g_cluster0_pclks",
	     "dout_cpucl0_dbg_pclkdbg",
	     GOUT_BLK_CPUCL0_UID_ADS_APB_G_CLUSTER0_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_ADS_APB_G_CLUSTER1_PCLKS, "gout_cpucl0_ads_apb_g_cluster1_pclks",
	     "dout_cpucl0_dbg_pclkdbg",
	     GOUT_BLK_CPUCL0_UID_ADS_APB_G_CLUSTER1_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_LHM_ATB_T1_CLUSTER0_I_CLK, "gout_cpucl0_lhm_atb_t1_cluster0_i_clk",
	     "pad_clk_cpucl0_dbg_atclk",
	     GOUT_BLK_CPUCL0_UID_LHM_ATB_T1_CLUSTER0_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_LHM_ATB_T1_CLUSTER1_I_CLK, "gout_cpucl0_lhm_atb_t1_cluster1_i_clk",
	     "pad_clk_cpucl0_dbg_atclk",
	     GOUT_BLK_CPUCL0_UID_LHM_ATB_T1_CLUSTER1_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_LHM_ATB_T2_CLUSTER0_I_CLK, "gout_cpucl0_lhm_atb_t2_cluster0_i_clk",
	     "pad_clk_cpucl0_dbg_atclk",
	     GOUT_BLK_CPUCL0_UID_LHM_ATB_T2_CLUSTER0_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_LHM_ATB_T2_CLUSTER1_I_CLK, "gout_cpucl0_lhm_atb_t2_cluster1_i_clk",
	     "pad_clk_cpucl0_dbg_atclk",
	     GOUT_BLK_CPUCL0_UID_LHM_ATB_T2_CLUSTER1_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_LHM_ATB_T3_CLUSTER0_I_CLK, "gout_cpucl0_lhm_atb_t3_cluster0_i_clk",
	     "pad_clk_cpucl0_dbg_atclk",
	     GOUT_BLK_CPUCL0_UID_LHM_ATB_T3_CLUSTER0_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_LHM_ATB_T3_CLUSTER1_I_CLK, "gout_cpucl0_lhm_atb_t3_cluster1_i_clk",
	     "pad_clk_cpucl0_dbg_atclk",
	     GOUT_BLK_CPUCL0_UID_LHM_ATB_T3_CLUSTER1_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_LHS_AXI_G_ETR_I_CLK, "gout_cpucl0_lhs_axi_g_etr_i_clk",
	     "pad_clk_cpucl0_dbg_atclk",
	     GOUT_BLK_CPUCL0_UID_LHS_AXI_G_ETR_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_LHS_AXI_G_CSSYS_I_CLK, "gout_cpucl0_lhs_axi_g_cssys_i_clk",
	     "dout_cpucl0_dbg_pclkdbg",
	     GOUT_BLK_CPUCL0_UID_LHS_AXI_G_CSSYS_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_SECJTAG_i_clk, "gout_cpucl0_secjtag_i_clk", "dout_cpucl0_dbg_pclkdbg",
	     GOUT_BLK_CPUCL0_UID_SECJTAG_IPCLKPORT_i_clk, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_ADS_AHB_G_SSS_HCLKS, "gout_cpucl0_ads_ahb_g_sss_hclks",
	     "dout_cpucl0_dbg_pclkdbg",
	     GOUT_BLK_CPUCL0_UID_ADS_AHB_G_SSS_IPCLKPORT_HCLKS, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_LHM_AXI_P_CPUCL0_I_CLK, "gout_cpucl0_lhm_axi_p_cpucl0_i_clk",
	     "dout_cpucl0_pclk",
	     GOUT_BLK_CPUCL0_UID_LHM_AXI_P_CPUCL0_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_RSTnSYNC_CLK_CPUCL0_DBG_PCLKDBG_CLK, "gout_cpucl0_rstnsync_clk_cpucl0_dbg_pclkdbg_clk",
	     "dout_cpucl0_dbg_pclkdbg",
	     CLK_BLK_CPUCL0_UID_RSTnSYNC_CLK_CPUCL0_DBG_PCLKDBG_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_RSTnSYNC_CLK_CPUCL0_DBG_ATCLK_CLK, "gout_cpucl0_rstnsync_clk_cpucl0_dbg_atclk_clk",
	     "pad_clk_cpucl0_dbg_atclk",
	     CLK_BLK_CPUCL0_UID_RSTnSYNC_CLK_CPUCL0_DBG_ATCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_ADS_APB_G_DSPM_PCLKS, "gout_cpucl0_ads_apb_g_dspm_pclks",
	     "dout_cpucl0_dbg_pclkdbg",
	     GOUT_BLK_CPUCL0_UID_ADS_APB_G_DSPM_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_LHM_AXI_P_CSSYS_I_CLK, "gout_cpucl0_lhm_axi_p_cssys_i_clk",
	     "dout_cpucl0_dbg_pclkdbg",
	     GOUT_BLK_CPUCL0_UID_LHM_AXI_P_CSSYS_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_AXI2APB_P_CSSYS_ACLK, "gout_cpucl0_axi2apb_p_cssys_aclk",
	     "dout_cpucl0_dbg_pclkdbg",
	     GOUT_BLK_CPUCL0_UID_AXI2APB_P_CSSYS_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_LHS_ACE_D_CLUSTER0_I_CLK, "gout_cpucl0_lhs_ace_d_cluster0_i_clk",
	     "mout_cluster0_aclk",
	     GOUT_BLK_CPUCL0_UID_LHS_ACE_D_CLUSTER0_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_LHS_ATB_T0_CLUSTER0_I_CLK, "gout_cpucl0_lhs_atb_t0_cluster0_i_clk",
	     "dout_cluster0_atclk",
	     GOUT_BLK_CPUCL0_UID_LHS_ATB_T0_CLUSTER0_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_LHS_ATB_T1_CLUSTER0_I_CLK, "gout_cpucl0_lhs_atb_t1_cluster0_i_clk",
	     "dout_cluster0_atclk",
	     GOUT_BLK_CPUCL0_UID_LHS_ATB_T1_CLUSTER0_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_LHS_ATB_T2_CLUSTER0_I_CLK, "gout_cpucl0_lhs_atb_t2_cluster0_i_clk",
	     "dout_cluster0_atclk",
	     GOUT_BLK_CPUCL0_UID_LHS_ATB_T2_CLUSTER0_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_LHS_ATB_T3_CLUSTER0_I_CLK, "gout_cpucl0_lhs_atb_t3_cluster0_i_clk",
	     "dout_cluster0_atclk",
	     GOUT_BLK_CPUCL0_UID_LHS_ATB_T3_CLUSTER0_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_ADM_APB_G_CLUSTER0_PCLKM, "gout_cpucl0_adm_apb_g_cluster0_pclkm",
	     "dout_cluster0_pclkdbg",
	     GOUT_BLK_CPUCL0_UID_ADM_APB_G_CLUSTER0_IPCLKPORT_PCLKM, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_AXI_DS_64to32_G_CSSYS_aclk, "gout_cpucl0_axi_ds_64to32_g_cssys_aclk",
	     "dout_cpucl0_dbg_pclkdbg",
	     GOUT_BLK_CPUCL0_UID_AXI_DS_64to32_G_CSSYS_IPCLKPORT_aclk, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_RSTnSYNC_CLK_CLUSTER0_ACLK_CLK, "gout_cpucl0_rstnsync_clk_cluster0_aclk_clk",
	     "mout_cluster0_aclk",
	     GOUT_BLK_CPUCL0_UID_RSTnSYNC_CLK_CLUSTER0_ACLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_RSTnSYNC_CLK_CLUSTER0_ATCLK_CLK, "gout_cpucl0_rstnsync_clk_cluster0_atclk_clk",
	     "dout_cluster0_atclk",
	     GOUT_BLK_CPUCL0_UID_RSTnSYNC_CLK_CLUSTER0_ATCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_RSTnSYNC_CLK_CLUSTER0_PCLKDBG_CLK, "gout_cpucl0_rstnsync_clk_cluster0_pclkdbg_clk",
	     "dout_cluster0_pclkdbg",
	     GOUT_BLK_CPUCL0_UID_RSTnSYNC_CLK_CLUSTER0_PCLKDBG_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_CPUCL0_CMU_CPUCL0_PCLK, "gout_cpucl0_cpucl0_cmu_cpucl0_pclk",
	     "dout_cpucl0_pclk",
	     CLK_BLK_CPUCL0_UID_CPUCL0_CMU_CPUCL0_IPCLKPORT_PCLK, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_CPUCL0_LHS_AXI_P_CLUSTER0_I_CLK, "gout_cpucl0_lhs_axi_p_cluster0_i_clk",
	     "mout_cluster0_aclkp",
	     GOUT_BLK_CPUCL0_UID_LHS_AXI_P_CLUSTER0_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_RSTnSYNC_CLK_CLUSTER0_ACLKP_CLK, "gout_cpucl0_rstnsync_clk_cluster0_aclkp_clk",
	     "mout_cluster0_aclkp",
	     GOUT_BLK_CPUCL0_UID_RSTnSYNC_CLK_CLUSTER0_ACLKP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_CLUSTER0_ATCLK, "gout_cpucl0_cluster0_atclk", "dout_cluster0_atclk",
	     CLK_BLK_CPUCL0_UID_CLUSTER0_IPCLKPORT_ATCLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_CLUSTER0_PCLK, "gout_cpucl0_cluster0_pclk", "dout_cluster0_pclkdbg",
	     CLK_BLK_CPUCL0_UID_CLUSTER0_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_CLUSTER0_PERIPHCLK, "gout_cpucl0_cluster0_periphclk",
	     "dout_cluster0_periphclk",
	     CLK_BLK_CPUCL0_UID_CLUSTER0_IPCLKPORT_PERIPHCLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_CSSYS_ATCLK, "gout_cpucl0_cssys_atclk", "pad_clk_cpucl0_dbg_atclk",
	     GOUT_BLK_CPUCL0_UID_CSSYS_IPCLKPORT_ATCLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_CLUSTER0_SCLK, "gout_cpucl0_cluster0_sclk", "gout_cpucl0_cpu",
	     CLK_BLK_CPUCL0_UID_CLUSTER0_IPCLKPORT_SCLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_ASB_APB_P_DUMPPC_CLUSTER0_PCLKM, "gout_cpucl0_asb_apb_p_dumppc_cluster0_pclkm",
	     "dout_cpucl0_dbg_pclkdbg",
	     GOUT_BLK_CPUCL0_UID_ASB_APB_P_DUMPPC_CLUSTER0_IPCLKPORT_PCLKM, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_ASB_APB_P_DUMPPC_CLUSTER0_PCLKS, "gout_cpucl0_asb_apb_p_dumppc_cluster0_pclks",
	     "dout_cpucl0_pclk",
	     GOUT_BLK_CPUCL0_UID_ASB_APB_P_DUMPPC_CLUSTER0_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_ASB_APB_P_DUMPPC_CLUSTER1_PCLKM, "gout_cpucl0_asb_apb_p_dumppc_cluster1_pclkm",
	     "dout_cpucl0_dbg_pclkdbg",
	     GOUT_BLK_CPUCL0_UID_ASB_APB_P_DUMPPC_CLUSTER1_IPCLKPORT_PCLKM, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_ASB_APB_P_DUMPPC_CLUSTER1_PCLKS, "gout_cpucl0_asb_apb_p_dumppc_cluster1_pclks",
	     "dout_cpucl0_pclk",
	     GOUT_BLK_CPUCL0_UID_ASB_APB_P_DUMPPC_CLUSTER1_IPCLKPORT_PCLKS, 21, 0, 0),
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
	.fixed_clks		= cpucl0_fixed_clks,
	.nr_fixed_clks		= ARRAY_SIZE(cpucl0_fixed_clks),
	.nr_clk_ids		= CLKS_NR_CPUCL0,
	.clk_regs		= cpucl0_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cpucl0_clk_regs),
	.qch_regs		= cpucl0_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(cpucl0_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_cpucl0_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &cpucl0_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_cpucl0, "samsung,exynos9810-cmu-cpucl0",
	       exynos9810_cmu_cpucl0_init);

/* ---- CMU_CPUCL1 ------------------------------------------------------*/

/* Register Offset definitions for CMU_CPUCL1 (0x1d100000) */
#define PLL_LOCKTIME_PLL_CPUCL1_PLL_LOCK_TIME			0x0000
#define MUX_CLKCMU_CPUCL1_SWITCH_USER			0x0100
#define PLL_CON0_PLL_CPUCL1_ENABLE			0x0120
#define MUX_CLK_CPUCL1_PLL			0x1000
#define DIV_CLK_CLUSTER1_ACLK			0x1800
#define DIV_CLK_CLUSTER1_ATCLK			0x1804
#define DIV_CLK_CPUCL1_CMUREF			0x1808
#define DIV_CLK_CPUCL1_CPU			0x180c
#define DIV_CLK_CPUCL1_PCLK			0x1810
#define DIV_CLK_CPUCL1_PCLKDBG			0x1818
#define CLK_BLK_CPUCL1_UID_CLUSTER1_IPCLKPORT_PCLKDBG	0x2000
#define CLK_BLK_CPUCL1_UID_CPUCL1_CMU_CPUCL1_IPCLKPORT_PCLK	0x2004
#define CLK_BLK_CPUCL1_UID_HPM_CPUCL1_0_IPCLKPORT_hpm_targetclk_c	0x2008
#define CLK_BLK_CPUCL1_UID_HPM_CPUCL1_1_IPCLKPORT_hpm_targetclk_c	0x200c
#define CLK_BLK_CPUCL1_UID_HPM_CPUCL1_2_IPCLKPORT_hpm_targetclk_c	0x2010
#define GATE_CLK_CPUCL1_CPU			0x201c
#define GOUT_BLK_CPUCL1_UID_AXI2APB_CPUCL1_IPCLKPORT_ACLK	0x2020
#define GOUT_BLK_CPUCL1_UID_BUSIF_HPMCPUCL1_IPCLKPORT_PCLK	0x2024
#define GOUT_BLK_CPUCL1_UID_LHM_AXI_P_CPUCL1_IPCLKPORT_I_CLK	0x2028
#define GOUT_BLK_CPUCL1_UID_RSTnSYNC_CLK_CPUCL1_PCLK_IPCLKPORT_CLK	0x202c
#define GOUT_BLK_CPUCL1_UID_SYSREG_CPUCL1_IPCLKPORT_PCLK	0x2030

static const unsigned long cpucl1_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_CPUCL1_PLL_LOCK_TIME,
	MUX_CLKCMU_CPUCL1_SWITCH_USER,
	PLL_CON0_PLL_CPUCL1_ENABLE,
	MUX_CLK_CPUCL1_PLL,
	DIV_CLK_CLUSTER1_ACLK,
	DIV_CLK_CLUSTER1_ATCLK,
	DIV_CLK_CPUCL1_CMUREF,
	DIV_CLK_CPUCL1_CPU,
	DIV_CLK_CPUCL1_PCLK,
	DIV_CLK_CPUCL1_PCLKDBG,
	CLK_BLK_CPUCL1_UID_CLUSTER1_IPCLKPORT_PCLKDBG,
	CLK_BLK_CPUCL1_UID_CPUCL1_CMU_CPUCL1_IPCLKPORT_PCLK,
	CLK_BLK_CPUCL1_UID_HPM_CPUCL1_0_IPCLKPORT_hpm_targetclk_c,
	CLK_BLK_CPUCL1_UID_HPM_CPUCL1_1_IPCLKPORT_hpm_targetclk_c,
	CLK_BLK_CPUCL1_UID_HPM_CPUCL1_2_IPCLKPORT_hpm_targetclk_c,
	GATE_CLK_CPUCL1_CPU,
	GOUT_BLK_CPUCL1_UID_AXI2APB_CPUCL1_IPCLKPORT_ACLK,
	GOUT_BLK_CPUCL1_UID_BUSIF_HPMCPUCL1_IPCLKPORT_PCLK,
	GOUT_BLK_CPUCL1_UID_LHM_AXI_P_CPUCL1_IPCLKPORT_I_CLK,
	GOUT_BLK_CPUCL1_UID_RSTnSYNC_CLK_CPUCL1_PCLK_IPCLKPORT_CLK,
	GOUT_BLK_CPUCL1_UID_SYSREG_CPUCL1_IPCLKPORT_PCLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long cpucl1_qch_regs[] __initconst = {
	0x3000,	/* CLUSTER1_QCH_CPU */
	0x3004,	/* CLUSTER1_QCH_PCLKDBG */
	0x301c,	/* BUSIF_HPMCPUCL1_QCH */
	0x3020,	/* CLUSTER1_QCH_LHS_ATB_T0_CLUSTER1 */
	0x3024,	/* CLUSTER1_QCH_LHS_ATB_T1_CLUSTER1 */
	0x3028,	/* CLUSTER1_QCH_LHS_ATB_T2_CLUSTER1 */
	0x302c,	/* CLUSTER1_QCH_LHS_ATB_T3_CLUSTER1 */
	0x3030,	/* CMU_CPUCL1_SHORTSTOP_QCH */
	0x3034,	/* CPUCL1_CMU_CPUCL1_QCH */
	0x3038,	/* LHM_AXI_P_CPUCL1_QCH */
	0x303c,	/* SYSREG_CPUCL1_QCH */
};

static const struct samsung_pll_rate_table fout_cpucl1_rate_table[] __initconst = {
	PLL_35XX_RATE(26 * MHZ, 2327000000U, 358, 4, 0),
	PLL_35XX_RATE(26 * MHZ, 1893666666U, 437, 6, 0),
	PLL_35XX_RATE(26 * MHZ, 1478750000U, 455, 4, 1),
	PLL_35XX_RATE(26 * MHZ, 928200000U, 357, 5, 1),
	PLL_35XX_RATE(26 * MHZ, 400000000U, 200, 13, 0),
};

static const struct samsung_pll_clock cpucl1_pll_clks[] __initconst = {
	PLL(pll_1019x, CLK_FOUT_CPUCL1, "fout_cpucl1", "oscclk",
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
	DIV(CLK_DOUT_CPUCL1_CMUREF, "dout_cpucl1_cmuref", "dout_cpucl1_cpu",
	    DIV_CLK_CPUCL1_CMUREF, 0, 3),
	DIV(CLK_DOUT_CPUCL1_PCLK, "dout_cpucl1_pclk", "dout_cpucl1_cpu",
	    DIV_CLK_CPUCL1_PCLK, 0, 4),
	DIV(CLK_DOUT_CLUSTER1_ACLK, "dout_cluster1_aclk", "gout_cpucl1_cpu",
	    DIV_CLK_CLUSTER1_ACLK, 0, 4),
	DIV(CLK_DOUT_CLUSTER1_ATCLK, "dout_cluster1_atclk", "gout_cpucl1_cpu",
	    DIV_CLK_CLUSTER1_ATCLK, 0, 4),
	DIV(CLK_DOUT_CPUCL1_CPU, "dout_cpucl1_cpu", "mout_cpucl1_pll",
	    DIV_CLK_CPUCL1_CPU, 0, 12),
	DIV(CLK_DOUT_CPUCL1_PCLKDBG, "dout_cpucl1_pclkdbg", "dout_cpucl1_cpu",
	    DIV_CLK_CPUCL1_PCLKDBG, 0, 4),
};

static const struct samsung_gate_clock cpucl1_gate_clks[] __initconst = {
	GATE(CLK_GOUT_CPUCL1_CPUCL1_CMU_CPUCL1_PCLK, "gout_cpucl1_cpucl1_cmu_cpucl1_pclk",
	     "dout_cpucl1_pclk",
	     CLK_BLK_CPUCL1_UID_CPUCL1_CMU_CPUCL1_IPCLKPORT_PCLK, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_CPUCL1_SYSREG_CPUCL1_PCLK, "gout_cpucl1_sysreg_cpucl1_pclk",
	     "dout_cpucl1_pclk",
	     GOUT_BLK_CPUCL1_UID_SYSREG_CPUCL1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL1_BUSIF_HPMCPUCL1_PCLK, "gout_cpucl1_busif_hpmcpucl1_pclk",
	     "dout_cpucl1_pclk",
	     GOUT_BLK_CPUCL1_UID_BUSIF_HPMCPUCL1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL1_HPM_CPUCL1_0_hpm_targetclk_c, "gout_cpucl1_hpm_cpucl1_0_hpm_targetclk_c",
	     "dout_clkcmu_hpm",
	     CLK_BLK_CPUCL1_UID_HPM_CPUCL1_0_IPCLKPORT_hpm_targetclk_c, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL1_CPU, "gout_cpucl1_cpu", "dout_cpucl1_cpu",
	     GATE_CLK_CPUCL1_CPU, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL1_RSTnSYNC_CLK_CPUCL1_PCLK_CLK, "gout_cpucl1_rstnsync_clk_cpucl1_pclk_clk",
	     "dout_cpucl1_pclk",
	     GOUT_BLK_CPUCL1_UID_RSTnSYNC_CLK_CPUCL1_PCLK_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL1_CLUSTER1_PCLKDBG, "gout_cpucl1_cluster1_pclkdbg",
	     "dout_cpucl1_pclkdbg",
	     CLK_BLK_CPUCL1_UID_CLUSTER1_IPCLKPORT_PCLKDBG, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL1_AXI2APB_CPUCL1_ACLK, "gout_cpucl1_axi2apb_cpucl1_aclk",
	     "dout_cpucl1_pclk",
	     GOUT_BLK_CPUCL1_UID_AXI2APB_CPUCL1_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL1_LHM_AXI_P_CPUCL1_I_CLK, "gout_cpucl1_lhm_axi_p_cpucl1_i_clk",
	     "dout_cpucl1_pclk",
	     GOUT_BLK_CPUCL1_UID_LHM_AXI_P_CPUCL1_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL1_HPM_CPUCL1_1_hpm_targetclk_c, "gout_cpucl1_hpm_cpucl1_1_hpm_targetclk_c",
	     "dout_clkcmu_hpm",
	     CLK_BLK_CPUCL1_UID_HPM_CPUCL1_1_IPCLKPORT_hpm_targetclk_c, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL1_HPM_CPUCL1_2_hpm_targetclk_c, "gout_cpucl1_hpm_cpucl1_2_hpm_targetclk_c",
	     "dout_clkcmu_hpm",
	     CLK_BLK_CPUCL1_UID_HPM_CPUCL1_2_IPCLKPORT_hpm_targetclk_c, 21, 0, 0),
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

static void __init exynos9810_cmu_cpucl1_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &cpucl1_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_cpucl1, "samsung,exynos9810-cmu-cpucl1",
	       exynos9810_cmu_cpucl1_init);

/* ---- CMU_DCF ---------------------------------------------------------*/

/* Register Offset definitions for CMU_DCF (0x16a00000) */
#define MUX_CLKCMU_DCF_BUS_USER			0x0100
#define DIV_CLK_DCF_BUSP			0x1800

static const unsigned long dcf_clk_regs[] __initconst = {
	MUX_CLKCMU_DCF_BUS_USER,
	DIV_CLK_DCF_BUSP,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long dcf_qch_regs[] __initconst = {
	0x3020,	/* BTM_DCF_QCH */
	0x3024,	/* DCF_CMU_DCF_QCH */
	0x3028,	/* IS_DCF_QCH_C2SYNC_2SLV */
	0x302c,	/* IS_DCF_QCH_CIP */
	0x3030,	/* IS_DCF_QCH_PGEN_LITE */
	0x3034,	/* IS_DCF_QCH_PPMU */
	0x3038,	/* IS_DCF_QCH_QE */
	0x303c,	/* IS_DCF_QCH_SYSMMU */
	0x3040,	/* IS_DCF_QCH_SYSREG */
	0x3044,	/* LHM_ATB_DCPOSTDCF_QCH */
	0x3048,	/* LHM_ATB_ISPHQDCF_QCH */
	0x304c,	/* LHM_AXI_D_DCPOSTDCF_QCH */
	0x3050,	/* LHM_AXI_P_DCF_QCH */
	0x3054,	/* LHS_ATB_DCFDCPOST_QCH */
	0x3058,	/* LHS_ATB_DCFISPLP_QCH */
	0x305c,	/* LHS_AXI_D_DCF_QCH */
	0x3060,	/* LHS_AXI_P_DCFDCPOST_QCH */
};

/* List of parent clocks for Muxes in CMU_DCF */
PNAME(mout_cmu_dcf_bus_user_p) = { "oscclk", "dout_clkcmu_dcf_bus" };

static const struct samsung_mux_clock dcf_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_DCF_BUS_USER, "mout_cmu_dcf_bus_user", mout_cmu_dcf_bus_user_p,
	    MUX_CLKCMU_DCF_BUS_USER, 4, 1),
};

static const struct samsung_div_clock dcf_div_clks[] __initconst = {
	DIV(CLK_DOUT_DCF_BUSP, "dout_dcf_busp", "mout_cmu_dcf_bus_user",
	    DIV_CLK_DCF_BUSP, 0, 3),
};

static const struct samsung_cmu_info dcf_cmu_info __initconst = {
	.mux_clks		= dcf_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(dcf_mux_clks),
	.div_clks		= dcf_div_clks,
	.nr_div_clks		= ARRAY_SIZE(dcf_div_clks),
	.nr_clk_ids		= CLKS_NR_DCF,
	.clk_regs		= dcf_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(dcf_clk_regs),
	.qch_regs		= dcf_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(dcf_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_dcf_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &dcf_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_dcf, "samsung,exynos9810-cmu-dcf",
	       exynos9810_cmu_dcf_init);

/* ---- CMU_DCPOST ------------------------------------------------------*/

/* Register Offset definitions for CMU_DCPOST (0x16b00000) */
#define MUX_CLKCMU_DCPOST_BUS_USER			0x0100
#define DIV_CLK_DCPOST_BUSP			0x1800
#define CLK_BLK_DCPOST_UID_DCPOST_CMU_DCPOST_IPCLKPORT_PCLK	0x2000
#define GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_AD_APB_DCPOST_M_C2SYNC_1SLV_PCLKM	0x2004
#define GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_AD_APB_DCPOST_M_CIP2_PCLKM	0x2008
#define GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_AD_APB_DCPOST_S_C2SYNC_1SLV_PCLKS	0x200c
#define GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_AD_APB_DCPOST_S_CIP2_PCLKS	0x2010
#define GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_AXI2APB_DCPOST_ACLK	0x2014
#define GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_C2SYNC_1SLV_ACLK	0x2018
#define GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_CIP2_ACLK	0x201c
#define GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_QE_DCPOST_ACLK	0x2020
#define GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_QE_DCPOST_PCLK	0x2024
#define GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_SYSREG_DCPOST_PCLK	0x2028
#define GOUT_BLK_DCPOST_UID_LHM_ATB_DCFDCPOST_IPCLKPORT_I_CLK	0x202c
#define GOUT_BLK_DCPOST_UID_LHM_ATB_DCRDDCPOST_IPCLKPORT_I_CLK	0x2030
#define GOUT_BLK_DCPOST_UID_LHM_AXI_P_DCFDCPOST_IPCLKPORT_I_CLK	0x2034
#define GOUT_BLK_DCPOST_UID_LHS_ATB_DCPOSTDCF_IPCLKPORT_I_CLK	0x2038
#define GOUT_BLK_DCPOST_UID_LHS_ATB_DCPOSTDCRD_IPCLKPORT_I_CLK	0x203c
#define GOUT_BLK_DCPOST_UID_LHS_AXI_D_DCPOSTDCF_IPCLKPORT_I_CLK	0x2040
#define GOUT_BLK_DCPOST_UID_RSTnSYNC_CLK_DCPOST_BUSD_IPCLKPORT_CLK	0x2044
#define GOUT_BLK_DCPOST_UID_RSTnSYNC_CLK_DCPOST_BUSP_IPCLKPORT_CLK	0x2048

static const unsigned long dcpost_clk_regs[] __initconst = {
	MUX_CLKCMU_DCPOST_BUS_USER,
	DIV_CLK_DCPOST_BUSP,
	CLK_BLK_DCPOST_UID_DCPOST_CMU_DCPOST_IPCLKPORT_PCLK,
	GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_AD_APB_DCPOST_M_C2SYNC_1SLV_PCLKM,
	GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_AD_APB_DCPOST_M_CIP2_PCLKM,
	GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_AD_APB_DCPOST_S_C2SYNC_1SLV_PCLKS,
	GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_AD_APB_DCPOST_S_CIP2_PCLKS,
	GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_AXI2APB_DCPOST_ACLK,
	GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_C2SYNC_1SLV_ACLK,
	GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_CIP2_ACLK,
	GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_QE_DCPOST_ACLK,
	GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_QE_DCPOST_PCLK,
	GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_SYSREG_DCPOST_PCLK,
	GOUT_BLK_DCPOST_UID_LHM_ATB_DCFDCPOST_IPCLKPORT_I_CLK,
	GOUT_BLK_DCPOST_UID_LHM_ATB_DCRDDCPOST_IPCLKPORT_I_CLK,
	GOUT_BLK_DCPOST_UID_LHM_AXI_P_DCFDCPOST_IPCLKPORT_I_CLK,
	GOUT_BLK_DCPOST_UID_LHS_ATB_DCPOSTDCF_IPCLKPORT_I_CLK,
	GOUT_BLK_DCPOST_UID_LHS_ATB_DCPOSTDCRD_IPCLKPORT_I_CLK,
	GOUT_BLK_DCPOST_UID_LHS_AXI_D_DCPOSTDCF_IPCLKPORT_I_CLK,
	GOUT_BLK_DCPOST_UID_RSTnSYNC_CLK_DCPOST_BUSD_IPCLKPORT_CLK,
	GOUT_BLK_DCPOST_UID_RSTnSYNC_CLK_DCPOST_BUSP_IPCLKPORT_CLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long dcpost_qch_regs[] __initconst = {
	0x3018,	/* DCPOST_CMU_DCPOST_QCH */
	0x301c,	/* IS_DCPOST_QCH_C2SYNC_1SLV_CLK */
	0x3020,	/* IS_DCPOST_QCH_CIP2 */
	0x3024,	/* IS_DCPOST_QCH_QE */
	0x3028,	/* IS_DCPOST_QCH_SYSREG */
	0x302c,	/* LHM_ATB_DCFDCPOST_QCH */
	0x3030,	/* LHM_ATB_DCRDDCPOST_QCH */
	0x3034,	/* LHM_AXI_P_DCFDCPOST_QCH */
	0x3038,	/* LHS_ATB_DCPOSTDCF_QCH */
	0x303c,	/* LHS_ATB_DCPOSTDCRD_QCH */
	0x3040,	/* LHS_AXI_D_DCPOSTDCF_QCH */
};

/* List of parent clocks for Muxes in CMU_DCPOST */
PNAME(mout_cmu_dcpost_bus_user_p) = { "oscclk", "dout_clkcmu_dcpost_bus" };

static const struct samsung_mux_clock dcpost_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_DCPOST_BUS_USER, "mout_cmu_dcpost_bus_user", mout_cmu_dcpost_bus_user_p,
	    MUX_CLKCMU_DCPOST_BUS_USER, 4, 1),
};

static const struct samsung_div_clock dcpost_div_clks[] __initconst = {
	DIV(CLK_DOUT_DCPOST_BUSP, "dout_dcpost_busp", "mout_cmu_dcpost_bus_user",
	    DIV_CLK_DCPOST_BUSP, 0, 4),
};

static const struct samsung_gate_clock dcpost_gate_clks[] __initconst = {
	GATE(CLK_GOUT_DCPOST_LHS_ATB_DCPOSTDCF_I_CLK, "gout_dcpost_lhs_atb_dcpostdcf_i_clk",
	     "mout_cmu_dcpost_bus_user",
	     GOUT_BLK_DCPOST_UID_LHS_ATB_DCPOSTDCF_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DCPOST_LHS_AXI_D_DCPOSTDCF_I_CLK, "gout_dcpost_lhs_axi_d_dcpostdcf_i_clk",
	     "mout_cmu_dcpost_bus_user",
	     GOUT_BLK_DCPOST_UID_LHS_AXI_D_DCPOSTDCF_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DCPOST_LHM_ATB_DCRDDCPOST_I_CLK, "gout_dcpost_lhm_atb_dcrddcpost_i_clk",
	     "mout_cmu_dcpost_bus_user",
	     GOUT_BLK_DCPOST_UID_LHM_ATB_DCRDDCPOST_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DCPOST_IS_DCPOST_SYSREG_DCPOST_PCLK, "gout_dcpost_is_dcpost_sysreg_dcpost_pclk",
	     "dout_dcpost_busp",
	     GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_SYSREG_DCPOST_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DCPOST_IS_DCPOST_AD_APB_DCPOST_M_CIP2_PCLKM, "gout_dcpost_is_dcpost_ad_apb_dcpost_m_cip2_pclkm",
	     "mout_cmu_dcpost_bus_user",
	     GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_AD_APB_DCPOST_M_CIP2_PCLKM, 21, 0, 0),
	GATE(CLK_GOUT_DCPOST_IS_DCPOST_AD_APB_DCPOST_S_CIP2_PCLKS, "gout_dcpost_is_dcpost_ad_apb_dcpost_s_cip2_pclks",
	     "dout_dcpost_busp",
	     GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_AD_APB_DCPOST_S_CIP2_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_DCPOST_IS_DCPOST_QE_DCPOST_ACLK, "gout_dcpost_is_dcpost_qe_dcpost_aclk",
	     "mout_cmu_dcpost_bus_user",
	     GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_QE_DCPOST_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_DCPOST_IS_DCPOST_QE_DCPOST_PCLK, "gout_dcpost_is_dcpost_qe_dcpost_pclk",
	     "dout_dcpost_busp",
	     GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_QE_DCPOST_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DCPOST_LHM_AXI_P_DCFDCPOST_I_CLK, "gout_dcpost_lhm_axi_p_dcfdcpost_i_clk",
	     "dout_dcpost_busp",
	     GOUT_BLK_DCPOST_UID_LHM_AXI_P_DCFDCPOST_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DCPOST_RSTnSYNC_CLK_DCPOST_BUSD_CLK, "gout_dcpost_rstnsync_clk_dcpost_busd_clk",
	     "mout_cmu_dcpost_bus_user",
	     GOUT_BLK_DCPOST_UID_RSTnSYNC_CLK_DCPOST_BUSD_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DCPOST_RSTnSYNC_CLK_DCPOST_BUSP_CLK, "gout_dcpost_rstnsync_clk_dcpost_busp_clk",
	     "dout_dcpost_busp",
	     GOUT_BLK_DCPOST_UID_RSTnSYNC_CLK_DCPOST_BUSP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DCPOST_DCPOST_CMU_DCPOST_PCLK, "gout_dcpost_dcpost_cmu_dcpost_pclk",
	     "dout_dcpost_busp",
	     CLK_BLK_DCPOST_UID_DCPOST_CMU_DCPOST_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DCPOST_IS_DCPOST_AD_APB_DCPOST_M_C2SYNC_1SLV_PCLKM, "gout_dcpost_is_dcpost_ad_apb_dcpost_m_c2sync_1slv_pclkm",
	     "mout_cmu_dcpost_bus_user",
	     GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_AD_APB_DCPOST_M_C2SYNC_1SLV_PCLKM, 21, 0, 0),
	GATE(CLK_GOUT_DCPOST_IS_DCPOST_AD_APB_DCPOST_S_C2SYNC_1SLV_PCLKS, "gout_dcpost_is_dcpost_ad_apb_dcpost_s_c2sync_1slv_pclks",
	     "dout_dcpost_busp",
	     GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_AD_APB_DCPOST_S_C2SYNC_1SLV_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_DCPOST_LHM_ATB_DCFDCPOST_I_CLK, "gout_dcpost_lhm_atb_dcfdcpost_i_clk",
	     "mout_cmu_dcpost_bus_user",
	     GOUT_BLK_DCPOST_UID_LHM_ATB_DCFDCPOST_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DCPOST_LHS_ATB_DCPOSTDCRD_I_CLK, "gout_dcpost_lhs_atb_dcpostdcrd_i_clk",
	     "mout_cmu_dcpost_bus_user",
	     GOUT_BLK_DCPOST_UID_LHS_ATB_DCPOSTDCRD_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DCPOST_IS_DCPOST_CIP2_ACLK, "gout_dcpost_is_dcpost_cip2_aclk",
	     "mout_cmu_dcpost_bus_user",
	     GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_CIP2_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_DCPOST_IS_DCPOST_C2SYNC_1SLV_ACLK, "gout_dcpost_is_dcpost_c2sync_1slv_aclk",
	     "mout_cmu_dcpost_bus_user",
	     GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_C2SYNC_1SLV_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_DCPOST_IS_DCPOST_AXI2APB_DCPOST_ACLK, "gout_dcpost_is_dcpost_axi2apb_dcpost_aclk",
	     "dout_dcpost_busp",
	     GOUT_BLK_DCPOST_UID_IS_DCPOST_IPCLKPORT_AXI2APB_DCPOST_ACLK, 21, 0, 0),
};

static const struct samsung_cmu_info dcpost_cmu_info __initconst = {
	.mux_clks		= dcpost_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(dcpost_mux_clks),
	.div_clks		= dcpost_div_clks,
	.nr_div_clks		= ARRAY_SIZE(dcpost_div_clks),
	.gate_clks		= dcpost_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(dcpost_gate_clks),
	.nr_clk_ids		= CLKS_NR_DCPOST,
	.clk_regs		= dcpost_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(dcpost_clk_regs),
	.qch_regs		= dcpost_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(dcpost_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_dcpost_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &dcpost_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_dcpost, "samsung,exynos9810-cmu-dcpost",
	       exynos9810_cmu_dcpost_init);

/* ---- CMU_DCRD --------------------------------------------------------*/

/* Register Offset definitions for CMU_DCRD (0x16800000) */
#define MUX_CLKCMU_DCRD_BUS_USER			0x0100
#define DIV_CLK_DCRD_BUSD_HALF			0x1800
#define DIV_CLK_DCRD_BUSP			0x1804

static const unsigned long dcrd_clk_regs[] __initconst = {
	MUX_CLKCMU_DCRD_BUS_USER,
	DIV_CLK_DCRD_BUSD_HALF,
	DIV_CLK_DCRD_BUSP,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long dcrd_qch_regs[] __initconst = {
	0x3014,	/* BTM_DCRD_QCH */
	0x3018,	/* DCRD_CMU_DCRD_QCH */
	0x301c,	/* IS_DCRD_QCH_DCP */
	0x3020,	/* IS_DCRD_QCH_DCP_C2C */
	0x3024,	/* IS_DCRD_QCH_DCP_DIV2 */
	0x3028,	/* IS_DCRD_QCH_PGEN_LITE */
	0x302c,	/* IS_DCRD_QCH_PPMU */
	0x3030,	/* IS_DCRD_QCH_SYSMMU */
	0x3034,	/* IS_DCRD_QCH_SYSREG */
	0x3038,	/* LHM_ATB_DCPOSTDCRD_QCH */
	0x303c,	/* LHM_AXI_P_DCRD_QCH */
	0x3040,	/* LHS_ATB_DCRDDCPOST_QCH */
	0x3044,	/* LHS_ATB_DCRDISPLP_QCH */
	0x3048,	/* LHS_AXI_D_DCRD_QCH */
};

/* List of parent clocks for Muxes in CMU_DCRD */
PNAME(mout_cmu_dcrd_bus_user_p) = { "oscclk", "dout_clkcmu_dcrd_bus" };

static const struct samsung_mux_clock dcrd_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_DCRD_BUS_USER, "mout_cmu_dcrd_bus_user", mout_cmu_dcrd_bus_user_p,
	    MUX_CLKCMU_DCRD_BUS_USER, 4, 1),
};

static const struct samsung_div_clock dcrd_div_clks[] __initconst = {
	DIV(CLK_DOUT_DCRD_BUSP, "dout_dcrd_busp", "mout_cmu_dcrd_bus_user",
	    DIV_CLK_DCRD_BUSP, 0, 3),
	DIV(CLK_DOUT_DCRD_BUSD_HALF, "dout_dcrd_busd_half", "mout_cmu_dcrd_bus_user",
	    DIV_CLK_DCRD_BUSD_HALF, 0, 3),
};

static const struct samsung_cmu_info dcrd_cmu_info __initconst = {
	.mux_clks		= dcrd_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(dcrd_mux_clks),
	.div_clks		= dcrd_div_clks,
	.nr_div_clks		= ARRAY_SIZE(dcrd_div_clks),
	.nr_clk_ids		= CLKS_NR_DCRD,
	.clk_regs		= dcrd_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(dcrd_clk_regs),
	.qch_regs		= dcrd_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(dcrd_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_dcrd_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &dcrd_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_dcrd, "samsung,exynos9810-cmu-dcrd",
	       exynos9810_cmu_dcrd_init);

/* ---- CMU_DPU ---------------------------------------------------------*/

/* Register Offset definitions for CMU_DPU (0x16000000) */
#define MUX_CLKCMU_DPU_BUS_USER			0x0100
#define DIV_CLK_DPU_BUSP			0x1800
#define CLK_BLK_DPU_UID_DPU_CMU_DPU_IPCLKPORT_PCLK	0x2000
#define GOUT_BLK_DPU_UID_AD_APB_DECON0_IPCLKPORT_PCLKS	0x200c
#define GOUT_BLK_DPU_UID_AD_APB_DECON1_IPCLKPORT_PCLKS	0x2014
#define GOUT_BLK_DPU_UID_AD_APB_DECON2_IPCLKPORT_PCLKS	0x201c
#define GOUT_BLK_DPU_UID_AD_APB_DPP_IPCLKPORT_PCLKS	0x2024
#define GOUT_BLK_DPU_UID_AD_APB_DPU_DMA_IPCLKPORT_PCLKS	0x202c
#define GOUT_BLK_DPU_UID_AD_APB_DPU_DMA_PGEN_IPCLKPORT_PCLKS	0x2034
#define GOUT_BLK_DPU_UID_AD_APB_DPU_WB_MUX_IPCLKPORT_PCLKS	0x203c
#define GOUT_BLK_DPU_UID_AD_APB_MIPI_DSIM0_IPCLKPORT_PCLKS	0x2040
#define GOUT_BLK_DPU_UID_AD_APB_MIPI_DSIM1_IPCLKPORT_PCLKS	0x2044
#define GOUT_BLK_DPU_UID_AD_APB_SYSMMU_DPUD0_IPCLKPORT_PCLKS	0x204c
#define GOUT_BLK_DPU_UID_AD_APB_SYSMMU_DPUD0_S_IPCLKPORT_PCLKS	0x2054
#define GOUT_BLK_DPU_UID_AD_APB_SYSMMU_DPUD1_IPCLKPORT_PCLKS	0x205c
#define GOUT_BLK_DPU_UID_AD_APB_SYSMMU_DPUD1_S_IPCLKPORT_PCLKS	0x2064
#define GOUT_BLK_DPU_UID_AD_APB_SYSMMU_DPUD2_IPCLKPORT_PCLKS	0x206c
#define GOUT_BLK_DPU_UID_AD_APB_SYSMMU_DPUD2_S_IPCLKPORT_PCLKS	0x2074
#define GOUT_BLK_DPU_UID_AXI2APB_DPUP0_IPCLKPORT_ACLK	0x2078
#define GOUT_BLK_DPU_UID_AXI2APB_DPUP1_IPCLKPORT_ACLK	0x207c
#define GOUT_BLK_DPU_UID_BTM_DPUD0_IPCLKPORT_I_PCLK	0x2084
#define GOUT_BLK_DPU_UID_BTM_DPUD1_IPCLKPORT_I_PCLK	0x208c
#define GOUT_BLK_DPU_UID_BTM_DPUD2_IPCLKPORT_I_PCLK	0x2094
#define GOUT_BLK_DPU_UID_LHM_AXI_P_DPU_IPCLKPORT_I_CLK	0x20a8
#define GOUT_BLK_DPU_UID_PPMU_DPUD0_IPCLKPORT_PCLK	0x20bc
#define GOUT_BLK_DPU_UID_PPMU_DPUD1_IPCLKPORT_PCLK	0x20c4
#define GOUT_BLK_DPU_UID_PPMU_DPUD2_IPCLKPORT_PCLK	0x20cc
#define GOUT_BLK_DPU_UID_RSTnSYNC_CLK_DPU_BUSP_IPCLKPORT_CLK	0x20d4
#define GOUT_BLK_DPU_UID_SYSREG_DPU_IPCLKPORT_PCLK	0x20e4
#define GOUT_BLK_DPU_UID_wrapper_for_s5i6211_hsi_dcphy_combo_top_IPCLKPORT_PCLK	0x20e8
#define GOUT_BLK_DPU_UID_XIU_P_DPU_IPCLKPORT_ACLK	0x20ec

static const unsigned long dpu_clk_regs[] __initconst = {
	MUX_CLKCMU_DPU_BUS_USER,
	DIV_CLK_DPU_BUSP,
	CLK_BLK_DPU_UID_DPU_CMU_DPU_IPCLKPORT_PCLK,
	GOUT_BLK_DPU_UID_AD_APB_DECON0_IPCLKPORT_PCLKS,
	GOUT_BLK_DPU_UID_AD_APB_DECON1_IPCLKPORT_PCLKS,
	GOUT_BLK_DPU_UID_AD_APB_DECON2_IPCLKPORT_PCLKS,
	GOUT_BLK_DPU_UID_AD_APB_DPP_IPCLKPORT_PCLKS,
	GOUT_BLK_DPU_UID_AD_APB_DPU_DMA_IPCLKPORT_PCLKS,
	GOUT_BLK_DPU_UID_AD_APB_DPU_DMA_PGEN_IPCLKPORT_PCLKS,
	GOUT_BLK_DPU_UID_AD_APB_DPU_WB_MUX_IPCLKPORT_PCLKS,
	GOUT_BLK_DPU_UID_AD_APB_MIPI_DSIM0_IPCLKPORT_PCLKS,
	GOUT_BLK_DPU_UID_AD_APB_MIPI_DSIM1_IPCLKPORT_PCLKS,
	GOUT_BLK_DPU_UID_AD_APB_SYSMMU_DPUD0_IPCLKPORT_PCLKS,
	GOUT_BLK_DPU_UID_AD_APB_SYSMMU_DPUD0_S_IPCLKPORT_PCLKS,
	GOUT_BLK_DPU_UID_AD_APB_SYSMMU_DPUD1_IPCLKPORT_PCLKS,
	GOUT_BLK_DPU_UID_AD_APB_SYSMMU_DPUD1_S_IPCLKPORT_PCLKS,
	GOUT_BLK_DPU_UID_AD_APB_SYSMMU_DPUD2_IPCLKPORT_PCLKS,
	GOUT_BLK_DPU_UID_AD_APB_SYSMMU_DPUD2_S_IPCLKPORT_PCLKS,
	GOUT_BLK_DPU_UID_AXI2APB_DPUP0_IPCLKPORT_ACLK,
	GOUT_BLK_DPU_UID_AXI2APB_DPUP1_IPCLKPORT_ACLK,
	GOUT_BLK_DPU_UID_BTM_DPUD0_IPCLKPORT_I_PCLK,
	GOUT_BLK_DPU_UID_BTM_DPUD1_IPCLKPORT_I_PCLK,
	GOUT_BLK_DPU_UID_BTM_DPUD2_IPCLKPORT_I_PCLK,
	GOUT_BLK_DPU_UID_LHM_AXI_P_DPU_IPCLKPORT_I_CLK,
	GOUT_BLK_DPU_UID_PPMU_DPUD0_IPCLKPORT_PCLK,
	GOUT_BLK_DPU_UID_PPMU_DPUD1_IPCLKPORT_PCLK,
	GOUT_BLK_DPU_UID_PPMU_DPUD2_IPCLKPORT_PCLK,
	GOUT_BLK_DPU_UID_RSTnSYNC_CLK_DPU_BUSP_IPCLKPORT_CLK,
	GOUT_BLK_DPU_UID_SYSREG_DPU_IPCLKPORT_PCLK,
	GOUT_BLK_DPU_UID_wrapper_for_s5i6211_hsi_dcphy_combo_top_IPCLKPORT_PCLK,
	GOUT_BLK_DPU_UID_XIU_P_DPU_IPCLKPORT_ACLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long dpu_qch_regs[] __initconst = {
	0x3010,	/* BTM_DPUD0_QCH */
	0x3014,	/* BTM_DPUD1_QCH */
	0x3018,	/* BTM_DPUD2_QCH */
	0x301c,	/* DPU_CMU_DPU_QCH */
	0x3020,	/* DPU_QCH_DPU */
	0x3024,	/* DPU_QCH_DPU_DMA */
	0x3028,	/* DPU_QCH_DPU_DPP */
	0x302c,	/* DPU_QCH_DPU_WB_MUX */
	0x3030,	/* LHM_AXI_P_DPU_QCH */
	0x3034,	/* LHS_AXI_D0_DPU_QCH */
	0x3038,	/* LHS_AXI_D1_DPU_QCH */
	0x303c,	/* LHS_AXI_D2_DPU_QCH */
	0x3040,	/* PPMU_DPUD0_QCH */
	0x3044,	/* PPMU_DPUD1_QCH */
	0x3048,	/* PPMU_DPUD2_QCH */
	0x304c,	/* SYSMMU_DPUD0_QCH */
	0x3050,	/* SYSMMU_DPUD1_QCH */
	0x3054,	/* SYSMMU_DPUD2_QCH */
	0x3058,	/* SYSREG_DPU_QCH */
};

/* List of parent clocks for Muxes in CMU_DPU */
PNAME(mout_cmu_dpu_bus_user_p) = { "oscclk", "mout_cmu_dpu_bus" };

static const struct samsung_mux_clock dpu_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_DPU_BUS_USER, "mout_cmu_dpu_bus_user", mout_cmu_dpu_bus_user_p,
	    MUX_CLKCMU_DPU_BUS_USER, 4, 1),
};

static const struct samsung_div_clock dpu_div_clks[] __initconst = {
	DIV(CLK_DOUT_DPU_BUSP, "dout_dpu_busp", "mout_cmu_dpu_bus_user",
	    DIV_CLK_DPU_BUSP, 0, 3),
};

static const struct samsung_gate_clock dpu_gate_clks[] __initconst = {
	GATE(CLK_GOUT_DPU_DPU_CMU_DPU_PCLK, "gout_dpu_dpu_cmu_dpu_pclk", "dout_dpu_busp",
	     CLK_BLK_DPU_UID_DPU_CMU_DPU_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DPU_BTM_DPUD0_I_PCLK, "gout_dpu_btm_dpud0_i_pclk", "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_BTM_DPUD0_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DPU_BTM_DPUD1_I_PCLK, "gout_dpu_btm_dpud1_i_pclk", "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_BTM_DPUD1_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DPU_SYSREG_DPU_PCLK, "gout_dpu_sysreg_dpu_pclk", "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_SYSREG_DPU_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DPU_AXI2APB_DPUP1_ACLK, "gout_dpu_axi2apb_dpup1_aclk", "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_AXI2APB_DPUP1_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_DPU_AXI2APB_DPUP0_ACLK, "gout_dpu_axi2apb_dpup0_aclk", "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_AXI2APB_DPUP0_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_DPU_LHM_AXI_P_DPU_I_CLK, "gout_dpu_lhm_axi_p_dpu_i_clk", "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_LHM_AXI_P_DPU_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DPU_XIU_P_DPU_ACLK, "gout_dpu_xiu_p_dpu_aclk", "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_XIU_P_DPU_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_DPU_AD_APB_DECON0_PCLKS, "gout_dpu_ad_apb_decon0_pclks", "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_AD_APB_DECON0_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_DPU_AD_APB_DECON1_PCLKS, "gout_dpu_ad_apb_decon1_pclks", "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_AD_APB_DECON1_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_DPU_AD_APB_MIPI_DSIM1_PCLKS, "gout_dpu_ad_apb_mipi_dsim1_pclks",
	     "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_AD_APB_MIPI_DSIM1_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_DPU_AD_APB_DPP_PCLKS, "gout_dpu_ad_apb_dpp_pclks", "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_AD_APB_DPP_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_DPU_BTM_DPUD2_I_PCLK, "gout_dpu_btm_dpud2_i_pclk", "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_BTM_DPUD2_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DPU_AD_APB_DPU_DMA_PCLKS, "gout_dpu_ad_apb_dpu_dma_pclks", "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_AD_APB_DPU_DMA_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_DPU_AD_APB_DPU_WB_MUX_PCLKS, "gout_dpu_ad_apb_dpu_wb_mux_pclks",
	     "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_AD_APB_DPU_WB_MUX_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_DPU_PPMU_DPUD0_PCLK, "gout_dpu_ppmu_dpud0_pclk", "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_PPMU_DPUD0_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DPU_PPMU_DPUD1_PCLK, "gout_dpu_ppmu_dpud1_pclk", "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_PPMU_DPUD1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DPU_PPMU_DPUD2_PCLK, "gout_dpu_ppmu_dpud2_pclk", "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_PPMU_DPUD2_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DPU_RSTnSYNC_CLK_DPU_BUSP_CLK, "gout_dpu_rstnsync_clk_dpu_busp_clk",
	     "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_RSTnSYNC_CLK_DPU_BUSP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DPU_AD_APB_MIPI_DSIM0_PCLKS, "gout_dpu_ad_apb_mipi_dsim0_pclks",
	     "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_AD_APB_MIPI_DSIM0_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_DPU_AD_APB_DECON2_PCLKS, "gout_dpu_ad_apb_decon2_pclks", "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_AD_APB_DECON2_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_DPU_AD_APB_SYSMMU_DPUD0_PCLKS, "gout_dpu_ad_apb_sysmmu_dpud0_pclks",
	     "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_AD_APB_SYSMMU_DPUD0_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_DPU_AD_APB_SYSMMU_DPUD0_S_PCLKS, "gout_dpu_ad_apb_sysmmu_dpud0_s_pclks",
	     "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_AD_APB_SYSMMU_DPUD0_S_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_DPU_AD_APB_SYSMMU_DPUD1_PCLKS, "gout_dpu_ad_apb_sysmmu_dpud1_pclks",
	     "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_AD_APB_SYSMMU_DPUD1_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_DPU_AD_APB_SYSMMU_DPUD1_S_PCLKS, "gout_dpu_ad_apb_sysmmu_dpud1_s_pclks",
	     "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_AD_APB_SYSMMU_DPUD1_S_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_DPU_AD_APB_SYSMMU_DPUD2_PCLKS, "gout_dpu_ad_apb_sysmmu_dpud2_pclks",
	     "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_AD_APB_SYSMMU_DPUD2_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_DPU_AD_APB_SYSMMU_DPUD2_S_PCLKS, "gout_dpu_ad_apb_sysmmu_dpud2_s_pclks",
	     "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_AD_APB_SYSMMU_DPUD2_S_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_DPU_wrapper_for_s5i6211_hsi_dcphy_combo_top_PCLK, "gout_dpu_wrapper_for_s5i6211_hsi_dcphy_combo_top_pclk",
	     "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_wrapper_for_s5i6211_hsi_dcphy_combo_top_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DPU_AD_APB_DPU_DMA_PGEN_PCLKS, "gout_dpu_ad_apb_dpu_dma_pgen_pclks",
	     "dout_dpu_busp",
	     GOUT_BLK_DPU_UID_AD_APB_DPU_DMA_PGEN_IPCLKPORT_PCLKS, 21, 0, 0),
};

static const struct samsung_cmu_info dpu_cmu_info __initconst = {
	.mux_clks		= dpu_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(dpu_mux_clks),
	.div_clks		= dpu_div_clks,
	.nr_div_clks		= ARRAY_SIZE(dpu_div_clks),
	.gate_clks		= dpu_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(dpu_gate_clks),
	.nr_clk_ids		= CLKS_NR_DPU,
	.clk_regs		= dpu_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(dpu_clk_regs),
	.qch_regs		= dpu_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(dpu_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_dpu_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &dpu_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_dpu, "samsung,exynos9810-cmu-dpu",
	       exynos9810_cmu_dpu_init);

/* ---- CMU_DSPM --------------------------------------------------------*/

/* Register Offset definitions for CMU_DSPM (0x16c00000) */
#define MUX_CLKCMU_DSPM_BUS_USER			0x0100
#define DIV_CLK_DSPM_BUSP			0x1800
#define CLKCMU_DSPS_BUS			0x2000
#define CLK_BLK_DSPM_UID_DSPM_CMU_DSPM_IPCLKPORT_PCLK	0x2004
#define GOUT_BLK_DSPM_UID_ADM_APB_DSPM_IPCLKPORT_PCLKM	0x2008
#define GOUT_BLK_DSPM_UID_AD_APB_DSPM0_IPCLKPORT_PCLKM	0x200c
#define GOUT_BLK_DSPM_UID_AD_APB_DSPM0_IPCLKPORT_PCLKS	0x2010
#define GOUT_BLK_DSPM_UID_AD_APB_DSPM1_IPCLKPORT_PCLKM	0x2014
#define GOUT_BLK_DSPM_UID_AD_APB_DSPM1_IPCLKPORT_PCLKS	0x2018
#define GOUT_BLK_DSPM_UID_AD_APB_DSPM3_IPCLKPORT_PCLKM	0x2024
#define GOUT_BLK_DSPM_UID_AD_APB_DSPM3_IPCLKPORT_PCLKS	0x2028
#define GOUT_BLK_DSPM_UID_AD_APB_DSPM4_IPCLKPORT_PCLKM	0x202c
#define GOUT_BLK_DSPM_UID_AD_APB_DSPM4_IPCLKPORT_PCLKS	0x2030
#define GOUT_BLK_DSPM_UID_AD_AXI_DSPM0_IPCLKPORT_ACLKM	0x203c
#define GOUT_BLK_DSPM_UID_AD_AXI_DSPM0_IPCLKPORT_ACLKS	0x2040
#define GOUT_BLK_DSPM_UID_AXI2APB_DSPM_IPCLKPORT_ACLK	0x2058
#define GOUT_BLK_DSPM_UID_BTM_DSPM0_IPCLKPORT_I_ACLK	0x2060
#define GOUT_BLK_DSPM_UID_BTM_DSPM0_IPCLKPORT_I_PCLK	0x2064
#define GOUT_BLK_DSPM_UID_BTM_DSPM1_IPCLKPORT_I_ACLK	0x2068
#define GOUT_BLK_DSPM_UID_BTM_DSPM1_IPCLKPORT_I_PCLK	0x206c
#define GOUT_BLK_DSPM_UID_LHM_AXI_D0_DSPSDSPM_IPCLKPORT_I_CLK	0x2084
#define GOUT_BLK_DSPM_UID_LHM_AXI_D1_DSPSDSPM_IPCLKPORT_I_CLK	0x2088
#define GOUT_BLK_DSPM_UID_LHM_AXI_P_DSPM_IPCLKPORT_I_CLK	0x208c
#define GOUT_BLK_DSPM_UID_LHM_AXI_P_IVADSPM_IPCLKPORT_I_CLK	0x2090
#define GOUT_BLK_DSPM_UID_LHS_ACEL_D0_DSPM_IPCLKPORT_I_CLK	0x2094
#define GOUT_BLK_DSPM_UID_LHS_ACEL_D1_DSPM_IPCLKPORT_I_CLK	0x2098
#define GOUT_BLK_DSPM_UID_LHS_ACEL_D2_DSPM_IPCLKPORT_I_CLK	0x209c
#define GOUT_BLK_DSPM_UID_LHS_AXI_P_DSPMDSPS_IPCLKPORT_I_CLK	0x20a0
#define GOUT_BLK_DSPM_UID_LHS_AXI_P_DSPMIVA_IPCLKPORT_I_CLK	0x20a4
#define GOUT_BLK_DSPM_UID_PGEN_lite_DSPM_IPCLKPORT_CLK	0x20a8
#define GOUT_BLK_DSPM_UID_PPMU_DSPM0_IPCLKPORT_ACLK	0x20ac
#define GOUT_BLK_DSPM_UID_PPMU_DSPM0_IPCLKPORT_PCLK	0x20b0
#define GOUT_BLK_DSPM_UID_PPMU_DSPM1_IPCLKPORT_ACLK	0x20b4
#define GOUT_BLK_DSPM_UID_PPMU_DSPM1_IPCLKPORT_PCLK	0x20b8
#define GOUT_BLK_DSPM_UID_RSTnSYNC_CLK_DSPM_BUSD_IPCLKPORT_CLK	0x20c4
#define GOUT_BLK_DSPM_UID_RSTnSYNC_CLK_DSPM_BUSP_IPCLKPORT_CLK	0x20c8
#define GOUT_BLK_DSPM_UID_SCORE_MASTER_IPCLKPORT_i_CLK	0x20cc
#define GOUT_BLK_DSPM_UID_SYSMMU_DSPM0_IPCLKPORT_CLK	0x20d0
#define GOUT_BLK_DSPM_UID_SYSMMU_DSPM1_IPCLKPORT_CLK	0x20d4
#define GOUT_BLK_DSPM_UID_SYSREG_DSPM_IPCLKPORT_PCLK	0x20dc
#define GOUT_BLK_DSPM_UID_WRAP2_CONV_DSPM_IPCLKPORT_I_CLK	0x20e0
#define GOUT_BLK_DSPM_UID_XIU_P_DSPM_IPCLKPORT_ACLK	0x20e4

static const unsigned long dspm_clk_regs[] __initconst = {
	MUX_CLKCMU_DSPM_BUS_USER,
	DIV_CLK_DSPM_BUSP,
	CLKCMU_DSPS_BUS,
	CLK_BLK_DSPM_UID_DSPM_CMU_DSPM_IPCLKPORT_PCLK,
	GOUT_BLK_DSPM_UID_ADM_APB_DSPM_IPCLKPORT_PCLKM,
	GOUT_BLK_DSPM_UID_AD_APB_DSPM0_IPCLKPORT_PCLKM,
	GOUT_BLK_DSPM_UID_AD_APB_DSPM0_IPCLKPORT_PCLKS,
	GOUT_BLK_DSPM_UID_AD_APB_DSPM1_IPCLKPORT_PCLKM,
	GOUT_BLK_DSPM_UID_AD_APB_DSPM1_IPCLKPORT_PCLKS,
	GOUT_BLK_DSPM_UID_AD_APB_DSPM3_IPCLKPORT_PCLKM,
	GOUT_BLK_DSPM_UID_AD_APB_DSPM3_IPCLKPORT_PCLKS,
	GOUT_BLK_DSPM_UID_AD_APB_DSPM4_IPCLKPORT_PCLKM,
	GOUT_BLK_DSPM_UID_AD_APB_DSPM4_IPCLKPORT_PCLKS,
	GOUT_BLK_DSPM_UID_AD_AXI_DSPM0_IPCLKPORT_ACLKM,
	GOUT_BLK_DSPM_UID_AD_AXI_DSPM0_IPCLKPORT_ACLKS,
	GOUT_BLK_DSPM_UID_AXI2APB_DSPM_IPCLKPORT_ACLK,
	GOUT_BLK_DSPM_UID_BTM_DSPM0_IPCLKPORT_I_ACLK,
	GOUT_BLK_DSPM_UID_BTM_DSPM0_IPCLKPORT_I_PCLK,
	GOUT_BLK_DSPM_UID_BTM_DSPM1_IPCLKPORT_I_ACLK,
	GOUT_BLK_DSPM_UID_BTM_DSPM1_IPCLKPORT_I_PCLK,
	GOUT_BLK_DSPM_UID_LHM_AXI_D0_DSPSDSPM_IPCLKPORT_I_CLK,
	GOUT_BLK_DSPM_UID_LHM_AXI_D1_DSPSDSPM_IPCLKPORT_I_CLK,
	GOUT_BLK_DSPM_UID_LHM_AXI_P_DSPM_IPCLKPORT_I_CLK,
	GOUT_BLK_DSPM_UID_LHM_AXI_P_IVADSPM_IPCLKPORT_I_CLK,
	GOUT_BLK_DSPM_UID_LHS_ACEL_D0_DSPM_IPCLKPORT_I_CLK,
	GOUT_BLK_DSPM_UID_LHS_ACEL_D1_DSPM_IPCLKPORT_I_CLK,
	GOUT_BLK_DSPM_UID_LHS_ACEL_D2_DSPM_IPCLKPORT_I_CLK,
	GOUT_BLK_DSPM_UID_LHS_AXI_P_DSPMDSPS_IPCLKPORT_I_CLK,
	GOUT_BLK_DSPM_UID_LHS_AXI_P_DSPMIVA_IPCLKPORT_I_CLK,
	GOUT_BLK_DSPM_UID_PGEN_lite_DSPM_IPCLKPORT_CLK,
	GOUT_BLK_DSPM_UID_PPMU_DSPM0_IPCLKPORT_ACLK,
	GOUT_BLK_DSPM_UID_PPMU_DSPM0_IPCLKPORT_PCLK,
	GOUT_BLK_DSPM_UID_PPMU_DSPM1_IPCLKPORT_ACLK,
	GOUT_BLK_DSPM_UID_PPMU_DSPM1_IPCLKPORT_PCLK,
	GOUT_BLK_DSPM_UID_RSTnSYNC_CLK_DSPM_BUSD_IPCLKPORT_CLK,
	GOUT_BLK_DSPM_UID_RSTnSYNC_CLK_DSPM_BUSP_IPCLKPORT_CLK,
	GOUT_BLK_DSPM_UID_SCORE_MASTER_IPCLKPORT_i_CLK,
	GOUT_BLK_DSPM_UID_SYSMMU_DSPM0_IPCLKPORT_CLK,
	GOUT_BLK_DSPM_UID_SYSMMU_DSPM1_IPCLKPORT_CLK,
	GOUT_BLK_DSPM_UID_SYSREG_DSPM_IPCLKPORT_PCLK,
	GOUT_BLK_DSPM_UID_WRAP2_CONV_DSPM_IPCLKPORT_I_CLK,
	GOUT_BLK_DSPM_UID_XIU_P_DSPM_IPCLKPORT_ACLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long dspm_qch_regs[] __initconst = {
	0x3000,	/* ADM_APB_DSPM_QCH */
	0x302c,	/* BTM_DSPM0_QCH */
	0x3030,	/* BTM_DSPM1_QCH */
	0x303c,	/* DSPM_CMU_DSPM_QCH */
	0x3040,	/* LHM_AXI_D0_DSPSDSPM_QCH */
	0x3044,	/* LHM_AXI_D1_DSPSDSPM_QCH */
	0x3048,	/* LHM_AXI_P_DSPM_QCH */
	0x304c,	/* LHM_AXI_P_IVADSPM_QCH */
	0x3050,	/* LHS_ACEL_D0_DSPM_QCH */
	0x3054,	/* LHS_ACEL_D1_DSPM_QCH */
	0x3058,	/* LHS_ACEL_D2_DSPM_QCH */
	0x305c,	/* LHS_AXI_P_DSPMDSPS_QCH */
	0x3060,	/* LHS_AXI_P_DSPMIVA_QCH */
	0x3064,	/* PGEN_LITE_DSPM_QCH */
	0x3068,	/* PPMU_DSPM0_QCH */
	0x306c,	/* PPMU_DSPM1_QCH */
	0x3074,	/* SCORE_MASTER_QCH */
	0x3078,	/* SYSMMU_DSPM0_QCH */
	0x307c,	/* SYSMMU_DSPM1_QCH */
	0x3084,	/* SYSREG_DSPM_QCH */
};

/* List of parent clocks for Muxes in CMU_DSPM */
PNAME(mout_cmu_dspm_bus_user_p) = { "oscclk", "dout_clkcmu_dspm_bus" };

static const struct samsung_mux_clock dspm_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_DSPM_BUS_USER, "mout_cmu_dspm_bus_user", mout_cmu_dspm_bus_user_p,
	    MUX_CLKCMU_DSPM_BUS_USER, 4, 1),
};

static const struct samsung_div_clock dspm_div_clks[] __initconst = {
	DIV(CLK_DOUT_DSPM_BUSP, "dout_dspm_busp", "mout_cmu_dspm_bus_user",
	    DIV_CLK_DSPM_BUSP, 0, 3),
};

static const struct samsung_gate_clock dspm_gate_clks[] __initconst = {
	GATE(CLK_GOUT_DSPM_DSPM_CMU_DSPM_PCLK, "gout_dspm_dspm_cmu_dspm_pclk", "dout_dspm_busp",
	     CLK_BLK_DSPM_UID_DSPM_CMU_DSPM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_SYSREG_DSPM_PCLK, "gout_dspm_sysreg_dspm_pclk", "dout_dspm_busp",
	     GOUT_BLK_DSPM_UID_SYSREG_DSPM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_AXI2APB_DSPM_ACLK, "gout_dspm_axi2apb_dspm_aclk", "dout_dspm_busp",
	     GOUT_BLK_DSPM_UID_AXI2APB_DSPM_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_PPMU_DSPM0_ACLK, "gout_dspm_ppmu_dspm0_aclk", "mout_cmu_dspm_bus_user",
	     GOUT_BLK_DSPM_UID_PPMU_DSPM0_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_PPMU_DSPM0_PCLK, "gout_dspm_ppmu_dspm0_pclk", "dout_dspm_busp",
	     GOUT_BLK_DSPM_UID_PPMU_DSPM0_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_SYSMMU_DSPM0_CLK, "gout_dspm_sysmmu_dspm0_clk", "mout_cmu_dspm_bus_user",
	     GOUT_BLK_DSPM_UID_SYSMMU_DSPM0_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_BTM_DSPM0_I_ACLK, "gout_dspm_btm_dspm0_i_aclk", "mout_cmu_dspm_bus_user",
	     GOUT_BLK_DSPM_UID_BTM_DSPM0_IPCLKPORT_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_LHM_AXI_P_DSPM_I_CLK, "gout_dspm_lhm_axi_p_dspm_i_clk", "dout_dspm_busp",
	     GOUT_BLK_DSPM_UID_LHM_AXI_P_DSPM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_BTM_DSPM0_I_PCLK, "gout_dspm_btm_dspm0_i_pclk", "dout_dspm_busp",
	     GOUT_BLK_DSPM_UID_BTM_DSPM0_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_LHS_ACEL_D0_DSPM_I_CLK, "gout_dspm_lhs_acel_d0_dspm_i_clk",
	     "mout_cmu_dspm_bus_user",
	     GOUT_BLK_DSPM_UID_LHS_ACEL_D0_DSPM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_RSTnSYNC_CLK_DSPM_BUSD_CLK, "gout_dspm_rstnsync_clk_dspm_busd_clk",
	     "mout_cmu_dspm_bus_user",
	     GOUT_BLK_DSPM_UID_RSTnSYNC_CLK_DSPM_BUSD_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_RSTnSYNC_CLK_DSPM_BUSP_CLK, "gout_dspm_rstnsync_clk_dspm_busp_clk",
	     "dout_dspm_busp",
	     GOUT_BLK_DSPM_UID_RSTnSYNC_CLK_DSPM_BUSP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_LHM_AXI_P_IVADSPM_I_CLK, "gout_dspm_lhm_axi_p_ivadspm_i_clk",
	     "mout_cmu_dspm_bus_user",
	     GOUT_BLK_DSPM_UID_LHM_AXI_P_IVADSPM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_LHS_AXI_P_DSPMIVA_I_CLK, "gout_dspm_lhs_axi_p_dspmiva_i_clk",
	     "mout_cmu_dspm_bus_user",
	     GOUT_BLK_DSPM_UID_LHS_AXI_P_DSPMIVA_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_WRAP2_CONV_DSPM_I_CLK, "gout_dspm_wrap2_conv_dspm_i_clk",
	     "mout_cmu_dspm_bus_user",
	     GOUT_BLK_DSPM_UID_WRAP2_CONV_DSPM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_AD_APB_DSPM0_PCLKM, "gout_dspm_ad_apb_dspm0_pclkm",
	     "mout_cmu_dspm_bus_user",
	     GOUT_BLK_DSPM_UID_AD_APB_DSPM0_IPCLKPORT_PCLKM, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_AD_APB_DSPM0_PCLKS, "gout_dspm_ad_apb_dspm0_pclks", "dout_dspm_busp",
	     GOUT_BLK_DSPM_UID_AD_APB_DSPM0_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_AD_APB_DSPM1_PCLKM, "gout_dspm_ad_apb_dspm1_pclkm",
	     "mout_cmu_dspm_bus_user",
	     GOUT_BLK_DSPM_UID_AD_APB_DSPM1_IPCLKPORT_PCLKM, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_AD_APB_DSPM1_PCLKS, "gout_dspm_ad_apb_dspm1_pclks", "dout_dspm_busp",
	     GOUT_BLK_DSPM_UID_AD_APB_DSPM1_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_AD_APB_DSPM3_PCLKS, "gout_dspm_ad_apb_dspm3_pclks", "dout_dspm_busp",
	     GOUT_BLK_DSPM_UID_AD_APB_DSPM3_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_AD_APB_DSPM3_PCLKM, "gout_dspm_ad_apb_dspm3_pclkm",
	     "mout_cmu_dspm_bus_user",
	     GOUT_BLK_DSPM_UID_AD_APB_DSPM3_IPCLKPORT_PCLKM, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_AD_AXI_DSPM0_ACLKS, "gout_dspm_ad_axi_dspm0_aclks", "dout_dspm_busp",
	     GOUT_BLK_DSPM_UID_AD_AXI_DSPM0_IPCLKPORT_ACLKS, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_AD_AXI_DSPM0_ACLKM, "gout_dspm_ad_axi_dspm0_aclkm",
	     "mout_cmu_dspm_bus_user",
	     GOUT_BLK_DSPM_UID_AD_AXI_DSPM0_IPCLKPORT_ACLKM, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_BTM_DSPM1_I_ACLK, "gout_dspm_btm_dspm1_i_aclk", "mout_cmu_dspm_bus_user",
	     GOUT_BLK_DSPM_UID_BTM_DSPM1_IPCLKPORT_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_BTM_DSPM1_I_PCLK, "gout_dspm_btm_dspm1_i_pclk", "dout_dspm_busp",
	     GOUT_BLK_DSPM_UID_BTM_DSPM1_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_LHS_ACEL_D1_DSPM_I_CLK, "gout_dspm_lhs_acel_d1_dspm_i_clk",
	     "mout_cmu_dspm_bus_user",
	     GOUT_BLK_DSPM_UID_LHS_ACEL_D1_DSPM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_LHS_AXI_P_DSPMDSPS_I_CLK, "gout_dspm_lhs_axi_p_dspmdsps_i_clk",
	     "dout_dspm_busp",
	     GOUT_BLK_DSPM_UID_LHS_AXI_P_DSPMDSPS_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_PPMU_DSPM1_ACLK, "gout_dspm_ppmu_dspm1_aclk", "mout_cmu_dspm_bus_user",
	     GOUT_BLK_DSPM_UID_PPMU_DSPM1_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_PPMU_DSPM1_PCLK, "gout_dspm_ppmu_dspm1_pclk", "dout_dspm_busp",
	     GOUT_BLK_DSPM_UID_PPMU_DSPM1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_SYSMMU_DSPM1_CLK, "gout_dspm_sysmmu_dspm1_clk", "mout_cmu_dspm_bus_user",
	     GOUT_BLK_DSPM_UID_SYSMMU_DSPM1_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_CLKCMU_DSPS_BUS, "gout_clkcmu_dsps_bus", "mout_cmu_dspm_bus_user",
	     CLKCMU_DSPS_BUS, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_ADM_APB_DSPM_PCLKM, "gout_dspm_adm_apb_dspm_pclkm",
	     "mout_cmu_dspm_bus_user",
	     GOUT_BLK_DSPM_UID_ADM_APB_DSPM_IPCLKPORT_PCLKM, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_LHM_AXI_D0_DSPSDSPM_I_CLK, "gout_dspm_lhm_axi_d0_dspsdspm_i_clk",
	     "mout_cmu_dspm_bus_user",
	     GOUT_BLK_DSPM_UID_LHM_AXI_D0_DSPSDSPM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_LHM_AXI_D1_DSPSDSPM_I_CLK, "gout_dspm_lhm_axi_d1_dspsdspm_i_clk",
	     "mout_cmu_dspm_bus_user",
	     GOUT_BLK_DSPM_UID_LHM_AXI_D1_DSPSDSPM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_XIU_P_DSPM_ACLK, "gout_dspm_xiu_p_dspm_aclk", "dout_dspm_busp",
	     GOUT_BLK_DSPM_UID_XIU_P_DSPM_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_PGEN_lite_DSPM_CLK, "gout_dspm_pgen_lite_dspm_clk", "dout_dspm_busp",
	     GOUT_BLK_DSPM_UID_PGEN_lite_DSPM_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_LHS_ACEL_D2_DSPM_I_CLK, "gout_dspm_lhs_acel_d2_dspm_i_clk",
	     "mout_cmu_dspm_bus_user",
	     GOUT_BLK_DSPM_UID_LHS_ACEL_D2_DSPM_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_AD_APB_DSPM4_PCLKM, "gout_dspm_ad_apb_dspm4_pclkm",
	     "mout_cmu_dspm_bus_user",
	     GOUT_BLK_DSPM_UID_AD_APB_DSPM4_IPCLKPORT_PCLKM, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_AD_APB_DSPM4_PCLKS, "gout_dspm_ad_apb_dspm4_pclks", "dout_dspm_busp",
	     GOUT_BLK_DSPM_UID_AD_APB_DSPM4_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_DSPM_SCORE_MASTER_i_CLK, "gout_dspm_score_master_i_clk",
	     "mout_cmu_dspm_bus_user",
	     GOUT_BLK_DSPM_UID_SCORE_MASTER_IPCLKPORT_i_CLK, 21, 0, 0),
};

static const struct samsung_cmu_info dspm_cmu_info __initconst = {
	.mux_clks		= dspm_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(dspm_mux_clks),
	.div_clks		= dspm_div_clks,
	.nr_div_clks		= ARRAY_SIZE(dspm_div_clks),
	.gate_clks		= dspm_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(dspm_gate_clks),
	.nr_clk_ids		= CLKS_NR_DSPM,
	.clk_regs		= dspm_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(dspm_clk_regs),
	.qch_regs		= dspm_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(dspm_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_dspm_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &dspm_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_dspm, "samsung,exynos9810-cmu-dspm",
	       exynos9810_cmu_dspm_init);

/* ---- CMU_DSPS --------------------------------------------------------*/

/* Register Offset definitions for CMU_DSPS (0x16f00000) */
#define MUX_CLKCMU_DSPS_AUD_USER			0x0100
#define MUX_CLKCMU_DSPS_BUS_USER			0x0120
#define MUX_CLK_DSPS_BUS			0x1000
#define DIV_CLK_DSPS_BUSP			0x1800
#define CLK_BLK_DSPS_UID_DSPS_CMU_DSPS_IPCLKPORT_PCLK	0x2000
#define GOUT_BLK_DSPS_UID_AXI2APB_DSPS_IPCLKPORT_ACLK	0x2004
#define GOUT_BLK_DSPS_UID_LHM_AXI_P_DSPMDSPS_IPCLKPORT_I_CLK	0x200c
#define GOUT_BLK_DSPS_UID_RSTnSYNC_CLK_DSPS_BUSP_IPCLKPORT_CLK	0x2024
#define GOUT_BLK_DSPS_UID_SYSREG_DSPS_IPCLKPORT_PCLK	0x202c

static const unsigned long dsps_clk_regs[] __initconst = {
	MUX_CLKCMU_DSPS_AUD_USER,
	MUX_CLKCMU_DSPS_BUS_USER,
	MUX_CLK_DSPS_BUS,
	DIV_CLK_DSPS_BUSP,
	CLK_BLK_DSPS_UID_DSPS_CMU_DSPS_IPCLKPORT_PCLK,
	GOUT_BLK_DSPS_UID_AXI2APB_DSPS_IPCLKPORT_ACLK,
	GOUT_BLK_DSPS_UID_LHM_AXI_P_DSPMDSPS_IPCLKPORT_I_CLK,
	GOUT_BLK_DSPS_UID_RSTnSYNC_CLK_DSPS_BUSP_IPCLKPORT_CLK,
	GOUT_BLK_DSPS_UID_SYSREG_DSPS_IPCLKPORT_PCLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long dsps_qch_regs[] __initconst = {
	0x3014,	/* DSPS_CMU_DSPS_QCH */
	0x3018,	/* LHM_AXI_D_IVADSPS_QCH */
	0x301c,	/* LHM_AXI_P_DSPMDSPS_QCH */
	0x3020,	/* LHS_AXI_D0_DSPSDSPM_QCH */
	0x3024,	/* LHS_AXI_D1_DSPSDSPM_QCH */
	0x3028,	/* LHS_AXI_D_DSPSIVA_QCH */
	0x3030,	/* SCORE_KNIGHT_QCH */
	0x3034,	/* SYSREG_DSPS_QCH */
};

/* List of parent clocks for Muxes in CMU_DSPS */
PNAME(mout_dsps_bus_p) = { "mout_cmu_dsps_bus_user", "mout_cmu_dsps_aud_user" };
PNAME(mout_cmu_dsps_bus_user_p) = { "oscclk", "gout_clkcmu_dsps_bus" };
PNAME(mout_cmu_dsps_aud_user_p) = { "oscclk", "dout_clkcmu_dsps_aud" };

static const struct samsung_mux_clock dsps_mux_clks[] __initconst = {
	MUX(CLK_MOUT_DSPS_BUS, "mout_dsps_bus", mout_dsps_bus_p,
	    MUX_CLK_DSPS_BUS, 0, 1),
	MUX(CLK_MOUT_CMU_DSPS_BUS_USER, "mout_cmu_dsps_bus_user", mout_cmu_dsps_bus_user_p,
	    MUX_CLKCMU_DSPS_BUS_USER, 4, 1),
	MUX(CLK_MOUT_CMU_DSPS_AUD_USER, "mout_cmu_dsps_aud_user", mout_cmu_dsps_aud_user_p,
	    MUX_CLKCMU_DSPS_AUD_USER, 4, 1),
};

static const struct samsung_div_clock dsps_div_clks[] __initconst = {
	DIV(CLK_DOUT_DSPS_BUSP, "dout_dsps_busp", "mout_dsps_bus",
	    DIV_CLK_DSPS_BUSP, 0, 3),
};

static const struct samsung_gate_clock dsps_gate_clks[] __initconst = {
	GATE(CLK_GOUT_DSPS_DSPS_CMU_DSPS_PCLK, "gout_dsps_dsps_cmu_dsps_pclk", "dout_dsps_busp",
	     CLK_BLK_DSPS_UID_DSPS_CMU_DSPS_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPS_AXI2APB_DSPS_ACLK, "gout_dsps_axi2apb_dsps_aclk", "dout_dsps_busp",
	     GOUT_BLK_DSPS_UID_AXI2APB_DSPS_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPS_LHM_AXI_P_DSPMDSPS_I_CLK, "gout_dsps_lhm_axi_p_dspmdsps_i_clk",
	     "dout_dsps_busp",
	     GOUT_BLK_DSPS_UID_LHM_AXI_P_DSPMDSPS_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPS_SYSREG_DSPS_PCLK, "gout_dsps_sysreg_dsps_pclk", "dout_dsps_busp",
	     GOUT_BLK_DSPS_UID_SYSREG_DSPS_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DSPS_RSTnSYNC_CLK_DSPS_BUSP_CLK, "gout_dsps_rstnsync_clk_dsps_busp_clk",
	     "dout_dsps_busp",
	     GOUT_BLK_DSPS_UID_RSTnSYNC_CLK_DSPS_BUSP_IPCLKPORT_CLK, 21, 0, 0),
};

static const struct samsung_cmu_info dsps_cmu_info __initconst = {
	.mux_clks		= dsps_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(dsps_mux_clks),
	.div_clks		= dsps_div_clks,
	.nr_div_clks		= ARRAY_SIZE(dsps_div_clks),
	.gate_clks		= dsps_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(dsps_gate_clks),
	.nr_clk_ids		= CLKS_NR_DSPS,
	.clk_regs		= dsps_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(dsps_clk_regs),
	.qch_regs		= dsps_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(dsps_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_dsps_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &dsps_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_dsps, "samsung,exynos9810-cmu-dsps",
	       exynos9810_cmu_dsps_init);

/* ---- CMU_FSYS0 -------------------------------------------------------*/

/* Register Offset definitions for CMU_FSYS0 (0x11000000) */
#define MUX_CLKCMU_FSYS0_BUS_USER			0x0100
#define MUX_CLKCMU_FSYS0_DPGTC_USER			0x0120
#define MUX_CLKCMU_FSYS0_UFS_EMBD_USER			0x0180
#define MUX_CLKCMU_FSYS0_USB30DRD_USER			0x01e0
#define MUX_CLKCMU_FSYS0_USBDP_DEBUG_USER			0x0240
#define CLK_BLK_FSYS0_UID_USB30DRD_IPCLKPORT_I_USBDPPHY_REF_SOC_PLL	0x2008
#define GOUT_BLK_FSYS0_UID_DP_LINK_IPCLKPORT_I_DP_GTC_CLK	0x2024
#define GOUT_BLK_FSYS0_UID_UFS_EMBD_IPCLKPORT_I_CLK_UNIPRO	0x205c
#define GOUT_BLK_FSYS0_UID_USB30DRD_IPCLKPORT_I_USB30DRD_ref_clk	0x206c

static const unsigned long fsys0_clk_regs[] __initconst = {
	MUX_CLKCMU_FSYS0_BUS_USER,
	MUX_CLKCMU_FSYS0_DPGTC_USER,
	MUX_CLKCMU_FSYS0_UFS_EMBD_USER,
	MUX_CLKCMU_FSYS0_USB30DRD_USER,
	MUX_CLKCMU_FSYS0_USBDP_DEBUG_USER,
	CLK_BLK_FSYS0_UID_USB30DRD_IPCLKPORT_I_USBDPPHY_REF_SOC_PLL,
	GOUT_BLK_FSYS0_UID_DP_LINK_IPCLKPORT_I_DP_GTC_CLK,
	GOUT_BLK_FSYS0_UID_UFS_EMBD_IPCLKPORT_I_CLK_UNIPRO,
	GOUT_BLK_FSYS0_UID_USB30DRD_IPCLKPORT_I_USB30DRD_ref_clk,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long fsys0_qch_regs[] __initconst = {
	0x3000,	/* USB30DRD_QCH_SOC_PLL */
	0x3010,	/* BTM_FSYS0_QCH */
	0x3014,	/* DP_LINK_QCH */
	0x3018,	/* DP_LINK_QCH_GTC */
	0x301c,	/* ETR_MIU_QCH_ACLK */
	0x3020,	/* ETR_MIU_QCH_PCLK */
	0x3024,	/* FSYS0_CMU_FSYS0_QCH */
	0x3028,	/* GPIO_FSYS0_QCH */
	0x302c,	/* LHM_AXI_G_ETR_QCH */
	0x3030,	/* LHM_AXI_P_FSYS0_QCH */
	0x3034,	/* LHS_ACEL_D_FSYS0_QCH */
	0x3038,	/* PGEN_LITE_FSYS0_QCH */
	0x303c,	/* PPMU_FSYS0_QCH */
	0x3040,	/* SYSREG_FSYS0_QCH */
	0x3044,	/* UFS_EMBD_QCH */
	0x3048,	/* UFS_EMBD_QCH_FMP */
	0x304c,	/* USB30DRD_QCH_USB30DRD_CTRL */
	0x3050,	/* USB30DRD_QCH_USB30DRD_LINK */
	0x3054,	/* USB30DRD_QCH_USBDPPHY */
	0x3058,	/* USB30DRD_QCH_USBPCS */
};

/* List of parent clocks for Muxes in CMU_FSYS0 */
PNAME(mout_cmu_fsys0_ufs_embd_user_p) = { "oscclk", "dout_clkcmu_fsys0_ufs_embd" };
PNAME(mout_cmu_fsys0_bus_user_p) = { "oscclk", "dout_clkcmu_fsys0_bus" };
PNAME(mout_cmu_fsys0_usb30drd_user_p) = { "oscclk", "dout_clkcmu_fsys0_usb30drd" };
PNAME(mout_cmu_fsys0_dpgtc_user_p) = { "oscclk", "dout_clkcmu_fsys0_dpgtc" };
PNAME(mout_cmu_fsys0_usbdp_debug_user_p) = { "oscclk", "UNRESOLVED_CLKCMU_FSYS0_USBDP_DEBUG" };

static const struct samsung_mux_clock fsys0_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_FSYS0_UFS_EMBD_USER, "mout_cmu_fsys0_ufs_embd_user",
	    mout_cmu_fsys0_ufs_embd_user_p,
	    MUX_CLKCMU_FSYS0_UFS_EMBD_USER, 4, 1),
	MUX(CLK_MOUT_CMU_FSYS0_BUS_USER, "mout_cmu_fsys0_bus_user", mout_cmu_fsys0_bus_user_p,
	    MUX_CLKCMU_FSYS0_BUS_USER, 4, 1),
	MUX(CLK_MOUT_CMU_FSYS0_USB30DRD_USER, "mout_cmu_fsys0_usb30drd_user",
	    mout_cmu_fsys0_usb30drd_user_p,
	    MUX_CLKCMU_FSYS0_USB30DRD_USER, 4, 1),
	MUX(CLK_MOUT_CMU_FSYS0_DPGTC_USER, "mout_cmu_fsys0_dpgtc_user", mout_cmu_fsys0_dpgtc_user_p,
	    MUX_CLKCMU_FSYS0_DPGTC_USER, 4, 1),
	MUX(CLK_MOUT_CMU_FSYS0_USBDP_DEBUG_USER, "mout_cmu_fsys0_usbdp_debug_user",
	    mout_cmu_fsys0_usbdp_debug_user_p,
	    MUX_CLKCMU_FSYS0_USBDP_DEBUG_USER, 4, 1),
};

static const struct samsung_gate_clock fsys0_gate_clks[] __initconst = {
	GATE(CLK_GOUT_FSYS0_UFS_EMBD_I_CLK_UNIPRO, "gout_fsys0_ufs_embd_i_clk_unipro",
	     "mout_cmu_fsys0_ufs_embd_user",
	     GOUT_BLK_FSYS0_UID_UFS_EMBD_IPCLKPORT_I_CLK_UNIPRO, 21, 0, 0),
	GATE(CLK_GOUT_FSYS0_USB30DRD_I_USB30DRD_ref_clk, "gout_fsys0_usb30drd_i_usb30drd_ref_clk",
	     "mout_cmu_fsys0_usb30drd_user",
	     GOUT_BLK_FSYS0_UID_USB30DRD_IPCLKPORT_I_USB30DRD_ref_clk, 21, 0, 0),
	GATE(CLK_GOUT_FSYS0_USB30DRD_I_USBDPPHY_REF_SOC_PLL, "gout_fsys0_usb30drd_i_usbdpphy_ref_soc_pll",
	     "mout_cmu_fsys0_usbdp_debug_user",
	     CLK_BLK_FSYS0_UID_USB30DRD_IPCLKPORT_I_USBDPPHY_REF_SOC_PLL, 21, 0, 0),
	GATE(CLK_GOUT_FSYS0_DP_LINK_I_DP_GTC_CLK, "gout_fsys0_dp_link_i_dp_gtc_clk",
	     "mout_cmu_fsys0_dpgtc_user",
	     GOUT_BLK_FSYS0_UID_DP_LINK_IPCLKPORT_I_DP_GTC_CLK, 21, 0, 0),
};

static const struct samsung_cmu_info fsys0_cmu_info __initconst = {
	.mux_clks		= fsys0_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(fsys0_mux_clks),
	.gate_clks		= fsys0_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(fsys0_gate_clks),
	.nr_clk_ids		= CLKS_NR_FSYS0,
	.clk_regs		= fsys0_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(fsys0_clk_regs),
	.qch_regs		= fsys0_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(fsys0_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_fsys0_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &fsys0_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_fsys0, "samsung,exynos9810-cmu-fsys0",
	       exynos9810_cmu_fsys0_init);

/* ---- CMU_FSYS1 -------------------------------------------------------*/

/* Register Offset definitions for CMU_FSYS1 (0x11400000) */
#define MUX_CLKCMU_FSYS1_BUS_USER			0x0100
#define MUX_CLKCMU_FSYS1_MMC_CARD_USER			0x0120
#define MUX_CLKCMU_FSYS1_PCIE_USER			0x0180
#define MUX_CLKCMU_FSYS1_UFS_CARD_USER			0x01e0
#define CLK_BLK_FSYS1_UID_PCIE_GEN2_IPCLKPORT_phy_refclk_in	0x2000
#define GOUT_BLK_FSYS1_UID_ADM_AHB_SSS_IPCLKPORT_HCLKM	0x2004
#define GOUT_BLK_FSYS1_UID_AHBBR_FSYS1_IPCLKPORT_HCLK	0x2008
#define GOUT_BLK_FSYS1_UID_AXI2AHB_FSYS1_IPCLKPORT_aclk	0x200c
#define GOUT_BLK_FSYS1_UID_AXI2APB_FSYS1P0_IPCLKPORT_ACLK	0x2010
#define GOUT_BLK_FSYS1_UID_AXI2APB_FSYS1P1_IPCLKPORT_ACLK	0x2014
#define GOUT_BLK_FSYS1_UID_BTM_FSYS1_IPCLKPORT_I_ACLK	0x2018
#define GOUT_BLK_FSYS1_UID_BTM_FSYS1_IPCLKPORT_I_PCLK	0x201c
#define GOUT_BLK_FSYS1_UID_FSYS1_CMU_FSYS1_IPCLKPORT_PCLK	0x2020
#define GOUT_BLK_FSYS1_UID_GPIO_FSYS1_IPCLKPORT_PCLK	0x2024
#define GOUT_BLK_FSYS1_UID_LHM_AXI_P_FSYS1_IPCLKPORT_I_CLK	0x2028
#define GOUT_BLK_FSYS1_UID_LHS_ACEL_D_FSYS1_IPCLKPORT_I_CLK	0x202c
#define GOUT_BLK_FSYS1_UID_MMC_CARD_IPCLKPORT_I_ACLK	0x2030
#define GOUT_BLK_FSYS1_UID_MMC_CARD_IPCLKPORT_SDCLKIN	0x2034
#define GOUT_BLK_FSYS1_UID_PCIE_GEN2_IPCLKPORT_dbi_aclk	0x2038
#define GOUT_BLK_FSYS1_UID_PCIE_GEN2_IPCLKPORT_ieee1500_wrapper_for_pcieg2_phy_x1_inst_0_i_scl_apb_pclk	0x203c
#define GOUT_BLK_FSYS1_UID_PCIE_GEN2_IPCLKPORT_mstr_aclk	0x2040
#define GOUT_BLK_FSYS1_UID_PCIE_GEN2_IPCLKPORT_pcie_sub_ctrl_inst_0_i_driver_apb_clk	0x2044
#define GOUT_BLK_FSYS1_UID_PCIE_GEN2_IPCLKPORT_pipe2_digital_x1_wrap_inst_0_i_apb_pclk_scl	0x2048
#define GOUT_BLK_FSYS1_UID_PCIE_GEN2_IPCLKPORT_slv_aclk	0x204c
#define GOUT_BLK_FSYS1_UID_PCIE_GEN3_IPCLKPORT_dbi_aclk	0x2050
#define GOUT_BLK_FSYS1_UID_PCIE_GEN3_IPCLKPORT_ieee1500_wrapper_for_qchannel_wrapper_for_pcieg3_phy_x1_top_inst_0_i_apb_pclk	0x2054
#define GOUT_BLK_FSYS1_UID_PCIE_GEN3_IPCLKPORT_LT_PCIE3_pcie_sub_ctrl_inst_0_i_driver_apb_clk	0x2058
#define GOUT_BLK_FSYS1_UID_PCIE_GEN3_IPCLKPORT_mstr_aclk	0x205c
#define GOUT_BLK_FSYS1_UID_PCIE_GEN3_IPCLKPORT_phy_refclk_in	0x2060
#define GOUT_BLK_FSYS1_UID_PCIE_GEN3_IPCLKPORT_pipe42_pcie_pcs_x1_wrap_inst_0_i_apb_pclk	0x2064
#define GOUT_BLK_FSYS1_UID_PCIE_GEN3_IPCLKPORT_slv_aclk	0x2068
#define GOUT_BLK_FSYS1_UID_PCIE_IA_GEN2_IPCLKPORT_i_CLK	0x206c
#define GOUT_BLK_FSYS1_UID_PCIE_IA_GEN3_IPCLKPORT_i_CLK	0x2070
#define GOUT_BLK_FSYS1_UID_PGEN_LITE_FSYS1_IPCLKPORT_CLK	0x2074
#define GOUT_BLK_FSYS1_UID_PPMU_FSYS1_IPCLKPORT_ACLK	0x2078
#define GOUT_BLK_FSYS1_UID_PPMU_FSYS1_IPCLKPORT_PCLK	0x207c
#define GOUT_BLK_FSYS1_UID_RSTnSYNC_CLK_FSYS1_BUS_IPCLKPORT_CLK	0x2080
#define GOUT_BLK_FSYS1_UID_RTIC_IPCLKPORT_i_ACLK	0x2088
#define GOUT_BLK_FSYS1_UID_RTIC_IPCLKPORT_i_PCLK	0x208c
#define GOUT_BLK_FSYS1_UID_SSS_IPCLKPORT_i_ACLK			0x2090
#define GOUT_BLK_FSYS1_UID_SSS_IPCLKPORT_i_PCLK			0x2094
#define GOUT_BLK_FSYS1_UID_SYSMMU_FSYS1_IPCLKPORT_CLK	0x2098
#define GOUT_BLK_FSYS1_UID_SYSREG_FSYS1_IPCLKPORT_PCLK	0x209c
#define GOUT_BLK_FSYS1_UID_UFS_CARD_IPCLKPORT_I_ACLK	0x20a0
#define GOUT_BLK_FSYS1_UID_UFS_CARD_IPCLKPORT_I_CLK_UNIPRO	0x20a4
#define GOUT_BLK_FSYS1_UID_UFS_CARD_IPCLKPORT_I_FMP_CLK	0x20a8
#define GOUT_BLK_FSYS1_UID_XIU_D_FSYS1_IPCLKPORT_ACLK	0x20ac
#define GOUT_BLK_FSYS1_UID_XIU_PCIE_GEN2_DBI_IPCLKPORT_ACLK	0x20b0
#define GOUT_BLK_FSYS1_UID_XIU_PCIE_GEN2_SLV_IPCLKPORT_ACLK	0x20b4
#define GOUT_BLK_FSYS1_UID_XIU_PCIE_GEN3_DBI_IPCLKPORT_ACLK	0x20b8
#define GOUT_BLK_FSYS1_UID_XIU_PCIE_GEN3_SLV_IPCLKPORT_ACLK	0x20bc
#define GOUT_BLK_FSYS1_UID_XIU_P_FSYS1_IPCLKPORT_ACLK	0x20c0

static const unsigned long fsys1_clk_regs[] __initconst = {
	MUX_CLKCMU_FSYS1_BUS_USER,
	MUX_CLKCMU_FSYS1_MMC_CARD_USER,
	MUX_CLKCMU_FSYS1_PCIE_USER,
	MUX_CLKCMU_FSYS1_UFS_CARD_USER,
	CLK_BLK_FSYS1_UID_PCIE_GEN2_IPCLKPORT_phy_refclk_in,
	GOUT_BLK_FSYS1_UID_ADM_AHB_SSS_IPCLKPORT_HCLKM,
	GOUT_BLK_FSYS1_UID_AHBBR_FSYS1_IPCLKPORT_HCLK,
	GOUT_BLK_FSYS1_UID_AXI2AHB_FSYS1_IPCLKPORT_aclk,
	GOUT_BLK_FSYS1_UID_AXI2APB_FSYS1P0_IPCLKPORT_ACLK,
	GOUT_BLK_FSYS1_UID_AXI2APB_FSYS1P1_IPCLKPORT_ACLK,
	GOUT_BLK_FSYS1_UID_BTM_FSYS1_IPCLKPORT_I_ACLK,
	GOUT_BLK_FSYS1_UID_BTM_FSYS1_IPCLKPORT_I_PCLK,
	GOUT_BLK_FSYS1_UID_FSYS1_CMU_FSYS1_IPCLKPORT_PCLK,
	GOUT_BLK_FSYS1_UID_GPIO_FSYS1_IPCLKPORT_PCLK,
	GOUT_BLK_FSYS1_UID_LHM_AXI_P_FSYS1_IPCLKPORT_I_CLK,
	GOUT_BLK_FSYS1_UID_LHS_ACEL_D_FSYS1_IPCLKPORT_I_CLK,
	GOUT_BLK_FSYS1_UID_MMC_CARD_IPCLKPORT_I_ACLK,
	GOUT_BLK_FSYS1_UID_MMC_CARD_IPCLKPORT_SDCLKIN,
	GOUT_BLK_FSYS1_UID_PCIE_GEN2_IPCLKPORT_dbi_aclk,
	GOUT_BLK_FSYS1_UID_PCIE_GEN2_IPCLKPORT_ieee1500_wrapper_for_pcieg2_phy_x1_inst_0_i_scl_apb_pclk,
	GOUT_BLK_FSYS1_UID_PCIE_GEN2_IPCLKPORT_mstr_aclk,
	GOUT_BLK_FSYS1_UID_PCIE_GEN2_IPCLKPORT_pcie_sub_ctrl_inst_0_i_driver_apb_clk,
	GOUT_BLK_FSYS1_UID_PCIE_GEN2_IPCLKPORT_pipe2_digital_x1_wrap_inst_0_i_apb_pclk_scl,
	GOUT_BLK_FSYS1_UID_PCIE_GEN2_IPCLKPORT_slv_aclk,
	GOUT_BLK_FSYS1_UID_PCIE_GEN3_IPCLKPORT_dbi_aclk,
	GOUT_BLK_FSYS1_UID_PCIE_GEN3_IPCLKPORT_ieee1500_wrapper_for_qchannel_wrapper_for_pcieg3_phy_x1_top_inst_0_i_apb_pclk,
	GOUT_BLK_FSYS1_UID_PCIE_GEN3_IPCLKPORT_LT_PCIE3_pcie_sub_ctrl_inst_0_i_driver_apb_clk,
	GOUT_BLK_FSYS1_UID_PCIE_GEN3_IPCLKPORT_mstr_aclk,
	GOUT_BLK_FSYS1_UID_PCIE_GEN3_IPCLKPORT_phy_refclk_in,
	GOUT_BLK_FSYS1_UID_PCIE_GEN3_IPCLKPORT_pipe42_pcie_pcs_x1_wrap_inst_0_i_apb_pclk,
	GOUT_BLK_FSYS1_UID_PCIE_GEN3_IPCLKPORT_slv_aclk,
	GOUT_BLK_FSYS1_UID_PCIE_IA_GEN2_IPCLKPORT_i_CLK,
	GOUT_BLK_FSYS1_UID_PCIE_IA_GEN3_IPCLKPORT_i_CLK,
	GOUT_BLK_FSYS1_UID_PGEN_LITE_FSYS1_IPCLKPORT_CLK,
	GOUT_BLK_FSYS1_UID_PPMU_FSYS1_IPCLKPORT_ACLK,
	GOUT_BLK_FSYS1_UID_PPMU_FSYS1_IPCLKPORT_PCLK,
	GOUT_BLK_FSYS1_UID_RSTnSYNC_CLK_FSYS1_BUS_IPCLKPORT_CLK,
	GOUT_BLK_FSYS1_UID_RTIC_IPCLKPORT_i_ACLK,
	GOUT_BLK_FSYS1_UID_RTIC_IPCLKPORT_i_PCLK,
	GOUT_BLK_FSYS1_UID_SSS_IPCLKPORT_i_ACLK,
	GOUT_BLK_FSYS1_UID_SSS_IPCLKPORT_i_PCLK,
	GOUT_BLK_FSYS1_UID_SYSMMU_FSYS1_IPCLKPORT_CLK,
	GOUT_BLK_FSYS1_UID_SYSREG_FSYS1_IPCLKPORT_PCLK,
	GOUT_BLK_FSYS1_UID_UFS_CARD_IPCLKPORT_I_ACLK,
	GOUT_BLK_FSYS1_UID_UFS_CARD_IPCLKPORT_I_CLK_UNIPRO,
	GOUT_BLK_FSYS1_UID_UFS_CARD_IPCLKPORT_I_FMP_CLK,
	GOUT_BLK_FSYS1_UID_XIU_D_FSYS1_IPCLKPORT_ACLK,
	GOUT_BLK_FSYS1_UID_XIU_PCIE_GEN2_DBI_IPCLKPORT_ACLK,
	GOUT_BLK_FSYS1_UID_XIU_PCIE_GEN2_SLV_IPCLKPORT_ACLK,
	GOUT_BLK_FSYS1_UID_XIU_PCIE_GEN3_DBI_IPCLKPORT_ACLK,
	GOUT_BLK_FSYS1_UID_XIU_PCIE_GEN3_SLV_IPCLKPORT_ACLK,
	GOUT_BLK_FSYS1_UID_XIU_P_FSYS1_IPCLKPORT_ACLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long fsys1_qch_regs[] __initconst = {
	0x3000,	/* PCIE_GEN2_QCH_SOCPLL */
	0x3004,	/* PCIE_GEN3_QCH_SOCPLL */
	0x3010,	/* ADM_AHB_SSS_QCH */
	0x3014,	/* BTM_FSYS1_QCH */
	0x3018,	/* FSYS1_CMU_FSYS1_QCH */
	0x301c,	/* GPIO_FSYS1_QCH */
	0x3020,	/* LHM_AXI_P_FSYS1_QCH */
	0x3024,	/* LHS_ACEL_D_FSYS1_QCH */
	0x3028,	/* MMC_CARD_QCH */
	0x302c,	/* PCIE_GEN2_QCH_APB */
	0x3030,	/* PCIE_GEN2_QCH_DBI */
	0x3034,	/* PCIE_GEN2_QCH_MSTR */
	0x3038,	/* PCIE_GEN2_QCH_PCS */
	0x303c,	/* PCIE_GEN2_QCH_PHY */
	0x3040,	/* PCIE_GEN3_QCH_APB */
	0x3044,	/* PCIE_GEN3_QCH_DBI */
	0x3048,	/* PCIE_GEN3_QCH_MSTR */
	0x304c,	/* PCIE_GEN3_QCH_PCS */
	0x3050,	/* PCIE_GEN3_QCH_PHY */
	0x3054,	/* PCIE_IA_GEN2_QCH */
	0x3058,	/* PCIE_IA_GEN3_QCH */
	0x305c,	/* PGEN_LITE_FSYS1_QCH */
	0x3060,	/* PPMU_FSYS1_QCH */
	0x3064,	/* RTIC_QCH */
	0x3068,	/* SSS_QCH */
	0x306c,	/* SYSMMU_FSYS1_QCH */
	0x3070,	/* SYSREG_FSYS1_QCH */
	0x3074,	/* UFS_CARD_QCH */
	0x3078,	/* UFS_CARD_QCH_FMP */
};

/* List of parent clocks for Muxes in CMU_FSYS1 */
PNAME(mout_cmu_fsys1_bus_user_p) = { "oscclk", "dout_clkcmu_fsys1_bus" };
PNAME(mout_cmu_fsys1_mmc_card_user_p) = { "oscclk", "dout_clkcmu_fsys1_mmc_card" };
PNAME(mout_cmu_fsys1_pcie_user_p) = { "oscclk", "UNRESOLVED_CLKCMU_FSYS1_PCIE" };
PNAME(mout_cmu_fsys1_ufs_card_user_p) = { "oscclk", "dout_clkcmu_fsys1_ufs_card" };

static const struct samsung_mux_clock fsys1_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_FSYS1_BUS_USER, "mout_cmu_fsys1_bus_user", mout_cmu_fsys1_bus_user_p,
	    MUX_CLKCMU_FSYS1_BUS_USER, 4, 1),
	MUX(CLK_MOUT_CMU_FSYS1_MMC_CARD_USER, "mout_cmu_fsys1_mmc_card_user",
	    mout_cmu_fsys1_mmc_card_user_p,
	    MUX_CLKCMU_FSYS1_MMC_CARD_USER, 4, 1),
	MUX(CLK_MOUT_CMU_FSYS1_PCIE_USER, "mout_cmu_fsys1_pcie_user", mout_cmu_fsys1_pcie_user_p,
	    MUX_CLKCMU_FSYS1_PCIE_USER, 4, 1),
	MUX(CLK_MOUT_CMU_FSYS1_UFS_CARD_USER, "mout_cmu_fsys1_ufs_card_user",
	    mout_cmu_fsys1_ufs_card_user_p,
	    MUX_CLKCMU_FSYS1_UFS_CARD_USER, 4, 1),
};

static const struct samsung_gate_clock fsys1_gate_clks[] __initconst = {
	GATE(CLK_GOUT_FSYS1_FSYS1_CMU_FSYS1_PCLK, "gout_fsys1_fsys1_cmu_fsys1_pclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_FSYS1_CMU_FSYS1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_MMC_CARD_SDCLKIN, "gout_fsys1_mmc_card_sdclkin",
	     "mout_cmu_fsys1_mmc_card_user",
	     GOUT_BLK_FSYS1_UID_MMC_CARD_IPCLKPORT_SDCLKIN, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_PCIE_GEN2_ieee1500_wrapper_for_pcieg2_phy_x1_inst_0_i_scl_apb_pclk, "gout_fsys1_pcie_gen2_ieee1500_wrapper_for_pcieg2_phy_x1_inst_0_i_scl_apb_pclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_PCIE_GEN2_IPCLKPORT_ieee1500_wrapper_for_pcieg2_phy_x1_inst_0_i_scl_apb_pclk, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_SSS_i_PCLK, "gout_fsys1_sss_i_pclk", "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_SSS_IPCLKPORT_i_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_RTIC_i_PCLK, "gout_fsys1_rtic_i_pclk", "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_RTIC_IPCLKPORT_i_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_SYSREG_FSYS1_PCLK, "gout_fsys1_sysreg_fsys1_pclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_SYSREG_FSYS1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_GPIO_FSYS1_PCLK, "gout_fsys1_gpio_fsys1_pclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_GPIO_FSYS1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_LHS_ACEL_D_FSYS1_I_CLK, "gout_fsys1_lhs_acel_d_fsys1_i_clk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_LHS_ACEL_D_FSYS1_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_LHM_AXI_P_FSYS1_I_CLK, "gout_fsys1_lhm_axi_p_fsys1_i_clk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_LHM_AXI_P_FSYS1_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_XIU_D_FSYS1_ACLK, "gout_fsys1_xiu_d_fsys1_aclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_XIU_D_FSYS1_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_XIU_P_FSYS1_ACLK, "gout_fsys1_xiu_p_fsys1_aclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_XIU_P_FSYS1_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_PPMU_FSYS1_ACLK, "gout_fsys1_ppmu_fsys1_aclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_PPMU_FSYS1_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_PPMU_FSYS1_PCLK, "gout_fsys1_ppmu_fsys1_pclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_PPMU_FSYS1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_AXI2AHB_FSYS1_aclk, "gout_fsys1_axi2ahb_fsys1_aclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_AXI2AHB_FSYS1_IPCLKPORT_aclk, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_AXI2APB_FSYS1P0_ACLK, "gout_fsys1_axi2apb_fsys1p0_aclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_AXI2APB_FSYS1P0_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_AHBBR_FSYS1_HCLK, "gout_fsys1_ahbbr_fsys1_hclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_AHBBR_FSYS1_IPCLKPORT_HCLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_AXI2APB_FSYS1P1_ACLK, "gout_fsys1_axi2apb_fsys1p1_aclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_AXI2APB_FSYS1P1_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_BTM_FSYS1_I_ACLK, "gout_fsys1_btm_fsys1_i_aclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_BTM_FSYS1_IPCLKPORT_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_BTM_FSYS1_I_PCLK, "gout_fsys1_btm_fsys1_i_pclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_BTM_FSYS1_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_PCIE_GEN2_phy_refclk_in, "gout_fsys1_pcie_gen2_phy_refclk_in",
	     "mout_cmu_fsys1_pcie_user",
	     CLK_BLK_FSYS1_UID_PCIE_GEN2_IPCLKPORT_phy_refclk_in, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_PCIE_GEN2_slv_aclk, "gout_fsys1_pcie_gen2_slv_aclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_PCIE_GEN2_IPCLKPORT_slv_aclk, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_PCIE_GEN2_dbi_aclk, "gout_fsys1_pcie_gen2_dbi_aclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_PCIE_GEN2_IPCLKPORT_dbi_aclk, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_PCIE_GEN2_pcie_sub_ctrl_inst_0_i_driver_apb_clk, "gout_fsys1_pcie_gen2_pcie_sub_ctrl_inst_0_i_driver_apb_clk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_PCIE_GEN2_IPCLKPORT_pcie_sub_ctrl_inst_0_i_driver_apb_clk, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_PCIE_GEN2_pipe2_digital_x1_wrap_inst_0_i_apb_pclk_scl, "gout_fsys1_pcie_gen2_pipe2_digital_x1_wrap_inst_0_i_apb_pclk_scl",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_PCIE_GEN2_IPCLKPORT_pipe2_digital_x1_wrap_inst_0_i_apb_pclk_scl, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_RSTnSYNC_CLK_FSYS1_BUS_CLK, "gout_fsys1_rstnsync_clk_fsys1_bus_clk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_RSTnSYNC_CLK_FSYS1_BUS_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_UFS_CARD_I_CLK_UNIPRO, "gout_fsys1_ufs_card_i_clk_unipro",
	     "mout_cmu_fsys1_ufs_card_user",
	     GOUT_BLK_FSYS1_UID_UFS_CARD_IPCLKPORT_I_CLK_UNIPRO, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_ADM_AHB_SSS_HCLKM, "gout_fsys1_adm_ahb_sss_hclkm",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_ADM_AHB_SSS_IPCLKPORT_HCLKM, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_SYSMMU_FSYS1_CLK, "gout_fsys1_sysmmu_fsys1_clk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_SYSMMU_FSYS1_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_PGEN_LITE_FSYS1_CLK, "gout_fsys1_pgen_lite_fsys1_clk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_PGEN_LITE_FSYS1_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_PCIE_GEN3_dbi_aclk, "gout_fsys1_pcie_gen3_dbi_aclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_PCIE_GEN3_IPCLKPORT_dbi_aclk, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_PCIE_GEN3_ieee1500_wrapper_for_qchannel_wrapper_for_pcieg3_phy_x1_top_inst_0_i_apb_pclk, "gout_fsys1_pcie_gen3_ieee1500_wrapper_for_qchannel_wrapper_for_pcieg3_phy_x1_top_inst_0_i_apb_pclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_PCIE_GEN3_IPCLKPORT_ieee1500_wrapper_for_qchannel_wrapper_for_pcieg3_phy_x1_top_inst_0_i_apb_pclk, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_PCIE_GEN3_LT_PCIE3_pcie_sub_ctrl_inst_0_i_driver_apb_clk, "gout_fsys1_pcie_gen3_lt_pcie3_pcie_sub_ctrl_inst_0_i_driver_apb_clk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_PCIE_GEN3_IPCLKPORT_LT_PCIE3_pcie_sub_ctrl_inst_0_i_driver_apb_clk, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_PCIE_GEN3_phy_refclk_in, "gout_fsys1_pcie_gen3_phy_refclk_in",
	     "mout_cmu_fsys1_pcie_user",
	     GOUT_BLK_FSYS1_UID_PCIE_GEN3_IPCLKPORT_phy_refclk_in, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_PCIE_GEN3_pipe42_pcie_pcs_x1_wrap_inst_0_i_apb_pclk, "gout_fsys1_pcie_gen3_pipe42_pcie_pcs_x1_wrap_inst_0_i_apb_pclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_PCIE_GEN3_IPCLKPORT_pipe42_pcie_pcs_x1_wrap_inst_0_i_apb_pclk, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_PCIE_GEN3_slv_aclk, "gout_fsys1_pcie_gen3_slv_aclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_PCIE_GEN3_IPCLKPORT_slv_aclk, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_XIU_PCIE_GEN2_DBI_ACLK, "gout_fsys1_xiu_pcie_gen2_dbi_aclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_XIU_PCIE_GEN2_DBI_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_XIU_PCIE_GEN3_SLV_ACLK, "gout_fsys1_xiu_pcie_gen3_slv_aclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_XIU_PCIE_GEN3_SLV_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_XIU_PCIE_GEN2_SLV_ACLK, "gout_fsys1_xiu_pcie_gen2_slv_aclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_XIU_PCIE_GEN2_SLV_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_XIU_PCIE_GEN3_DBI_ACLK, "gout_fsys1_xiu_pcie_gen3_dbi_aclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_XIU_PCIE_GEN3_DBI_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_PCIE_GEN2_mstr_aclk, "gout_fsys1_pcie_gen2_mstr_aclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_PCIE_GEN2_IPCLKPORT_mstr_aclk, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_MMC_CARD_I_ACLK, "gout_fsys1_mmc_card_i_aclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_MMC_CARD_IPCLKPORT_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_PCIE_GEN3_mstr_aclk, "gout_fsys1_pcie_gen3_mstr_aclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_PCIE_GEN3_IPCLKPORT_mstr_aclk, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_RTIC_i_ACLK, "gout_fsys1_rtic_i_aclk", "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_RTIC_IPCLKPORT_i_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_SSS_i_ACLK, "gout_fsys1_sss_i_aclk", "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_SSS_IPCLKPORT_i_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_UFS_CARD_I_ACLK, "gout_fsys1_ufs_card_i_aclk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_UFS_CARD_IPCLKPORT_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_UFS_CARD_I_FMP_CLK, "gout_fsys1_ufs_card_i_fmp_clk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_UFS_CARD_IPCLKPORT_I_FMP_CLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_PCIE_IA_GEN2_i_CLK, "gout_fsys1_pcie_ia_gen2_i_clk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_PCIE_IA_GEN2_IPCLKPORT_i_CLK, 21, 0, 0),
	GATE(CLK_GOUT_FSYS1_PCIE_IA_GEN3_i_CLK, "gout_fsys1_pcie_ia_gen3_i_clk",
	     "mout_cmu_fsys1_bus_user",
	     GOUT_BLK_FSYS1_UID_PCIE_IA_GEN3_IPCLKPORT_i_CLK, 21, 0, 0),
};

static const struct samsung_cmu_info fsys1_cmu_info __initconst = {
	.mux_clks		= fsys1_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(fsys1_mux_clks),
	.gate_clks		= fsys1_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(fsys1_gate_clks),
	.nr_clk_ids		= CLKS_NR_FSYS1,
	.clk_regs		= fsys1_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(fsys1_clk_regs),
	.qch_regs		= fsys1_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(fsys1_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_fsys1_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &fsys1_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_fsys1, "samsung,exynos9810-cmu-fsys1",
	       exynos9810_cmu_fsys1_init);

/* ---- CMU_G2D ---------------------------------------------------------*/

/* Register Offset definitions for CMU_G2D (0x17600000) */
#define MUX_CLKCMU_G2D_G2D_USER			0x0100
#define MUX_CLKCMU_G2D_MSCL_USER			0x0120
#define DIV_CLK_G2D_BUSP			0x1804

static const unsigned long g2d_clk_regs[] __initconst = {
	MUX_CLKCMU_G2D_G2D_USER,
	MUX_CLKCMU_G2D_MSCL_USER,
	DIV_CLK_G2D_BUSP,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long g2d_qch_regs[] __initconst = {
	0x3010,	/* ASTC_QCH */
	0x3014,	/* BTM_G2DD0_QCH */
	0x3018,	/* BTM_G2DD1_QCH */
	0x301c,	/* BTM_G2DD2_QCH */
	0x3020,	/* G2D_CMU_G2D_QCH */
	0x3024,	/* G2D_QCH */
	0x3028,	/* JPEG_QCH */
	0x302c,	/* LHM_AXI_P_G2D_QCH */
	0x3030,	/* LHS_ACEL_D0_G2D_QCH */
	0x3034,	/* LHS_ACEL_D1_G2D_QCH */
	0x3038,	/* LHS_ACEL_D2_G2D_QCH */
	0x303c,	/* MSCL_QCH */
	0x3040,	/* PGEN100_LITE_G2D_QCH */
	0x3044,	/* PPMU_G2DD0_QCH */
	0x3048,	/* PPMU_G2DD1_QCH */
	0x304c,	/* PPMU_G2DD2_QCH */
	0x3050,	/* QE_ASTC_QCH */
	0x3054,	/* QE_JPEG_QCH */
	0x3058,	/* QE_MSCL_QCH */
	0x305c,	/* SYSMMU_G2DD0_QCH */
	0x3060,	/* SYSMMU_G2DD1_QCH */
	0x3064,	/* SYSMMU_G2DD2_QCH */
	0x3068,	/* SYSREG_G2D_QCH */
};

/* List of parent clocks for Muxes in CMU_G2D */
PNAME(mout_cmu_g2d_g2d_user_p) = { "oscclk", "dout_clkcmu_g2d_g2d" };
PNAME(mout_cmu_g2d_mscl_user_p) = { "oscclk", "dout_clkcmu_g2d_mscl" };

static const struct samsung_mux_clock g2d_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_G2D_G2D_USER, "mout_cmu_g2d_g2d_user", mout_cmu_g2d_g2d_user_p,
	    MUX_CLKCMU_G2D_G2D_USER, 4, 1),
	MUX(CLK_MOUT_CMU_G2D_MSCL_USER, "mout_cmu_g2d_mscl_user", mout_cmu_g2d_mscl_user_p,
	    MUX_CLKCMU_G2D_MSCL_USER, 4, 1),
};

static const struct samsung_div_clock g2d_div_clks[] __initconst = {
	DIV(CLK_DOUT_G2D_BUSP, "dout_g2d_busp", "mout_cmu_g2d_mscl_user",
	    DIV_CLK_G2D_BUSP, 0, 3),
};

static const struct samsung_cmu_info g2d_cmu_info __initconst = {
	.mux_clks		= g2d_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(g2d_mux_clks),
	.div_clks		= g2d_div_clks,
	.nr_div_clks		= ARRAY_SIZE(g2d_div_clks),
	.nr_clk_ids		= CLKS_NR_G2D,
	.clk_regs		= g2d_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(g2d_clk_regs),
	.qch_regs		= g2d_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(g2d_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_g2d_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &g2d_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_g2d, "samsung,exynos9810-cmu-g2d",
	       exynos9810_cmu_g2d_init);

/* ---- CMU_G3D ---------------------------------------------------------*/

/* Register Offset definitions for CMU_G3D (0x17400000) */
#define PLL_LOCKTIME_PLL_G3D_PLL_LOCK_TIME			0x0000
#define MUX_CLKCMU_EMBEDDED_G3D_USER			0x0100
#define MUX_CLKCMU_G3D_SWITCH_USER			0x0120
#define PLL_CON0_PLL_G3D_ENABLE			0x0140
#define MUX_CLK_G3D_BUSD			0x1000
#define DIV_CLK_G3D_BUSD			0x1800
#define DIV_CLK_G3D_BUSP			0x1804
#define CLK_BLK_G3D_UID_G3D_CMU_G3D_IPCLKPORT_PCLK	0x2000
#define CLK_BLK_G3D_UID_GPU_IPCLKPORT_CLK			0x2004
#define CLK_BLK_G3D_UID_HPM_G3D0_IPCLKPORT_hpm_targetclk_c	0x2008
#define GOUT_BLK_G3D_UID_AXI2APB_G3D_IPCLKPORT_ACLK	0x2014
#define GOUT_BLK_G3D_UID_BUSIF_HPMG3D_IPCLKPORT_PCLK	0x2018
#define GOUT_BLK_G3D_UID_GRAY2BIN_G3D_IPCLKPORT_CLK	0x2020
#define GOUT_BLK_G3D_UID_LHM_AXI_G3DSFR_IPCLKPORT_I_CLK	0x2024
#define GOUT_BLK_G3D_UID_LHM_AXI_P_G3D_IPCLKPORT_I_CLK	0x2028
#define GOUT_BLK_G3D_UID_LHS_AXI_G3DSFR_IPCLKPORT_I_CLK	0x202c
#define GOUT_BLK_G3D_UID_PGEN_LITE_G3D_IPCLKPORT_CLK	0x2030
#define GOUT_BLK_G3D_UID_RSTnSYNC_CLK_G3D_BUSP_IPCLKPORT_CLK	0x2034
#define GOUT_BLK_G3D_UID_SYSREG_G3D_IPCLKPORT_PCLK	0x2038
#define GOUT_BLK_G3D_UID_XIU_P_G3D_IPCLKPORT_ACLK	0x203c

static const unsigned long g3d_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_G3D_PLL_LOCK_TIME,
	MUX_CLKCMU_EMBEDDED_G3D_USER,
	MUX_CLKCMU_G3D_SWITCH_USER,
	PLL_CON0_PLL_G3D_ENABLE,
	MUX_CLK_G3D_BUSD,
	DIV_CLK_G3D_BUSD,
	DIV_CLK_G3D_BUSP,
	CLK_BLK_G3D_UID_G3D_CMU_G3D_IPCLKPORT_PCLK,
	CLK_BLK_G3D_UID_GPU_IPCLKPORT_CLK,
	CLK_BLK_G3D_UID_HPM_G3D0_IPCLKPORT_hpm_targetclk_c,
	GOUT_BLK_G3D_UID_AXI2APB_G3D_IPCLKPORT_ACLK,
	GOUT_BLK_G3D_UID_BUSIF_HPMG3D_IPCLKPORT_PCLK,
	GOUT_BLK_G3D_UID_GRAY2BIN_G3D_IPCLKPORT_CLK,
	GOUT_BLK_G3D_UID_LHM_AXI_G3DSFR_IPCLKPORT_I_CLK,
	GOUT_BLK_G3D_UID_LHM_AXI_P_G3D_IPCLKPORT_I_CLK,
	GOUT_BLK_G3D_UID_LHS_AXI_G3DSFR_IPCLKPORT_I_CLK,
	GOUT_BLK_G3D_UID_PGEN_LITE_G3D_IPCLKPORT_CLK,
	GOUT_BLK_G3D_UID_RSTnSYNC_CLK_G3D_BUSP_IPCLKPORT_CLK,
	GOUT_BLK_G3D_UID_SYSREG_G3D_IPCLKPORT_PCLK,
	GOUT_BLK_G3D_UID_XIU_P_G3D_IPCLKPORT_ACLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long g3d_qch_regs[] __initconst = {
	0x301c,	/* BUSIF_HPMG3D_QCH */
	0x3020,	/* G3D_CMU_G3D_QCH */
	0x3024,	/* GPU_QCH */
	0x3028,	/* LHM_AXI_G3DSFR_QCH */
	0x302c,	/* LHM_AXI_P_G3D_QCH */
	0x3030,	/* LHS_ACE_D0_G3D_QCH */
	0x3034,	/* LHS_ACE_D1_G3D_QCH */
	0x3038,	/* LHS_ACE_D2_G3D_QCH */
	0x303c,	/* LHS_ACE_D3_G3D_QCH */
	0x3040,	/* LHS_AXI_G3DSFR_QCH */
	0x3044,	/* PGEN_LITE_G3D_QCH */
	0x3048,	/* SYSREG_G3D_QCH */
};

static const struct samsung_pll_rate_table fout_g3d_rate_table[] __initconst = {
	PLL_35XX_RATE(26 * MHZ, 860000000U, 430, 13, 0),
	PLL_35XX_RATE(26 * MHZ, 650000000U, 175, 7, 0),
	PLL_35XX_RATE(26 * MHZ, 320000000U, 320, 13, 1),
};

static const struct samsung_pll_clock g3d_pll_clks[] __initconst = {
	PLL(pll_1018x, CLK_FOUT_G3D, "fout_g3d", "oscclk",
	    PLL_LOCKTIME_PLL_G3D_PLL_LOCK_TIME, PLL_CON0_PLL_G3D_ENABLE, fout_g3d_rate_table),
};

/* List of parent clocks for Muxes in CMU_G3D */
PNAME(mout_g3d_busd_p) = { "fout_g3d", "mout_cmu_g3d_switch_user" };
PNAME(mout_cmu_g3d_switch_user_p) = { "oscclk", "dout_clkcmu_g3d_switch" };
PNAME(mout_cmu_embedded_g3d_user_p) = { "oscclk", "dout_g3d_busd" };

static const struct samsung_mux_clock g3d_mux_clks[] __initconst = {
	MUX(CLK_MOUT_G3D_BUSD, "mout_g3d_busd", mout_g3d_busd_p,
	    MUX_CLK_G3D_BUSD, 0, 1),
	MUX(CLK_MOUT_CMU_G3D_SWITCH_USER, "mout_cmu_g3d_switch_user", mout_cmu_g3d_switch_user_p,
	    MUX_CLKCMU_G3D_SWITCH_USER, 4, 1),
	MUX(CLK_MOUT_CMU_EMBEDDED_G3D_USER, "mout_cmu_embedded_g3d_user",
	    mout_cmu_embedded_g3d_user_p,
	    MUX_CLKCMU_EMBEDDED_G3D_USER, 4, 1),
};

static const struct samsung_div_clock g3d_div_clks[] __initconst = {
	DIV(CLK_DOUT_G3D_BUSP, "dout_g3d_busp", "mout_g3d_busd",
	    DIV_CLK_G3D_BUSP, 0, 3),
	DIV(CLK_DOUT_G3D_BUSD, "dout_g3d_busd", "mout_g3d_busd",
	    DIV_CLK_G3D_BUSD, 0, 12),
};

static const struct samsung_gate_clock g3d_gate_clks[] __initconst = {
	GATE(CLK_GOUT_G3D_XIU_P_G3D_ACLK, "gout_g3d_xiu_p_g3d_aclk", "dout_g3d_busp",
	     GOUT_BLK_G3D_UID_XIU_P_G3D_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_LHM_AXI_P_G3D_I_CLK, "gout_g3d_lhm_axi_p_g3d_i_clk", "dout_g3d_busp",
	     GOUT_BLK_G3D_UID_LHM_AXI_P_G3D_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_BUSIF_HPMG3D_PCLK, "gout_g3d_busif_hpmg3d_pclk", "dout_g3d_busp",
	     GOUT_BLK_G3D_UID_BUSIF_HPMG3D_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_HPM_G3D0_hpm_targetclk_c, "gout_g3d_hpm_g3d0_hpm_targetclk_c",
	     "dout_clkcmu_hpm",
	     CLK_BLK_G3D_UID_HPM_G3D0_IPCLKPORT_hpm_targetclk_c, 21, 0, 0),
	GATE(CLK_GOUT_G3D_SYSREG_G3D_PCLK, "gout_g3d_sysreg_g3d_pclk", "dout_g3d_busp",
	     GOUT_BLK_G3D_UID_SYSREG_G3D_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_RSTnSYNC_CLK_G3D_BUSP_CLK, "gout_g3d_rstnsync_clk_g3d_busp_clk",
	     "dout_g3d_busp",
	     GOUT_BLK_G3D_UID_RSTnSYNC_CLK_G3D_BUSP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_G3D_CMU_G3D_PCLK, "gout_g3d_g3d_cmu_g3d_pclk", "dout_g3d_busp",
	     CLK_BLK_G3D_UID_G3D_CMU_G3D_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_LHS_AXI_G3DSFR_I_CLK, "gout_g3d_lhs_axi_g3dsfr_i_clk", "dout_g3d_busp",
	     GOUT_BLK_G3D_UID_LHS_AXI_G3DSFR_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_PGEN_LITE_G3D_CLK, "gout_g3d_pgen_lite_g3d_clk", "dout_g3d_busp",
	     GOUT_BLK_G3D_UID_PGEN_LITE_G3D_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_GPU_CLK, "gout_g3d_gpu_clk", "mout_cmu_embedded_g3d_user",
	     CLK_BLK_G3D_UID_GPU_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_AXI2APB_G3D_ACLK, "gout_g3d_axi2apb_g3d_aclk", "dout_g3d_busp",
	     GOUT_BLK_G3D_UID_AXI2APB_G3D_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_LHM_AXI_G3DSFR_I_CLK, "gout_g3d_lhm_axi_g3dsfr_i_clk",
	     "mout_cmu_embedded_g3d_user",
	     GOUT_BLK_G3D_UID_LHM_AXI_G3DSFR_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_GRAY2BIN_G3D_CLK, "gout_g3d_gray2bin_g3d_clk",
	     "mout_cmu_embedded_g3d_user",
	     GOUT_BLK_G3D_UID_GRAY2BIN_G3D_IPCLKPORT_CLK, 21, 0, 0),
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

static void __init exynos9810_cmu_g3d_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &g3d_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_g3d, "samsung,exynos9810-cmu-g3d",
	       exynos9810_cmu_g3d_init);

/* ---- CMU_ISPHQ -------------------------------------------------------*/

/* Register Offset definitions for CMU_ISPHQ (0x16600000) */
#define MUX_CLKCMU_ISPHQ_BUS_USER			0x0100
#define DIV_CLK_ISPHQ_BUSP			0x1800

static const unsigned long isphq_clk_regs[] __initconst = {
	MUX_CLKCMU_ISPHQ_BUS_USER,
	DIV_CLK_ISPHQ_BUSP,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long isphq_qch_regs[] __initconst = {
	0x3018,	/* BTM_ISPHQ_QCH */
	0x301c,	/* ISPHQ_CMU_ISPHQ_QCH */
	0x3020,	/* IS_ISPHQ_QCH_ISPHQ */
	0x3024,	/* IS_ISPHQ_QCH_ISPHQ_C2COM */
	0x3028,	/* IS_ISPHQ_QCH_PGEN_LITE_ISPHQ */
	0x302c,	/* IS_ISPHQ_QCH_PPMU_ISPHQ */
	0x3030,	/* IS_ISPHQ_QCH_SYSMMU_ISPHQ */
	0x3034,	/* LHM_ATB_ISPLPISPHQ_QCH */
	0x3038,	/* LHM_ATB_ISPPREISPHQ_QCH */
	0x303c,	/* LHM_AXI_P_ISPHQ_QCH */
	0x3040,	/* LHS_ATB_ISPHQDCF_QCH */
	0x3044,	/* LHS_ATB_ISPHQISPLP_QCH */
	0x3048,	/* LHS_AXI_D_ISPHQ_QCH */
	0x304c,	/* SYSREG_ISPHQ_QCH */
};

/* List of parent clocks for Muxes in CMU_ISPHQ */
PNAME(mout_cmu_isphq_bus_user_p) = { "oscclk", "dout_clkcmu_isphq_bus" };

static const struct samsung_mux_clock isphq_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_ISPHQ_BUS_USER, "mout_cmu_isphq_bus_user", mout_cmu_isphq_bus_user_p,
	    MUX_CLKCMU_ISPHQ_BUS_USER, 4, 1),
};

static const struct samsung_div_clock isphq_div_clks[] __initconst = {
	DIV(CLK_DOUT_ISPHQ_BUSP, "dout_isphq_busp", "mout_cmu_isphq_bus_user",
	    DIV_CLK_ISPHQ_BUSP, 0, 3),
};

static const struct samsung_cmu_info isphq_cmu_info __initconst = {
	.mux_clks		= isphq_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(isphq_mux_clks),
	.div_clks		= isphq_div_clks,
	.nr_div_clks		= ARRAY_SIZE(isphq_div_clks),
	.nr_clk_ids		= CLKS_NR_ISPHQ,
	.clk_regs		= isphq_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(isphq_clk_regs),
	.qch_regs		= isphq_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(isphq_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_isphq_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &isphq_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_isphq, "samsung,exynos9810-cmu-isphq",
	       exynos9810_cmu_isphq_init);

/* ---- CMU_ISPLP -------------------------------------------------------*/

/* Register Offset definitions for CMU_ISPLP (0x16400000) */
#define MUX_CLKCMU_ISPLP_BUS_USER			0x0100
#define MUX_CLKCMU_ISPLP_GDC_USER			0x0120
#define MUX_CLKCMU_ISPLP_VRA_USER			0x0140
#define DIV_CLK_ISPLP_BUSP			0x1800

static const unsigned long isplp_clk_regs[] __initconst = {
	MUX_CLKCMU_ISPLP_BUS_USER,
	MUX_CLKCMU_ISPLP_GDC_USER,
	MUX_CLKCMU_ISPLP_VRA_USER,
	DIV_CLK_ISPLP_BUSP,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long isplp_qch_regs[] __initconst = {
	0x3020,	/* BTM_ISPLP0_QCH */
	0x3024,	/* BTM_ISPLP1_QCH */
	0x3028,	/* ISPLP_CMU_ISPLP_QCH */
	0x302c,	/* IS_ISPLP_QCH_GDC */
	0x3030,	/* IS_ISPLP_QCH_ISPLP */
	0x3034,	/* IS_ISPLP_QCH_ISPLP_C2 */
	0x3038,	/* IS_ISPLP_QCH_MC_SCALER */
	0x303c,	/* IS_ISPLP_QCH_PGEN_LITE */
	0x3040,	/* IS_ISPLP_QCH_PPMU_ISPLP0 */
	0x3044,	/* IS_ISPLP_QCH_PPMU_ISPLP1 */
	0x3048,	/* IS_ISPLP_QCH_QE_GDC */
	0x304c,	/* IS_ISPLP_QCH_QE_ISPLP */
	0x3054,	/* IS_ISPLP_QCH_QE_VRA */
	0x305c,	/* IS_ISPLP_QCH_SYSMMU_ISPLP0 */
	0x3060,	/* IS_ISPLP_QCH_SYSMMU_ISPLP1 */
	0x3064,	/* IS_ISPLP_QCH_VRA */
	0x3068,	/* LHM_ATB_DCFISPLP_QCH */
	0x306c,	/* LHM_ATB_DCRDISPLP_QCH */
	0x3070,	/* LHM_ATB_ISPHQISPLP_QCH */
	0x3074,	/* LHM_ATB_ISPPREISPLP_QCH */
	0x3078,	/* LHM_AXI_P_ISPLP_QCH */
	0x307c,	/* LHS_ATB_ISPLPISPHQ_QCH */
	0x3080,	/* LHS_AXI_D0_ISPLP_QCH */
	0x3084,	/* LHS_AXI_D1_ISPLP_QCH */
	0x3088,	/* SYSREG_ISPLP_QCH */
};

/* List of parent clocks for Muxes in CMU_ISPLP */
PNAME(mout_cmu_isplp_bus_user_p) = { "oscclk", "dout_clkcmu_isplp_bus" };
PNAME(mout_cmu_isplp_vra_user_p) = { "oscclk", "dout_clkcmu_isplp_vra" };
PNAME(mout_cmu_isplp_gdc_user_p) = { "oscclk", "dout_clkcmu_isplp_gdc" };

static const struct samsung_mux_clock isplp_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_ISPLP_BUS_USER, "mout_cmu_isplp_bus_user", mout_cmu_isplp_bus_user_p,
	    MUX_CLKCMU_ISPLP_BUS_USER, 4, 1),
	MUX(CLK_MOUT_CMU_ISPLP_VRA_USER, "mout_cmu_isplp_vra_user", mout_cmu_isplp_vra_user_p,
	    MUX_CLKCMU_ISPLP_VRA_USER, 4, 1),
	MUX(CLK_MOUT_CMU_ISPLP_GDC_USER, "mout_cmu_isplp_gdc_user", mout_cmu_isplp_gdc_user_p,
	    MUX_CLKCMU_ISPLP_GDC_USER, 4, 1),
};

static const struct samsung_div_clock isplp_div_clks[] __initconst = {
	DIV(CLK_DOUT_ISPLP_BUSP, "dout_isplp_busp", "mout_cmu_isplp_bus_user",
	    DIV_CLK_ISPLP_BUSP, 0, 3),
};

static const struct samsung_cmu_info isplp_cmu_info __initconst = {
	.mux_clks		= isplp_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(isplp_mux_clks),
	.div_clks		= isplp_div_clks,
	.nr_div_clks		= ARRAY_SIZE(isplp_div_clks),
	.nr_clk_ids		= CLKS_NR_ISPLP,
	.clk_regs		= isplp_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(isplp_clk_regs),
	.qch_regs		= isplp_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(isplp_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_isplp_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &isplp_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_isplp, "samsung,exynos9810-cmu-isplp",
	       exynos9810_cmu_isplp_init);

/* ---- CMU_ISPPRE ------------------------------------------------------*/

/* Register Offset definitions for CMU_ISPPRE (0x16200000) */
#define MUX_CLKCMU_ISPPRE_BUS_USER			0x0100
#define DIV_CLK_ISPPRE_BUSP			0x1804

static const unsigned long isppre_clk_regs[] __initconst = {
	MUX_CLKCMU_ISPPRE_BUS_USER,
	DIV_CLK_ISPPRE_BUSP,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long isppre_qch_regs[] __initconst = {
	0x3010,	/* BTM_ISPPRE_QCH */
	0x3014,	/* ISPPRE_CMU_ISPPRE_QCH */
	0x3018,	/* IS_ISPPRE_QCH_3AA */
	0x301c,	/* IS_ISPPRE_QCH_3AAM */
	0x3020,	/* IS_ISPPRE_QCH_CSIS0 */
	0x3024,	/* IS_ISPPRE_QCH_CSIS1 */
	0x3028,	/* IS_ISPPRE_QCH_CSIS2 */
	0x302c,	/* IS_ISPPRE_QCH_CSIS3 */
	0x3030,	/* IS_ISPPRE_QCH_PDP_CORE0 */
	0x3034,	/* IS_ISPPRE_QCH_PDP_CORE1 */
	0x3038,	/* IS_ISPPRE_QCH_PDP_DMA */
	0x3040,	/* IS_ISPPRE_QCH_PGEN_LITE */
	0x3044,	/* IS_ISPPRE_QCH_PGEN_LITE1 */
	0x3048,	/* IS_ISPPRE_QCH_PPMU_ISPPRE */
	0x304c,	/* IS_ISPPRE_QCH_QE_3AA */
	0x3050,	/* IS_ISPPRE_QCH_QE_3AAM */
	0x3054,	/* IS_ISPPRE_QCH_QE_PDP */
	0x3058,	/* IS_ISPPRE_QCH_QE_PDP_STAT */
	0x305c,	/* IS_ISPPRE_QCH_SYSMMU_ISPPRE */
	0x3060,	/* LHM_AXI_P_ISPPRE_QCH */
	0x3064,	/* LHS_ATB_ISPPREISPHQ_QCH */
	0x3068,	/* LHS_ATB_ISPPREISPLP_QCH */
	0x306c,	/* LHS_AXI_D_ISPPRE_QCH */
	0x3070,	/* SYSREG_ISPPRE_QCH_SYSREG */
};

/* List of parent clocks for Muxes in CMU_ISPPRE */
PNAME(mout_cmu_isppre_bus_user_p) = { "oscclk", "dout_clkcmu_isppre_bus" };

static const struct samsung_mux_clock isppre_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_ISPPRE_BUS_USER, "mout_cmu_isppre_bus_user", mout_cmu_isppre_bus_user_p,
	    MUX_CLKCMU_ISPPRE_BUS_USER, 4, 1),
};

static const struct samsung_div_clock isppre_div_clks[] __initconst = {
	DIV(CLK_DOUT_ISPPRE_BUSP, "dout_isppre_busp", "mout_cmu_isppre_bus_user",
	    DIV_CLK_ISPPRE_BUSP, 0, 3),
};

static const struct samsung_cmu_info isppre_cmu_info __initconst = {
	.mux_clks		= isppre_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(isppre_mux_clks),
	.div_clks		= isppre_div_clks,
	.nr_div_clks		= ARRAY_SIZE(isppre_div_clks),
	.nr_clk_ids		= CLKS_NR_ISPPRE,
	.clk_regs		= isppre_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(isppre_clk_regs),
	.qch_regs		= isppre_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(isppre_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_isppre_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &isppre_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_isppre, "samsung,exynos9810-cmu-isppre",
	       exynos9810_cmu_isppre_init);

/* ---- CMU_IVA ---------------------------------------------------------*/

/* Register Offset definitions for CMU_IVA (0x17000000) */
#define MUX_CLKCMU_IVA_BUS_USER			0x0100
#define DIV_CLK_IVA_BUSP			0x1800
#define DIV_CLK_IVA_DEBUG			0x1804
#define CLK_BLK_IVA_UID_IVA_CMU_IVA_IPCLKPORT_PCLK	0x2000
#define GOUT_BLK_IVA_UID_ADM_DAP_IVA_IPCLKPORT_dapclkm	0x2004
#define GOUT_BLK_IVA_UID_AD_APB_IVA0_IPCLKPORT_PCLKS	0x200c
#define GOUT_BLK_IVA_UID_AD_APB_IVA1_IPCLKPORT_PCLKS	0x2014
#define GOUT_BLK_IVA_UID_AD_APB_IVA2_IPCLKPORT_PCLKS	0x201c
#define GOUT_BLK_IVA_UID_AXI2APB_2M_IVA_IPCLKPORT_ACLK	0x2020
#define GOUT_BLK_IVA_UID_AXI2APB_IVA_IPCLKPORT_ACLK	0x2024
#define GOUT_BLK_IVA_UID_BTM_IVA_IPCLKPORT_I_PCLK	0x202c
#define GOUT_BLK_IVA_UID_IVA_IPCLKPORT_dap_clk			0x2038
#define GOUT_BLK_IVA_UID_LHM_AXI_P_IVA_IPCLKPORT_I_CLK	0x2048
#define GOUT_BLK_IVA_UID_PGEN_lite_IVA_IPCLKPORT_CLK	0x2058
#define GOUT_BLK_IVA_UID_PPMU_IVA_IPCLKPORT_PCLK	0x2060
#define GOUT_BLK_IVA_UID_RSTnSYNC_CLK_IVA_BUSP_IPCLKPORT_CLK	0x2068
#define GOUT_BLK_IVA_UID_RSTnSYNC_CLK_IVA_DEBUG_IPCLKPORT_CLK	0x206c
#define GOUT_BLK_IVA_UID_SYSREG_IVA_IPCLKPORT_PCLK	0x2074
#define GOUT_BLK_IVA_UID_TREX_RB_IVA_IPCLKPORT_pclk	0x207c
#define GOUT_BLK_IVA_UID_XIU_P_IVA_IPCLKPORT_ACLK	0x2088

static const unsigned long iva_clk_regs[] __initconst = {
	MUX_CLKCMU_IVA_BUS_USER,
	DIV_CLK_IVA_BUSP,
	DIV_CLK_IVA_DEBUG,
	CLK_BLK_IVA_UID_IVA_CMU_IVA_IPCLKPORT_PCLK,
	GOUT_BLK_IVA_UID_ADM_DAP_IVA_IPCLKPORT_dapclkm,
	GOUT_BLK_IVA_UID_AD_APB_IVA0_IPCLKPORT_PCLKS,
	GOUT_BLK_IVA_UID_AD_APB_IVA1_IPCLKPORT_PCLKS,
	GOUT_BLK_IVA_UID_AD_APB_IVA2_IPCLKPORT_PCLKS,
	GOUT_BLK_IVA_UID_AXI2APB_2M_IVA_IPCLKPORT_ACLK,
	GOUT_BLK_IVA_UID_AXI2APB_IVA_IPCLKPORT_ACLK,
	GOUT_BLK_IVA_UID_BTM_IVA_IPCLKPORT_I_PCLK,
	GOUT_BLK_IVA_UID_IVA_IPCLKPORT_dap_clk,
	GOUT_BLK_IVA_UID_LHM_AXI_P_IVA_IPCLKPORT_I_CLK,
	GOUT_BLK_IVA_UID_PGEN_lite_IVA_IPCLKPORT_CLK,
	GOUT_BLK_IVA_UID_PPMU_IVA_IPCLKPORT_PCLK,
	GOUT_BLK_IVA_UID_RSTnSYNC_CLK_IVA_BUSP_IPCLKPORT_CLK,
	GOUT_BLK_IVA_UID_RSTnSYNC_CLK_IVA_DEBUG_IPCLKPORT_CLK,
	GOUT_BLK_IVA_UID_SYSREG_IVA_IPCLKPORT_PCLK,
	GOUT_BLK_IVA_UID_TREX_RB_IVA_IPCLKPORT_pclk,
	GOUT_BLK_IVA_UID_XIU_P_IVA_IPCLKPORT_ACLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long iva_qch_regs[] __initconst = {
	0x3000,	/* ADM_DAP_IVA_QCH */
	0x3020,	/* BTM_IVA_QCH */
	0x3024,	/* IVA_CMU_IVA_QCH */
	0x3028,	/* IVA_INTMEM_QCH */
	0x302c,	/* IVA_QCH_IVA */
	0x3030,	/* IVA_QCH_IVA_DEBUG */
	0x3034,	/* LHM_AXI_D_DSPSIVA_QCH */
	0x3038,	/* LHM_AXI_D_IVASC_QCH */
	0x303c,	/* LHM_AXI_P_DSPMIVA_QCH */
	0x3040,	/* LHM_AXI_P_IVA_QCH */
	0x3044,	/* LHS_ACEL_D_IVA_QCH */
	0x3048,	/* LHS_AXI_D_IVADSPS_QCH */
	0x304c,	/* LHS_AXI_P_IVADSPM_QCH */
	0x3050,	/* PGEN_LITE_IVA_QCH */
	0x3054,	/* PPMU_IVA_QCH */
	0x3058,	/* SYSMMU_IVA_QCH */
	0x305c,	/* SYSREG_IVA_QCH */
	0x3060,	/* TREX_RB_IVA_QCH */
};

/* List of parent clocks for Muxes in CMU_IVA */
PNAME(mout_cmu_iva_bus_user_p) = { "oscclk", "dout_clkcmu_iva_bus" };

static const struct samsung_mux_clock iva_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_IVA_BUS_USER, "mout_cmu_iva_bus_user", mout_cmu_iva_bus_user_p,
	    MUX_CLKCMU_IVA_BUS_USER, 4, 1),
};

static const struct samsung_div_clock iva_div_clks[] __initconst = {
	DIV(CLK_DOUT_IVA_BUSP, "dout_iva_busp", "mout_cmu_iva_bus_user",
	    DIV_CLK_IVA_BUSP, 0, 3),
	DIV(CLK_DOUT_IVA_DEBUG, "dout_iva_debug", "mout_cmu_iva_bus_user",
	    DIV_CLK_IVA_DEBUG, 0, 4),
};

static const struct samsung_gate_clock iva_gate_clks[] __initconst = {
	GATE(CLK_GOUT_IVA_IVA_CMU_IVA_PCLK, "gout_iva_iva_cmu_iva_pclk", "dout_iva_busp",
	     CLK_BLK_IVA_UID_IVA_CMU_IVA_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_IVA_LHM_AXI_P_IVA_I_CLK, "gout_iva_lhm_axi_p_iva_i_clk", "dout_iva_busp",
	     GOUT_BLK_IVA_UID_LHM_AXI_P_IVA_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_IVA_BTM_IVA_I_PCLK, "gout_iva_btm_iva_i_pclk", "dout_iva_busp",
	     GOUT_BLK_IVA_UID_BTM_IVA_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_IVA_PPMU_IVA_PCLK, "gout_iva_ppmu_iva_pclk", "dout_iva_busp",
	     GOUT_BLK_IVA_UID_PPMU_IVA_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_IVA_XIU_P_IVA_ACLK, "gout_iva_xiu_p_iva_aclk", "dout_iva_busp",
	     GOUT_BLK_IVA_UID_XIU_P_IVA_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_IVA_AD_APB_IVA0_PCLKS, "gout_iva_ad_apb_iva0_pclks", "dout_iva_busp",
	     GOUT_BLK_IVA_UID_AD_APB_IVA0_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_IVA_AXI2APB_2M_IVA_ACLK, "gout_iva_axi2apb_2m_iva_aclk", "dout_iva_busp",
	     GOUT_BLK_IVA_UID_AXI2APB_2M_IVA_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_IVA_AXI2APB_IVA_ACLK, "gout_iva_axi2apb_iva_aclk", "dout_iva_busp",
	     GOUT_BLK_IVA_UID_AXI2APB_IVA_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_IVA_SYSREG_IVA_PCLK, "gout_iva_sysreg_iva_pclk", "dout_iva_busp",
	     GOUT_BLK_IVA_UID_SYSREG_IVA_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_IVA_RSTnSYNC_CLK_IVA_BUSP_CLK, "gout_iva_rstnsync_clk_iva_busp_clk",
	     "dout_iva_busp",
	     GOUT_BLK_IVA_UID_RSTnSYNC_CLK_IVA_BUSP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_IVA_ADM_DAP_IVA_dapclkm, "gout_iva_adm_dap_iva_dapclkm", "dout_iva_debug",
	     GOUT_BLK_IVA_UID_ADM_DAP_IVA_IPCLKPORT_dapclkm, 21, 0, 0),
	GATE(CLK_GOUT_IVA_RSTnSYNC_CLK_IVA_DEBUG_CLK, "gout_iva_rstnsync_clk_iva_debug_clk",
	     "dout_iva_debug",
	     GOUT_BLK_IVA_UID_RSTnSYNC_CLK_IVA_DEBUG_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_IVA_AD_APB_IVA1_PCLKS, "gout_iva_ad_apb_iva1_pclks", "dout_iva_busp",
	     GOUT_BLK_IVA_UID_AD_APB_IVA1_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_IVA_AD_APB_IVA2_PCLKS, "gout_iva_ad_apb_iva2_pclks", "dout_iva_busp",
	     GOUT_BLK_IVA_UID_AD_APB_IVA2_IPCLKPORT_PCLKS, 21, 0, 0),
	GATE(CLK_GOUT_IVA_PGEN_lite_IVA_CLK, "gout_iva_pgen_lite_iva_clk", "dout_iva_busp",
	     GOUT_BLK_IVA_UID_PGEN_lite_IVA_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_IVA_TREX_RB_IVA_pclk, "gout_iva_trex_rb_iva_pclk", "dout_iva_busp",
	     GOUT_BLK_IVA_UID_TREX_RB_IVA_IPCLKPORT_pclk, 21, 0, 0),
	GATE(CLK_GOUT_IVA_IVA_dap_clk, "gout_iva_iva_dap_clk", "dout_iva_debug",
	     GOUT_BLK_IVA_UID_IVA_IPCLKPORT_dap_clk, 21, 0, 0),
};

static const struct samsung_cmu_info iva_cmu_info __initconst = {
	.mux_clks		= iva_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(iva_mux_clks),
	.div_clks		= iva_div_clks,
	.nr_div_clks		= ARRAY_SIZE(iva_div_clks),
	.gate_clks		= iva_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(iva_gate_clks),
	.nr_clk_ids		= CLKS_NR_IVA,
	.clk_regs		= iva_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(iva_clk_regs),
	.qch_regs		= iva_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(iva_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_iva_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &iva_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_iva, "samsung,exynos9810-cmu-iva",
	       exynos9810_cmu_iva_init);

/* ---- CMU_MFC ---------------------------------------------------------*/

/* Register Offset definitions for CMU_MFC (0x17800000) */
#define MUX_CLKCMU_MFC_BUS_USER			0x0100
#define MUX_CLKCMU_MFC_WFD_USER			0x0120
#define DIV_CLK_MFC_BUSP			0x1804

static const unsigned long mfc_clk_regs[] __initconst = {
	MUX_CLKCMU_MFC_BUS_USER,
	MUX_CLKCMU_MFC_WFD_USER,
	DIV_CLK_MFC_BUSP,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long mfc_qch_regs[] __initconst = {
	0x300c,	/* BTM_MFCD0_QCH */
	0x3010,	/* BTM_MFCD1_QCH */
	0x3014,	/* LHM_AXI_P_MFC_QCH */
	0x3018,	/* LHS_AXI_D0_MFC_QCH */
	0x301c,	/* LHS_AXI_D1_MFC_QCH */
	0x3020,	/* LH_ATB_QCH_MI */
	0x3024,	/* LH_ATB_QCH_SI */
	0x3028,	/* MFC_CMU_MFC_QCH */
	0x302c,	/* MFC_QCH */
	0x3030,	/* PGEN100_LITE_MFC_QCH */
	0x3034,	/* PPMU_MFCD0_QCH */
	0x3038,	/* PPMU_MFCD1_QCH */
	0x303c,	/* PPMU_MFCD2_QCH */
	0x3040,	/* RSTNSYNC_CLK_MFC_BUSD_LH_ATB_MI_SW_RESET_QCH */
	0x3044,	/* RSTNSYNC_CLK_MFC_BUSD_LH_ATB_SI_SW_RESET_QCH */
	0x3048,	/* RSTNSYNC_CLK_MFC_BUSD_MFC_SW_RESET_QCH */
	0x304c,	/* RSTNSYNC_CLK_MFC_BUSD_WFD_SW_RESET_QCH */
	0x3050,	/* SYSMMU_MFCD0_QCH */
	0x3054,	/* SYSMMU_MFCD1_QCH */
	0x3058,	/* SYSREG_MFC_QCH */
	0x305c,	/* WFD_QCH */
};

/* List of parent clocks for Muxes in CMU_MFC */
PNAME(mout_cmu_mfc_bus_user_p) = { "oscclk", "dout_clkcmu_mfc_bus" };
PNAME(mout_cmu_mfc_wfd_user_p) = { "oscclk", "dout_clkcmu_mfc_wfd" };

static const struct samsung_mux_clock mfc_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_MFC_BUS_USER, "mout_cmu_mfc_bus_user", mout_cmu_mfc_bus_user_p,
	    MUX_CLKCMU_MFC_BUS_USER, 4, 1),
	MUX(CLK_MOUT_CMU_MFC_WFD_USER, "mout_cmu_mfc_wfd_user", mout_cmu_mfc_wfd_user_p,
	    MUX_CLKCMU_MFC_WFD_USER, 4, 1),
};

static const struct samsung_div_clock mfc_div_clks[] __initconst = {
	DIV(CLK_DOUT_MFC_BUSP, "dout_mfc_busp", "mout_cmu_mfc_bus_user",
	    DIV_CLK_MFC_BUSP, 0, 3),
};

static const struct samsung_cmu_info mfc_cmu_info __initconst = {
	.mux_clks		= mfc_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(mfc_mux_clks),
	.div_clks		= mfc_div_clks,
	.nr_div_clks		= ARRAY_SIZE(mfc_div_clks),
	.nr_clk_ids		= CLKS_NR_MFC,
	.clk_regs		= mfc_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(mfc_clk_regs),
	.qch_regs		= mfc_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(mfc_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_mfc_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &mfc_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_mfc, "samsung,exynos9810-cmu-mfc",
	       exynos9810_cmu_mfc_init);

/* ---- CMU_MIF ---------------------------------------------------------*/

/* Register Offset definitions for CMU_MIF (0x1b800000) */
#define PLL_LOCKTIME_PLL_MIF_PLL_LOCK_TIME			0x0000
#define MUX_CLKCMU_MIF_BUSP_USER			0x0100
#define PLL_CON0_PLL_MIF_ENABLE			0x0120
#define CLKMUX_MIF_DDRPHY2X			0x1000
#define MUX_MIF_CMUREF			0x1004
#define DIV_CLK_MIF_PRE			0x1810
#define CLK_BLK_MIF_UID_HPM_MIF_IPCLKPORT_hpm_targetclk_c	0x2004

static const unsigned long mif_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_MIF_PLL_LOCK_TIME,
	MUX_CLKCMU_MIF_BUSP_USER,
	PLL_CON0_PLL_MIF_ENABLE,
	CLKMUX_MIF_DDRPHY2X,
	MUX_MIF_CMUREF,
	DIV_CLK_MIF_PRE,
	CLK_BLK_MIF_UID_HPM_MIF_IPCLKPORT_hpm_targetclk_c,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long mif_qch_regs[] __initconst = {
	0x3000,	/* CMU_MIF_CMUREF_QCH */
	0x3008,	/* APBBR_DDRPHY_QCH */
	0x300c,	/* APBBR_DMCTZ_QCH */
	0x3010,	/* BUSIF_HPMMIF_QCH */
	0x3014,	/* DMC_QCH */
	0x301c,	/* LHM_AXI_P_MIF_QCH */
	0x3020,	/* MIF_CMU_MIF_QCH */
	0x3024,	/* QCH_ADAPTER_PPMUPPC_DEBUG_QCH */
	0x3028,	/* QCH_ADAPTER_PPMUPPC_DVFS_QCH */
	0x302c,	/* SYSREG_MIF_QCH */
	0x3030,	/* APBBR_DMC_QCH */
};

static const struct samsung_pll_rate_table fout_mif_rate_table[] __initconst = {
	PLL_35XX_RATE(26 * MHZ, 4264000000U, 492, 3, 0),
	PLL_35XX_RATE(26 * MHZ, 3731000000U, 574, 4, 0),
	PLL_35XX_RATE(26 * MHZ, 2576600000U, 991, 5, 1),
	PLL_35XX_RATE(26 * MHZ, 1418000000U, 709, 13, 0),
};

static const struct samsung_pll_clock mif_pll_clks[] __initconst = {
	PLL(pll_1050x, CLK_FOUT_MIF, "fout_mif", "oscclk",
	    PLL_LOCKTIME_PLL_MIF_PLL_LOCK_TIME, PLL_CON0_PLL_MIF_ENABLE, fout_mif_rate_table),
};

/* List of parent clocks for Muxes in CMU_MIF */
PNAME(mout_clkmux_mif_ddrphy2x_p) = { "fout_mif", "gout_clkcmu_mif_switch" };
PNAME(mout_mif_cmuref_p) = { "oscclk", "mout_cmu_mif_busp_user" };
PNAME(mout_cmu_mif_busp_user_p) = { "oscclk", "dout_clkcmu_mif_busp" };

static const struct samsung_mux_clock mif_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CLKMUX_MIF_DDRPHY2X, "mout_clkmux_mif_ddrphy2x", mout_clkmux_mif_ddrphy2x_p,
	    CLKMUX_MIF_DDRPHY2X, 0, 1),
	MUX(CLK_MOUT_MIF_CMUREF, "mout_mif_cmuref", mout_mif_cmuref_p,
	    MUX_MIF_CMUREF, 0, 1),
	MUX(CLK_MOUT_CMU_MIF_BUSP_USER, "mout_cmu_mif_busp_user", mout_cmu_mif_busp_user_p,
	    MUX_CLKCMU_MIF_BUSP_USER, 4, 1),
};

static const struct samsung_div_clock mif_div_clks[] __initconst = {
	DIV(CLK_DOUT_MIF_PRE, "dout_mif_pre", "gout_clkcmu_mif_switch",
	    DIV_CLK_MIF_PRE, 0, 3),
};

static const struct samsung_gate_clock mif_gate_clks[] __initconst = {
	GATE(CLK_GOUT_MIF_HPM_MIF_hpm_targetclk_c, "gout_mif_hpm_mif_hpm_targetclk_c",
	     "dout_clkcmu_hpm",
	     CLK_BLK_MIF_UID_HPM_MIF_IPCLKPORT_hpm_targetclk_c, 21, 0, 0),
};

static const struct samsung_cmu_info mif_cmu_info __initconst = {
	.pll_clks		= mif_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(mif_pll_clks),
	.mux_clks		= mif_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(mif_mux_clks),
	.div_clks		= mif_div_clks,
	.nr_div_clks		= ARRAY_SIZE(mif_div_clks),
	.gate_clks		= mif_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(mif_gate_clks),
	.nr_clk_ids		= CLKS_NR_MIF,
	.clk_regs		= mif_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(mif_clk_regs),
	.qch_regs		= mif_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(mif_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_mif_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &mif_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_mif, "samsung,exynos9810-cmu-mif",
	       exynos9810_cmu_mif_init);

/* ---- CMU_PERIC0 ------------------------------------------------------*/

/* Register Offset definitions for CMU_PERIC0 (0x10400000) */
#define MUX_CLKCMU_PERIC0_BUS_USER			0x0100
#define MUX_CLKCMU_PERIC0_IP_USER			0x0120
#define DIV_CLK_PERIC0_UART_DBG			0x1800
#define DIV_CLK_PERIC0_USI00_USI			0x1804
#define DIV_CLK_PERIC0_USI01_USI			0x1808
#define DIV_CLK_PERIC0_USI02_USI			0x180c
#define DIV_CLK_PERIC0_USI03_USI			0x1810
#define DIV_CLK_PERIC0_USI04_USI			0x1814
#define DIV_CLK_PERIC0_USI05_USI			0x1818
#define DIV_CLK_PERIC0_USI12_USI			0x181c
#define DIV_CLK_PERIC0_USI13_USI			0x1820
#define DIV_CLK_PERIC0_USI14_USI			0x1824
#define DIV_CLK_PERIC0_USI_I2C			0x1828
#define CLK_BLK_PERIC0_UID_PERIC0_CMU_PERIC0_IPCLKPORT_PCLK	0x2000
#define GATE_CLK_PERIC0_UART_DBG			0x2008
#define GATE_CLK_PERIC0_USI00_USI			0x200c
#define GATE_CLK_PERIC0_USI01_USI			0x2010
#define GATE_CLK_PERIC0_USI02_USI			0x2014
#define GATE_CLK_PERIC0_USI03_USI			0x2018
#define GATE_CLK_PERIC0_USI04_USI			0x201c
#define GATE_CLK_PERIC0_USI05_USI			0x2020
#define GATE_CLK_PERIC0_USI12_USI			0x2024
#define GATE_CLK_PERIC0_USI13_USI			0x2028
#define GATE_CLK_PERIC0_USI14_USI			0x202c
#define GATE_CLK_PERIC0_USI_I2C			0x2030
#define GOUT_BLK_PERIC0_UID_AXI2APB_PERIC0P0_IPCLKPORT_ACLK	0x2034
#define GOUT_BLK_PERIC0_UID_AXI2APB_PERIC0P1_IPCLKPORT_ACLK	0x2038
#define GOUT_BLK_PERIC0_UID_GPIO_PERIC0_IPCLKPORT_PCLK	0x203c
#define GOUT_BLK_PERIC0_UID_LHM_AXI_P_PERIC0_IPCLKPORT_I_CLK	0x2040
#define GOUT_BLK_PERIC0_UID_PWM_IPCLKPORT_i_PCLK_S0	0x2044
#define GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_BUSP_IPCLKPORT_CLK	0x2048
#define GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_UART_DBG_IPCLKPORT_CLK	0x204c
#define GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI00_I2C_IPCLKPORT_CLK	0x2050
#define GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI00_USI_IPCLKPORT_CLK	0x2054
#define GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI01_I2C_IPCLKPORT_CLK	0x2058
#define GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI01_USI_IPCLKPORT_CLK	0x205c
#define GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI02_I2C_IPCLKPORT_CLK	0x2060
#define GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI02_USI_IPCLKPORT_CLK	0x2064
#define GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI03_I2C_IPCLKPORT_CLK	0x2068
#define GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI03_USI_IPCLKPORT_CLK	0x206c
#define GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI04_I2C_IPCLKPORT_CLK	0x2070
#define GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI04_USI_IPCLKPORT_CLK	0x2074
#define GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI05_I2C_IPCLKPORT_CLK	0x2078
#define GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI05_USI_IPCLKPORT_CLK	0x207c
#define GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI12_I2C_IPCLKPORT_CLK	0x2080
#define GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI12_USI_IPCLKPORT_CLK	0x2084
#define GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI13_I2C_IPCLKPORT_CLK	0x2088
#define GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI13_USI_IPCLKPORT_CLK	0x208c
#define GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI14_I2C_IPCLKPORT_CLK	0x2090
#define GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI14_USI_IPCLKPORT_CLK	0x2094
#define GOUT_BLK_PERIC0_UID_SYSREG_PERIC0_IPCLKPORT_PCLK	0x2098
#define GOUT_BLK_PERIC0_UID_UART_DBG_IPCLKPORT_IPCLK	0x209c
#define GOUT_BLK_PERIC0_UID_UART_DBG_IPCLKPORT_PCLK	0x20a0
#define GOUT_BLK_PERIC0_UID_USI00_I2C_IPCLKPORT_IPCLK	0x20a4
#define GOUT_BLK_PERIC0_UID_USI00_I2C_IPCLKPORT_PCLK	0x20a8
#define GOUT_BLK_PERIC0_UID_USI00_USI_IPCLKPORT_IPCLK	0x20ac
#define GOUT_BLK_PERIC0_UID_USI00_USI_IPCLKPORT_PCLK	0x20b0
#define GOUT_BLK_PERIC0_UID_USI01_I2C_IPCLKPORT_IPCLK	0x20b4
#define GOUT_BLK_PERIC0_UID_USI01_I2C_IPCLKPORT_PCLK	0x20b8
#define GOUT_BLK_PERIC0_UID_USI01_USI_IPCLKPORT_IPCLK	0x20bc
#define GOUT_BLK_PERIC0_UID_USI01_USI_IPCLKPORT_PCLK	0x20c0
#define GOUT_BLK_PERIC0_UID_USI02_I2C_IPCLKPORT_IPCLK	0x20c4
#define GOUT_BLK_PERIC0_UID_USI02_I2C_IPCLKPORT_PCLK	0x20c8
#define GOUT_BLK_PERIC0_UID_USI02_USI_IPCLKPORT_IPCLK	0x20cc
#define GOUT_BLK_PERIC0_UID_USI02_USI_IPCLKPORT_PCLK	0x20d0
#define GOUT_BLK_PERIC0_UID_USI03_I2C_IPCLKPORT_IPCLK	0x20d4
#define GOUT_BLK_PERIC0_UID_USI03_I2C_IPCLKPORT_PCLK	0x20d8
#define GOUT_BLK_PERIC0_UID_USI03_USI_IPCLKPORT_IPCLK	0x20dc
#define GOUT_BLK_PERIC0_UID_USI03_USI_IPCLKPORT_PCLK	0x20e0
#define GOUT_BLK_PERIC0_UID_USI04_I2C_IPCLKPORT_IPCLK	0x20e4
#define GOUT_BLK_PERIC0_UID_USI04_I2C_IPCLKPORT_PCLK	0x20e8
#define GOUT_BLK_PERIC0_UID_USI04_USI_IPCLKPORT_IPCLK	0x20ec
#define GOUT_BLK_PERIC0_UID_USI04_USI_IPCLKPORT_PCLK	0x20f0
#define GOUT_BLK_PERIC0_UID_USI05_I2C_IPCLKPORT_IPCLK	0x20f4
#define GOUT_BLK_PERIC0_UID_USI05_I2C_IPCLKPORT_PCLK	0x20f8
#define GOUT_BLK_PERIC0_UID_USI05_USI_IPCLKPORT_IPCLK	0x20fc
#define GOUT_BLK_PERIC0_UID_USI05_USI_IPCLKPORT_PCLK	0x2100
#define GOUT_BLK_PERIC0_UID_USI12_I2C_IPCLKPORT_IPCLK	0x2104
#define GOUT_BLK_PERIC0_UID_USI12_I2C_IPCLKPORT_PCLK	0x2108
#define GOUT_BLK_PERIC0_UID_USI12_USI_IPCLKPORT_IPCLK	0x210c
#define GOUT_BLK_PERIC0_UID_USI12_USI_IPCLKPORT_PCLK	0x2110
#define GOUT_BLK_PERIC0_UID_USI13_I2C_IPCLKPORT_IPCLK	0x2114
#define GOUT_BLK_PERIC0_UID_USI13_I2C_IPCLKPORT_PCLK	0x2118
#define GOUT_BLK_PERIC0_UID_USI13_USI_IPCLKPORT_IPCLK	0x211c
#define GOUT_BLK_PERIC0_UID_USI13_USI_IPCLKPORT_PCLK	0x2120
#define GOUT_BLK_PERIC0_UID_USI14_I2C_IPCLKPORT_IPCLK	0x2124
#define GOUT_BLK_PERIC0_UID_USI14_I2C_IPCLKPORT_PCLK	0x2128
#define GOUT_BLK_PERIC0_UID_USI14_USI_IPCLKPORT_IPCLK	0x212c
#define GOUT_BLK_PERIC0_UID_USI14_USI_IPCLKPORT_PCLK	0x2130
#define GOUT_BLK_PERIC0_UID_XIU_P_PERIC0_IPCLKPORT_ACLK	0x2134

static const unsigned long peric0_clk_regs[] __initconst = {
	MUX_CLKCMU_PERIC0_BUS_USER,
	MUX_CLKCMU_PERIC0_IP_USER,
	DIV_CLK_PERIC0_UART_DBG,
	DIV_CLK_PERIC0_USI00_USI,
	DIV_CLK_PERIC0_USI01_USI,
	DIV_CLK_PERIC0_USI02_USI,
	DIV_CLK_PERIC0_USI03_USI,
	DIV_CLK_PERIC0_USI04_USI,
	DIV_CLK_PERIC0_USI05_USI,
	DIV_CLK_PERIC0_USI12_USI,
	DIV_CLK_PERIC0_USI13_USI,
	DIV_CLK_PERIC0_USI14_USI,
	DIV_CLK_PERIC0_USI_I2C,
	CLK_BLK_PERIC0_UID_PERIC0_CMU_PERIC0_IPCLKPORT_PCLK,
	GATE_CLK_PERIC0_UART_DBG,
	GATE_CLK_PERIC0_USI00_USI,
	GATE_CLK_PERIC0_USI01_USI,
	GATE_CLK_PERIC0_USI02_USI,
	GATE_CLK_PERIC0_USI03_USI,
	GATE_CLK_PERIC0_USI04_USI,
	GATE_CLK_PERIC0_USI05_USI,
	GATE_CLK_PERIC0_USI12_USI,
	GATE_CLK_PERIC0_USI13_USI,
	GATE_CLK_PERIC0_USI14_USI,
	GATE_CLK_PERIC0_USI_I2C,
	GOUT_BLK_PERIC0_UID_AXI2APB_PERIC0P0_IPCLKPORT_ACLK,
	GOUT_BLK_PERIC0_UID_AXI2APB_PERIC0P1_IPCLKPORT_ACLK,
	GOUT_BLK_PERIC0_UID_GPIO_PERIC0_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC0_UID_LHM_AXI_P_PERIC0_IPCLKPORT_I_CLK,
	GOUT_BLK_PERIC0_UID_PWM_IPCLKPORT_i_PCLK_S0,
	GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_BUSP_IPCLKPORT_CLK,
	GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_UART_DBG_IPCLKPORT_CLK,
	GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI00_I2C_IPCLKPORT_CLK,
	GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI00_USI_IPCLKPORT_CLK,
	GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI01_I2C_IPCLKPORT_CLK,
	GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI01_USI_IPCLKPORT_CLK,
	GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI02_I2C_IPCLKPORT_CLK,
	GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI02_USI_IPCLKPORT_CLK,
	GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI03_I2C_IPCLKPORT_CLK,
	GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI03_USI_IPCLKPORT_CLK,
	GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI04_I2C_IPCLKPORT_CLK,
	GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI04_USI_IPCLKPORT_CLK,
	GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI05_I2C_IPCLKPORT_CLK,
	GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI05_USI_IPCLKPORT_CLK,
	GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI12_I2C_IPCLKPORT_CLK,
	GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI12_USI_IPCLKPORT_CLK,
	GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI13_I2C_IPCLKPORT_CLK,
	GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI13_USI_IPCLKPORT_CLK,
	GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI14_I2C_IPCLKPORT_CLK,
	GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI14_USI_IPCLKPORT_CLK,
	GOUT_BLK_PERIC0_UID_SYSREG_PERIC0_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC0_UID_UART_DBG_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC0_UID_UART_DBG_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC0_UID_USI00_I2C_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC0_UID_USI00_I2C_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC0_UID_USI00_USI_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC0_UID_USI00_USI_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC0_UID_USI01_I2C_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC0_UID_USI01_I2C_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC0_UID_USI01_USI_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC0_UID_USI01_USI_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC0_UID_USI02_I2C_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC0_UID_USI02_I2C_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC0_UID_USI02_USI_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC0_UID_USI02_USI_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC0_UID_USI03_I2C_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC0_UID_USI03_I2C_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC0_UID_USI03_USI_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC0_UID_USI03_USI_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC0_UID_USI04_I2C_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC0_UID_USI04_I2C_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC0_UID_USI04_USI_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC0_UID_USI04_USI_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC0_UID_USI05_I2C_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC0_UID_USI05_I2C_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC0_UID_USI05_USI_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC0_UID_USI05_USI_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC0_UID_USI12_I2C_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC0_UID_USI12_I2C_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC0_UID_USI12_USI_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC0_UID_USI12_USI_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC0_UID_USI13_I2C_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC0_UID_USI13_I2C_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC0_UID_USI13_USI_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC0_UID_USI13_USI_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC0_UID_USI14_I2C_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC0_UID_USI14_I2C_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC0_UID_USI14_USI_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC0_UID_USI14_USI_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC0_UID_XIU_P_PERIC0_IPCLKPORT_ACLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long peric0_qch_regs[] __initconst = {
	0x3004,	/* GPIO_PERIC0_QCH */
	0x3008,	/* LHM_AXI_P_PERIC0_QCH */
	0x300c,	/* PERIC0_CMU_PERIC0_QCH */
	0x3010,	/* PWM_QCH */
	0x3014,	/* SYSREG_PERIC0_QCH */
	0x3018,	/* UART_DBG_QCH */
	0x301c,	/* USI00_I2C_QCH */
	0x3020,	/* USI00_USI_QCH */
	0x3024,	/* USI01_I2C_QCH */
	0x3028,	/* USI01_USI_QCH */
	0x302c,	/* USI02_I2C_QCH */
	0x3030,	/* USI02_USI_QCH */
	0x3034,	/* USI03_I2C_QCH */
	0x3038,	/* USI03_USI_QCH */
	0x303c,	/* USI04_I2C_QCH */
	0x3040,	/* USI04_USI_QCH */
	0x3044,	/* USI05_I2C_QCH */
	0x3048,	/* USI05_USI_QCH */
	0x304c,	/* USI12_I2C_QCH */
	0x3050,	/* USI12_USI_QCH */
	0x3054,	/* USI13_I2C_QCH */
	0x3058,	/* USI13_USI_QCH */
	0x305c,	/* USI14_I2C_QCH */
	0x3060,	/* USI14_USI_QCH */
};

/* List of parent clocks for Muxes in CMU_PERIC0 */
PNAME(mout_cmu_peric0_bus_user_p) = { "oscclk", "dout_clkcmu_peric0_bus" };
PNAME(mout_cmu_peric0_ip_user_p) = { "oscclk", "dout_clkcmu_peric0_ip" };

static const struct samsung_mux_clock peric0_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_PERIC0_BUS_USER, "mout_cmu_peric0_bus_user", mout_cmu_peric0_bus_user_p,
	    MUX_CLKCMU_PERIC0_BUS_USER, 4, 1),
	MUX(CLK_MOUT_CMU_PERIC0_IP_USER, "mout_cmu_peric0_ip_user", mout_cmu_peric0_ip_user_p,
	    MUX_CLKCMU_PERIC0_IP_USER, 4, 1),
};

static const struct samsung_div_clock peric0_div_clks[] __initconst = {
	DIV(CLK_DOUT_PERIC0_USI00_USI, "dout_peric0_usi00_usi", "gout_peric0_usi00_usi",
	    DIV_CLK_PERIC0_USI00_USI, 0, 7),
	DIV(CLK_DOUT_PERIC0_USI01_USI, "dout_peric0_usi01_usi", "gout_peric0_usi01_usi",
	    DIV_CLK_PERIC0_USI01_USI, 0, 4),
	DIV(CLK_DOUT_PERIC0_USI02_USI, "dout_peric0_usi02_usi", "gout_peric0_usi02_usi",
	    DIV_CLK_PERIC0_USI02_USI, 0, 4),
	DIV(CLK_DOUT_PERIC0_USI03_USI, "dout_peric0_usi03_usi", "gout_peric0_usi03_usi",
	    DIV_CLK_PERIC0_USI03_USI, 0, 4),
	DIV(CLK_DOUT_PERIC0_USI04_USI, "dout_peric0_usi04_usi", "gout_peric0_usi04_usi",
	    DIV_CLK_PERIC0_USI04_USI, 0, 4),
	DIV(CLK_DOUT_PERIC0_USI05_USI, "dout_peric0_usi05_usi", "gout_peric0_usi05_usi",
	    DIV_CLK_PERIC0_USI05_USI, 0, 7),
	DIV(CLK_DOUT_PERIC0_USI_I2C, "dout_peric0_usi_i2c", "gout_peric0_usi_i2c",
	    DIV_CLK_PERIC0_USI_I2C, 0, 4),
	DIV(CLK_DOUT_PERIC0_UART_DBG, "dout_peric0_uart_dbg", "gout_peric0_uart_dbg",
	    DIV_CLK_PERIC0_UART_DBG, 0, 4),
	DIV(CLK_DOUT_PERIC0_USI12_USI, "dout_peric0_usi12_usi", "gout_peric0_usi12_usi",
	    DIV_CLK_PERIC0_USI12_USI, 0, 4),
	DIV(CLK_DOUT_PERIC0_USI13_USI, "dout_peric0_usi13_usi", "gout_peric0_usi13_usi",
	    DIV_CLK_PERIC0_USI13_USI, 0, 4),
	DIV(CLK_DOUT_PERIC0_USI14_USI, "dout_peric0_usi14_usi", "gout_peric0_usi14_usi",
	    DIV_CLK_PERIC0_USI14_USI, 0, 4),
};

static const struct samsung_gate_clock peric0_gate_clks[] __initconst = {
	GATE(CLK_GOUT_PERIC0_GPIO_PERIC0_PCLK, "gout_peric0_gpio_peric0_pclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_GPIO_PERIC0_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PWM_i_PCLK_S0, "gout_peric0_pwm_i_pclk_s0", "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_PWM_IPCLKPORT_i_PCLK_S0, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_SYSREG_PERIC0_PCLK, "gout_peric0_sysreg_peric0_pclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_SYSREG_PERIC0_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI00_USI_PCLK, "gout_peric0_usi00_usi_pclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_USI00_USI_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI01_USI_PCLK, "gout_peric0_usi01_usi_pclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_USI01_USI_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI02_USI_PCLK, "gout_peric0_usi02_usi_pclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_USI02_USI_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI03_USI_PCLK, "gout_peric0_usi03_usi_pclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_USI03_USI_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_AXI2APB_PERIC0P0_ACLK, "gout_peric0_axi2apb_peric0p0_aclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_AXI2APB_PERIC0P0_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_CMU_PERIC0_PCLK, "gout_peric0_peric0_cmu_peric0_pclk",
	     "mout_cmu_peric0_bus_user",
	     CLK_BLK_PERIC0_UID_PERIC0_CMU_PERIC0_IPCLKPORT_PCLK, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERIC0_RSTnSYNC_CLK_PERIC0_BUSP_CLK, "gout_peric0_rstnsync_clk_peric0_busp_clk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_BUSP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI04_USI_PCLK, "gout_peric0_usi04_usi_pclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_USI04_USI_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_AXI2APB_PERIC0P1_ACLK, "gout_peric0_axi2apb_peric0p1_aclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_AXI2APB_PERIC0P1_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI05_USI_PCLK, "gout_peric0_usi05_usi_pclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_USI05_USI_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI00_I2C_PCLK, "gout_peric0_usi00_i2c_pclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_USI00_I2C_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI01_I2C_PCLK, "gout_peric0_usi01_i2c_pclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_USI01_I2C_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI02_I2C_PCLK, "gout_peric0_usi02_i2c_pclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_USI02_I2C_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI03_I2C_PCLK, "gout_peric0_usi03_i2c_pclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_USI03_I2C_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI04_I2C_PCLK, "gout_peric0_usi04_i2c_pclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_USI04_I2C_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI05_I2C_PCLK, "gout_peric0_usi05_i2c_pclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_USI05_I2C_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_RSTnSYNC_CLK_PERIC0_USI00_USI_CLK, "gout_peric0_rstnsync_clk_peric0_usi00_usi_clk",
	     "dout_peric0_usi00_usi",
	     GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI00_USI_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_RSTnSYNC_CLK_PERIC0_USI00_I2C_CLK, "gout_peric0_rstnsync_clk_peric0_usi00_i2c_clk",
	     "dout_peric0_usi_i2c",
	     GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI00_I2C_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_RSTnSYNC_CLK_PERIC0_USI01_USI_CLK, "gout_peric0_rstnsync_clk_peric0_usi01_usi_clk",
	     "dout_peric0_usi01_usi",
	     GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI01_USI_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_RSTnSYNC_CLK_PERIC0_USI02_USI_CLK, "gout_peric0_rstnsync_clk_peric0_usi02_usi_clk",
	     "dout_peric0_usi02_usi",
	     GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI02_USI_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_RSTnSYNC_CLK_PERIC0_USI03_USI_CLK, "gout_peric0_rstnsync_clk_peric0_usi03_usi_clk",
	     "dout_peric0_usi03_usi",
	     GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI03_USI_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_RSTnSYNC_CLK_PERIC0_USI04_USI_CLK, "gout_peric0_rstnsync_clk_peric0_usi04_usi_clk",
	     "dout_peric0_usi04_usi",
	     GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI04_USI_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_RSTnSYNC_CLK_PERIC0_USI05_USI_CLK, "gout_peric0_rstnsync_clk_peric0_usi05_usi_clk",
	     "dout_peric0_usi05_usi",
	     GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI05_USI_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_UART_DBG_PCLK, "gout_peric0_uart_dbg_pclk", "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_UART_DBG_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_RSTnSYNC_CLK_PERIC0_UART_DBG_CLK, "gout_peric0_rstnsync_clk_peric0_uart_dbg_clk",
	     "dout_peric0_uart_dbg",
	     GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_UART_DBG_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_XIU_P_PERIC0_ACLK, "gout_peric0_xiu_p_peric0_aclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_XIU_P_PERIC0_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_RSTnSYNC_CLK_PERIC0_USI01_I2C_CLK, "gout_peric0_rstnsync_clk_peric0_usi01_i2c_clk",
	     "dout_peric0_usi_i2c",
	     GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI01_I2C_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_RSTnSYNC_CLK_PERIC0_USI02_I2C_CLK, "gout_peric0_rstnsync_clk_peric0_usi02_i2c_clk",
	     "dout_peric0_usi_i2c",
	     GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI02_I2C_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_RSTnSYNC_CLK_PERIC0_USI03_I2C_CLK, "gout_peric0_rstnsync_clk_peric0_usi03_i2c_clk",
	     "dout_peric0_usi_i2c",
	     GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI03_I2C_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_RSTnSYNC_CLK_PERIC0_USI04_I2C_CLK, "gout_peric0_rstnsync_clk_peric0_usi04_i2c_clk",
	     "dout_peric0_usi_i2c",
	     GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI04_I2C_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_RSTnSYNC_CLK_PERIC0_USI05_I2C_CLK, "gout_peric0_rstnsync_clk_peric0_usi05_i2c_clk",
	     "dout_peric0_usi_i2c",
	     GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI05_I2C_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_UART_DBG_IPCLK, "gout_peric0_uart_dbg_ipclk", "dout_peric0_uart_dbg",
	     GOUT_BLK_PERIC0_UID_UART_DBG_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI00_USI_IPCLK, "gout_peric0_usi00_usi_ipclk",
	     "dout_peric0_usi00_usi",
	     GOUT_BLK_PERIC0_UID_USI00_USI_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI01_I2C_IPCLK, "gout_peric0_usi01_i2c_ipclk", "dout_peric0_usi_i2c",
	     GOUT_BLK_PERIC0_UID_USI01_I2C_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI01_USI_IPCLK, "gout_peric0_usi01_usi_ipclk",
	     "dout_peric0_usi01_usi",
	     GOUT_BLK_PERIC0_UID_USI01_USI_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI02_I2C_IPCLK, "gout_peric0_usi02_i2c_ipclk", "dout_peric0_usi_i2c",
	     GOUT_BLK_PERIC0_UID_USI02_I2C_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI02_USI_IPCLK, "gout_peric0_usi02_usi_ipclk",
	     "dout_peric0_usi02_usi",
	     GOUT_BLK_PERIC0_UID_USI02_USI_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI03_I2C_IPCLK, "gout_peric0_usi03_i2c_ipclk", "dout_peric0_usi_i2c",
	     GOUT_BLK_PERIC0_UID_USI03_I2C_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI03_USI_IPCLK, "gout_peric0_usi03_usi_ipclk",
	     "dout_peric0_usi03_usi",
	     GOUT_BLK_PERIC0_UID_USI03_USI_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI04_I2C_IPCLK, "gout_peric0_usi04_i2c_ipclk", "dout_peric0_usi_i2c",
	     GOUT_BLK_PERIC0_UID_USI04_I2C_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI04_USI_IPCLK, "gout_peric0_usi04_usi_ipclk",
	     "dout_peric0_usi04_usi",
	     GOUT_BLK_PERIC0_UID_USI04_USI_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI05_I2C_IPCLK, "gout_peric0_usi05_i2c_ipclk", "dout_peric0_usi_i2c",
	     GOUT_BLK_PERIC0_UID_USI05_I2C_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI05_USI_IPCLK, "gout_peric0_usi05_usi_ipclk",
	     "dout_peric0_usi05_usi",
	     GOUT_BLK_PERIC0_UID_USI05_USI_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_LHM_AXI_P_PERIC0_I_CLK, "gout_peric0_lhm_axi_p_peric0_i_clk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_LHM_AXI_P_PERIC0_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI00_USI, "gout_peric0_usi00_usi", "mout_cmu_peric0_ip_user",
	     GATE_CLK_PERIC0_USI00_USI, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI01_USI, "gout_peric0_usi01_usi", "mout_cmu_peric0_ip_user",
	     GATE_CLK_PERIC0_USI01_USI, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI02_USI, "gout_peric0_usi02_usi", "mout_cmu_peric0_ip_user",
	     GATE_CLK_PERIC0_USI02_USI, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI03_USI, "gout_peric0_usi03_usi", "mout_cmu_peric0_ip_user",
	     GATE_CLK_PERIC0_USI03_USI, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI04_USI, "gout_peric0_usi04_usi", "mout_cmu_peric0_ip_user",
	     GATE_CLK_PERIC0_USI04_USI, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI05_USI, "gout_peric0_usi05_usi", "mout_cmu_peric0_ip_user",
	     GATE_CLK_PERIC0_USI05_USI, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI_I2C, "gout_peric0_usi_i2c", "mout_cmu_peric0_ip_user",
	     GATE_CLK_PERIC0_USI_I2C, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_UART_DBG, "gout_peric0_uart_dbg", "mout_cmu_peric0_ip_user",
	     GATE_CLK_PERIC0_UART_DBG, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI00_I2C_IPCLK, "gout_peric0_usi00_i2c_ipclk", "dout_peric0_usi_i2c",
	     GOUT_BLK_PERIC0_UID_USI00_I2C_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI12_USI, "gout_peric0_usi12_usi", "mout_cmu_peric0_ip_user",
	     GATE_CLK_PERIC0_USI12_USI, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI13_USI, "gout_peric0_usi13_usi", "mout_cmu_peric0_ip_user",
	     GATE_CLK_PERIC0_USI13_USI, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI14_USI, "gout_peric0_usi14_usi", "mout_cmu_peric0_ip_user",
	     GATE_CLK_PERIC0_USI14_USI, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_RSTnSYNC_CLK_PERIC0_USI12_USI_CLK, "gout_peric0_rstnsync_clk_peric0_usi12_usi_clk",
	     "dout_peric0_usi12_usi",
	     GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI12_USI_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_RSTnSYNC_CLK_PERIC0_USI13_USI_CLK, "gout_peric0_rstnsync_clk_peric0_usi13_usi_clk",
	     "dout_peric0_usi13_usi",
	     GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI13_USI_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_RSTnSYNC_CLK_PERIC0_USI14_USI_CLK, "gout_peric0_rstnsync_clk_peric0_usi14_usi_clk",
	     "dout_peric0_usi14_usi",
	     GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI14_USI_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI12_USI_PCLK, "gout_peric0_usi12_usi_pclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_USI12_USI_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI12_USI_IPCLK, "gout_peric0_usi12_usi_ipclk",
	     "dout_peric0_usi12_usi",
	     GOUT_BLK_PERIC0_UID_USI12_USI_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI12_I2C_PCLK, "gout_peric0_usi12_i2c_pclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_USI12_I2C_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI12_I2C_IPCLK, "gout_peric0_usi12_i2c_ipclk", "dout_peric0_usi_i2c",
	     GOUT_BLK_PERIC0_UID_USI12_I2C_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_RSTnSYNC_CLK_PERIC0_USI12_I2C_CLK, "gout_peric0_rstnsync_clk_peric0_usi12_i2c_clk",
	     "dout_peric0_usi_i2c",
	     GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI12_I2C_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_RSTnSYNC_CLK_PERIC0_USI13_I2C_CLK, "gout_peric0_rstnsync_clk_peric0_usi13_i2c_clk",
	     "dout_peric0_usi_i2c",
	     GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI13_I2C_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_RSTnSYNC_CLK_PERIC0_USI14_I2C_CLK, "gout_peric0_rstnsync_clk_peric0_usi14_i2c_clk",
	     "dout_peric0_usi_i2c",
	     GOUT_BLK_PERIC0_UID_RSTnSYNC_CLK_PERIC0_USI14_I2C_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI13_I2C_PCLK, "gout_peric0_usi13_i2c_pclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_USI13_I2C_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI13_I2C_IPCLK, "gout_peric0_usi13_i2c_ipclk", "dout_peric0_usi_i2c",
	     GOUT_BLK_PERIC0_UID_USI13_I2C_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI13_USI_PCLK, "gout_peric0_usi13_usi_pclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_USI13_USI_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI13_USI_IPCLK, "gout_peric0_usi13_usi_ipclk",
	     "dout_peric0_usi13_usi",
	     GOUT_BLK_PERIC0_UID_USI13_USI_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI14_USI_PCLK, "gout_peric0_usi14_usi_pclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_USI14_USI_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI14_USI_IPCLK, "gout_peric0_usi14_usi_ipclk",
	     "dout_peric0_usi14_usi",
	     GOUT_BLK_PERIC0_UID_USI14_USI_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI14_I2C_PCLK, "gout_peric0_usi14_i2c_pclk",
	     "mout_cmu_peric0_bus_user",
	     GOUT_BLK_PERIC0_UID_USI14_I2C_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI14_I2C_IPCLK, "gout_peric0_usi14_i2c_ipclk", "dout_peric0_usi_i2c",
	     GOUT_BLK_PERIC0_UID_USI14_I2C_IPCLKPORT_IPCLK, 21, 0, 0),
};

static const struct samsung_cmu_info peric0_cmu_info __initconst = {
	.mux_clks		= peric0_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(peric0_mux_clks),
	.div_clks		= peric0_div_clks,
	.nr_div_clks		= ARRAY_SIZE(peric0_div_clks),
	.gate_clks		= peric0_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(peric0_gate_clks),
	.nr_clk_ids		= CLKS_NR_PERIC0,
	.clk_regs		= peric0_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(peric0_clk_regs),
	.qch_regs		= peric0_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(peric0_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_peric0_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &peric0_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_peric0, "samsung,exynos9810-cmu-peric0",
	       exynos9810_cmu_peric0_init);

/* ---- CMU_PERIC1 ------------------------------------------------------*/

/* Register Offset definitions for CMU_PERIC1 (0x10800000) */
#define MUX_CLKCMU_PERIC1_BUS_USER			0x0100
#define MUX_CLKCMU_PERIC1_IP_USER			0x0120
#define DIV_CLK_PERIC1_I2C_CAM0			0x1800
#define DIV_CLK_PERIC1_I2C_CAM1			0x1804
#define DIV_CLK_PERIC1_I2C_CAM2			0x1808
#define DIV_CLK_PERIC1_I2C_CAM3			0x180c
#define DIV_CLK_PERIC1_SPI_CAM0			0x1810
#define DIV_CLK_PERIC1_UART_BT			0x1814
#define DIV_CLK_PERIC1_USI06_USI			0x1818
#define DIV_CLK_PERIC1_USI07_USI			0x181c
#define DIV_CLK_PERIC1_USI08_USI			0x1820
#define DIV_CLK_PERIC1_USI09_USI			0x1824
#define DIV_CLK_PERIC1_USI10_USI			0x1828
#define DIV_CLK_PERIC1_USI11_USI			0x182c
#define DIV_CLK_PERIC1_USI_I2C			0x1830
#define CLK_BLK_PERIC1_UID_PERIC1_CMU_PERIC1_IPCLKPORT_PCLK	0x2000
#define CLK_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_I2C_CAM0_IPCLKPORT_CLK	0x2004
#define CLK_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_I2C_CAM1_IPCLKPORT_CLK	0x2008
#define CLK_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_I2C_CAM2_IPCLKPORT_CLK	0x200c
#define CLK_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_I2C_CAM3_IPCLKPORT_CLK	0x2010
#define CLK_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_SPI_CAM0_IPCLKPORT_CLK	0x2018
#define CLK_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_UART_BT_IPCLKPORT_CLK	0x201c
#define GATE_CLK_PERIC1_I2C_CAM0			0x2020
#define GATE_CLK_PERIC1_I2C_CAM1			0x2024
#define GATE_CLK_PERIC1_I2C_CAM2			0x2028
#define GATE_CLK_PERIC1_I2C_CAM3			0x202c
#define GATE_CLK_PERIC1_SPI_CAM0			0x2030
#define GATE_CLK_PERIC1_UART_BT			0x2034
#define GATE_CLK_PERIC1_USI06_USI			0x2038
#define GATE_CLK_PERIC1_USI07_USI			0x203c
#define GATE_CLK_PERIC1_USI08_USI			0x2040
#define GATE_CLK_PERIC1_USI09_USI			0x2044
#define GATE_CLK_PERIC1_USI10_USI			0x2048
#define GATE_CLK_PERIC1_USI11_USI			0x204c
#define GATE_CLK_PERIC1_USI_I2C			0x2050
#define GOUT_BLK_PERIC1_UID_AXI2APB_PERIC1P0_IPCLKPORT_ACLK	0x2054
#define GOUT_BLK_PERIC1_UID_AXI2APB_PERIC1P1_IPCLKPORT_ACLK	0x2058
#define GOUT_BLK_PERIC1_UID_GPIO_PERIC1_IPCLKPORT_PCLK	0x205c
#define GOUT_BLK_PERIC1_UID_I2C_CAM0_IPCLKPORT_IPCLK	0x2060
#define GOUT_BLK_PERIC1_UID_I2C_CAM0_IPCLKPORT_PCLK	0x2064
#define GOUT_BLK_PERIC1_UID_I2C_CAM1_IPCLKPORT_IPCLK	0x2068
#define GOUT_BLK_PERIC1_UID_I2C_CAM1_IPCLKPORT_PCLK	0x206c
#define GOUT_BLK_PERIC1_UID_I2C_CAM2_IPCLKPORT_IPCLK	0x2070
#define GOUT_BLK_PERIC1_UID_I2C_CAM2_IPCLKPORT_PCLK	0x2074
#define GOUT_BLK_PERIC1_UID_I2C_CAM3_IPCLKPORT_IPCLK	0x2078
#define GOUT_BLK_PERIC1_UID_I2C_CAM3_IPCLKPORT_PCLK	0x207c
#define GOUT_BLK_PERIC1_UID_LHM_AXI_P_PERIC1_IPCLKPORT_I_CLK	0x2080
#define GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_BUSP_IPCLKPORT_CLK	0x2084
#define GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI06_I2C_IPCLKPORT_CLK	0x2088
#define GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI06_USI_IPCLKPORT_CLK	0x208c
#define GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI07_I2C_IPCLKPORT_CLK	0x2090
#define GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI07_USI_IPCLKPORT_CLK	0x2094
#define GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI08_I2C_IPCLKPORT_CLK	0x2098
#define GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI08_USI_IPCLKPORT_CLK	0x209c
#define GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI09_I2C_IPCLKPORT_CLK	0x20a0
#define GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI09_USI_IPCLKPORT_CLK	0x20a4
#define GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI10_I2C_IPCLKPORT_CLK	0x20a8
#define GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI10_USI_IPCLKPORT_CLK	0x20ac
#define GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI11_I2C_IPCLKPORT_CLK	0x20b0
#define GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI11_USI_IPCLKPORT_CLK	0x20b4
#define GOUT_BLK_PERIC1_UID_SPI_CAM0_IPCLKPORT_IPCLK	0x20b8
#define GOUT_BLK_PERIC1_UID_SPI_CAM0_IPCLKPORT_PCLK	0x20bc
#define GOUT_BLK_PERIC1_UID_SYSREG_PERIC1_IPCLKPORT_PCLK	0x20c0
#define GOUT_BLK_PERIC1_UID_UART_BT_IPCLKPORT_IPCLK	0x20c4
#define GOUT_BLK_PERIC1_UID_UART_BT_IPCLKPORT_PCLK	0x20c8
#define GOUT_BLK_PERIC1_UID_USI06_I2C_IPCLKPORT_IPCLK	0x20cc
#define GOUT_BLK_PERIC1_UID_USI06_I2C_IPCLKPORT_PCLK	0x20d0
#define GOUT_BLK_PERIC1_UID_USI06_USI_IPCLKPORT_IPCLK	0x20d4
#define GOUT_BLK_PERIC1_UID_USI06_USI_IPCLKPORT_PCLK	0x20d8
#define GOUT_BLK_PERIC1_UID_USI07_I2C_IPCLKPORT_IPCLK	0x20dc
#define GOUT_BLK_PERIC1_UID_USI07_I2C_IPCLKPORT_PCLK	0x20e0
#define GOUT_BLK_PERIC1_UID_USI07_USI_IPCLKPORT_IPCLK	0x20e4
#define GOUT_BLK_PERIC1_UID_USI07_USI_IPCLKPORT_PCLK	0x20e8
#define GOUT_BLK_PERIC1_UID_USI08_I2C_IPCLKPORT_IPCLK	0x20ec
#define GOUT_BLK_PERIC1_UID_USI08_I2C_IPCLKPORT_PCLK	0x20f0
#define GOUT_BLK_PERIC1_UID_USI08_USI_IPCLKPORT_IPCLK	0x20f4
#define GOUT_BLK_PERIC1_UID_USI08_USI_IPCLKPORT_PCLK	0x20f8
#define GOUT_BLK_PERIC1_UID_USI09_I2C_IPCLKPORT_IPCLK	0x20fc
#define GOUT_BLK_PERIC1_UID_USI09_I2C_IPCLKPORT_PCLK	0x2100
#define GOUT_BLK_PERIC1_UID_USI09_USI_IPCLKPORT_IPCLK	0x2104
#define GOUT_BLK_PERIC1_UID_USI09_USI_IPCLKPORT_PCLK	0x2108
#define GOUT_BLK_PERIC1_UID_USI10_I2C_IPCLKPORT_IPCLK	0x210c
#define GOUT_BLK_PERIC1_UID_USI10_I2C_IPCLKPORT_PCLK	0x2110
#define GOUT_BLK_PERIC1_UID_USI10_USI_IPCLKPORT_IPCLK	0x2114
#define GOUT_BLK_PERIC1_UID_USI10_USI_IPCLKPORT_PCLK	0x2118
#define GOUT_BLK_PERIC1_UID_USI11_I2C_IPCLKPORT_IPCLK	0x211c
#define GOUT_BLK_PERIC1_UID_USI11_I2C_IPCLKPORT_PCLK	0x2120
#define GOUT_BLK_PERIC1_UID_USI11_USI_IPCLKPORT_IPCLK	0x2124
#define GOUT_BLK_PERIC1_UID_USI11_USI_IPCLKPORT_PCLK	0x2128
#define GOUT_BLK_PERIC1_UID_XIU_P_PERIC1_IPCLKPORT_ACLK	0x212c

static const unsigned long peric1_clk_regs[] __initconst = {
	MUX_CLKCMU_PERIC1_BUS_USER,
	MUX_CLKCMU_PERIC1_IP_USER,
	DIV_CLK_PERIC1_I2C_CAM0,
	DIV_CLK_PERIC1_I2C_CAM1,
	DIV_CLK_PERIC1_I2C_CAM2,
	DIV_CLK_PERIC1_I2C_CAM3,
	DIV_CLK_PERIC1_SPI_CAM0,
	DIV_CLK_PERIC1_UART_BT,
	DIV_CLK_PERIC1_USI06_USI,
	DIV_CLK_PERIC1_USI07_USI,
	DIV_CLK_PERIC1_USI08_USI,
	DIV_CLK_PERIC1_USI09_USI,
	DIV_CLK_PERIC1_USI10_USI,
	DIV_CLK_PERIC1_USI11_USI,
	DIV_CLK_PERIC1_USI_I2C,
	CLK_BLK_PERIC1_UID_PERIC1_CMU_PERIC1_IPCLKPORT_PCLK,
	CLK_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_I2C_CAM0_IPCLKPORT_CLK,
	CLK_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_I2C_CAM1_IPCLKPORT_CLK,
	CLK_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_I2C_CAM2_IPCLKPORT_CLK,
	CLK_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_I2C_CAM3_IPCLKPORT_CLK,
	CLK_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_SPI_CAM0_IPCLKPORT_CLK,
	CLK_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_UART_BT_IPCLKPORT_CLK,
	GATE_CLK_PERIC1_I2C_CAM0,
	GATE_CLK_PERIC1_I2C_CAM1,
	GATE_CLK_PERIC1_I2C_CAM2,
	GATE_CLK_PERIC1_I2C_CAM3,
	GATE_CLK_PERIC1_SPI_CAM0,
	GATE_CLK_PERIC1_UART_BT,
	GATE_CLK_PERIC1_USI06_USI,
	GATE_CLK_PERIC1_USI07_USI,
	GATE_CLK_PERIC1_USI08_USI,
	GATE_CLK_PERIC1_USI09_USI,
	GATE_CLK_PERIC1_USI10_USI,
	GATE_CLK_PERIC1_USI11_USI,
	GATE_CLK_PERIC1_USI_I2C,
	GOUT_BLK_PERIC1_UID_AXI2APB_PERIC1P0_IPCLKPORT_ACLK,
	GOUT_BLK_PERIC1_UID_AXI2APB_PERIC1P1_IPCLKPORT_ACLK,
	GOUT_BLK_PERIC1_UID_GPIO_PERIC1_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC1_UID_I2C_CAM0_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC1_UID_I2C_CAM0_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC1_UID_I2C_CAM1_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC1_UID_I2C_CAM1_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC1_UID_I2C_CAM2_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC1_UID_I2C_CAM2_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC1_UID_I2C_CAM3_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC1_UID_I2C_CAM3_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC1_UID_LHM_AXI_P_PERIC1_IPCLKPORT_I_CLK,
	GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_BUSP_IPCLKPORT_CLK,
	GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI06_I2C_IPCLKPORT_CLK,
	GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI06_USI_IPCLKPORT_CLK,
	GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI07_I2C_IPCLKPORT_CLK,
	GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI07_USI_IPCLKPORT_CLK,
	GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI08_I2C_IPCLKPORT_CLK,
	GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI08_USI_IPCLKPORT_CLK,
	GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI09_I2C_IPCLKPORT_CLK,
	GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI09_USI_IPCLKPORT_CLK,
	GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI10_I2C_IPCLKPORT_CLK,
	GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI10_USI_IPCLKPORT_CLK,
	GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI11_I2C_IPCLKPORT_CLK,
	GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI11_USI_IPCLKPORT_CLK,
	GOUT_BLK_PERIC1_UID_SPI_CAM0_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC1_UID_SPI_CAM0_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC1_UID_SYSREG_PERIC1_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC1_UID_UART_BT_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC1_UID_UART_BT_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC1_UID_USI06_I2C_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC1_UID_USI06_I2C_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC1_UID_USI06_USI_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC1_UID_USI06_USI_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC1_UID_USI07_I2C_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC1_UID_USI07_I2C_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC1_UID_USI07_USI_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC1_UID_USI07_USI_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC1_UID_USI08_I2C_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC1_UID_USI08_I2C_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC1_UID_USI08_USI_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC1_UID_USI08_USI_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC1_UID_USI09_I2C_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC1_UID_USI09_I2C_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC1_UID_USI09_USI_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC1_UID_USI09_USI_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC1_UID_USI10_I2C_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC1_UID_USI10_I2C_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC1_UID_USI10_USI_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC1_UID_USI10_USI_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC1_UID_USI11_I2C_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC1_UID_USI11_I2C_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC1_UID_USI11_USI_IPCLKPORT_IPCLK,
	GOUT_BLK_PERIC1_UID_USI11_USI_IPCLKPORT_PCLK,
	GOUT_BLK_PERIC1_UID_XIU_P_PERIC1_IPCLKPORT_ACLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long peric1_qch_regs[] __initconst = {
	0x3004,	/* GPIO_PERIC1_QCH */
	0x3008,	/* I2C_CAM0_QCH */
	0x300c,	/* I2C_CAM1_QCH */
	0x3010,	/* I2C_CAM2_QCH */
	0x3014,	/* I2C_CAM3_QCH */
	0x3018,	/* LHM_AXI_P_PERIC1_QCH */
	0x301c,	/* PERIC1_CMU_PERIC1_QCH */
	0x3020,	/* SPI_CAM0_QCH */
	0x3024,	/* SYSREG_PERIC1_QCH */
	0x3028,	/* UART_BT_QCH */
	0x302c,	/* USI06_I2C_QCH */
	0x3030,	/* USI06_USI_QCH */
	0x3034,	/* USI07_I2C_QCH */
	0x3038,	/* USI07_USI_QCH */
	0x303c,	/* USI08_I2C_QCH */
	0x3040,	/* USI08_USI_QCH */
	0x3044,	/* USI09_I2C_QCH */
	0x3048,	/* USI09_USI_QCH */
	0x304c,	/* USI10_I2C_QCH */
	0x3050,	/* USI10_USI_QCH */
	0x3054,	/* USI11_I2C_QCH */
	0x3058,	/* USI11_USI_QCH */
};

/* List of parent clocks for Muxes in CMU_PERIC1 */
PNAME(mout_cmu_peric1_bus_user_p) = { "oscclk", "dout_clkcmu_peric1_bus" };
PNAME(mout_cmu_peric1_ip_user_p) = { "oscclk", "dout_clkcmu_peric1_ip" };

static const struct samsung_mux_clock peric1_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_PERIC1_BUS_USER, "mout_cmu_peric1_bus_user", mout_cmu_peric1_bus_user_p,
	    MUX_CLKCMU_PERIC1_BUS_USER, 4, 1),
	MUX(CLK_MOUT_CMU_PERIC1_IP_USER, "mout_cmu_peric1_ip_user", mout_cmu_peric1_ip_user_p,
	    MUX_CLKCMU_PERIC1_IP_USER, 4, 1),
};

static const struct samsung_div_clock peric1_div_clks[] __initconst = {
	DIV(CLK_DOUT_PERIC1_UART_BT, "dout_peric1_uart_bt", "gout_peric1_uart_bt",
	    DIV_CLK_PERIC1_UART_BT, 0, 4),
	DIV(CLK_DOUT_PERIC1_USI_I2C, "dout_peric1_usi_i2c", "gout_peric1_usi_i2c",
	    DIV_CLK_PERIC1_USI_I2C, 0, 4),
	DIV(CLK_DOUT_PERIC1_USI06_USI, "dout_peric1_usi06_usi", "gout_peric1_usi06_usi",
	    DIV_CLK_PERIC1_USI06_USI, 0, 4),
	DIV(CLK_DOUT_PERIC1_USI07_USI, "dout_peric1_usi07_usi", "gout_peric1_usi07_usi",
	    DIV_CLK_PERIC1_USI07_USI, 0, 7),
	DIV(CLK_DOUT_PERIC1_USI08_USI, "dout_peric1_usi08_usi", "gout_peric1_usi08_usi",
	    DIV_CLK_PERIC1_USI08_USI, 0, 7),
	DIV(CLK_DOUT_PERIC1_I2C_CAM0, "dout_peric1_i2c_cam0", "gout_peric1_i2c_cam0",
	    DIV_CLK_PERIC1_I2C_CAM0, 0, 4),
	DIV(CLK_DOUT_PERIC1_I2C_CAM1, "dout_peric1_i2c_cam1", "gout_peric1_i2c_cam1",
	    DIV_CLK_PERIC1_I2C_CAM1, 0, 4),
	DIV(CLK_DOUT_PERIC1_I2C_CAM2, "dout_peric1_i2c_cam2", "gout_peric1_i2c_cam2",
	    DIV_CLK_PERIC1_I2C_CAM2, 0, 4),
	DIV(CLK_DOUT_PERIC1_I2C_CAM3, "dout_peric1_i2c_cam3", "gout_peric1_i2c_cam3",
	    DIV_CLK_PERIC1_I2C_CAM3, 0, 4),
	DIV(CLK_DOUT_PERIC1_SPI_CAM0, "dout_peric1_spi_cam0", "gout_peric1_spi_cam0",
	    DIV_CLK_PERIC1_SPI_CAM0, 0, 4),
	DIV(CLK_DOUT_PERIC1_USI09_USI, "dout_peric1_usi09_usi", "gout_peric1_usi09_usi",
	    DIV_CLK_PERIC1_USI09_USI, 0, 4),
	DIV(CLK_DOUT_PERIC1_USI10_USI, "dout_peric1_usi10_usi", "gout_peric1_usi10_usi",
	    DIV_CLK_PERIC1_USI10_USI, 0, 4),
	DIV(CLK_DOUT_PERIC1_USI11_USI, "dout_peric1_usi11_usi", "gout_peric1_usi11_usi",
	    DIV_CLK_PERIC1_USI11_USI, 0, 4),
};

static const struct samsung_gate_clock peric1_gate_clks[] __initconst = {
	GATE(CLK_GOUT_PERIC1_AXI2APB_PERIC1P1_ACLK, "gout_peric1_axi2apb_peric1p1_aclk",
	     "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_AXI2APB_PERIC1P1_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_GPIO_PERIC1_PCLK, "gout_peric1_gpio_peric1_pclk",
	     "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_GPIO_PERIC1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_SYSREG_PERIC1_PCLK, "gout_peric1_sysreg_peric1_pclk",
	     "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_SYSREG_PERIC1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_UART_BT_PCLK, "gout_peric1_uart_bt_pclk", "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_UART_BT_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_I2C_CAM1_PCLK, "gout_peric1_i2c_cam1_pclk", "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_I2C_CAM1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_I2C_CAM2_PCLK, "gout_peric1_i2c_cam2_pclk", "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_I2C_CAM2_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_I2C_CAM3_PCLK, "gout_peric1_i2c_cam3_pclk", "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_I2C_CAM3_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI06_USI_PCLK, "gout_peric1_usi06_usi_pclk",
	     "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_USI06_USI_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI07_USI_PCLK, "gout_peric1_usi07_usi_pclk",
	     "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_USI07_USI_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI08_USI_PCLK, "gout_peric1_usi08_usi_pclk",
	     "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_USI08_USI_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_I2C_CAM0_PCLK, "gout_peric1_i2c_cam0_pclk", "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_I2C_CAM0_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_XIU_P_PERIC1_ACLK, "gout_peric1_xiu_p_peric1_aclk",
	     "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_XIU_P_PERIC1_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_AXI2APB_PERIC1P0_ACLK, "gout_peric1_axi2apb_peric1p0_aclk",
	     "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_AXI2APB_PERIC1P0_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_PERIC1_CMU_PERIC1_PCLK, "gout_peric1_peric1_cmu_peric1_pclk",
	     "mout_cmu_peric1_bus_user",
	     CLK_BLK_PERIC1_UID_PERIC1_CMU_PERIC1_IPCLKPORT_PCLK, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERIC1_SPI_CAM0_PCLK, "gout_peric1_spi_cam0_pclk", "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_SPI_CAM0_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI09_USI_PCLK, "gout_peric1_usi09_usi_pclk",
	     "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_USI09_USI_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI06_I2C_PCLK, "gout_peric1_usi06_i2c_pclk",
	     "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_USI06_I2C_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_RSTnSYNC_CLK_PERIC1_BUSP_CLK, "gout_peric1_rstnsync_clk_peric1_busp_clk",
	     "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_BUSP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI10_USI_PCLK, "gout_peric1_usi10_usi_pclk",
	     "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_USI10_USI_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI07_I2C_PCLK, "gout_peric1_usi07_i2c_pclk",
	     "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_USI07_I2C_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI08_I2C_PCLK, "gout_peric1_usi08_i2c_pclk",
	     "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_USI08_I2C_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI09_I2C_PCLK, "gout_peric1_usi09_i2c_pclk",
	     "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_USI09_I2C_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI10_I2C_PCLK, "gout_peric1_usi10_i2c_pclk",
	     "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_USI10_I2C_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_RSTnSYNC_CLK_PERIC1_USI06_USI_CLK, "gout_peric1_rstnsync_clk_peric1_usi06_usi_clk",
	     "dout_peric1_usi06_usi",
	     GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI06_USI_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_RSTnSYNC_CLK_PERIC1_USI07_USI_CLK, "gout_peric1_rstnsync_clk_peric1_usi07_usi_clk",
	     "dout_peric1_usi07_usi",
	     GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI07_USI_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_RSTnSYNC_CLK_PERIC1_USI08_USI_CLK, "gout_peric1_rstnsync_clk_peric1_usi08_usi_clk",
	     "dout_peric1_usi08_usi",
	     GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI08_USI_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_RSTnSYNC_CLK_PERIC1_USI09_USI_CLK, "gout_peric1_rstnsync_clk_peric1_usi09_usi_clk",
	     "dout_peric1_usi09_usi",
	     GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI09_USI_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_RSTnSYNC_CLK_PERIC1_USI10_USI_CLK, "gout_peric1_rstnsync_clk_peric1_usi10_usi_clk",
	     "dout_peric1_usi10_usi",
	     GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI10_USI_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_RSTnSYNC_CLK_PERIC1_USI06_I2C_CLK, "gout_peric1_rstnsync_clk_peric1_usi06_i2c_clk",
	     "dout_peric1_usi_i2c",
	     GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI06_I2C_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_RSTnSYNC_CLK_PERIC1_UART_BT_CLK, "gout_peric1_rstnsync_clk_peric1_uart_bt_clk",
	     "dout_peric1_uart_bt",
	     CLK_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_UART_BT_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_RSTnSYNC_CLK_PERIC1_SPI_CAM0_CLK, "gout_peric1_rstnsync_clk_peric1_spi_cam0_clk",
	     "dout_peric1_spi_cam0",
	     CLK_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_SPI_CAM0_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_RSTnSYNC_CLK_PERIC1_I2C_CAM0_CLK, "gout_peric1_rstnsync_clk_peric1_i2c_cam0_clk",
	     "dout_peric1_i2c_cam0",
	     CLK_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_I2C_CAM0_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_RSTnSYNC_CLK_PERIC1_I2C_CAM1_CLK, "gout_peric1_rstnsync_clk_peric1_i2c_cam1_clk",
	     "dout_peric1_i2c_cam1",
	     CLK_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_I2C_CAM1_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_RSTnSYNC_CLK_PERIC1_I2C_CAM2_CLK, "gout_peric1_rstnsync_clk_peric1_i2c_cam2_clk",
	     "dout_peric1_i2c_cam2",
	     CLK_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_I2C_CAM2_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_RSTnSYNC_CLK_PERIC1_I2C_CAM3_CLK, "gout_peric1_rstnsync_clk_peric1_i2c_cam3_clk",
	     "dout_peric1_i2c_cam3",
	     CLK_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_I2C_CAM3_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_RSTnSYNC_CLK_PERIC1_USI07_I2C_CLK, "gout_peric1_rstnsync_clk_peric1_usi07_i2c_clk",
	     "dout_peric1_usi_i2c",
	     GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI07_I2C_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_RSTnSYNC_CLK_PERIC1_USI08_I2C_CLK, "gout_peric1_rstnsync_clk_peric1_usi08_i2c_clk",
	     "dout_peric1_usi_i2c",
	     GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI08_I2C_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_RSTnSYNC_CLK_PERIC1_USI09_I2C_CLK, "gout_peric1_rstnsync_clk_peric1_usi09_i2c_clk",
	     "dout_peric1_usi_i2c",
	     GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI09_I2C_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_RSTnSYNC_CLK_PERIC1_USI10_I2C_CLK, "gout_peric1_rstnsync_clk_peric1_usi10_i2c_clk",
	     "dout_peric1_usi_i2c",
	     GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI10_I2C_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_I2C_CAM0_IPCLK, "gout_peric1_i2c_cam0_ipclk", "dout_peric1_i2c_cam0",
	     GOUT_BLK_PERIC1_UID_I2C_CAM0_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_I2C_CAM1_IPCLK, "gout_peric1_i2c_cam1_ipclk", "dout_peric1_i2c_cam1",
	     GOUT_BLK_PERIC1_UID_I2C_CAM1_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_I2C_CAM2_IPCLK, "gout_peric1_i2c_cam2_ipclk", "dout_peric1_i2c_cam2",
	     GOUT_BLK_PERIC1_UID_I2C_CAM2_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_I2C_CAM3_IPCLK, "gout_peric1_i2c_cam3_ipclk", "dout_peric1_i2c_cam3",
	     GOUT_BLK_PERIC1_UID_I2C_CAM3_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_LHM_AXI_P_PERIC1_I_CLK, "gout_peric1_lhm_axi_p_peric1_i_clk",
	     "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_LHM_AXI_P_PERIC1_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_SPI_CAM0_IPCLK, "gout_peric1_spi_cam0_ipclk", "dout_peric1_spi_cam0",
	     GOUT_BLK_PERIC1_UID_SPI_CAM0_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_UART_BT_IPCLK, "gout_peric1_uart_bt_ipclk", "dout_peric1_uart_bt",
	     GOUT_BLK_PERIC1_UID_UART_BT_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI06_I2C_IPCLK, "gout_peric1_usi06_i2c_ipclk", "dout_peric1_usi_i2c",
	     GOUT_BLK_PERIC1_UID_USI06_I2C_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI06_USI_IPCLK, "gout_peric1_usi06_usi_ipclk",
	     "dout_peric1_usi06_usi",
	     GOUT_BLK_PERIC1_UID_USI06_USI_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI07_I2C_IPCLK, "gout_peric1_usi07_i2c_ipclk", "dout_peric1_usi_i2c",
	     GOUT_BLK_PERIC1_UID_USI07_I2C_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI07_USI_IPCLK, "gout_peric1_usi07_usi_ipclk",
	     "dout_peric1_usi07_usi",
	     GOUT_BLK_PERIC1_UID_USI07_USI_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI08_I2C_IPCLK, "gout_peric1_usi08_i2c_ipclk", "dout_peric1_usi_i2c",
	     GOUT_BLK_PERIC1_UID_USI08_I2C_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI08_USI_IPCLK, "gout_peric1_usi08_usi_ipclk",
	     "dout_peric1_usi08_usi",
	     GOUT_BLK_PERIC1_UID_USI08_USI_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI09_I2C_IPCLK, "gout_peric1_usi09_i2c_ipclk", "dout_peric1_usi_i2c",
	     GOUT_BLK_PERIC1_UID_USI09_I2C_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI09_USI_IPCLK, "gout_peric1_usi09_usi_ipclk",
	     "dout_peric1_usi09_usi",
	     GOUT_BLK_PERIC1_UID_USI09_USI_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI10_I2C_IPCLK, "gout_peric1_usi10_i2c_ipclk", "dout_peric1_usi_i2c",
	     GOUT_BLK_PERIC1_UID_USI10_I2C_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI10_USI_IPCLK, "gout_peric1_usi10_usi_ipclk",
	     "dout_peric1_usi10_usi",
	     GOUT_BLK_PERIC1_UID_USI10_USI_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_UART_BT, "gout_peric1_uart_bt", "mout_cmu_peric1_ip_user",
	     GATE_CLK_PERIC1_UART_BT, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI_I2C, "gout_peric1_usi_i2c", "mout_cmu_peric1_ip_user",
	     GATE_CLK_PERIC1_USI_I2C, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI06_USI, "gout_peric1_usi06_usi", "mout_cmu_peric1_ip_user",
	     GATE_CLK_PERIC1_USI06_USI, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI07_USI, "gout_peric1_usi07_usi", "mout_cmu_peric1_ip_user",
	     GATE_CLK_PERIC1_USI07_USI, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI08_USI, "gout_peric1_usi08_usi", "mout_cmu_peric1_ip_user",
	     GATE_CLK_PERIC1_USI08_USI, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_I2C_CAM0, "gout_peric1_i2c_cam0", "mout_cmu_peric1_ip_user",
	     GATE_CLK_PERIC1_I2C_CAM0, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_I2C_CAM1, "gout_peric1_i2c_cam1", "mout_cmu_peric1_ip_user",
	     GATE_CLK_PERIC1_I2C_CAM1, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_I2C_CAM2, "gout_peric1_i2c_cam2", "mout_cmu_peric1_ip_user",
	     GATE_CLK_PERIC1_I2C_CAM2, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_I2C_CAM3, "gout_peric1_i2c_cam3", "mout_cmu_peric1_ip_user",
	     GATE_CLK_PERIC1_I2C_CAM3, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_SPI_CAM0, "gout_peric1_spi_cam0", "mout_cmu_peric1_ip_user",
	     GATE_CLK_PERIC1_SPI_CAM0, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI09_USI, "gout_peric1_usi09_usi", "mout_cmu_peric1_ip_user",
	     GATE_CLK_PERIC1_USI09_USI, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI10_USI, "gout_peric1_usi10_usi", "mout_cmu_peric1_ip_user",
	     GATE_CLK_PERIC1_USI10_USI, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI11_USI, "gout_peric1_usi11_usi", "mout_cmu_peric1_ip_user",
	     GATE_CLK_PERIC1_USI11_USI, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI11_USI_PCLK, "gout_peric1_usi11_usi_pclk",
	     "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_USI11_USI_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI11_USI_IPCLK, "gout_peric1_usi11_usi_ipclk",
	     "dout_peric1_usi11_usi",
	     GOUT_BLK_PERIC1_UID_USI11_USI_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI11_I2C_PCLK, "gout_peric1_usi11_i2c_pclk",
	     "mout_cmu_peric1_bus_user",
	     GOUT_BLK_PERIC1_UID_USI11_I2C_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI11_I2C_IPCLK, "gout_peric1_usi11_i2c_ipclk", "dout_peric1_usi_i2c",
	     GOUT_BLK_PERIC1_UID_USI11_I2C_IPCLKPORT_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_RSTnSYNC_CLK_PERIC1_USI11_USI_CLK, "gout_peric1_rstnsync_clk_peric1_usi11_usi_clk",
	     "dout_peric1_usi11_usi",
	     GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI11_USI_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIC1_RSTnSYNC_CLK_PERIC1_USI11_I2C_CLK, "gout_peric1_rstnsync_clk_peric1_usi11_i2c_clk",
	     "dout_peric1_usi_i2c",
	     GOUT_BLK_PERIC1_UID_RSTnSYNC_CLK_PERIC1_USI11_I2C_IPCLKPORT_CLK, 21, 0, 0),
};

static const struct samsung_cmu_info peric1_cmu_info __initconst = {
	.mux_clks		= peric1_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(peric1_mux_clks),
	.div_clks		= peric1_div_clks,
	.nr_div_clks		= ARRAY_SIZE(peric1_div_clks),
	.gate_clks		= peric1_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(peric1_gate_clks),
	.nr_clk_ids		= CLKS_NR_PERIC1,
	.clk_regs		= peric1_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(peric1_clk_regs),
	.qch_regs		= peric1_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(peric1_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_peric1_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &peric1_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_peric1, "samsung,exynos9810-cmu-peric1",
	       exynos9810_cmu_peric1_init);

/* ---- CMU_PERIS -------------------------------------------------------*/

/* Register Offset definitions for CMU_PERIS (0x10020000) */
#define MUX_CLKCMU_PERIS_BUS_USER			0x0100
#define MUX_CLK_PERIS_GIC			0x1000
#define CLK_BLK_PERIS_UID_PERIS_CMU_PERIS_IPCLKPORT_PCLK	0x2000
#define GOUT_BLK_PERIS_UID_AD_AXI_P_PERIS_IPCLKPORT_ACLKM	0x2008
#define GOUT_BLK_PERIS_UID_AD_AXI_P_PERIS_IPCLKPORT_ACLKS	0x200c
#define GOUT_BLK_PERIS_UID_AXI2APB_PERISP_IPCLKPORT_ACLK	0x2010
#define GOUT_BLK_PERIS_UID_BUSIF_TMU_IPCLKPORT_PCLK	0x2014
#define GOUT_BLK_PERIS_UID_GIC_IPCLKPORT_CLK			0x2018
#define GOUT_BLK_PERIS_UID_LHM_AXI_P_PERIS_IPCLKPORT_I_CLK	0x201c
#define GOUT_BLK_PERIS_UID_MCT_IPCLKPORT_PCLK			0x2020
#define GOUT_BLK_PERIS_UID_OTP_CON_BIRA_IPCLKPORT_PCLK	0x2024
#define GOUT_BLK_PERIS_UID_OTP_CON_TOP_IPCLKPORT_PCLK	0x2028
#define GOUT_BLK_PERIS_UID_RSTnSYNC_CLK_PERIS_BUSP_IPCLKPORT_CLK	0x202c
#define GOUT_BLK_PERIS_UID_RSTnSYNC_CLK_PERIS_GIC_IPCLKPORT_CLK	0x2030
#define GOUT_BLK_PERIS_UID_SYSREG_PERIS_IPCLKPORT_PCLK	0x2034
#define GOUT_BLK_PERIS_UID_WDT_CLUSTER0_IPCLKPORT_PCLK	0x2038
#define GOUT_BLK_PERIS_UID_WDT_CLUSTER1_IPCLKPORT_PCLK	0x203c
#define GOUT_BLK_PERIS_UID_XIU_P_PERIS_IPCLKPORT_ACLK	0x2040

static const unsigned long peris_clk_regs[] __initconst = {
	MUX_CLKCMU_PERIS_BUS_USER,
	MUX_CLK_PERIS_GIC,
	CLK_BLK_PERIS_UID_PERIS_CMU_PERIS_IPCLKPORT_PCLK,
	GOUT_BLK_PERIS_UID_AD_AXI_P_PERIS_IPCLKPORT_ACLKM,
	GOUT_BLK_PERIS_UID_AD_AXI_P_PERIS_IPCLKPORT_ACLKS,
	GOUT_BLK_PERIS_UID_AXI2APB_PERISP_IPCLKPORT_ACLK,
	GOUT_BLK_PERIS_UID_BUSIF_TMU_IPCLKPORT_PCLK,
	GOUT_BLK_PERIS_UID_GIC_IPCLKPORT_CLK,
	GOUT_BLK_PERIS_UID_LHM_AXI_P_PERIS_IPCLKPORT_I_CLK,
	GOUT_BLK_PERIS_UID_MCT_IPCLKPORT_PCLK,
	GOUT_BLK_PERIS_UID_OTP_CON_BIRA_IPCLKPORT_PCLK,
	GOUT_BLK_PERIS_UID_OTP_CON_TOP_IPCLKPORT_PCLK,
	GOUT_BLK_PERIS_UID_RSTnSYNC_CLK_PERIS_BUSP_IPCLKPORT_CLK,
	GOUT_BLK_PERIS_UID_RSTnSYNC_CLK_PERIS_GIC_IPCLKPORT_CLK,
	GOUT_BLK_PERIS_UID_SYSREG_PERIS_IPCLKPORT_PCLK,
	GOUT_BLK_PERIS_UID_WDT_CLUSTER0_IPCLKPORT_PCLK,
	GOUT_BLK_PERIS_UID_WDT_CLUSTER1_IPCLKPORT_PCLK,
	GOUT_BLK_PERIS_UID_XIU_P_PERIS_IPCLKPORT_ACLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long peris_qch_regs[] __initconst = {
	0x3004,	/* BUSIF_TMU_QCH */
	0x3008,	/* GIC_QCH */
	0x300c,	/* LHM_AXI_P_PERIS_QCH */
	0x3010,	/* MCT_QCH */
	0x3014,	/* OTP_CON_BIRA_QCH */
	0x3018,	/* OTP_CON_TOP_QCH */
	0x301c,	/* PERIS_CMU_PERIS_QCH */
	0x3020,	/* SYSREG_PERIS_QCH */
	0x3024,	/* WDT_CLUSTER0_QCH */
	0x3028,	/* WDT_CLUSTER1_QCH */
};

/* List of parent clocks for Muxes in CMU_PERIS */
PNAME(mout_peris_gic_p) = { "mout_cmu_peris_bus_user", "oscclk" };
PNAME(mout_cmu_peris_bus_user_p) = { "oscclk", "dout_clkcmu_peris_bus" };

static const struct samsung_mux_clock peris_mux_clks[] __initconst = {
	MUX(CLK_MOUT_PERIS_GIC, "mout_peris_gic", mout_peris_gic_p,
	    MUX_CLK_PERIS_GIC, 0, 5),
	MUX(CLK_MOUT_CMU_PERIS_BUS_USER, "mout_cmu_peris_bus_user", mout_cmu_peris_bus_user_p,
	    MUX_CLKCMU_PERIS_BUS_USER, 4, 1),
};

static const struct samsung_gate_clock peris_gate_clks[] __initconst = {
	GATE(CLK_GOUT_PERIS_AXI2APB_PERISP_ACLK, "gout_peris_axi2apb_perisp_aclk",
	     "mout_cmu_peris_bus_user",
	     GOUT_BLK_PERIS_UID_AXI2APB_PERISP_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIS_XIU_P_PERIS_ACLK, "gout_peris_xiu_p_peris_aclk",
	     "mout_cmu_peris_bus_user",
	     GOUT_BLK_PERIS_UID_XIU_P_PERIS_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIS_BUSIF_TMU_PCLK, "gout_peris_busif_tmu_pclk", "mout_cmu_peris_bus_user",
	     GOUT_BLK_PERIS_UID_BUSIF_TMU_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIS_SYSREG_PERIS_PCLK, "gout_peris_sysreg_peris_pclk",
	     "mout_cmu_peris_bus_user",
	     GOUT_BLK_PERIS_UID_SYSREG_PERIS_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIS_WDT_CLUSTER1_PCLK, "gout_peris_wdt_cluster1_pclk",
	     "mout_cmu_peris_bus_user",
	     GOUT_BLK_PERIS_UID_WDT_CLUSTER1_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIS_WDT_CLUSTER0_PCLK, "gout_peris_wdt_cluster0_pclk",
	     "mout_cmu_peris_bus_user",
	     GOUT_BLK_PERIS_UID_WDT_CLUSTER0_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIS_PERIS_CMU_PERIS_PCLK, "gout_peris_peris_cmu_peris_pclk",
	     "mout_cmu_peris_bus_user",
	     CLK_BLK_PERIS_UID_PERIS_CMU_PERIS_IPCLKPORT_PCLK, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERIS_RSTnSYNC_CLK_PERIS_BUSP_CLK, "gout_peris_rstnsync_clk_peris_busp_clk",
	     "mout_cmu_peris_bus_user",
	     GOUT_BLK_PERIS_UID_RSTnSYNC_CLK_PERIS_BUSP_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIS_RSTnSYNC_CLK_PERIS_GIC_CLK, "gout_peris_rstnsync_clk_peris_gic_clk",
	     "mout_peris_gic",
	     GOUT_BLK_PERIS_UID_RSTnSYNC_CLK_PERIS_GIC_IPCLKPORT_CLK, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERIS_AD_AXI_P_PERIS_ACLKS, "gout_peris_ad_axi_p_peris_aclks",
	     "mout_cmu_peris_bus_user",
	     GOUT_BLK_PERIS_UID_AD_AXI_P_PERIS_IPCLKPORT_ACLKS, 21, 0, 0),
	GATE(CLK_GOUT_PERIS_AD_AXI_P_PERIS_ACLKM, "gout_peris_ad_axi_p_peris_aclkm",
	     "mout_peris_gic",
	     GOUT_BLK_PERIS_UID_AD_AXI_P_PERIS_IPCLKPORT_ACLKM, 21, 0, 0),
	GATE(CLK_GOUT_PERIS_OTP_CON_BIRA_PCLK, "gout_peris_otp_con_bira_pclk",
	     "mout_cmu_peris_bus_user",
	     GOUT_BLK_PERIS_UID_OTP_CON_BIRA_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIS_GIC_CLK, "gout_peris_gic_clk", "mout_peris_gic",
	     GOUT_BLK_PERIS_UID_GIC_IPCLKPORT_CLK, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERIS_LHM_AXI_P_PERIS_I_CLK, "gout_peris_lhm_axi_p_peris_i_clk",
	     "mout_cmu_peris_bus_user",
	     GOUT_BLK_PERIS_UID_LHM_AXI_P_PERIS_IPCLKPORT_I_CLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIS_MCT_PCLK, "gout_peris_mct_pclk", "mout_cmu_peris_bus_user",
	     GOUT_BLK_PERIS_UID_MCT_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PERIS_OTP_CON_TOP_PCLK, "gout_peris_otp_con_top_pclk",
	     "mout_cmu_peris_bus_user",
	     GOUT_BLK_PERIS_UID_OTP_CON_TOP_IPCLKPORT_PCLK, 21, 0, 0),
};

static const struct samsung_cmu_info peris_cmu_info __initconst = {
	.mux_clks		= peris_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(peris_mux_clks),
	.gate_clks		= peris_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(peris_gate_clks),
	.nr_clk_ids		= CLKS_NR_PERIS,
	.clk_regs		= peris_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(peris_clk_regs),
	.qch_regs		= peris_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(peris_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_peris_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &peris_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_peris, "samsung,exynos9810-cmu-peris",
	       exynos9810_cmu_peris_init);

/* ---- CMU_S2D ---------------------------------------------------------*/

/* Register Offset definitions for CMU_S2D (0x14400000) */
#define PLL_LOCKTIME_PLL_MIF_S2D_PLL_LOCK_TIME			0x0000
#define PLL_CON0_PLL_MIF_S2D_ENABLE			0x0100
#define CLKCMU_MIF_DDRPHY2X_S2D			0x1000
#define MUX_CLK_S2D_CORE			0x1004
#define CLK_BLK_S2D_UID_S2D_CMU_S2D_IPCLKPORT_PCLK	0x2004
#define GOUT_BLK_S2D_UID_RSTnSYNC_CLK_S2D_CORE_IPCLKPORT_CLK	0x200c

static const unsigned long s2d_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_MIF_S2D_PLL_LOCK_TIME,
	PLL_CON0_PLL_MIF_S2D_ENABLE,
	CLKCMU_MIF_DDRPHY2X_S2D,
	MUX_CLK_S2D_CORE,
	CLK_BLK_S2D_UID_S2D_CMU_S2D_IPCLKPORT_PCLK,
	GOUT_BLK_S2D_UID_RSTnSYNC_CLK_S2D_CORE_IPCLKPORT_CLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long s2d_qch_regs[] __initconst = {
	0x3004,	/* S2D_CMU_S2D_QCH */
};

static const struct samsung_pll_clock s2d_pll_clks[] __initconst = {
	PLL(pll_1016x, CLK_FOUT_MIF_S2D, "fout_mif_s2d", "oscclk",
	    PLL_LOCKTIME_PLL_MIF_S2D_PLL_LOCK_TIME, PLL_CON0_PLL_MIF_S2D_ENABLE, NULL),
};

/* List of parent clocks for Muxes in CMU_S2D */
PNAME(mout_cmu_mif_ddrphy2x_s2d_p) = { "fout_mif_s2d", "oscclk" };
PNAME(mout_s2d_core_p) = { "oscclk", "UNRESOLVED_CLK_MIF_BUSD_S2D" };

static const struct samsung_mux_clock s2d_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMU_MIF_DDRPHY2X_S2D, "mout_cmu_mif_ddrphy2x_s2d", mout_cmu_mif_ddrphy2x_s2d_p,
	    CLKCMU_MIF_DDRPHY2X_S2D, 0, 1),
	MUX(CLK_MOUT_S2D_CORE, "mout_s2d_core", mout_s2d_core_p,
	    MUX_CLK_S2D_CORE, 0, 1),
};

static const struct samsung_gate_clock s2d_gate_clks[] __initconst = {
	GATE(CLK_GOUT_S2D_S2D_CMU_S2D_PCLK, "gout_s2d_s2d_cmu_s2d_pclk", "mout_s2d_core",
	     CLK_BLK_S2D_UID_S2D_CMU_S2D_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_S2D_RSTnSYNC_CLK_S2D_CORE_CLK, "gout_s2d_rstnsync_clk_s2d_core_clk",
	     "mout_s2d_core",
	     GOUT_BLK_S2D_UID_RSTnSYNC_CLK_S2D_CORE_IPCLKPORT_CLK, 21, 0, 0),
};

static const struct samsung_cmu_info s2d_cmu_info __initconst = {
	.pll_clks		= s2d_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(s2d_pll_clks),
	.mux_clks		= s2d_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(s2d_mux_clks),
	.gate_clks		= s2d_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(s2d_gate_clks),
	.nr_clk_ids		= CLKS_NR_S2D,
	.clk_regs		= s2d_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(s2d_clk_regs),
	.qch_regs		= s2d_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(s2d_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_s2d_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &s2d_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_s2d, "samsung,exynos9810-cmu-s2d",
	       exynos9810_cmu_s2d_init);

/* ---- CMU_VTS ---------------------------------------------------------*/

/* Register Offset definitions for CMU_VTS (0x13800000) */
#define MUX_CLKCMU_VTS_BUS_USER			0x0100
#define MUX_CLKCMU_VTS_DLL_USER			0x0120
#define MUX_CLK_VTS_BUS			0x1000
#define DIV_CLK_VTS_BUS			0x1800
#define DIV_CLK_VTS_DMIC			0x1804
#define DIV_CLK_VTS_DMIC_DIV2			0x1808
#define DIV_CLK_VTS_DMIC_IF			0x180c
#define CLK_BLK_VTS_UID_DMIC_IF_IPCLKPORT_DMIC_IF_DIV2_CLK	0x2000
#define CLK_BLK_VTS_UID_u_DMIC_CLK_MUX_IPCLKPORT_D0	0x2008
#define GOUT_BLK_VTS_UID_DMIC_IF_IPCLKPORT_DMIC_IF_CLK	0x2038
#define GOUT_BLK_VTS_UID_RSTnSYNC_CLK_VTS_DMIC_IF_IPCLKPORT_CLK	0x207c
#define GOUT_BLK_VTS_UID_RSTnSYNC_CLK_VTS_DMIC_IPCLKPORT_CLK	0x2080

static const unsigned long vts_clk_regs[] __initconst = {
	MUX_CLKCMU_VTS_BUS_USER,
	MUX_CLKCMU_VTS_DLL_USER,
	MUX_CLK_VTS_BUS,
	DIV_CLK_VTS_BUS,
	DIV_CLK_VTS_DMIC,
	DIV_CLK_VTS_DMIC_DIV2,
	DIV_CLK_VTS_DMIC_IF,
	CLK_BLK_VTS_UID_DMIC_IF_IPCLKPORT_DMIC_IF_DIV2_CLK,
	CLK_BLK_VTS_UID_u_DMIC_CLK_MUX_IPCLKPORT_D0,
	GOUT_BLK_VTS_UID_DMIC_IF_IPCLKPORT_DMIC_IF_CLK,
	GOUT_BLK_VTS_UID_RSTnSYNC_CLK_VTS_DMIC_IF_IPCLKPORT_CLK,
	GOUT_BLK_VTS_UID_RSTnSYNC_CLK_VTS_DMIC_IPCLKPORT_CLK,
};

/* Legacy Q-Channel HWACG control registers (see clk-exynos-arm64.c) */
static const unsigned long vts_qch_regs[] __initconst = {
	0x3000,	/* DMIC_IF_QCH_DMIC_CLK */
	0x3008,	/* U_DMIC_CLK_MUX_QCH */
	0x3014,	/* AHB_BUSMATRIX_QCH_SYS */
	0x3018,	/* ASYNCAHBM_VTS_QCH */
	0x301c,	/* CORTEXM4INTEGRATION_QCH_CPU */
	0x3020,	/* DMIC_AHB0_QCH_PCLK */
	0x3024,	/* DMIC_AHB1_QCH_PCLK */
	0x3028,	/* DMIC_IF_QCH_PCLK */
	0x302c,	/* GPIO_VTS_QCH */
	0x3030,	/* HWACG_SYS_DMIC0_QCH */
	0x3034,	/* HWACG_SYS_DMIC1_QCH */
	0x303c,	/* MAILBOX_VTS2CHUB_QCH */
	0x3040,	/* SYSREG_VTS_QCH */
	0x3044,	/* VTS_CMU_VTS_QCH */
	0x3048,	/* WDT_VTS_QCH */
};

static const struct samsung_fixed_rate_clock vts_fixed_clks[] __initconst = {
	FRATE(CLK_CLK_RCO_VTS, "clk_rco_vts", NULL, 0, 49152000),
};

/* List of parent clocks for Muxes in CMU_VTS */
PNAME(mout_vts_bus_p) = { "clk_rco_vts", "mout_cmu_vts_bus_user", "mout_cmu_vts_dll_user", "mout_cmu_vts_dll_user" };
PNAME(mout_cmu_vts_bus_user_p) = { "oscclk", "dout_clkcmu_vts_bus" };
PNAME(mout_cmu_vts_dll_user_p) = { "oscclk", "dout_clkcmu_apm_dll_vts" };

static const struct samsung_mux_clock vts_mux_clks[] __initconst = {
	MUX(CLK_MOUT_VTS_BUS, "mout_vts_bus", mout_vts_bus_p,
	    MUX_CLK_VTS_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_VTS_BUS_USER, "mout_cmu_vts_bus_user", mout_cmu_vts_bus_user_p,
	    MUX_CLKCMU_VTS_BUS_USER, 4, 1),
	MUX(CLK_MOUT_CMU_VTS_DLL_USER, "mout_cmu_vts_dll_user", mout_cmu_vts_dll_user_p,
	    MUX_CLKCMU_VTS_DLL_USER, 4, 1),
};

static const struct samsung_div_clock vts_div_clks[] __initconst = {
	DIV(CLK_DOUT_VTS_DMIC_IF, "dout_vts_dmic_if", "clk_rco_vts",
	    DIV_CLK_VTS_DMIC_IF, 0, 6),
	DIV(CLK_DOUT_VTS_DMIC, "dout_vts_dmic", "dout_vts_dmic_if",
	    DIV_CLK_VTS_DMIC, 0, 1),
	DIV(CLK_DOUT_VTS_DMIC_DIV2, "dout_vts_dmic_div2", "dout_vts_dmic_if",
	    DIV_CLK_VTS_DMIC_DIV2, 0, 1),
	DIV(CLK_DOUT_VTS_BUS, "dout_vts_bus", "mout_vts_bus",
	    DIV_CLK_VTS_BUS, 0, 3),
};

static const struct samsung_gate_clock vts_gate_clks[] __initconst = {
	GATE(CLK_GOUT_VTS_RSTnSYNC_CLK_VTS_DMIC_IF_CLK, "gout_vts_rstnsync_clk_vts_dmic_if_clk",
	     "dout_vts_dmic_if",
	     GOUT_BLK_VTS_UID_RSTnSYNC_CLK_VTS_DMIC_IF_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VTS_DMIC_IF_DMIC_IF_CLK, "gout_vts_dmic_if_dmic_if_clk", "dout_vts_dmic_if",
	     GOUT_BLK_VTS_UID_DMIC_IF_IPCLKPORT_DMIC_IF_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VTS_DMIC_IF_DMIC_IF_DIV2_CLK, "gout_vts_dmic_if_dmic_if_div2_clk",
	     "dout_vts_dmic_div2",
	     CLK_BLK_VTS_UID_DMIC_IF_IPCLKPORT_DMIC_IF_DIV2_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VTS_RSTnSYNC_CLK_VTS_DMIC_CLK, "gout_vts_rstnsync_clk_vts_dmic_clk",
	     "dout_vts_dmic_if",
	     GOUT_BLK_VTS_UID_RSTnSYNC_CLK_VTS_DMIC_IPCLKPORT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_VTS_u_DMIC_CLK_MUX_D0, "gout_vts_u_dmic_clk_mux_d0", "dout_vts_dmic",
	     CLK_BLK_VTS_UID_u_DMIC_CLK_MUX_IPCLKPORT_D0, 21, 0, 0),
};

static const struct samsung_cmu_info vts_cmu_info __initconst = {
	.mux_clks		= vts_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(vts_mux_clks),
	.div_clks		= vts_div_clks,
	.nr_div_clks		= ARRAY_SIZE(vts_div_clks),
	.gate_clks		= vts_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(vts_gate_clks),
	.fixed_clks		= vts_fixed_clks,
	.nr_fixed_clks		= ARRAY_SIZE(vts_fixed_clks),
	.nr_clk_ids		= CLKS_NR_VTS,
	.clk_regs		= vts_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(vts_clk_regs),
	.qch_regs		= vts_qch_regs,
	.nr_qch_regs		= ARRAY_SIZE(vts_qch_regs),
	.clk_name		= "bus",
};

static void __init exynos9810_cmu_vts_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &vts_cmu_info);
}

CLK_OF_DECLARE(exynos9810_cmu_vts, "samsung,exynos9810-cmu-vts",
	       exynos9810_cmu_vts_init);

