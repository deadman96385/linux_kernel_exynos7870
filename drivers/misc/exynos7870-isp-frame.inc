/* Experimental fixed-size RAW16 -> RGB888 single-frame interface.
 * Root-only; all mappings belong to the native processing IOMMU domain.
 * ioctl 0x4901 submits once. Read returns output only after AXI reset/idle.
 * No mmap: userspace can never access allocations while hardware owns them.
 */
#include <linux/bitops.h>
#define ISP_INPUT_BYTES (4144U * 3106U * 2U)
#define ISP_OUTPUT_BYTES (4144U * 3106U * 3U)
#define ISP_STATS_BYTES (64U * 64U * 28U)
/* Stock handlers 1683a4/16721c: input corruption and FIMC start error. */
#define ISP_FRAME_ERRORS (BIT(7) | BIT(9))
/* Vendor ISR 16a6dc identifies these faults in all four channel groups.
 * This exclusive owner must not report success with any DMA channel fault. */
#define ISP_DMA_ERRORS 0xccccccccU
/* Comparison switch for the original 1a9cc4 matched reset tail. */
static bool isp_stock_reset_tail = true;
module_param_named(stock_reset_tail, isp_stock_reset_tail, bool, 0644);
MODULE_PARM_DESC(stock_reset_tail, "Apply matched thumbnail/MSM resets to both banks after idle");
/* Promoted to default-on in R42: 132 candidate-enabled jobs across single-
 * client and the R32 two-client stress scenario (the only one that has ever
 * produced 0x8c) all returned DMA0/success with exact RGB/statistics; three
 * disabled-regression runs still reproduce baseline DMA8/EIO unchanged.
 * Register semantics on silicon remain unconfirmed; keep the switch to allow
 * reverting for diagnostics. See ISP310-R42-TRANSPORT-FIX.md. */
static bool isp_stock_dma_setup = true;
module_param_named(stock_dma_setup, isp_stock_dma_setup, bool, 0644);
MODULE_PARM_DESC(stock_dma_setup, "Write stock memory-input setup value at 3154");
static bool isp_configuration_readback;
module_param_named(configuration_readback, isp_configuration_readback, bool, 0644);
MODULE_PARM_DESC(configuration_readback, "Read back each programmed configuration value for detailed audit");
/* Opt-in, read-only diagnostic for the still-unlocated AF-statistics register
 * search (see ISP310-AFSTAT-REGISTRY-LOAD.md). Bounded to 0x3000..0x361c,
 * strictly inside the same densely-populated bank this driver already reads
 * and writes throughout a normal job (0x3008..0x3614 are all already real,
 * known registers) - no address outside that already-proven-safe window is
 * ever touched. Default off; changes nothing when disabled. */
#define ISP_REGISTER_SCAN_WORDS 0x188
static bool isp_af_register_scan;
module_param_named(af_register_scan, isp_af_register_scan, bool, 0644);
MODULE_PARM_DESC(af_register_scan, "Read-only dump of 0x3000-0x361c after each job, for AF-statistics register search");
struct isp_frame {
	struct exynos7870_isp *isp;
	struct device *dev;
	struct mutex lock;
	void *input, *output;
	void *statistics;
	dma_addr_t statistics_dma;
	size_t statistics_downloaded;
	u32 statistics_enabled, read_plane, statistics_changed;
	dma_addr_t input_dma, output_dma;
	size_t uploaded, downloaded;
	bool submitted, quiescent;
	bool configuration_readback, stock_dma_setup; /* Snapshotted once per job. */
	u32 status[8]; /* IRQ0, IRQ1, IRQ2, DMA, idle, stop-ack, result, changed */
	/* ioctl I/3: version, baseline IRQ0/1/2/DMA, first event times in us
	 * (0xffffffff = unseen), pre-reset IRQ0/1/2/DMA, stop result, polls. */
	u32 diagnostic[16];
	u32 phase; /* Bounded SMIA phase experiment, 0..3; default is r16's 1. */
	u32 crop_mode; /* 0 full; 1 even margins; 2 stock RGGB margin translation. */
	u32 layout[4]; /* output width, height, stride, bytes; ioctl I/5 */
	u32 launch_mode; /* 0 baseline, 1 delay, 2 stock order, 3 suppress top start */
	u32 launch[32]; /* version, mode, five IRQ0/1/2/DMA/time snapshots, readbacks */
	u32 wbg_mode; /* 0 bypass; 1 unity; 2 double; 3 half; 4..7 channel double */
	u32 wbg_gains[3]; /* I/15: bounded Q10 R/G/B; selects internal mode 8. */
	u32 gamma_mode; /* 0 bypass; 1 identity; 2 half; 3 full candidate strength */
	u32 audit[8]; /* v1 audited; v2 skips reads and reports skipped count in word7. */
	u64 timing[8]; /* I/17: version,total,configure,prepare,launch/wait,stop,scan ns,valid-mask */
	u32 register_scan[ISP_REGISTER_SCAN_WORDS]; /* I/19: 0x3000..0x361c, valid iff word[0]!=0 */
	bool register_scan_valid;
};

