// SPDX-License-Identifier: GPL-2.0

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <dt-bindings/clock/hichip,hc16xx-clock.h>

#define HC16XX_CLK_GATE0   0x00
#define HC16XX_CLK_GATE1   0x04
#define HC16XX_CLK_REG_070 0x10
#define HC16XX_CLK_REG_074 0x14
#define HC16XX_CLK_REG_078 0x18
#define HC16XX_CLK_REG_07C 0x1c

/* internal clocks */
#define HC16XX_CLK_F1M843 (HC16XX_CLK_NR_CLKS +  0)
#define HC16XX_CLK_F12M   (HC16XX_CLK_NR_CLKS +  1)
#define HC16XX_CLK_F24M   (HC16XX_CLK_NR_CLKS +  2)
#define HC16XX_CLK_F27M   (HC16XX_CLK_NR_CLKS +  3)
#define HC16XX_CLK_F108M  (HC16XX_CLK_NR_CLKS +  4)
#define HC16XX_CLK_F198M  (HC16XX_CLK_NR_CLKS +  5)
#define HC16XX_CLK_F297M  (HC16XX_CLK_NR_CLKS +  6)
#define HC16XX_CLK_F396M  (HC16XX_CLK_NR_CLKS +  7)
#define HC16XX_CLK_F450M  (HC16XX_CLK_NR_CLKS +  8)
#define HC16XX_CLK_F594M  (HC16XX_CLK_NR_CLKS +  9)
#define HC16XX_CLK_F800M  (HC16XX_CLK_NR_CLKS + 10)
#define HC16XX_CLK_F900M  (HC16XX_CLK_NR_CLKS + 11)
#define HC16XX_CLK_F1188M (HC16XX_CLK_NR_CLKS + 12)
#define HC16XX_CLK_USB0_SEL (HC16XX_CLK_NR_CLKS + 13)
#define HC16XX_CLK_USB1_SEL (HC16XX_CLK_NR_CLKS + 14)
#define HC16XX_CLK_DE_SEL   (HC16XX_CLK_NR_CLKS + 15)
#define HC16XX_CLK_DE_PCLK  (HC16XX_CLK_NR_CLKS + 16)

#define HC16XX_CLKS_NUM     (HC16XX_CLK_NR_CLKS + 17)

struct hc16xx_clk {
	void __iomem *base;
	spinlock_t lock;

	struct clk_hw_onecell_data *hw_data;
};

struct hc16xx_gate_desc {
	unsigned int id;
	const char *name;
	const char *parent;
	u32 reg;
	u8 bit;
};

struct hc16xx_fixed_factor {
	unsigned int id;
	const char *name;
	const char *parent;
	unsigned int mult;
	unsigned int div;
};

struct hc16xx_clksel_rate {
	unsigned long rate;
	u8 mux;
};

struct clk_hc16xx_selector;

struct hc16xx_clk_selector_data {
	int				id;
	const char			*name;
	u16				reg_offset;
	u16				reg_pll;
	u8				field_width;
	u8				field_offset;
	bool				has_pll_mux;
	u8				pll_mux;
	unsigned long			pll_source_rate;
	const struct hc16xx_clksel_rate	*rates;
	int (*write_locked)(struct clk_hc16xx_selector *clk, u32 mux);
};

struct clk_hc16xx_selector {
	struct clk_hw				hw;
	struct hc16xx_clk			*controller;
	const struct hc16xx_clk_selector_data	*pdata;
};

#define to_clk_hc16xx_selector(_hw) \
	container_of(_hw, struct clk_hc16xx_selector, hw)

static const u32 rounding_threshold = 1;

static unsigned long
hc16xx_pll_rate_for_reg(struct clk_hc16xx_selector *clk, u32 reg_value)
{
	u32 postdiv, m, n;
	u64 tmp;

	if (!(reg_value & BIT(31))) /* PLL is disabled */
		return 0;

	postdiv = (reg_value & 0xff) + 1;
	n = ((reg_value & 0xff00) >> 8) + 1;
	m = ((reg_value & 0x7fff0000) >> 16) + 1;

	tmp = (u64)clk->pdata->pll_source_rate * m;
	do_div(tmp, n * postdiv);

	return  tmp;
}

