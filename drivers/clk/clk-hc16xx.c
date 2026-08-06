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

#define HC16XX_CLK_GATE0      0x00
#define HC16XX_CLK_GATE1      0x04
#define HC16XX_CLK_MCLK       0x14

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

#define HC16XX_CLKS_NUM   (HC16XX_CLK_NR_CLKS + 13)

struct hc16xx_clk {
	void __iomem *base;
	spinlock_t lock;

	struct hc16xx_cpuclk *cpu;
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

struct hc16xx_cpuclk {
	struct clk_hw hw;
	struct hc16xx_clk *controller;
};

#define to_hc16xx_cpuclk(_hw) \
	container_of(_hw, struct hc16xx_cpuclk, hw)

struct hc16xx_cpuclk_rate {
	unsigned long rate;
	u8 mux;
};

static const struct hc16xx_cpuclk_rate hc16xx_cpuclk_rates[] = {
	{  198000000, 3 },
	{  297000000, 2 },
	{  396000000, 1 },
	{  594000000, 0 },
	{  900000000, 4 },
	{ 1188000000, 5 },
};

static unsigned long hc16xx_cpuclk_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct hc16xx_cpuclk *cpuclk = to_hc16xx_cpuclk(hw);
	u32 mux;
	int i;

	mux = readl(cpuclk->controller->base + HC16XX_CLK_MCLK);
	mux = (mux >> 8) & 0x07;

	for (i = 0; i < ARRAY_SIZE(hc16xx_cpuclk_rates); i++)
		if (hc16xx_cpuclk_rates[i].mux == mux)
			return hc16xx_cpuclk_rates[i].rate;

	return 0;
}

static int hc16xx_cpuclk_determine_rate(struct clk_hw *hw,
					struct clk_rate_request *req)
{
	const struct hc16xx_cpuclk_rate *best = NULL;
	unsigned long best_diff = ULONG_MAX;
	int i;

	for (i = 0; i < ARRAY_SIZE(hc16xx_cpuclk_rates); i++) {
		unsigned long r = hc16xx_cpuclk_rates[i].rate;
		unsigned long diff = abs_diff(r, req->rate);

		if (!best || diff < best_diff) {
			best = &hc16xx_cpuclk_rates[i];
			best_diff = diff;
		}
	}

	if (!best)
		return -EINVAL;

	req->rate = best->rate;
	return 0;
}

static int hc16xx_cpuclk_set_rate(struct clk_hw *hw,
				  unsigned long rate,
				  unsigned long parent_rate)
{
	struct hc16xx_cpuclk *cpuclk = to_hc16xx_cpuclk(hw);
	unsigned long flags;
	u32 val;
	int i;

	for (i = 0; i < ARRAY_SIZE(hc16xx_cpuclk_rates); i++) {
		if (hc16xx_cpuclk_rates[i].rate == rate)
			break;
	}

	if (i == ARRAY_SIZE(hc16xx_cpuclk_rates))
		return -EINVAL;

	spin_lock_irqsave(&cpuclk->controller->lock, flags);

	val = readl(cpuclk->controller->base + HC16XX_CLK_MCLK);
	val &= ~GENMASK(10, 8);
	val |= hc16xx_cpuclk_rates[i].mux << 8;
	val |= BIT(22); /* Apply new clock source */
	writel(val, cpuclk->controller->base + HC16XX_CLK_MCLK);

	spin_unlock_irqrestore(&cpuclk->controller->lock, flags);

	return 0;
}

static const struct clk_ops hc16xx_cpuclk_ops = {
	.recalc_rate    = hc16xx_cpuclk_recalc_rate,
	.determine_rate = hc16xx_cpuclk_determine_rate,
	.set_rate       = hc16xx_cpuclk_set_rate,
};