/* Bulk copy bounded blocks from coherent memory, then count cached bytes.
 * Exact count and tail handling are preserved; DMA has already stopped. */
static u32 isp_frame_changed(const void *buffer, size_t bytes)
{
	u64 words[64];
	u32 changed = 0;
	size_t offset = 0, i;

	while (offset < bytes) {
		size_t chunk = bytes - offset;

		if (chunk > sizeof(words))
			chunk = sizeof(words);
		memcpy(words, (const u8 *)buffer + offset, chunk);
		for (i = 0; i < chunk / 8; i++) {
			u64 value = words[i] ^ 0xa5a5a5a5a5a5a5a5ULL;
			u64 nonzero = (((value & 0x7f7f7f7f7f7f7f7fULL) +
					0x7f7f7f7f7f7f7f7fULL) | value) &
					0x8080808080808080ULL;

			changed += hweight64(nonzero);
		}
		for (i *= 8; i < chunk; i++)
			changed += ((const u8 *)words)[i] != 0xa5;
		offset += chunk;
	}
	return changed;
}

static int isp_frame_check(struct isp_frame *f, u32 offset, u32 expected, u32 stage)
{
	u32 actual;

	if (!f->configuration_readback) {
		f->audit[0] = 2;
		f->audit[7]++;
		return 0;
	}
	actual = readl(f->isp->regs + offset);

	f->audit[0] = 1;
	f->audit[1]++;
	if (actual == expected)
		return 0;
	if (!f->audit[2]++) {
		f->audit[3] = offset;
		f->audit[4] = expected;
		f->audit[5] = actual;
		f->audit[6] = stage;
	}
	f->status[6] = -EIO;
	return -EIO;
}

static void isp_frame_input_setup(struct isp_frame *f)
{
	/* Original 1a91e8 memory-input branch, before 1fbef0 input configure.
	 * Semantics are unresolved; do not reinterpret or mask DMA status. */
	if (f->stock_dma_setup)
		writel(0xf3f3f3f3, f->isp->regs + 0x3154);
}

static void isp_frame_launch_sample(struct isp_frame *f, unsigned int stage,
				    ktime_t started)
{
	u32 *s = &f->launch[2 + stage * 5];
	void __iomem *r = f->isp->regs;
	unsigned int i;

	s[0] = readl(r + 0x3604);
	s[1] = readl(r + 0x360c);
	s[2] = readl(r + 0x3614);
	s[3] = readl(r + 0x3150);
	s[4] = (u32)ktime_us_delta(ktime_get(), started);
	for (i = 0; i < 4; i++)
		f->status[i] |= s[i];
}

static void isp_frame_sample(struct isp_frame *f)
{
	void __iomem *r = f->isp->regs;

	f->status[0] |= readl(r + 0x3604);
	f->status[1] |= readl(r + 0x360c);
	f->status[2] |= readl(r + 0x3614);
	f->status[3] |= readl(r + 0x3150);
}

static int isp_frame_stop(struct isp_frame *f)
{
	void __iomem *r = f->isp->regs;
	u32 v;
	int ret;

	writel(readl(r + 0x310c) & ~1U, r + 0x310c);
	writel(0, r + 0x312c);
	writel(0, r + 0x3148);
	writel(2, r + 0x3200);
	ret = readl_poll_timeout(r + 0x3204, v, v != 0, 100, 100000);
	f->status[5] = v;
	/* Reset may clear evidence. Sample after the stop request and ACK wait. */
	if (f->submitted) {
		isp_frame_sample(f);
		memcpy(&f->diagnostic[10], f->status, 4 * sizeof(u32));
	}
	if (ret)
		return ret;
	writel(1, r + 0x3208);
	ret = readl_poll_timeout(r + 0x3208, v, v == 0, 100, 100000);
	if (ret)
		return ret;
	ret = readl_poll_timeout(r + 0x3020, v, v == 1, 100, 100000);
	f->status[4] = v;
	if (!ret) {
		if (READ_ONCE(isp_stock_reset_tail)) {
			void __iomem *b = f->isp->regs_b;

			/* Original 1a9cc4 -> 20103c/1b88f4, matched R15/R23 IDs.
			 * Only after reset-clear and idle; no extra readback loop. */
			writel(1, r + 0x3028);
			writel(1, r + 0x3024);
			writel(1, r + 0x3030);
			writel(1, r + 0x3008);
			writel(1, b + 0x3008);
			writel(0, r + 0x4850);
			writel(0, r + 0x4808);
			writel(1, r + 0x4500);
			writel(0, b + 0x4850);
			writel(0, b + 0x4808);
			writel(1, b + 0x4500);
		}
		f->quiescent = true;
	}
	return ret;
}