static int hc16xx_pll_reg_for_rate(struct clk_hc16xx_selector *clk,
				   unsigned long rate, u32 *reg_value)
{
	u32 best_mult = 0;
	u32 div, best_div = 0;
	u32 best_diff = rate;
	const u32 postdiv = 1;

	/* FIXME Hardware limitations don't known,
	 * iterating over all divs */
	for (div = 1; div <= 256; div++) {
		u64 tmp = (u64)rate * div * postdiv
			+ clk->pdata->pll_source_rate / 2;
		u32 mult;
		unsigned long diff, _rate;

		do_div(tmp, clk->pdata->pll_source_rate);
		if (tmp == 0 || tmp > 32768)
			continue;
		mult = tmp;

		tmp = (u64)mult * clk->pdata->pll_source_rate;
		do_div(tmp, div * postdiv);

		_rate = tmp;
		diff = abs_diff(_rate, rate);

		if (diff <= rounding_threshold) {
			best_mult = mult;
			best_div = div;
			best_diff = diff;
			break;
		} else if (diff < best_diff) {
			best_diff = diff;
			best_div = div;
			best_mult = mult;
		}
	}

	if (best_div == 0)
		return -EINVAL;

	*reg_value = BIT(31)
		   | (((best_mult - 1) & 0x7fff) << 16)
		   | (((best_div - 1) & 0xff) << 8)
		   | ((postdiv - 1) & 0xff);
	return 0;
}

static unsigned long hc16xx_pll_recalc_rate(struct clk_hc16xx_selector *clk)
{
	u32 val = readl(clk->controller->base + clk->pdata->reg_pll);

	return hc16xx_pll_rate_for_reg(clk, val);
}

static unsigned long hc16xx_clksel_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct clk_hc16xx_selector *clk = to_clk_hc16xx_selector(hw);
	const struct hc16xx_clksel_rate	*sel = clk->pdata->rates;
	u32 mux;

	(void)parent_rate; /* parent(s) for selectors aren't known */

	mux = readl(clk->controller->base + clk->pdata->reg_offset);
	mux = (mux >> clk->pdata->field_offset)
		& (BIT(clk->pdata->field_width) - 1);

	while (sel && sel->rate) {
		if (sel->mux == mux)
			return sel->rate;
		sel++;
	}

	if (!clk->pdata->has_pll_mux || mux != clk->pdata->pll_mux)
		return 0;

	return hc16xx_pll_recalc_rate(clk);
}

static int hc16xx_pll_determine_rate(struct clk_hc16xx_selector *clk,
				     struct clk_rate_request *req)
{
	u32 val;
	int err;

	err = hc16xx_pll_reg_for_rate(clk, req->rate, &val);
	if (err)
		return err;

	req->rate = hc16xx_pll_rate_for_reg(clk, val);
	return 0;
}

static int hc16xx_clksel_determine_rate(struct clk_hw *hw,
					struct clk_rate_request *req)
{
	struct clk_hc16xx_selector *clk = to_clk_hc16xx_selector(hw);
	const struct hc16xx_clksel_rate *sel = clk->pdata->rates;
	const struct hc16xx_clksel_rate *best = NULL;
	unsigned long best_diff = ULONG_MAX;

	while (sel && sel->rate) {
		unsigned long rate = sel->rate;
		unsigned long diff = abs_diff(rate, req->rate);
		if (!best || diff < best_diff) {
			best = sel;
			best_diff = diff;
		}
		sel++;
	}

	if (!best)
		return -EINVAL;

	if (!clk->pdata->has_pll_mux || best_diff <= rounding_threshold) {
		req->rate = best->rate;
		return 0;
	}

	return hc16xx_pll_determine_rate(clk, req);
}