/* mostly guessing by names */
static const struct hc16xx_fixed_factor hc16xx_fixed[] = {
	/*       id           name     parent  mult,   div */
	{ HC16XX_CLK_F24M,   "f24m",   "hosc",    1,     1 },
	{ HC16XX_CLK_F12M,   "f12m",   "f24m",    1,     2 },
	{ HC16XX_CLK_F108M,  "f108m",  "sb-apb",  9,     2 },
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
	{ HC16XX_CLK_TIMER,           "timer",          "dummy-bus", HC16XX_CLK_GATE0, 23 },
	{ HC16XX_CLK_I2C1,            "i2c1",           "dummy-bus", HC16XX_CLK_GATE0, 25 },
	{ HC16XX_CLK_I2C2,            "i2c2",           "dummy-bus", HC16XX_CLK_GATE0, 26 },
	{ HC16XX_CLK_I2C3,            "i2c3",           "dummy-bus", HC16XX_CLK_GATE0, 27 },
	{ HC16XX_CLK_SB_APB,          "sb-apb",         "dummy-bus", HC16XX_CLK_GATE0, 28 },
	{ HC16XX_CLK_SB_108M,         "sb-108m",        "f108m",     HC16XX_CLK_GATE0, 29 },
	{ HC16XX_CLK_SB_27M,          "sb-27m",         "f27m",      HC16XX_CLK_GATE0, 31 },
	{ HC16XX_CLK_SB_1843K,        "sb-1843k",       "f1m843",    HC16XX_CLK_GATE0, 30 },

	{ HC16XX_CLK_VOU_HD,          "vou-hd",         "dummy-bus", HC16XX_CLK_GATE1,  0 },
	{ HC16XX_CLK_VE,              "ve",             "dummy-bus", HC16XX_CLK_GATE1,  1 },
	{ HC16XX_CLK_DE,              "de",             "dummy-bus", HC16XX_CLK_GATE1,  2 },
	{ HC16XX_CLK_TVENC_SD,        "tvenc-sd",       "dummy-bus", HC16XX_CLK_GATE1,  3 },
	{ HC16XX_CLK_GE,              "ge",             "dummy-bus", HC16XX_CLK_GATE1,  4 },
	{ HC16XX_CLK_TVENC_HD,        "tvenc-hd",       "dummy-bus", HC16XX_CLK_GATE1,  5 },
	{ HC16XX_CLK_VIN,             "vin",            "dummy-bus", HC16XX_CLK_GATE1,  6 },
	{ HC16XX_CLK_DE_HDMI,         "de-hdmi",        "dummy-bus", HC16XX_CLK_GATE1,  7 },
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
	{ HC16XX_CLK_USB0,            "usb0",           "dummy-bus", HC16XX_CLK_GATE1, 24 },
	{ HC16XX_CLK_USB1,            "usb1",           "dummy-bus", HC16XX_CLK_GATE1, 25 },
	{ HC16XX_CLK_HDMI_TX_PHY_SYS, "hdmi-tx-phy-sys","dummy-bus", HC16XX_CLK_GATE1, 27 },
	{ HC16XX_CLK_MIPI,            "mipi",           "dummy-bus", HC16XX_CLK_GATE1, 28 },
	{ HC16XX_CLK_BOOTROM,         "bootrom",        "dummy-bus", HC16XX_CLK_GATE1, 29 },
	{ HC16XX_CLK_TVDEC,           "tvdec",          "dummy-bus", HC16XX_CLK_GATE1, 30 },
};

static inline const char* hc16xx_parent_name(const char *name, const char *hosc)
{
	return strcmp(name, "hosc") != 0 ? name : hosc;
}

static int hc16xx_mcpu_clk_register(struct hc16xx_clk *priv)
{
	struct hc16xx_cpuclk *cpuclk;
	struct clk_init_data init = { 0 };
	int ret;

	cpuclk = kzalloc(sizeof(*cpuclk), GFP_KERNEL);
	if (!cpuclk)
		return -ENOMEM;

	init.name = "mcpu";
	init.ops = &hc16xx_cpuclk_ops;
	init.flags = 0;
	init.num_parents = 0;
	init.parent_names = NULL;

	cpuclk->hw.init = &init;
	cpuclk->controller = priv;
	priv->cpu = cpuclk;

	ret = clk_hw_register(NULL, &cpuclk->hw);
	if (ret)
		return ret;

	priv->hw_data->hws[HC16XX_CLK_MCPU] = &cpuclk->hw;
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
	if (IS_ERR(priv->base))
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
	clk_put(hosc);

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

	ret = hc16xx_mcpu_clk_register(priv);
	if (ret)
		panic("failed to register cpu clock %d\n", ret);

	ret = of_clk_add_hw_provider(np, of_clk_hw_onecell_get, priv->hw_data);
	if (ret)
		panic("failed to register clock provider\n");

	pr_info("hc16xx-clk probed\n");
}

CLK_OF_DECLARE(hc16xx_clk, "hichip,hc16xx-clk", hc16xx_clk_init);

MODULE_AUTHOR("Siarhei Volkau <lis8215@gmail.com>");
MODULE_DESCRIPTION("HiChip HC16xx Clock Controller");
MODULE_LICENSE("GPL");