static int isp_frame_execute(struct isp_frame *f)
{
	static const struct { u32 offset, value; } wbg_tests[][17] = {
#include "wbg-test-registers.inc"
	};
	static const struct { u32 offset, value; } gamma_tests[][35] = {
#include "rgb-gamma-test-registers.inc"
	};
	static const struct { u32 offset, value; } processing[] = {
		{0x4008, 1}, {0x400c, 0}, {0x4030, 4144}, {0x4034, 3106},
		{0x4104, 0}, {0x4108, 0}, {0x410c, 4144}, {0x4110, 3106},
		{0x4100, 0}, {0x4204, 0x3ff0}, {0x4200, 0},
#include "isp310-processing-bypass.inc"
		{0x5000, 1}, {0x5400, 1}, {0x7100, 1}, {0x7400, 1},
		{0x7500, 1}, {0x7a00, 1}, {0xc054, 1},
#include "dmsc-imx258-registers.inc"
		{0xc300, 0}, {0xc304, 0}, {0xc308, 1}, {0xc30c, 2},
	};
	static const struct { u32 offset, value; } dma[] = {
		{0x3104, 0x1002}, {0x3140, 0x400}, {0x3144, 1},
		{0x3130, 0}, {0x3134, 0x0c221030}, {0x3300, 0}, {0x3320, 0x0c223090},
		{0x3124, 0x460}, {0x3128, 3}, {0x3114, 0x0c221030},
		{0x311c, 0x0c221030}, {0x3118, 0x320000}, {0x3120, 0},
		{0x310c, 0x10001010}, {0x3110, 0x100010}, {0x3218, 0},
		{0x32a0, 0}, {0x32c0, 0x0c222060}, {0x3108, 1},
	};
	/* Matched 1fbef0 stores in execution order. Mode 2 deliberately delays
	 * input configuration until output has been armed, as in 1a91e8.
	 * Values and allocations are unchanged from the qualified baseline.
	 */
	static const struct { u32 offset, value; } input_stock[] = {
		{0x310c, 0x10001010}, {0x3110, 0x100010},
		{0x3114, 0x0c221030}, {0x3118, 0x320000},
		{0x311c, 0x0c221030}, {0x3120, 0},
		{0x3124, 0x460}, {0x3128, 3}, {0x3218, 0},
		{0x32a0, 0}, {0x32c0, 0x0c222060}, {0x3108, 1},
	};
	void __iomem *r = f->isp->regs;
	unsigned int i;
	ktime_t deadline, started, phase_started = ktime_get();
	int ret = -ETIMEDOUT, stop_ret;

	if (f->submitted || f->uploaded != ISP_INPUT_BYTES)
		return -EINVAL;
	memset(f->audit, 0, sizeof(f->audit));
	f->configuration_readback = READ_ONCE(isp_configuration_readback);
	f->stock_dma_setup = READ_ONCE(isp_stock_dma_setup);
	if (readl(r + 0x30f0) != 0x030a0000 || readl(r + 0x30f4) != 0xd ||
	    readl(r + 0x31f4) != 0xb || (readl(r + 0x310c) & 1) ||
	    readl(r + 0x312c) || readl(r + 0x3148) || readl(r + 0x3020) != 1)
		return -EBUSY;
	if (f->statistics_enabled &&
	    (readl(r + 0x48f4) != 0xe || readl(r + 0x48f8) != 0xd3060e31))
		return -ENODEV;
	/* Begin from the qualified reset state, before programming allocations. */
	stop_ret = isp_frame_stop(f);
	if (stop_ret)
		return stop_ret;
	writel(0, r + 0x3000);
	writel(2, r + 0x3004);
	writel(1, r + 0x3028);
	writel(1, r + 0x3024);
	writel(1, r + 0x3030);
	writel(1, r + 0x3008);
	writel(1, f->isp->regs_b + 0x3008);
	for (i = 0; i < ARRAY_SIZE(processing); i++)
		writel(processing[i].value, r + processing[i].offset);
	if (f->wbg_mode) {
		u32 values[17];
		unsigned int preset = f->wbg_mode == 8 ? 0 : f->wbg_mode - 1;

		/* Fixed register layout; custom gains follow stock R/G/G/B expansion
		 * and 12-bit mantissa/shift encoding at unity Q8 scale. */
		for (i = 0; i < ARRAY_SIZE(wbg_tests[0]); i++) {
			u32 offset = wbg_tests[preset][i].offset;

			values[i] = wbg_tests[preset][i].value;
			if (f->wbg_mode == 8 &&
			    ((offset >= 0x5008 && offset <= 0x5014) ||
			     (offset >= 0x5028 && offset <= 0x5034))) {
				u32 channel = ((offset - 0x5008) & 0x1f) / 4;
				u32 gain = f->wbg_gains[channel == 0 ? 0 : channel == 3 ? 2 : 1] * 2;
				u32 shift = 0;

				while (gain > 0xfff) {
					gain >>= 1;
					shift++;
				}
				values[i] = offset >= 0x5028 ? shift : gain;
			}
			writel(values[i], r + offset);
		}
		/* Include WBG in the same detailed audit as gamma/geometry. */
		for (i = 0; i < ARRAY_SIZE(wbg_tests[0]); i++)
			if (isp_frame_check(f, wbg_tests[preset][i].offset, values[i], 7))
				return -EIO;
	}
	if (f->gamma_mode) {
		for (i = 0; i < ARRAY_SIZE(gamma_tests[0]); i++)
			writel(gamma_tests[f->gamma_mode - 1][i].value,
			       r + gamma_tests[f->gamma_mode - 1][i].offset);
		for (i = 0; i < ARRAY_SIZE(gamma_tests[0]); i++)
			if (isp_frame_check(f, gamma_tests[f->gamma_mode - 1][i].offset,
					    gamma_tests[f->gamma_mode - 1][i].value, 1))
				return -EIO;
	}
	writel(f->phase, r + 0x4008);
	/* First seven entries configure the output route and DMA. */
	for (i = 0; i < (f->launch_mode == 2 ? 7 : ARRAY_SIZE(dma)); i++) {
		if (i == 7)
			isp_frame_input_setup(f);
		writel(dma[i].value, r + dma[i].offset);
	}
	if (f->crop_mode) {
		/* DDK 17d480/15f3f4 translates RGGB left/right margins by -1/+1.
		 * Eight-pixel initial margins are a bounded diagnostic rectangle,
		 * not yet the recovered stock sensor mode's exact crop. */
		writel(f->crop_mode == 2 ? 7 : 8, r + 0x4104);
		writel(8, r + 0x4108);
		writel(f->layout[0], r + 0x410c);
		writel(f->layout[1], r + 0x4110);
		writel((f->layout[1] << 16) | f->layout[0], r + 0x3134);
		writel((f->layout[1] << 16) | f->layout[2], r + 0x3320);
	}
	writel(0, r + 0x3010);
	writel(0, r + 0x3014);
	/* Stock generation-2 memory input and A-bank selection. These controls
	 * were previously written without readback; reject mismatch before DMA. */
	{
		const struct { u32 offset, value; } top[] = {
			{0x3000, 0}, {0x3004, 2}, {0x3010, 0}, {0x3014, 0},
			{0x3028, 1}, {0x3024, 1}, {0x3030, 1},
		};

		for (i = 0; i < ARRAY_SIZE(top); i++)
			if (isp_frame_check(f, top[i].offset, top[i].value, 8))
				return -EIO;
	}
	/* Read back programmed configuration before enabling producers. In mode
	 * 2 input configuration is intentionally deferred; check it below.
	 * Any mismatch is retained for userspace, never silently tolerated. */
	for (i = 0; i < (f->launch_mode == 2 ? 7 : ARRAY_SIZE(dma)); i++) {
		u32 expected = dma[i].value;

		if (dma[i].offset == 0x3134)
			expected = (f->layout[1] << 16) | f->layout[0];
		if (dma[i].offset == 0x3320)
			expected = (f->layout[1] << 16) | f->layout[2];
		if (isp_frame_check(f, dma[i].offset, expected, 2))
			return -EIO;
	}
	/* Poll status with every interrupt masked; no unhandled IRQ delivery. */
	writel(0x3ff, r + 0x3608);
	writel(7, r + 0x3610);
	writel(~0U, r + 0x3618);
	writel(readl(r + 0x3604), r + 0x3604);
	writel(readl(r + 0x360c), r + 0x360c);
	writel(readl(r + 0x3614), r + 0x3614);
	f->diagnostic[0] = 1;
	f->diagnostic[1] = readl(r + 0x3604);
	f->diagnostic[2] = readl(r + 0x360c);
	f->diagnostic[3] = readl(r + 0x3614);
	f->diagnostic[4] = readl(r + 0x3150);
	/* Reject stale status; never attribute a previous frame's events to this one. */
	if (f->diagnostic[1] || f->diagnostic[2] || f->diagnostic[3] ||
	    (f->diagnostic[4] & ISP_DMA_ERRORS)) {
		f->status[6] = -EIO;
		return -EIO;
	}
	for (i = 5; i <= 9; i++)
		f->diagnostic[i] = ~0U;
	f->timing[2] = ktime_to_ns(ktime_sub(ktime_get(), phase_started));
	f->timing[7] |= BIT(0);
	phase_started = ktime_get();
	memset(f->output, 0xa5, ISP_OUTPUT_BYTES);
	memset(f->statistics, 0xa5, ISP_STATS_BYTES);
	dma_wmb();
	f->timing[3] = ktime_to_ns(ktime_sub(ktime_get(), phase_started));
	f->timing[7] |= BIT(1);
	f->submitted = true;
	f->quiescent = false;
	started = ktime_get();
	f->launch[0] = 1;
	f->launch[1] = f->launch_mode;
	f->launch[27] = readl(r + 0x310c);
	f->launch[28] = readl(r + 0x3118);
	f->launch[29] = readl(r + 0x3200);
	f->launch[30] = readl(r + 0x3204);
	f->launch[31] = readl(r + 0x3020);
	isp_frame_launch_sample(f, 0, started);
	if (f->statistics_enabled) {
		/* Executed stock geometry/writer sequence; fixed diagnostic grid.
		 * Include the allocation only after submission owns the stop path. */
		const struct { u32 offset, value; } stats[] = {
			{0x4850, 0}, {0x4808, 0},
			{0x4800, 0}, {0x4838, 0x888},
			{0x4844, 0x40}, {0x4848, 0x40}, {0x484c, 0x40},
			{0x4800, 0}, {0x480c, 0x4040},
			{0x4810, f->crop_mode ? 0x80010 : 0x100018},
			{0x4814, 0x3040}, {0x4818, 0x3040},
			{0x314c, 224}, {0x3268, lower_32_bits(f->statistics_dma)},
			{0x3304, ISP_STATS_BYTES},
		};

		for (i = 0; i < ARRAY_SIZE(stats); i++) {
			writel(stats[i].value, r + stats[i].offset);
			if (isp_frame_check(f, stats[i].offset, stats[i].value, 6)) {
				ret = -EIO;
				goto stop;
			}
		}
		writel(1, r + 0x3148);
		if (isp_frame_check(f, 0x3148, 1, 6)) {
			ret = -EIO;
			goto stop;
		}
	}
	writel(lower_32_bits(f->output_dma), r + 0x3260);
	if (isp_frame_check(f, 0x3260, lower_32_bits(f->output_dma), 3)) {
		ret = -EIO;
		goto stop;
	}
	writel(3, r + 0x312c);
	isp_frame_launch_sample(f, 1, started);
	if (f->launch_mode == 2) {
		isp_frame_input_setup(f);
		writel(readl(r + 0x310c) & ~1U, r + 0x310c);
		for (i = 0; i < ARRAY_SIZE(input_stock); i++)
			writel(input_stock[i].value, r + input_stock[i].offset);
		for (i = 0; i < ARRAY_SIZE(input_stock); i++)
			if (isp_frame_check(f, input_stock[i].offset, input_stock[i].value, 4)) {
				ret = -EIO;
				goto stop;
			}
	}
	writel(lower_32_bits(f->input_dma), r + 0x3220);
	if (isp_frame_check(f, 0x3220, lower_32_bits(f->input_dma), 5)) {
		ret = -EIO;
		goto stop;
	}
	writel(readl(r + 0x310c) | 1, r + 0x310c);
	isp_frame_launch_sample(f, 2, started);
	if (f->launch_mode == 1)
		udelay(100);
	isp_frame_launch_sample(f, 3, started);
	/* Mode 3 is an explicit timeout/recovery test, never a success path. */
	if (f->launch_mode != 3)
		writel(3, r + 0x3000);
	isp_frame_launch_sample(f, 4, started);
	deadline = ktime_add_ms(started, 1000);
	do {
		u32 elapsed = (u32)ktime_us_delta(ktime_get(), started);

		isp_frame_sample(f);
		f->diagnostic[15]++;
		if ((f->status[0] & BIT(2)) && f->diagnostic[5] == ~0U)
			f->diagnostic[5] = elapsed;
		if ((f->status[2] & BIT(1)) && f->diagnostic[6] == ~0U)
			f->diagnostic[6] = elapsed;
		if ((f->status[3] & ISP_DMA_ERRORS) && f->diagnostic[7] == ~0U)
			f->diagnostic[7] = elapsed;
		if ((f->status[3] & BIT(3)) && f->diagnostic[8] == ~0U)
			f->diagnostic[8] = elapsed;
		if ((f->status[0] & BIT(2)) && (f->status[2] & BIT(1))) {
			ret = 0;
			break;
		}
		usleep_range(100, 200);
	} while (ktime_before(ktime_get(), deadline));
stop:
	f->timing[4] = ktime_to_ns(ktime_sub(ktime_get(), started));
	f->timing[7] |= BIT(2);
	f->diagnostic[9] = (u32)ktime_us_delta(ktime_get(), started);
	if (READ_ONCE(isp_af_register_scan)) {
		/* Still fully powered here, same as every other diagnostic read
		 * above; isp_frame_stop() below is what powers things down. */
		for (i = 0; i < ISP_REGISTER_SCAN_WORDS; i++)
			f->register_scan[i] = readl(r + 0x3000 + i * 4);
		f->register_scan_valid = true;
	} else {
		f->register_scan_valid = false;
	}
	phase_started = ktime_get();
	stop_ret = isp_frame_stop(f);
	f->timing[5] = ktime_to_ns(ktime_sub(ktime_get(), phase_started));
	f->timing[7] |= BIT(3);
	f->diagnostic[14] = stop_ret;
	if (stop_ret) {
		/* Keep allocations, clocks and code alive after the fd closes. */
		__module_get(THIS_MODULE);
		dev_err(f->dev, "ISP stop unproven; buffers and PM retained\n");
		ret = stop_ret;
	} else {
		phase_started = ktime_get();
		dma_rmb();
		f->status[7] = isp_frame_changed(f->output, f->layout[3]);
		if (f->statistics_enabled)
			f->statistics_changed = isp_frame_changed(f->statistics, ISP_STATS_BYTES);
		f->timing[6] = ktime_to_ns(ktime_sub(ktime_get(), phase_started));
		f->timing[7] |= BIT(4);
		/* Sentinel change is a liveness check, not proof every byte was written. */
		if (!ret && (!f->status[7] || (f->status[3] & ISP_DMA_ERRORS) ||
			     (f->status[0] & ISP_FRAME_ERRORS) ||
			     (f->statistics_enabled && !f->statistics_changed)))
			ret = -EIO;
	}
	f->status[6] = ret;
	dev_info(f->dev, "frame irq=%08x/%08x/%08x dma=%08x idle=%x ack=%x result=%d changed=%u\n",
		 f->status[0], f->status[1], f->status[2], f->status[3],
		 f->status[4], f->status[5], ret, f->status[7]);
	return ret;
}