static int hc16xx_clksel_set_rate(struct clk_hw *hw,
				  unsigned long rate,
				  unsigned long parent_rate)
{
	struct clk_hc16xx_selector *clk = to_clk_hc16xx_selector(hw);
	const struct hc16xx_clksel_rate *sel = clk->pdata->rates;
	u8 width = clk->pdata->field_width;
	u8 shift = clk->pdata->field_offset;
	unsigned long flags;
	u32 val, mux;
	int err = 0;

	while (sel && sel->rate) {
		unsigned long diff = abs_diff(sel->rate, rate);
		if (diff <= rounding_threshold)
			break;
		sel++;
	}

	if (sel && sel->rate) {
		mux = sel->mux;
	} else if (sel && clk->pdata->has_pll_mux) {
		mux = clk->pdata->pll_mux;
		err = hc16xx_pll_reg_for_rate(clk, rate, &val);
		if (err)
			return err;
	} else {
		return -EINVAL;
	}

	spin_lock_irqsave(&clk->controller->lock, flags);

	if (clk->pdata->has_pll_mux && mux == clk->pdata->pll_mux)
		writel(val, clk->controller->base + clk->pdata->reg_pll);

	if (clk->pdata->write_locked) {
		err = clk->pdata->write_locked(clk, mux);
	} else {
		val = readl(clk->controller->base + clk->pdata->reg_offset);
		val &= ~((BIT(width) - 1) << shift);
		val |= mux << shift;
		writel(val, clk->controller->base + clk->pdata->reg_offset);
	}

	spin_unlock_irqrestore(&clk->controller->lock, flags);

	return err;
}

static const struct clk_ops hc16xx_clksel_ops = {
	.recalc_rate    = hc16xx_clksel_recalc_rate,
	.determine_rate = hc16xx_clksel_determine_rate,
	.set_rate       = hc16xx_clksel_set_rate,
};

static const struct hc16xx_clksel_rate hc16xx_mcpu_rates[] = {
	{  198000000, 3 },
	{  297000000, 2 },
	{  396000000, 1 },
	{  594000000, 0 },
	{  900000000, 4 },
	{ 1188000000, 5 },
	{},
};

static int hc16xx_mcpu_write(struct clk_hc16xx_selector *clk, u32 mux)
{
	u32 val;
	u8 width = clk->pdata->field_width;
	u8 shift = clk->pdata->field_offset;

	val = readl(clk->controller->base + HC16XX_CLK_REG_07C);
	if (mux == clk->pdata->pll_mux)
		val |= BIT(7);
	else
		val &= ~BIT(7);
	writel(val, clk->controller->base + HC16XX_CLK_REG_07C);

	val = readl(clk->controller->base + HC16XX_CLK_REG_074);
	val &= ~((BIT(width) - 1) << shift);
	val |= mux << shift;
	val |= BIT(22); /* Apply new clock source */
	writel(val, clk->controller->base + HC16XX_CLK_REG_074);

	return 0;
}

static const struct hc16xx_clksel_rate hc16xx_mmc_rates[] = {
	{  198000000, 3 },
	{  148500000, 2 },
	{   99000000, 1 },
	{   49500000, 0 },
	{},
};

static int hc16xx_mmc_write(struct clk_hc16xx_selector *clk, u32 mux)
{
	u32 val;
	u8 width = clk->pdata->field_width;
	u8 shift = clk->pdata->field_offset;

	val = readl(clk->controller->base + clk->pdata->reg_offset);
	val &= ~((BIT(width) - 1) << shift);
	val |= mux << shift;
	val |= BIT(1); /* Apply new clock source */
	writel(val, clk->controller->base + clk->pdata->reg_offset);

	return 0;
}

static const struct hc16xx_clksel_rate hc16xx_de_rates[] = {
	{  175000000, 0 },
	{  148500000, 1 },
	{  198000000, 2 },
	{  225000000, 3 },
	{},
};

static const struct hc16xx_clksel_rate hc16xx_de_pclk_rates[] = {
	{   27000000,  0 },
	{   54000000,  1 },
	{   74250000,  2 },
	{  108000000,  3 },
	{  132000000,  4 },
	{  148500000,  5 },
	{    6600000,  8 },
	{    9000000,  9 },
	{   33000000, 10 },
	{   39600000, 11 },
	{   49000000, 12 },
	{   66000000, 13 },
	{   85000000, 14 },
	{},
};