static int isp_frame_run(struct isp_frame *f)
{
	ktime_t started;
	int ret;

	/* Preserve previous timing on duplicate/incomplete submissions. */
	if (f->submitted || f->uploaded != ISP_INPUT_BYTES)
		return -EINVAL;
	memset(f->timing, 0, sizeof(f->timing));
	f->timing[0] = 1;
	started = ktime_get();
	ret = isp_frame_execute(f);
	f->timing[1] = ktime_to_ns(ktime_sub(ktime_get(), started));
	return ret;
}

/* Common owner for misc and queued adapters. Acquisition owns the exclusive
 * busy gate and PM reference; release requires callers to join their work. */
static struct isp_frame *isp_frame_acquire(struct exynos7870_isp *isp)
{
	struct isp_frame *f;
	struct iommu_domain *domain;
	size_t offset;
	int ret;

	if (atomic_cmpxchg(&isp->busy, 0, 1))
		return ERR_PTR(-EBUSY);
	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (!f) { ret = -ENOMEM; goto clear; }
	f->isp = isp;
	f->dev = isp->misc.parent;
	f->phase = 1;
	f->layout[0] = 4144;
	f->layout[1] = 3106;
	f->layout[2] = 4144 * 3;
	f->layout[3] = ISP_OUTPUT_BYTES;
	mutex_init(&f->lock);
	ret = pm_runtime_resume_and_get(f->dev);
	if (ret < 0)
		goto free;
	domain = iommu_get_domain_for_dev(f->dev);
	ret = -ENODEV;
	if (!domain || (domain->type != IOMMU_DOMAIN_DMA && domain->type != IOMMU_DOMAIN_DMA_FQ))
		goto buffers;
	f->input = dma_alloc_coherent(f->dev, PAGE_ALIGN(ISP_INPUT_BYTES), &f->input_dma, GFP_KERNEL);
	f->output = dma_alloc_coherent(f->dev, PAGE_ALIGN(ISP_OUTPUT_BYTES), &f->output_dma, GFP_KERNEL);
	f->statistics = dma_alloc_coherent(f->dev, PAGE_ALIGN(ISP_STATS_BYTES), &f->statistics_dma, GFP_KERNEL);
	ret = -ENOMEM;
	if (!f->input || !f->output || !f->statistics)
		goto buffers;
	ret = -ERANGE;
	if (upper_32_bits(f->input_dma + ISP_INPUT_BYTES - 1) ||
	    upper_32_bits(f->output_dma + ISP_OUTPUT_BYTES - 1) ||
	    upper_32_bits(f->statistics_dma + ISP_STATS_BYTES - 1))
		goto buffers;
	ret = -EFAULT;
	for (offset = 0; offset < ISP_INPUT_BYTES; offset += PAGE_SIZE)
		if (!iommu_iova_to_phys(domain, f->input_dma + offset))
			goto buffers;
	for (offset = 0; offset < ISP_OUTPUT_BYTES; offset += PAGE_SIZE)
		if (!iommu_iova_to_phys(domain, f->output_dma + offset))
			goto buffers;
	for (offset = 0; offset < ISP_STATS_BYTES; offset += PAGE_SIZE)
		if (!iommu_iova_to_phys(domain, f->statistics_dma + offset))
			goto buffers;
	f->quiescent = true;
	return f;
buffers:
	if (f->statistics) dma_free_coherent(f->dev, PAGE_ALIGN(ISP_STATS_BYTES), f->statistics, f->statistics_dma);
	if (f->input) dma_free_coherent(f->dev, PAGE_ALIGN(ISP_INPUT_BYTES), f->input, f->input_dma);
	if (f->output) dma_free_coherent(f->dev, PAGE_ALIGN(ISP_OUTPUT_BYTES), f->output, f->output_dma);
	pm_runtime_put_sync(f->dev);
free:
	kfree(f);
clear:
	atomic_set(&isp->busy, 0);
	return ERR_PTR(ret);
}

static void isp_frame_put(struct isp_frame *f)
{
	if (!f->quiescent)
		return; /* Intentionally retain owner and buffers until reboot. */
	dma_free_coherent(f->dev, PAGE_ALIGN(ISP_STATS_BYTES), f->statistics, f->statistics_dma);
	dma_free_coherent(f->dev, PAGE_ALIGN(ISP_INPUT_BYTES), f->input, f->input_dma);
	dma_free_coherent(f->dev, PAGE_ALIGN(ISP_OUTPUT_BYTES), f->output, f->output_dma);
	pm_runtime_put_sync(f->dev);
	atomic_set(&f->isp->busy, 0);
	kfree(f);
}

/* Caller holds the job lock. Preserve allocations and settings, but require
 * a complete fresh upload. Explicit rearm discards the previous job record. */
static int isp_frame_rearm(struct isp_frame *f)
{
	if (!f->submitted || !f->quiescent)
		return -EBUSY;
	f->submitted = false;
	f->uploaded = 0;
	f->downloaded = 0;
	f->statistics_downloaded = 0;
	f->statistics_changed = 0;
	f->read_plane = 0;
	memset(f->status, 0, sizeof(f->status));
	memset(f->diagnostic, 0, sizeof(f->diagnostic));
	memset(f->launch, 0, sizeof(f->launch));
	memset(f->audit, 0, sizeof(f->audit));
	memset(f->timing, 0, sizeof(f->timing));
	return 0;
}