static int __init
hc16xx_sel_clk_register(struct hc16xx_clk *priv,
			const struct hc16xx_clk_selector_data* data)
{
	struct clk_hc16xx_selector *clk;
	struct clk_init_data init = { 0 };
	int ret;

	clk = kzalloc(sizeof(*clk), GFP_KERNEL);
	if (!clk)
		return -ENOMEM;

	init.name = data->name;
	init.ops = &hc16xx_clksel_ops;
	init.flags = 0;
	init.num_parents = 0;
	init.parent_names = NULL;

	clk->hw.init = &init;
	clk->controller = priv;
	clk->pdata = data;

	ret = clk_hw_register(NULL, &clk->hw);
	if (ret) {
		kfree(clk);
		return ret;
	}

	priv->hw_data->hws[data->id] = &clk->hw;
	return 0;
}

/* mostly guessing by names */
static const struct hc16xx_fixed_factor hc16xx_fixed[] = {
	/*       id           name     parent  mult,   div */
	{ HC16XX_CLK_F24M,   "f24m",   "hosc",    1,     1 },
	{ HC16XX_CLK_F12M,   "f12m",   "f24m",    1,     2 },
	{ HC16XX_CLK_F108M,  "f108m",  "sb-apb",  9,     2 },
	/* f1m843 actual mul/div aren't known, set these to exact
	 * match expected baudrate with reported by UART driver */
	{ HC16XX_CLK_F1M843, "f1m843", "sb-108m", 32, 1875,},
	{ HC16XX_CLK_F27M,   "f27m",   "f108m",   1,     4 },
	{ HC16XX_CLK_F198M,  "f198m",  "f1188m",  1,     6 },
	{ HC16XX_CLK_F297M,  "f297m",  "f1188m",  1,     4 },
	{ HC16XX_CLK_F396M,  "f396m",  "f1188m",  1,     3 },
	{ HC16XX_CLK_F450M,  "f450m",  "f900m",   1,     2 },
	{ HC16XX_CLK_F594M,  "f594m",  "f1188m",  1,     2 },
	{ HC16XX_CLK_F800M,  "f800m",  "hosc",  100,     3 },
	{ HC16XX_CLK_F900M,  "f900m",  "hosc",   75,     2 },
	{ HC16XX_CLK_F1188M, "f1188m", "hosc",   99,     2 },
	/* dummy for unknown parent of clocks */
	{ HC16XX_CLK_DUMMY, "dummy-bus", "hosc",  1,     1 },
};