static int isp_frame_open(struct inode *inode, struct file *file)
{
	struct exynos7870_isp *isp = container_of(file->private_data, struct exynos7870_isp, misc);
	struct isp_frame *f = isp_frame_acquire(isp);

	if (IS_ERR(f))
		return PTR_ERR(f);
	file->private_data = f;
	return nonseekable_open(inode, file);
}

static int isp_frame_release(struct inode *inode, struct file *file)
{
	isp_frame_put(file->private_data);
	return 0;
}

static ssize_t isp_frame_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	struct isp_frame *f = file->private_data;
	ssize_t ret;

	mutex_lock(&f->lock);
	ret = -EBUSY;
	if (f->submitted)
		goto out;
	count = min_t(size_t, count, ISP_INPUT_BYTES - f->uploaded);
	ret = -EFAULT;
	if (copy_from_user(f->input + f->uploaded, buf, count))
		goto out;
	f->uploaded += count;
	ret = count;
out:
	mutex_unlock(&f->lock);
	return ret;
}

static ssize_t isp_frame_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
	struct isp_frame *f = file->private_data;
	ssize_t ret = -EAGAIN;

	mutex_lock(&f->lock);
	if (!f->submitted || !f->quiescent)
		goto out;
	if (f->read_plane) {
		count = min_t(size_t, count, ISP_STATS_BYTES - f->statistics_downloaded);
		ret = -EFAULT;
		if (copy_to_user(buf, f->statistics + f->statistics_downloaded, count))
			goto out;
		f->statistics_downloaded += count;
		ret = count;
		goto out;
	}
	count = min_t(size_t, count, f->layout[3] - f->downloaded);
	ret = -EFAULT;
	if (copy_to_user(buf, f->output + f->downloaded, count))
		goto out;
	f->downloaded += count;
	ret = count;