static const struct hc16xx_gate_desc hc16xx_gates[] = {
	/* UARTs has 3 baudrate clock sources but how to switch them isn't known yet.
	 * They are mentioned as:
	 *  - normal speed (1.843MHz)
	 *  - high speed (54MHz)
	 *  - high speed DPLL (14.745 MHz)
	 * Parent is probably wrong, likely it is APB clock gate, not baudrate
	 * but it done so to avoid writing wrapper for 8250 driver.
	 * Also `sb-apb` and `sb-108m` clocks have to be enabled.
	 */
	{ HC16XX_CLK_UART0,           "uart0",          "sb-1843k",  HC16XX_CLK_GATE0, 16 },
	{ HC16XX_CLK_UART1,           "uart1",          "sb-1843k",  HC16XX_CLK_GATE0, 17 },
	{ HC16XX_CLK_UART23,          "uart2-3",        "sb-1843k",  HC16XX_CLK_GATE1, 11 },

	{ HC16XX_CLK_SFSPI,           "sfspi",          "dummy-bus", HC16XX_CLK_GATE0,  0 },
	/* disabling SFSPI hangs the system when SDMMC is in use,
	 * so set as parent but might not be actual parent though */
	{ HC16XX_CLK_SDMMC,           "sdmmc",          "dummy-bus", HC16XX_CLK_GATE0,  1 },
	{ HC16XX_CLK_NAND,            "nand",           "dummy-bus", HC16XX_CLK_GATE0,  2 },
	{ HC16XX_CLK_DESCRAMBLE,      "descramble",     "dummy-bus", HC16XX_CLK_GATE0,  3 },
	{ HC16XX_CLK_TSG,             "tsg",            "dummy-bus", HC16XX_CLK_GATE0,  7 },
	{ HC16XX_CLK_DEMUX1,          "demux1",         "dummy-bus", HC16XX_CLK_GATE0,  8 },
	{ HC16XX_CLK_DEMUX2_CI,       "demux2-ci",      "dummy-bus", HC16XX_CLK_GATE0,  9 },
	{ HC16XX_CLK_TS_SWITCH,       "ts-switch",      "dummy-bus", HC16XX_CLK_GATE0, 10 },
	{ HC16XX_CLK_ETH_MAC,         "eth-mac",        "dummy-bus", HC16XX_CLK_GATE0, 11 },
	{ HC16XX_CLK_SAR_ADC,         "sar-adc",        "dummy-bus", HC16XX_CLK_GATE0, 12 },
	{ HC16XX_CLK_VDEC,            "vdec",           "dummy-bus", HC16XX_CLK_GATE0, 13 },
#if 0 /* disabling, hangs the system without CLK_IGNORE_UNUSED */
	{ HC16XX_CLK_DDR_PHY_APB,     "ddr-phy-apb",    "dummy-bus", HC16XX_CLK_GATE0, 14 },
	{ HC16XX_CLK_DFI,             "dfi",            "dummy-bus", HC16XX_CLK_GATE0, 15 },
#endif
	{ HC16XX_CLK_I2C0,            "i2c0",           "dummy-bus", HC16XX_CLK_GATE0, 18 },
	{ HC16XX_CLK_HDMI_TX_PHY,     "hdmi-tx-phy",    "dummy-bus", HC16XX_CLK_GATE0, 19 },
	{ HC16XX_CLK_WATCHDOG,        "watchdog",       "dummy-bus", HC16XX_CLK_GATE0, 22 },
	{ HC16XX_CLK_TIMER,           "timer",          "sb-27m",    HC16XX_CLK_GATE0, 23 },
	{ HC16XX_CLK_I2C1,            "i2c1",           "dummy-bus", HC16XX_CLK_GATE0, 25 },
	{ HC16XX_CLK_I2C2,            "i2c2",           "dummy-bus", HC16XX_CLK_GATE0, 26 },
	{ HC16XX_CLK_I2C3,            "i2c3",           "dummy-bus", HC16XX_CLK_GATE0, 27 },
	{ HC16XX_CLK_SB_APB,          "sb-apb",         "dummy-bus", HC16XX_CLK_GATE0, 28 },
	{ HC16XX_CLK_SB_108M,         "sb-108m",        "f108m",     HC16XX_CLK_GATE0, 29 },
	{ HC16XX_CLK_SB_27M,          "sb-27m",         "f27m",      HC16XX_CLK_GATE0, 31 },
	{ HC16XX_CLK_SB_1843K,        "sb-1843k",       "f1m843",    HC16XX_CLK_GATE0, 30 },

	{ HC16XX_CLK_VOU_HD,          "vou-hd",         "dummy-bus", HC16XX_CLK_GATE1,  0 },
	{ HC16XX_CLK_VE,              "ve",             "dummy-bus", HC16XX_CLK_GATE1,  1 },
	{ HC16XX_CLK_DE,              "de",             "de-select", HC16XX_CLK_GATE1,  2 },
	{ HC16XX_CLK_TVENC_SD,        "tvenc-sd",       "dummy-bus", HC16XX_CLK_GATE1,  3 },
	{ HC16XX_CLK_GE,              "ge",             "dummy-bus", HC16XX_CLK_GATE1,  4 },
	{ HC16XX_CLK_TVENC_HD,        "tvenc-hd",       "dummy-bus", HC16XX_CLK_GATE1,  5 },
	{ HC16XX_CLK_VIN,             "vin",            "dummy-bus", HC16XX_CLK_GATE1,  6 },
	{ HC16XX_CLK_DE_HDMI,         "de-hdmi",        "de-pclk",   HC16XX_CLK_GATE1,  7 },
	{ HC16XX_CLK_VDAC1,           "vdac1",          "dummy-bus", HC16XX_CLK_GATE1,  8 },
	{ HC16XX_CLK_VDAC2,           "vdac2",          "dummy-bus", HC16XX_CLK_GATE1,  9 },
	{ HC16XX_CLK_VDAC3,           "vdac3",          "dummy-bus", HC16XX_CLK_GATE1, 10 },
	{ HC16XX_CLK_SGDMA1,          "sgdma1",         "dummy-bus", HC16XX_CLK_GATE1, 12 },
	{ HC16XX_CLK_SGDMA2,          "sgdma2",         "dummy-bus", HC16XX_CLK_GATE1, 13 },
	{ HC16XX_CLK_HDMI_TX,         "hdmi-tx",        "dummy-bus", HC16XX_CLK_GATE1, 14 },
	{ HC16XX_CLK_AUDIO,           "audio",          "dummy-bus", HC16XX_CLK_GATE1, 15 },
	{ HC16XX_CLK_SPI,             "spi",            "dummy-bus", HC16XX_CLK_GATE1, 16 },
	{ HC16XX_CLK_DVI,             "dvi",            "dummy-bus", HC16XX_CLK_GATE1, 17 },
	{ HC16XX_CLK_VOU_HD_EXT,      "vou-hd-ext",     "dummy-bus", HC16XX_CLK_GATE1, 18 },
	{ HC16XX_CLK_SP0,             "sp0",            "dummy-bus", HC16XX_CLK_GATE1, 20 },
	{ HC16XX_CLK_DDP_SP0,         "ddp-sp0",        "dummy-bus", HC16XX_CLK_GATE1, 21 },
	{ HC16XX_CLK_TSDMA,           "tsdma",          "dummy-bus", HC16XX_CLK_GATE1, 22 },
	{ HC16XX_CLK_JENC,            "jenc",           "dummy-bus", HC16XX_CLK_GATE1, 23 },
	{ HC16XX_CLK_USB0,            "usb0",           "usb0-sel",  HC16XX_CLK_GATE1, 24 },
	{ HC16XX_CLK_USB1,            "usb1",           "usb1-sel",  HC16XX_CLK_GATE1, 25 },
	{ HC16XX_CLK_HDMI_TX_PHY_SYS, "hdmi-tx-phy-sys","dummy-bus", HC16XX_CLK_GATE1, 27 },
	{ HC16XX_CLK_MIPI,            "mipi",           "dummy-bus", HC16XX_CLK_GATE1, 28 },
	{ HC16XX_CLK_BOOTROM,         "bootrom",        "dummy-bus", HC16XX_CLK_GATE1, 29 },
	{ HC16XX_CLK_TVDEC,           "tvdec",          "dummy-bus", HC16XX_CLK_GATE1, 30 },
};