out:
	mutex_unlock(&f->lock);
	return ret;
}

static long isp_frame_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct isp_frame *f = file->private_data;
	long ret = -ENOTTY;

	mutex_lock(&f->lock);
	if (cmd == 0x4901)
		ret = isp_frame_run(f);
	else if (cmd == _IO('I', 16))
		ret = isp_frame_rearm(f);
	else if (cmd == _IOR('I', 17, __u64[8]))
		ret = copy_to_user((void __user *)arg, f->timing, sizeof(f->timing)) ? -EFAULT : 0;
	else if (cmd == _IO('I', 18)) {
		/* Rewind only the selected plane; keep configuration/error evidence. */
		ret = -EBUSY;
		if (f->submitted && f->quiescent) {
			if (f->read_plane)
				f->statistics_downloaded = 0;
			else
				f->downloaded = 0;
			ret = 0;
		}
	}
	else if (cmd == _IOR('I', 2, __u32[8]))
		ret = copy_to_user((void __user *)arg, f->status, sizeof(f->status)) ? -EFAULT : 0;
	else if (cmd == _IOR('I', 3, __u32[16]))
		ret = copy_to_user((void __user *)arg, f->diagnostic, sizeof(f->diagnostic)) ? -EFAULT : 0;
	else if (cmd == _IOR('I', 5, __u32[4]))
		ret = copy_to_user((void __user *)arg, f->layout, sizeof(f->layout)) ? -EFAULT : 0;
	else if (cmd == _IOR('I', 7, __u32[32]))
		ret = copy_to_user((void __user *)arg, f->launch, sizeof(f->launch)) ? -EFAULT : 0;
	else if (cmd == _IOR('I', 11, __u32[8]))
		ret = copy_to_user((void __user *)arg, f->audit, sizeof(f->audit)) ? -EFAULT : 0;
	else if (cmd == _IOR('I', 19, __u32[ISP_REGISTER_SCAN_WORDS])) {
		ret = -ENODATA;
		if (f->register_scan_valid)
			ret = copy_to_user((void __user *)arg, f->register_scan,
					    sizeof(f->register_scan)) ? -EFAULT : 0;
	}
	else if (cmd == _IOW('I', 15, __u32[3])) {
		u32 gains[3];

		ret = -EBUSY;
		if (f->submitted || !f->quiescent)
			goto ioctl_out;
		ret = -EFAULT;
		if (copy_from_user(gains, (void __user *)arg, sizeof(gains)))
			goto ioctl_out;
		ret = -EINVAL;
		if (!gains[0] || !gains[1] || !gains[2] ||
		    gains[0] > 8191 || gains[1] > 8191 || gains[2] > 8191)
			goto ioctl_out;
		memcpy(f->wbg_gains, gains, sizeof(gains));
		f->wbg_mode = 8;
		ret = 0;
	}
	else if (cmd == _IOR('I', 13, __u32[16])) {
		/* v2: origin relative to ISP crop, plus absolute RAW origin in 14/15.
		 * Enables exact spatial correspondence without guessing crop offsets. */
		u32 metadata[16] = { 2, f->statistics_enabled, 64, 64, 64, 48,
			f->crop_mode ? 16 : 24, f->crop_mode ? 8 : 16,
			8, ISP_STATS_BYTES, f->statistics_changed,
			f->quiescent, f->status[6], f->read_plane,
			f->crop_mode == 2 ? 23 : 24, 16 };

		ret = copy_to_user((void __user *)arg, metadata, sizeof(metadata)) ? -EFAULT : 0;
	}
	else if (cmd == _IOW('I', 12, __u32) || cmd == _IOW('I', 14, __u32)) {
		u32 mode;

		ret = -EFAULT;
		if (copy_from_user(&mode, (void __user *)arg, sizeof(mode)))
			goto ioctl_out;
		ret = -EINVAL;
		if (mode > 1)
			goto ioctl_out;
		ret = -EBUSY;
		if (cmd == _IOW('I', 12, __u32)) {
			if (!f->submitted && f->quiescent) {
				f->statistics_enabled = mode;
				ret = 0;
			}
		} else if (f->submitted && f->quiescent) {
			ret = -ENODATA;
			if (!mode || f->statistics_enabled) {
				f->read_plane = mode;
				ret = 0;
			}
		}
	}
	else if (cmd == _IOW('I', 10, __u32)) {
		u32 mode;

		ret = -EBUSY;
		if (!f->submitted && f->quiescent) {
			ret = -EFAULT;
			if (!copy_from_user(&mode, (void __user *)arg, sizeof(mode))) {
				ret = -EINVAL;
				if (mode <= 3) {
					f->gamma_mode = mode;
					ret = 0;
				}
			}
		}
	}
	else if (cmd == _IOW('I', 9, __u32)) {
		u32 mode;

		ret = -EBUSY;
		if (!f->submitted && f->quiescent) {
			ret = -EFAULT;
			if (!copy_from_user(&mode, (void __user *)arg, sizeof(mode))) {
				ret = -EINVAL;
				if (mode <= 7) {
					f->wbg_mode = mode;
					ret = 0;
				}
			}
		}
	}
	else if (cmd == _IOW('I', 8, __u32)) {
		u32 mode;

		ret = -EBUSY;
		if (!f->submitted && f->quiescent) {
			ret = -EFAULT;
			if (!copy_from_user(&mode, (void __user *)arg, sizeof(mode))) {
				ret = -EINVAL;
				if (mode <= 3) {
					f->launch_mode = mode;
					ret = 0;
				}
			}
		}
	}
	else if (cmd == _IOW('I', 6, __u32)) {
		u32 mode;

		ret = -EBUSY;
		if (!f->submitted && f->quiescent) {
			ret = -EFAULT;
			if (!copy_from_user(&mode, (void __user *)arg, sizeof(mode))) {
				ret = -EINVAL;
				if (mode <= 2) {
					f->crop_mode = mode;
					f->layout[0] = mode ? 4128 : 4144;
					f->layout[1] = mode ? 3090 : 3106;
					f->layout[2] = f->layout[0] * 3;
					f->layout[3] = f->layout[2] * f->layout[1];
					ret = 0;
				}
			}
		}
	}
	else if (cmd == _IOW('I', 4, __u32)) {
		u32 phase;

		ret = -EBUSY;
		if (!f->submitted && f->quiescent) {
			ret = -EFAULT;
			if (!copy_from_user(&phase, (void __user *)arg, sizeof(phase))) {
				ret = -EINVAL;
				if (phase <= 3) {
					f->phase = phase;
					ret = 0;
				}
			}
		}
	}
ioctl_out:
	mutex_unlock(&f->lock);
	return ret;
}

static const struct file_operations isp_frame_fops = {
	.owner = THIS_MODULE,
	.open = isp_frame_open, .release = isp_frame_release,
	.write = isp_frame_write, .read = isp_frame_read,
	.unlocked_ioctl = isp_frame_ioctl,
};
static void isp_frame_deregister(void *data)
{
	struct exynos7870_isp *isp = data;
	misc_deregister(&isp->misc);
}