static const struct hc16xx_clk_selector_data hc16xx_selectors[] = {
	{
		.id = HC16XX_CLK_MCPU,
		.name = "mcpu",
		.reg_offset = HC16XX_CLK_REG_070,
		.field_width = 3,
		.field_offset = 8,
		.rates = hc16xx_mcpu_rates,
		.write_locked = hc16xx_mcpu_write,
		.reg_pll = 0x0320,
		.has_pll_mux = true,
		.pll_mux = 7,
		.pll_source_rate = 24000000UL,
	},
	{
		.id = HC16XX_CLK_SDMMC_CARD,
		.name = "sdmmc-card",
		.reg_offset = HC16XX_CLK_REG_07C,
		.field_width = 2,
		.field_offset = 20,
		.rates = hc16xx_mmc_rates,
		.write_locked = hc16xx_mmc_write,
	},
	{
		.id = HC16XX_CLK_DE_SEL,
		.name = "de-select",
		.reg_offset = HC16XX_CLK_REG_078,
		.field_width = 2,
		.field_offset = 18,
		.rates = hc16xx_de_rates,
	},
	{
		.id = HC16XX_CLK_DE_PCLK,
		.name = "de-pclk",
		.reg_offset = HC16XX_CLK_REG_078,
		.field_width = 4,
		.field_offset = 8,
		.rates = hc16xx_de_pclk_rates,
		.reg_pll = 0x0330,
		.has_pll_mux = true,
		.pll_mux = 7,
		.pll_source_rate = 12000000UL,
	},
};

static inline const char* hc16xx_parent_name(const char *name, const char *hosc)
{
	return strcmp(name, "hosc") != 0 ? name : hosc;
}

static int __init hc16xx_register_selector_clocks(struct hc16xx_clk *priv)
{
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(hc16xx_selectors); i++) {
		ret = hc16xx_sel_clk_register(priv, &hc16xx_selectors[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int __init hc16xx_register_usb_clks(struct hc16xx_clk *priv)
{
	struct clk_hw *hw;

	/* We only know that when the bit is set
	 * the clock lowered down to 99MHz.
	 * Clock rate for bit 0 is pure guessing. */
	hw = clk_hw_register_divider(NULL,
				     "usb0-sel", "f198m", 0,
				     priv->base + HC16XX_CLK_REG_07C,
				     12, 1, 0, &priv->lock);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	priv->hw_data->hws[HC16XX_CLK_USB0_SEL] = hw;

	hw = clk_hw_register_divider(NULL,
				     "usb1-sel", "f198m", 0,
				     priv->base + HC16XX_CLK_REG_07C,
				     13, 1, 0, &priv->lock);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	priv->hw_data->hws[HC16XX_CLK_USB1_SEL] = hw;

	return 0;
}

static void __init hc16xx_clk_init(struct device_node *np)
{
	struct hc16xx_clk *priv;
	struct clk *hosc;
	const char *hosc_name;
	int i, ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		panic("failed to allocate hc16xx_clk\n");

	spin_lock_init(&priv->lock);

	priv->base = of_iomap(np, 0);
	if (!priv->base)
		panic("failed to remap registers block 0\n");

	priv->hw_data = kzalloc(
			struct_size(priv->hw_data, hws, HC16XX_CLKS_NUM),
			GFP_KERNEL);
	if (!priv->hw_data)
		panic("failed to allocate hc16xx_clk.hw_data\n");

	priv->hw_data->num = HC16XX_CLKS_NUM;

	hosc = of_clk_get_by_name(np, "hosc");
	if (IS_ERR(hosc))
		panic("failed to get hosc clock\n");
	hosc_name = __clk_get_name(hosc);
	clk_prepare_enable(hosc);

	/* Fixed-factor clocks */
	for (i = 0; i < ARRAY_SIZE(hc16xx_fixed); i++) {
		const struct hc16xx_fixed_factor *f = &hc16xx_fixed[i];
		struct clk_hw *hw;

		hw = clk_hw_register_fixed_factor(NULL, f->name,
				hc16xx_parent_name(f->parent, hosc_name),
				CLK_SET_RATE_PARENT,
				f->mult, f->div);
		if (IS_ERR(hw))
			panic("failed to register clock: %s\n", f->name);

		priv->hw_data->hws[f->id] = hw;
	}

	/* Gates */
	for (i = 0; i < ARRAY_SIZE(hc16xx_gates); i++) {
		const struct hc16xx_gate_desc *g = &hc16xx_gates[i];
		struct clk_hw *hw;

		hw = clk_hw_register_gate(NULL, g->name,
				hc16xx_parent_name(g->parent, hosc_name),
				CLK_SET_RATE_PARENT,
				priv->base + g->reg, g->bit,
				CLK_GATE_SET_TO_DISABLE,
				&priv->lock);
		if (IS_ERR(hw))
			panic("failed to register clock: %s\n", g->name);

		priv->hw_data->hws[g->id] = hw;
	}

	ret = hc16xx_register_selector_clocks(priv);
	if (ret)
		panic("failed to register selector clocks %d\n", ret);

	ret = hc16xx_register_usb_clks(priv);
	if (ret)
		panic("failed to register usb clocks %d\n", ret);

	ret = of_clk_add_hw_provider(np, of_clk_hw_onecell_get, priv->hw_data);
	if (ret)
		panic("failed to register clock provider\n");

	pr_info("hc16xx-clk probed\n");
}

CLK_OF_DECLARE(hc16xx_clk, "hichip,hc16xx-clk", hc16xx_clk_init);

MODULE_AUTHOR("Siarhei Volkau <lis8215@gmail.com>");
MODULE_DESCRIPTION("HiChip HC16xx Clock Controller");
MODULE_LICENSE("GPL");
