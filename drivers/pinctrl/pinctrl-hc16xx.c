// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "core.h"
#include "pinmux.h"

#define HC16XX_L_MUX_BASE 0x00
#define HC16XX_B_MUX_BASE 0x40
#define HC16XX_R_MUX_BASE 0x80
#define HC16XX_T_MUX_BASE 0xc0

static const u32 mux_bases[4] = {
	HC16XX_L_MUX_BASE,
	HC16XX_B_MUX_BASE,
	HC16XX_R_MUX_BASE,
	HC16XX_T_MUX_BASE,
};

enum hc16xx_function {
	HC16XX_FUNC_GPIO = 0,

	/* UART */
	HC16XX_FUNC_UART0,
	HC16XX_FUNC_UART1,
	HC16XX_FUNC_UART2,
	HC16XX_FUNC_UART3,

	/* I2C */
	HC16XX_FUNC_I2C0,
	HC16XX_FUNC_I2C1,
	HC16XX_FUNC_I2C2,
	HC16XX_FUNC_I2C3,

	/* SPI */
	HC16XX_FUNC_SPI,

	/* Audio */
	HC16XX_FUNC_I2S_CLK,
	HC16XX_FUNC_I2S_DATA,
	HC16XX_FUNC_I2S0,
	HC16XX_FUNC_I2S1,
	HC16XX_FUNC_I2S2,

	HC16XX_FUNC_PCM,
	HC16XX_FUNC_PCM0,
	HC16XX_FUNC_PCM1,
	HC16XX_FUNC_PCM2,

	HC16XX_FUNC_TDM,

	HC16XX_FUNC_PDM0,
	HC16XX_FUNC_PDM1,
	HC16XX_FUNC_PDM2,

	HC16XX_FUNC_SSI,
	HC16XX_FUNC_SSI_ERR,

	HC16XX_FUNC_SPDIF,
	HC16XX_FUNC_SPDIF_IN,

	/* PWM */
	HC16XX_FUNC_PWM0,
	HC16XX_FUNC_PWM1,
	HC16XX_FUNC_PWM2,
	HC16XX_FUNC_PWM3,
	HC16XX_FUNC_PWM4,
	HC16XX_FUNC_PWM5,

	/* Video */
	HC16XX_FUNC_VIN,
#if 0
	HC16XX_FUNC_RGB,
	HC16XX_FUNC_RGB_ALT,
	HC16XX_FUNC_RGBHV,

	HC16XX_FUNC_HDMI_RX,
	HC16XX_FUNC_HDMI_TX,
	HC16XX_FUNC_CEC,
#endif

	/* Ethernet */
	HC16XX_FUNC_RMII,

	/* Storage */
	HC16XX_FUNC_SDMMC,
	HC16XX_FUNC_SFSPI,
	HC16XX_FUNC_NAND,
	HC16XX_FUNC_NAND_SM,

	/* Misc peripherals */
	HC16XX_FUNC_IR,

	/* EJTAG */
	HC16XX_FUNC_EJTAG,

#if 0
	/* PLL test outputs */
	HC16XX_FUNC_CPU_DIG_PLL_TFCK,
	HC16XX_FUNC_CPU1_DIG_PLL_TFCK,
	HC16XX_FUNC_VE_DIG_PLL_TFCK,
	HC16XX_FUNC_DE_DIG_PLL_TFCK,

	/* Power outputs */
	HC16XX_FUNC_POK0_OUT11,
	HC16XX_FUNC_POK0_OUT33,
	HC16XX_FUNC_POK1_OUT11,
	HC16XX_FUNC_POK1_OUT33,
#endif

	/* Misc */
	HC16XX_FUNC_SARADC,

#if 0
	HC16XX_FUNC_F_CRYSTAL,
	HC16XX_FUNC_USB_DBG_STATUS,
#endif

	HC16XX_FUNCTION_COUNT,
};

struct hc16xx_mux_data {
	s16 pin;
	u16 mux_sel;
};

struct hc16xx_function_desc {
	const char *name;
	const struct hc16xx_mux_data *pins;
};

static const struct hc16xx_function_desc hc16xx_functions[] = {
	[HC16XX_FUNC_GPIO] = {
		.name = "gpio",
		.pins = (struct hc16xx_mux_data[]) {
			/* L0..31 pins */
			{   0, 0 }, {   1, 0 }, {   2, 0 }, {   3, 0 },
			{   4, 0 }, {   5, 0 }, {   6, 0 }, {   7, 0 },
			{   8, 0 }, {   9, 0 }, {  10, 0 }, {  11, 0 },
			{  12, 0 }, {  13, 0 }, {  14, 0 }, {  15, 0 },
			{  16, 0 }, {  17, 0 }, {  18, 0 }, {  19, 0 },
			{  20, 0 }, {  21, 0 }, {  22, 0 }, {  23, 0 },
			{  24, 0 }, {  25, 0 }, {  26, 0 }, {  27, 0 },
			{  28, 0 }, {  29, 0 }, {  30, 0 }, {  31, 0 },

			/* B0..31 pins */
			{  32, 0 }, {  33, 0 }, {  34, 0 }, {  35, 0 },
			{  36, 0 }, {  37, 0 }, {  38, 0 }, {  39, 0 },
			{  40, 0 }, {  41, 0 }, {  42, 0 }, {  43, 0 },
			{  44, 0 }, {  45, 0 }, {  46, 0 }, {  47, 0 },
			{  48, 0 }, {  49, 0 }, {  50, 0 }, {  51, 0 },
			{  52, 0 }, {  53, 0 }, {  54, 0 }, {  55, 0 },
			{  56, 0 }, {  57, 0 }, {  58, 0 }, {  59, 0 },
			{  60, 0 }, {  61, 0 }, {  62, 0 }, {  63, 0 },

			/* R0..31 pins */
			{  64, 0 }, {  65, 0 }, {  66, 0 }, {  67, 0 },
			{  68, 0 }, {  69, 0 }, {  70, 0 }, {  71, 0 },
			{  72, 0 }, {  73, 0 }, {  74, 0 }, {  75, 0 },
			{  76, 0 }, {  77, 0 }, {  78, 0 }, {  79, 0 },
			{  80, 0 }, {  81, 0 }, {  82, 0 }, {  83, 0 },
			{  84, 0 }, {  85, 0 }, {  86, 0 }, {  87, 0 },
			{  88, 0 }, {  89, 0 }, {  90, 0 }, {  91, 0 },
			{  92, 0 }, {  93, 0 }, {  94, 0 }, {  95, 0 },

			/* T0..31 pins */
			{  96, 0 }, {  97, 0 }, {  98, 0 }, {  99, 0 },
			{ 100, 0 }, { 101, 0 }, { 102, 0 }, { 103, 0 },
			{ 104, 0 }, { 105, 0 }, { 106, 0 }, { 107, 0 },
			{ 108, 0 }, { 109, 0 }, { 110, 0 }, { 111, 0 },
			{ 112, 0 }, { 113, 0 }, { 114, 0 }, { 115, 0 },
			{ 116, 0 }, { 117, 0 }, { 118, 0 }, { 119, 0 },
			{ 120, 0 }, { 121, 0 }, { 122, 0 }, { 123, 0 },
			{ 124, 0 }, { 125, 0 }, { 126, 0 }, { 127, 0 },

			/* sentinel */
			{  -1, 0 },
		},
	},
	[HC16XX_FUNC_UART0] = {
		.name = "uart0",
		.pins = (struct hc16xx_mux_data[]) {
			{      12,  1 }, /* L12 UART0_TX  */
			{      13,  1 }, /* L13 UART0_RX  */
			{      15,  2 }, /* L15 UART0_RTS */
			{      16,  2 }, /* L16 UART0_CTS */
			{      17,  6 }, /* L17 UART0_TX  */
			{      18,  6 }, /* L18 UART0_RX  */
			{ 32 +  4,  6 }, /* B04 UART0_RTS */
			{ 32 +  6,  6 }, /* B06 UART0_CTS */
			{ 32 +  8,  1 }, /* B08 UART0_TX  */
			{ 32 +  9,  1 }, /* B09 UART0_RX  */
			{ 64 +  9,  1 }, /* R09 UART0_TX  */
			{ 64 + 10,  1 }, /* R10 UART0_RX  */
			{ 96 + 11,  1 }, /* T11 UART0_TX  */
			{ 96 + 12,  1 }, /* T12 UART0_RX  */
			{ 96 + 22,  4 }, /* T22 UART0_TX  */
			{ 96 + 23,  4 }, /* T23 UART0_RX  */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_UART1] = {
		.name = "uart1",
		.pins = (struct hc16xx_mux_data[]) {
			{      15, 10 }, /* L15 UART1_RTS */
			{      16, 10 }, /* L16 UART1_CTS */
			{      17,  2 }, /* L17 UART1_TX  */
			{      18,  2 }, /* L18 UART1_RX  */
			{ 32 +  4,  7 }, /* B04 UART1_RTS */
			{ 32 +  5,  2 }, /* B05 UART1_TX  */
			{ 32 +  6,  2 }, /* B06 UART1_RX  */
			{ 32 +  6,  7 }, /* B06 UART1_CTS */
			{ 64 +  0,  4 }, /* R00 UART1_TX  */
			{ 64 +  1,  4 }, /* R01 UART1_TX  */
			{ 64 +  6,  4 }, /* R06 UART1_RX  */
			{ 96 + 13,  2 }, /* T13 UART1_TX  */
			{ 96 + 14,  2 }, /* T14 UART1_TX  */
			{ 96 + 19,  2 }, /* T19 UART1_RX  */
			{ 96 + 20,  2 }, /* T20 UART1_RTS */
			{ 96 + 21,  2 }, /* T21 UART1_CTS */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_UART2] = {
		.name = "uart2",
		.pins = (struct hc16xx_mux_data[]) {
			{       8,  2 }, /* L08 UART2_TX  */
			{       9,  2 }, /* L09 UART2_RX  */
			{      12,  7 }, /* L12 UART2_TX  */
			{      13,  7 }, /* L13 UART2_RX  */
			{      15,  7 }, /* L15 UART2_RTS */
			{      16,  7 }, /* L16 UART2_CTS */
			{      17,  3 }, /* L17 UART2_TX  */
			{      18,  3 }, /* L18 UART2_RX  */
			{ 32 + 19,  3 }, /* B19 UART2_TX  */
			{ 32 + 20,  3 }, /* B20 UART2_RX  */
			{ 64 +  1,  7 }, /* R01 UART2_TX  */
			{ 64 +  6,  7 }, /* R06 UART2_RX  */
			{ 64 +  7,  3 }, /* R07 UART2_RTS */
			{ 64 +  8,  3 }, /* R08 UART2_CTS */
			{ 96 + 11,  2 }, /* T11 UART2_TX  */
			{ 96 + 12,  2 }, /* T12 UART2_RX  */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_UART3] = {
		.name = "uart3",
		.pins = (struct hc16xx_mux_data[]) {
			{      12,  4 }, /* L12 UART3_TX  */
			{      13,  4 }, /* L13 UART3_RX  */
			{      19,  3 }, /* L19 UART3_TX  */
			{      20,  3 }, /* L20 UART3_RX  */
			{ 32 +  4,  8 }, /* B04 UART3_RTS */
			{ 32 +  6,  8 }, /* B06 UART3_CTS */
			{ 32 +  8,  3 }, /* B08 UART3_TX  */
			{ 32 +  9,  3 }, /* B09 UART3_RX  */
			{ 32 + 10,  3 }, /* B10 UART3_TX  */
			{ 32 + 11,  3 }, /* B11 UART3_RX  */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_I2C0] = {
		.name = "i2c0",
		.pins = (struct hc16xx_mux_data[]) {
			{       6,  2 }, /* L06 I2C0_SDA */
			{       7,  2 }, /* L07 I2C0_SCL */
			{      12,  5 }, /* L12 I2C0_SDA */
			{      13,  5 }, /* L13 I2C0_SCL */
			{      15,  3 }, /* L15 I2C0_SDA */
			{      16,  3 }, /* L16 I2C0_SCL */
			{      17,  1 }, /* L17 I2C0_SDA */
			{      18,  1 }, /* L18 I2C0_SCL */
			{ 32 +  8,  2 }, /* B08 I2C0_SCL */
			{ 32 +  9,  2 }, /* B09 I2C0_SDA */
			{ 32 + 21, 10 }, /* B21 I2C0_SCL */
			{ 32 + 22, 10 }, /* B22 I2C0_SDA */
			{ 96 + 13,  3 }, /* T13 I2C0_SCL */
			{ 96 + 14,  3 }, /* T14 I2C0_SCL */
			{ 96 + 19,  3 }, /* T19 I2C0_SDA */
			{ 96 + 22,  2 }, /* T22 I2C0_SDA */
			{ 96 + 23,  2 }, /* T23 I2C0_SCL */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_I2C1] = {
		.name = "i2c1",
		.pins = (struct hc16xx_mux_data[]) {
			{       6,  3 }, /* L06 I2C1_SDA */
			{       7,  3 }, /* L07 I2C1_SCL */
			{      12,  2 }, /* L12 I2C1_SDA */
			{      13,  2 }, /* L13 I2C1_SCL */
			{      15,  4 }, /* L15 I2C1_SDA */
			{      16,  4 }, /* L16 I2C1_SCL */
			{ 32 +  0,  2 }, /* B00 I2C1_SCL */
			{ 32 +  1,  2 }, /* B01 I2C1_SDA */
			{ 32 + 23, 10 }, /* B23 I2C1_SCL */
			{ 32 + 24, 10 }, /* B24 I2C1_SDA */
			{ 64 +  0,  3 }, /* R00 I2C1_SCL */
			{ 64 +  1,  3 }, /* R01 I2C1_SDA */
			{ 64 +  5,  3 }, /* R05 I2C1_SCL */
			{ 64 +  6,  3 }, /* R06 I2C1_SDA */
			{ 64 +  9,  2 }, /* R09 I2C1_SCL */
			{ 64 + 10,  2 }, /* R10 I2C1_SDA */
			{ 96 + 11,  4 }, /* T11 I2C1_SDA */
			{ 96 + 12,  4 }, /* T12 I2C1_SCL */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_I2C2] = {
		.name = "i2c2",
		.pins = (struct hc16xx_mux_data[]) {
			{      10,  2 }, /* L10 I2C2_SDA */
			{      11,  2 }, /* L11 I2C2_SCL */
			{      19,  1 }, /* L19 I2C2_SDA */
			{      20,  1 }, /* L20 I2C2_SCL */
			{ 32 +  2,  2 }, /* B02 I2C2_SCL */
			{ 32 +  3,  2 }, /* B03 I2C2_SDA */
			{ 32 + 19,  2 }, /* B19 I2C2_SCL */
			{ 32 + 20,  2 }, /* B20 I2C2_SDA */
			{ 64 +  2,  3 }, /* R02 I2C2_SCL */
			{ 64 +  4,  3 }, /* R04 I2C2_SDA */
			{ 96 +  6,  2 }, /* T06 I2C2_SCL */
			{ 96 +  7,  2 }, /* T07 I2C2_SDA */
			{ 96 + 22,  3 }, /* T22 I2C2_SDA */
			{ 96 + 23,  3 }, /* T23 I2C2_SCL */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_I2C3] = {
		.name = "i2c3",
		.pins = (struct hc16xx_mux_data[]) {
			{       9,  5 }, /* L09 I2C3_SDA */
			{      10,  5 }, /* L10 I2C3_SCL */
			{      12,  8 }, /* L12 I2C3_SDA */
			{      13,  8 }, /* L13 I2C3_SCL */
			{      17,  8 }, /* L17 I2C3_SDA */
			{      18,  8 }, /* L18 I2C3_SCL */
			{      19,  2 }, /* L19 I2C3_SDA */
			{      20,  2 }, /* L20 I2C3_SCL */
			{ 32 +  4,  2 }, /* B04 I2C3_SCL */
			{ 32 +  7,  2 }, /* B07 I2C3_SDA */
			{ 32 + 10,  2 }, /* B10 I2C3_SCL */
			{ 32 + 11,  2 }, /* B11 I2C3_SDA */
			{ 64 +  0,  2 }, /* R00 I2C3_SCL */
			{ 64 +  1,  2 }, /* R01 I2C3_SCL */
			{ 64 +  6,  2 }, /* R06 I2C3_SDA */
			{ 64 +  7,  2 }, /* R07 I2C3_SCL */
			{ 64 +  8,  2 }, /* R08 I2C3_SDA */
			{ 64 +  9,  3 }, /* R09 I2C3_SCL */
			{ 64 + 10,  3 }, /* R10 I2C3_SDA */
			{ 96 +  1,  4 }, /* T01 I2C3_SCL */
			{ 96 +  2,  4 }, /* T02 I2C3_SDA */
			{ 96 +  8,  2 }, /* T08 I2C3_SCL */
			{ 96 +  9,  2 }, /* T09 I2C3_SDA */
			{ 96 + 13,  6 }, /* T13 I2C3_SCL */
			{ 96 + 14,  6 }, /* T14 I2C3_SCL */
			{ 96 + 19,  6 }, /* T19 I2C3_SDA */
			{ 96 + 22,  8 }, /* T22 I2C3_SDA */
			{ 96 + 23,  8 }, /* T23 I2C3_SCL */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_SPI] = {
		.name = "spi",
		.pins = (struct hc16xx_mux_data[]) {
			{       1, 10 }, /* L01 SPI_MOSI  */
			{       2, 10 }, /* L02 SPI_CLK   */
			{       3, 10 }, /* L03 SPIM_DCX  */
			{       4, 10 }, /* L04 SPI_MISO  */
			{       5, 10 }, /* L05 SPI_CSJ   */
			{ 32 + 12, 10 }, /* B12 SPI_MOSI  */
			{ 32 + 13, 10 }, /* B13 SPI_CLK   */
			{ 32 + 15, 10 }, /* B15 SPIM_DCX  */
			{ 32 + 16, 10 }, /* B16 SPI_MISO  */
			{ 32 + 17, 10 }, /* B17 SPI_CSJ   */
			{ 32 + 18,  4 }, /* B18 SPIM_DCX  */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_I2S_CLK] = {
		.name = "i2s-clk",
		.pins = (struct hc16xx_mux_data[]) {
			{       2,  2 }, /* L02 I2S_MCLK  */
			{       3,  2 }, /* L03 I2S_LRCLK */
			{       4,  2 }, /* L03 I2S_BCLK  */
			{ 32 +  4,  3 }, /* B04 I2S_MCLK  */
			{ 32 +  5,  3 }, /* B05 I2S_LRCLK */
			{ 32 +  6,  3 }, /* B06 I2S_BCLK  */
			{ 32 + 13,  3 }, /* B13 I2S_MCLK  */
			{ 32 + 15,  3 }, /* B15 I2S_LRCLK */
			{ 32 + 16,  3 }, /* B16 I2S_BCLK  */
			{ 32 + 21,  6 }, /* B21 I2S_MCLK  */
			{ 32 + 22,  6 }, /* B22 I2S_LRCLK */
			{ 32 + 23,  6 }, /* B23 I2S_BCLK  */
			{ 64 +  1, 10 }, /* R01 I2S_BCLK  */
			{ 64 +  8, 10 }, /* R08 I2S_LRCLK */
			{ 64 + 10, 10 }, /* R10 I2S_MCLK  */
			{ 96 +  7,  3 }, /* T07 I2S_MCLK  */
			{ 96 +  8,  3 }, /* T07 I2S_LRCLK */
			{ 96 +  9,  3 }, /* T07 I2S_BCLK  */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_I2S_DATA] = {
		.name = "i2s-data",
		.pins = (struct hc16xx_mux_data[]) {
			{       1,  2 }, /* L01 I2S_DATAI   */
			{       5,  2 }, /* L05 I2S_DATAO_0 */
			{       6, 10 }, /* L06 I2S_DATAO_1 */
			{       7, 10 }, /* L07 I2S_DATAO_2 */
			{       8, 10 }, /* L08 I2S_DATAO_3 */
			{ 32 +  0, 10 }, /* B00 I2S_DATAO_1 */
			{ 32 +  1, 10 }, /* B01 I2S_DATAO_2 */
			{ 32 +  2, 10 }, /* B02 I2S_DATAO_3 */
			{ 32 +  3,  4 }, /* B03 I2S_DATAI   */
			{ 32 +  7,  3 }, /* B07 I2S_DATAO   */
			{ 32 + 12,  3 }, /* B12 I2S_DATAI   */
			{ 32 + 15,  6 }, /* B15 I2S_DATAI   */
			{ 32 + 17,  3 }, /* B17 I2S_DATAO   */
			{ 32 + 21,  3 }, /* B21 I2S_DATAO_1 */
			{ 32 + 22,  3 }, /* B22 I2S_DATAO_2 */
			{ 32 + 23,  3 }, /* B23 I2S_DATAO_3 */
			{ 32 + 24,  6 }, /* B24 I2S_DATAO   */
			{ 64 +  6, 10 }, /* R06 I2S_DATAO_0 */
			{ 64 +  9, 10 }, /* R09 I2S_DATAI   */
			{ 96 +  6,  3 }, /* T06 I2S_DATAI   */
			{ 96 + 10,  3 }, /* T10 I2S_DATAO_0 */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_I2S0] = {
		.name = "i2s0",
		.pins = (struct hc16xx_mux_data[]) {
			{ 64 +  7,  7 }, /* R07 I2S0_LRCK  */
			{ 64 +  8,  7 }, /* R08 I2S0_BCLK  */
			{ 64 +  9,  7 }, /* R09 I2S0_DATAI */
			{ 64 + 10,  7 }, /* R10 I2S0_MCLK  */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_I2S1] = {
		.name = "i2s1",
		.pins = (struct hc16xx_mux_data[]) {
			{ 64 +  0,  7 }, /* R00 I2S1_DATAI */
			{ 64 +  2,  7 }, /* R02 I2S1_MCLK  */
			{ 64 +  3,  7 }, /* R03 I2S1_LRCK  */
			{ 64 +  4,  7 }, /* R04 I2S1_BCLK  */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_I2S2] = {
		.name = "i2s2",
		.pins = (struct hc16xx_mux_data[]) {
			{ 32 + 21,  4 }, /* B21 I2S2_DATAI */
			{ 32 + 22,  4 }, /* B22 I2S2_MCLK  */
			{ 32 + 23,  4 }, /* B23 I2S2_LRCK */
			{ 32 + 24,  4 }, /* B24 I2S2_BCLK  */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_PCM] = {
		.name = "pcm",
		.pins = (struct hc16xx_mux_data[]) {
			{       1,  4 }, /* L01 PCMI_DATA */
			{       2,  4 }, /* L02 PCMO_FS   */
			{       3,  4 }, /* L03 PCMO_BCLK */
			{       5,  4 }, /* L05 PCMO_DATA */
			{ 32 + 12,  5 }, /* B12 PCMI_DATA */
			{ 32 + 13,  5 }, /* B13 PCMO_FS   */
			{ 32 + 15,  5 }, /* B15 PCMO_BCLK */
			{ 32 + 17,  5 }, /* B17 PCMO_DATA */
			{ 96 +  6,  5 }, /* T06 PCMI_DATA */
			{ 96 +  7,  5 }, /* T07 PCMO_FS   */
			{ 96 +  8,  5 }, /* T08 PCMO_BCLK */
			{ 96 + 10,  5 }, /* T10 PCMO_DATA */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_PCM0] = {
		.name = "pcm0",
		.pins = (struct hc16xx_mux_data[]) {
			{       1,  5 }, /* L01 PCM0_DATA  */
			{       2,  5 }, /* L02 PCM0_FS    */
			{       4,  5 }, /* L04 PCM0_MCLK  */
			{       5,  5 }, /* L05 PCM0_BCLK  */
			{ 32 + 12,  6 }, /* B12 PCM0_DATA  */
			{ 32 + 13,  6 }, /* B13 PCM0_FS    */
			{ 32 + 16,  6 }, /* B16 PCM0_MCLK  */
			{ 32 + 17,  6 }, /* B17 PCM0_BCLK  */
			{ 32 + 19,  5 }, /* B19 PCM0_MCLK  */
			{ 64 +  3,  4 }, /* R03 PCM0_MCLK  */
			{ 64 +  7,  8 }, /* R07 PCM0_BCLK  */
			{ 64 +  9,  8 }, /* R09 PCM0_DATAI */
			{ 64 + 10,  8 }, /* R10 PCM0_FS    */
			{ 96 +  6,  6 }, /* T06 PCM0_DATA  */
			{ 96 +  7,  6 }, /* T07 PCM0_FS    */
			{ 96 +  8,  6 }, /* T08 PCM0_BCLK  */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_PCM1] = {
		.name = "pcm1",
		.pins = (struct hc16xx_mux_data[]) {
			{ 64 +  0,  8 }, /* R00 PCM1_FS    */
			{ 64 +  1,  8 }, /* R01 PCM1_BCLK  */
			{ 64 +  4,  4 }, /* R04 PCM1_MCLK  */
			{ 64 +  8,  8 }, /* R08 PCM1_DATAI */
			{ 64 + 10,  9 }, /* R10 PCM1_MCLK  */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_PCM2] = {
		.name = "pcm2",
		.pins = (struct hc16xx_mux_data[]) {
			{ 64 +  2,  8 }, /* R02 PCM2_DATAI */
			{ 64 +  3,  8 }, /* R03 PCM2_FS    */
			{ 64 +  4,  8 }, /* R04 PCM2_BCLK  */
			{ 64 +  7,  9 }, /* R07 PCM2_MCLK */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_TDM] = {
		.name = "tdm",
		.pins = (struct hc16xx_mux_data[]) {
			{       1,  6 }, /* L01 TDM_DATA  */
			{       2,  6 }, /* L02 TDM_FS    */
			{       5,  6 }, /* L05 TDM_BCLK  */
			{ 32 + 12,  7 }, /* B12 TDM_DATA  */
			{ 32 + 13,  7 }, /* B13 TDM_FS    */
			{ 32 + 16,  7 }, /* B16 TDM_MCLK  */
			{ 32 + 17,  7 }, /* B17 TDM_BCLK  */
			{ 32 + 19,  6 }, /* B19 TDM_MCLK  */
			{ 64 +  0, 10 }, /* R00 TDM_MCLK  */
			{ 64 +  2, 10 }, /* R02 TDM_DATAI */
			{ 64 +  3, 10 }, /* R03 TDM_FS    */
			{ 64 +  4, 10 }, /* R04 TDM_BCLK  */
			{ 96 +  6,  7 }, /* T06 TDM_DATA  */
			{ 96 +  7,  7 }, /* T07 TDM_FS    */
			{ 96 +  8,  7 }, /* T08 TDM_BCLK  */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_PDM0] = {
		.name = "pdm0",
		.pins = (struct hc16xx_mux_data[]) {
			{       1,  7 }, /* L01 PDM0_DATA  */
			{       5,  7 }, /* L05 PDM0_CLK   */
			{ 32 + 12,  8 }, /* B12 PDM0_DATA  */
			{ 32 + 17,  8 }, /* B17 PDM0_CLK   */
			{ 64 +  8,  9 }, /* R08 PDM0_CLK   */
			{ 64 +  9,  9 }, /* R09 PDM0_DATAI */
			{ 96 +  6,  8 }, /* T06 PDM0_DATA  */
			{ 96 +  9,  8 }, /* T09 PDM0_CLK   */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_PDM1] = {
		.name = "pdm1",
		.pins = (struct hc16xx_mux_data[]) {
			{ 64 +  0,  9 }, /* R00 PDM1_DATAI */
			{ 64 +  1,  9 }, /* R01 PDM1_CLK   */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_PDM2] = {
		.name = "pdm2",
		.pins = (struct hc16xx_mux_data[]) {
			{ 64 +  2,  9 }, /* R02 PDM2_DATAI */
			{ 64 +  4,  9 }, /* R04 PDM2_CLK   */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_SSI] = {
		.name = "ssi",
		.pins = (struct hc16xx_mux_data[]) {
			{       1,  3 }, /* L01 SSI_MDI0    */
			{       2,  3 }, /* L02 SSI_MICLK   */
			{       3,  3 }, /* L03 SSI_MIVALA  */
			{       4,  3 }, /* L04 SSI_MISTART */
			{       6,  1 }, /* L06 SSI_MDI_0   */
			{       7,  1 }, /* L07 SSI_MICLK   */
			{       9,  1 }, /* L09 SSI_MIVALA  */
			{      10,  1 }, /* L10 SSI_MISTART */
			{ 32 +  0,  3 }, /* B00 SSI_MDI_0   */
			{ 32 +  1,  3 }, /* B01 SSI_MICLK   */
			{ 32 +  2,  3 }, /* B02 SSI_MIVALA  */
			{ 32 +  3,  3 }, /* B03 SSI_MISTART */
			{ 32 + 12,  4 }, /* B12 SSI_MDI_0   */
			{ 32 + 13,  4 }, /* B13 SSI_MICLK   */
			{ 32 + 15,  4 }, /* B15 SSI_MIVALA  */
			{ 32 + 16,  4 }, /* B16 SSI_MISTART */
			{ 32 + 21,  2 }, /* B21 SSI_MDI_0   */
			{ 32 + 22,  2 }, /* B22 SSI_MICLK   */
			{ 32 + 23,  2 }, /* B23 SSI_MIVALA  */
			{ 32 + 24,  3 }, /* B24 SSI_MISTART */
			{ 96 +  6,  4 }, /* T06 SSI_MDI1    */
			{ 96 +  8,  4 }, /* T08 SSI_MICLK   */
			{ 96 +  9,  4 }, /* T09 SSI_MIVALA  */
			{ 96 + 10,  4 }, /* T10 SSI_MISTART */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_SSI_ERR] = {
		.name = "ssi-err",
		.pins = (struct hc16xx_mux_data[]) {
			{ 32 + 15,  9 }, /* B15 SSI_MIERR */
			{ 32 + 17,  9 }, /* B17 SSI_MIERR */
			{ 96 +  6, 10 }, /* T06 SSI_ERR   */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_SPDIF] = {
		.name = "spdif",
		.pins = (struct hc16xx_mux_data[]) {
			{       0,  1 }, /* L00 SPDIF */
			{       5,  3 }, /* L05 SPDIF */
			{ 32 + 17,  4 }, /* B17 SPDIF */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_SPDIF_IN] = {
		.name = "spdif-in",
		.pins = (struct hc16xx_mux_data[]) {
			{       0, 10 }, /* L00 SPDIF_IN_PAD */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_PWM0] = {
		.name = "pwm0",
		.pins = (struct hc16xx_mux_data[]) {
			{       0,  4 }, /* L00 PWM_0 */
			{      11,  4 }, /* L11 PWM_0 */
			{      15,  5 }, /* L15 PWM_0 */
			{ 32 +  0,  5 }, /* B00 PWM_0 */
			{ 32 +  3,  5 }, /* B03 PWM_0 */
			{ 32 + 10,  5 }, /* B10 PWM_0 */
			{ 32 + 21,  5 }, /* B21 PWM_0 */
			{ 64 +  3,  3 }, /* R03 PWM_0 */
			{ 96 +  0,  4 }, /* T00 PWM_0 */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_PWM1] = {
		.name = "pwm1",
		.pins = (struct hc16xx_mux_data[]) {
			{      10, 10 }, /* L10 PWM_1 */
			{      16,  5 }, /* L16 PWM_1 */
			{ 32 +  2,  5 }, /* B02 PWM_1 */
			{ 32 + 11,  5 }, /* B11 PWM_1 */
			{ 32 + 22,  5 }, /* B22 PWM_1 */
			{ 64 +  2,  4 }, /* R02 PWM_1 */
			{ 96 +  9,  5 }, /* T09 PWM_1 */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_PWM2] = {
		.name = "pwm2",
		.pins = (struct hc16xx_mux_data[]) {
			{       6,  4 }, /* L06 PWM_2 */
			{      14,  5 }, /* L14 PWM_2 */
			{      17,  5 }, /* L17 PWM_2 */
			{ 32 +  1,  5 }, /* B01 PWM_2 */
			{ 32 +  6,  5 }, /* B06 PWM_2 */
			{ 32 + 23,  5 }, /* B23 PWM_2 */
			{ 64 +  7, 10 }, /* R07 PWM_2 */
			{ 64 +  8, 11 }, /* R08 PWM_2 */
			{ 96 + 12,  5 }, /* T12 PWM_2 */
			{ 96 + 19,  5 }, /* T19 PWM_2 */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_PWM3] = {
		.name = "pwm3",
		.pins = (struct hc16xx_mux_data[]) {
			{       4,  4 }, /* L04 PWM_3 */
			{       7,  4 }, /* L07 PWM_3 */
			{      18,  5 }, /* L18 PWM_3 */
			{ 32 +  5,  5 }, /* B05 PWM_3 */
			{ 32 + 14,  5 }, /* B14 PWM_3 */
			{ 32 + 24,  5 }, /* B24 PWM_3 */
			{ 64 +  5,  6 }, /* R05 PWM_3 */
			{ 96 + 11,  5 }, /* T11 PWM_3 */
			{ 96 + 20,  5 }, /* T20 PWM_3 */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_PWM4] = {
		.name = "pwm4",
		.pins = (struct hc16xx_mux_data[]) {
			{       5,  8 }, /* L05 PWM_4 */
			{       8,  4 }, /* L08 PWM_4 */
			{      19,  5 }, /* L19 PWM_4 */
			{ 32 +  4,  5 }, /* B04 PWM_4 */
			{ 32 + 16,  5 }, /* B16 PWM_4 */
			{ 64 +  6,  6 }, /* R06 PWM_4 */
			{ 96 + 14, 10 }, /* T14 PWM_4 */
			{ 96 + 21,  5 }, /* T21 PWM_4 */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_PWM5] = {
		.name = "pwm5",
		.pins = (struct hc16xx_mux_data[]) {
			{       9,  4 }, /* L09 PWM_5 */
			{      20,  5 }, /* L20 PWM_5 */
			{ 32 +  7,  5 }, /* B07 PWM_5 */
			{ 32 + 18,  5 }, /* B18 PWM_5 */
			{ 96 + 10,  7 }, /* T10 PWM_5 */
			{ 96 + 13,  5 }, /* T13 PWM_5 */
			{ 96 + 14,  5 }, /* T14 PWM_5 */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_VIN] = {
		.name = "video-in",
		.pins = (struct hc16xx_mux_data[]) {
			{ 64 +  0,  5 }, /* R00 VIN_D4  */
			{ 64 +  1,  5 }, /* R01 VIN_D5  */
			{ 64 +  2,  5 }, /* R02 VIN_D6  */
			{ 64 +  3,  5 }, /* R03 VIN_D7  */
			{ 64 +  4,  5 }, /* R04 VIN_HS  */
			{ 64 +  5,  5 }, /* R05 VIN_VS  */
			{ 64 +  6,  5 }, /* R06 VIN_CLK */
			{ 64 +  7,  5 }, /* R07 VIN_D2  */
			{ 64 +  8,  5 }, /* R08 VIN_D3  */
			{ 64 +  9,  5 }, /* R09 VIN_D0  */
			{ 64 + 10,  5 }, /* R10 VIN_D1  */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_RMII] = {
		.name = "rmii",
		.pins = (struct hc16xx_mux_data[]) {
			{ 64 +  0,  6 }, /* R00 RMII_MDIO   */
			{ 64 +  1,  6 }, /* R01 RMII_CRS_DV */
			{ 64 +  2,  6 }, /* R02 RMII_RXD_0  */
			{ 64 +  3,  6 }, /* R03 RMII_RXD_1  */
			{ 64 +  4,  6 }, /* R04 RMII_CLK    */
			{ 64 +  7,  6 }, /* R07 RMII_TXE    */
			{ 64 +  8,  6 }, /* R08 RMII_MDC    */
			{ 64 +  9,  6 }, /* R09 RMII_TXD_1  */
			{ 64 + 10,  6 }, /* R10 RMII_TXD_0  */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_SDMMC] = {
		.name = "sdmmc",
		.pins = (struct hc16xx_mux_data[]) {
			{ 96 +  0,  1 }, /* T00 SD_D1     */
			{ 96 +  1,  1 }, /* T01 SD_D0     */
			{ 96 +  2,  1 }, /* T02 SD_CLK    */
			{ 96 +  3,  1 }, /* T03 SD_CMD    */
			{ 96 +  4,  1 }, /* T04 SD_D3     */
			{ 96 +  5,  1 }, /* T05 SD_D2     */
			{ 96 +  6,  1 }, /* T06 SD_D4     */
			{ 96 +  7,  1 }, /* T07 SD_D5     */
			{ 96 +  8,  1 }, /* T08 SD_D6     */
			{ 96 +  9,  1 }, /* T09 SD_D7     */
			{ 96 + 10,  1 }, /* T10 MMC_RST_N */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_SFSPI] = {
		.name = "sfspi",
		.pins = (struct hc16xx_mux_data[]) {
			{ 64 +  0,  1 }, /* R00 SF_CSJ1  */
			{ 64 +  1,  1 }, /* R01 SF_WJ    */
			{ 64 +  2,  1 }, /* R02 SF_CSJ0  */
			{ 64 +  3,  1 }, /* R03 SF_MISO  */
			{ 64 +  4,  1 }, /* R04 SF_MOSI  */
			{ 64 +  5,  1 }, /* R05 SF_CLK   */
			{ 64 +  6,  1 }, /* R06 SF_HOLDJ */
			{ 96 + 13,  1 }, /* T13 SF_CSJ1  */
			{ 96 + 14,  1 }, /* T14 SF_WJ    */
			{ 96 + 15,  1 }, /* T15 SF_CSJ0  */
			{ 96 + 16,  1 }, /* T16 SF_MISO  */
			{ 96 + 17,  1 }, /* T17 SF_MOSI  */
			{ 96 + 18,  1 }, /* T18 SF_CLK   */
			{ 96 + 19,  1 }, /* T19 SF_HOLDJ */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_NAND] = {
		.name = "nand",
		.pins = (struct hc16xx_mux_data[]) {
			{ 32 + 10,  1 }, /* B10 NF_DATA_0 */
			{ 32 + 11,  1 }, /* B11 NF_DATA_1 */
			{ 32 + 12,  1 }, /* B12 NF_DATA_2 */
			{ 32 + 13,  1 }, /* B13 NF_DATA_3 */
			{ 32 + 14,  1 }, /* B14 NF_DQS    */
			{ 32 + 15,  1 }, /* B15 NF_DATA_4 */
			{ 32 + 16,  1 }, /* B16 NF_DATA_5 */
			{ 32 + 17,  1 }, /* B17 NF_DATA_6 */
			{ 32 + 18,  1 }, /* B18 NF_DATA_7 */
			{ 32 + 19,  1 }, /* B19 NF_RDJ    */
			{ 32 + 20,  1 }, /* B20 NF_CEJ    */
			{ 32 + 21,  1 }, /* B21 NF_CLE    */
			{ 32 + 22,  1 }, /* B22 NF_ALE    */
			{ 32 + 23,  1 }, /* B23 NF_WRJ    */
			{ 32 + 24,  1 }, /* B24 NF_WPJ    */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_NAND_SM] = {
		.name = "nand-sm",
		.pins = (struct hc16xx_mux_data[]) {
			{      14,  2 }, /* L14 NF_SMREJ_TST */
			{ 32 + 12,  9 }, /* B12 NF_SMREJ_TST */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_IR] = {
		.name = "ir",
		.pins = (struct hc16xx_mux_data[]) {
			{      14,  1 }, /* L14 IR */
			{ 32 + 18, 10 }, /* B18 IR */
			{ 64 +  8,  1 }, /* R08 IR */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_EJTAG] = {
		.name = "ejtag",
		.pins = (struct hc16xx_mux_data[]) {
			{       1,  1 }, /* L01 EJ_TRSTJ */
			{       2,  1 }, /* L02 EJ_TDI   */
			{       3,  1 }, /* L03 EJ_TDO   */
			{       4,  1 }, /* L04 EJ_TMS   */
			{       5,  1 }, /* L05 EJ_TCLK  */
			{ 32 + 12,  2 }, /* B12 EJ_TRSTJ */
			{ 32 + 13,  2 }, /* B13 EJ_TDI   */
			{ 32 + 15,  2 }, /* B15 EJ_TDO   */
			{ 32 + 16,  2 }, /* B16 EJ_TMS   */
			{ 32 + 17,  2 }, /* B17 EJ_TCLK  */
			{      -1,  0 },
		},
	},
	[HC16XX_FUNC_SARADC] = {
		.name = "saradc",
		.pins = (struct hc16xx_mux_data[]) {
			{ 32 +  0,  8 }, /* B00 SAR_INT        */
			{ 32 +  1,  8 }, /* B01 ADC8B_SW_SEL   */
			{ 32 +  2,  8 }, /* B02 SAR_DOUT_0     */
			{ 32 +  3,  8 }, /* B03 SAR_DOUT_1     */
			{ 32 +  5,  8 }, /* B05 SAR_DOUT_2     */
			{ 32 +  7,  8 }, /* B07 SAR_DOUT_3     */
			{ 32 +  8,  8 }, /* B08 SAR_DOUT_4     */
			{ 32 +  9,  8 }, /* B09 SAR_DOUT_5     */
			{ 32 + 10,  8 }, /* B10 SAR_DOUT_6     */
			{ 32 + 11,  8 }, /* B11 SAR_DOUT_7     */
			{ 32 + 13,  8 }, /* B13 SAR_INT_DOUT_0 */
			{ 32 + 14,  8 }, /* B14 SAR_INT_DOUT_1 */
			{ 32 + 15,  8 }, /* B15 SAR_INT_DOUT_2 */
			{ 32 + 16,  8 }, /* B16 SAR_INT_DOUT_3 */
			{ 32 + 18,  8 }, /* B18 SAR_INT_DOUT_4 */
			{ 32 + 19,  8 }, /* B19 SAR_INT_DOUT_5 */
			{ 32 + 20,  8 }, /* B20 SAR_INT_DOUT_6 */
			{ 32 + 21,  8 }, /* B21 SAR_INT_DOUT_7 */
			{      -1,  0 },
		},
	},
};

static const struct pinctrl_pin_desc hc16xx_pins[] = {
	PINCTRL_PIN(0,   "l00"),
	PINCTRL_PIN(1,   "l01"),
	PINCTRL_PIN(2,   "l02"),
	PINCTRL_PIN(3,   "l03"),
	PINCTRL_PIN(4,   "l04"),
	PINCTRL_PIN(5,   "l05"),
	PINCTRL_PIN(6,   "l06"),
	PINCTRL_PIN(7,   "l07"),
	PINCTRL_PIN(8,   "l08"),
	PINCTRL_PIN(9,   "l09"),
	PINCTRL_PIN(10,  "l10"),
	PINCTRL_PIN(11,  "l11"),
	PINCTRL_PIN(12,  "l12"),
	PINCTRL_PIN(13,  "l13"),
	PINCTRL_PIN(14,  "l14"),
	PINCTRL_PIN(15,  "l15"),
	PINCTRL_PIN(16,  "l16"),
	PINCTRL_PIN(17,  "l17"),
	PINCTRL_PIN(18,  "l18"),
	PINCTRL_PIN(19,  "l19"),
	PINCTRL_PIN(20,  "l20"),
	PINCTRL_PIN(21,  "l21"),
	PINCTRL_PIN(22,  "l22"),
	PINCTRL_PIN(23,  "l23"),
	PINCTRL_PIN(24,  "l24"),
	PINCTRL_PIN(25,  "l25"),
	PINCTRL_PIN(26,  "l26"),
	PINCTRL_PIN(27,  "l27"),
	PINCTRL_PIN(28,  "l28"),
	PINCTRL_PIN(29,  "l29"),
	PINCTRL_PIN(30,  "l30"),
	PINCTRL_PIN(31,  "l31"),

	PINCTRL_PIN(32,  "b00"),
	PINCTRL_PIN(33,  "b01"),
	PINCTRL_PIN(34,  "b02"),
	PINCTRL_PIN(35,  "b03"),
	PINCTRL_PIN(36,  "b04"),
	PINCTRL_PIN(37,  "b05"),
	PINCTRL_PIN(38,  "b06"),
	PINCTRL_PIN(39,  "b07"),
	PINCTRL_PIN(40,  "b08"),
	PINCTRL_PIN(41,  "b09"),
	PINCTRL_PIN(42,  "b10"),
	PINCTRL_PIN(43,  "b11"),
	PINCTRL_PIN(44,  "b12"),
	PINCTRL_PIN(45,  "b13"),
	PINCTRL_PIN(46,  "b14"),
	PINCTRL_PIN(47,  "b15"),
	PINCTRL_PIN(48,  "b16"),
	PINCTRL_PIN(49,  "b17"),
	PINCTRL_PIN(50,  "b18"),
	PINCTRL_PIN(51,  "b19"),
	PINCTRL_PIN(52,  "b20"),
	PINCTRL_PIN(53,  "b21"),
	PINCTRL_PIN(54,  "b22"),
	PINCTRL_PIN(55,  "b23"),
	PINCTRL_PIN(56,  "b24"),
	PINCTRL_PIN(57,  "b25"),
	PINCTRL_PIN(58,  "b26"),
	PINCTRL_PIN(59,  "b27"),
	PINCTRL_PIN(60,  "b28"),
	PINCTRL_PIN(61,  "b29"),
	PINCTRL_PIN(62,  "b30"),
	PINCTRL_PIN(63,  "b31"),

	PINCTRL_PIN(64,  "r00"),
	PINCTRL_PIN(65,  "r01"),
	PINCTRL_PIN(66,  "r02"),
	PINCTRL_PIN(67,  "r03"),
	PINCTRL_PIN(68,  "r04"),
	PINCTRL_PIN(69,  "r05"),
	PINCTRL_PIN(70,  "r06"),
	PINCTRL_PIN(71,  "r07"),
	PINCTRL_PIN(72,  "r08"),
	PINCTRL_PIN(73,  "r09"),
	PINCTRL_PIN(74,  "r10"),
	PINCTRL_PIN(75,  "r11"),
	PINCTRL_PIN(76,  "r12"),
	PINCTRL_PIN(77,  "r13"),
	PINCTRL_PIN(78,  "r14"),
	PINCTRL_PIN(79,  "r15"),
	PINCTRL_PIN(80,  "r16"),
	PINCTRL_PIN(81,  "r17"),
	PINCTRL_PIN(82,  "r18"),
	PINCTRL_PIN(83,  "r19"),
	PINCTRL_PIN(84,  "r20"),
	PINCTRL_PIN(85,  "r21"),
	PINCTRL_PIN(86,  "r22"),
	PINCTRL_PIN(87,  "r23"),
	PINCTRL_PIN(88,  "r24"),
	PINCTRL_PIN(89,  "r25"),
	PINCTRL_PIN(90,  "r26"),
	PINCTRL_PIN(91,  "r27"),
	PINCTRL_PIN(92,  "r28"),
	PINCTRL_PIN(93,  "r29"),
	PINCTRL_PIN(94,  "r30"),
	PINCTRL_PIN(95,  "r31"),

	PINCTRL_PIN(96,  "t00"),
	PINCTRL_PIN(97,  "t01"),
	PINCTRL_PIN(98,  "t02"),
	PINCTRL_PIN(99,  "t03"),
	PINCTRL_PIN(100, "t04"),
	PINCTRL_PIN(101, "t05"),
	PINCTRL_PIN(102, "t06"),
	PINCTRL_PIN(103, "t07"),
	PINCTRL_PIN(104, "t08"),
	PINCTRL_PIN(105, "t09"),
	PINCTRL_PIN(106, "t10"),
	PINCTRL_PIN(107, "t11"),
	PINCTRL_PIN(108, "t12"),
	PINCTRL_PIN(109, "t13"),
	PINCTRL_PIN(110, "t14"),
	PINCTRL_PIN(111, "t15"),
	PINCTRL_PIN(112, "t16"),
	PINCTRL_PIN(113, "t17"),
	PINCTRL_PIN(114, "t18"),
	PINCTRL_PIN(115, "t19"),
	PINCTRL_PIN(116, "t20"),
	PINCTRL_PIN(117, "t21"),
	PINCTRL_PIN(118, "t22"),
	PINCTRL_PIN(119, "t23"),
	PINCTRL_PIN(120, "t24"),
	PINCTRL_PIN(121, "t25"),
	PINCTRL_PIN(122, "t26"),
	PINCTRL_PIN(123, "t27"),
	PINCTRL_PIN(124, "t28"),
	PINCTRL_PIN(125, "t29"),
	PINCTRL_PIN(126, "t30"),
	PINCTRL_PIN(127, "t31"),
};

#define HC16XX_MAX_PINS 128

struct hc16xx_pinctrl {
	struct device *dev;
	void __iomem *base;

	struct pinctrl_dev *pctldev;
	struct pinctrl_desc desc;
};

static inline u32 pin_to_reg_offset(unsigned int pin)
{
	u32 reg, bank;

	bank = pin / 32;
	reg = pin % 32;
	return  mux_bases[bank] + reg;
}

static int __maybe_unused
hc16xx_pin_get_mux(struct hc16xx_pinctrl *pctl, unsigned int pin)
{
	u8 val;

	if (pin >= HC16XX_MAX_PINS)
		return -EINVAL;

	val = readb(pctl->base + pin_to_reg_offset(pin));

	return val & 0xf;
}

static int hc16xx_pin_set_mux(struct hc16xx_pinctrl *pctl,
			      unsigned int pin,
			      unsigned int mux)
{

	u8 val;
	u32 reg;

	if (pin >= HC16XX_MAX_PINS)
		return -EINVAL;

	reg = pin_to_reg_offset(pin);

	val = readb(pctl->base + reg);
	val &= 0xf0;
	val |= mux;

	writeb(val, pctl->base + reg);
	return 0;
}

static int hc16xx_pin_get_drive(struct hc16xx_pinctrl *pctl,
				unsigned int pin)
{
	if (pin >= HC16XX_MAX_PINS)
		return -EINVAL;

	return readb(pctl->base + pin_to_reg_offset(pin)) >> 4;
}

static int hc16xx_pin_set_drive(struct hc16xx_pinctrl *pctl,
				unsigned int pin,
				unsigned int drive)
{
	u8 val;
	u32 reg;

	if (pin >= HC16XX_MAX_PINS)
		return -EINVAL;

	reg = pin_to_reg_offset(pin);

	val = readb(pctl->base + reg);
	val &= 0x0f;
	val |= drive << 4;

	writeb(val, pctl->base + reg);
	return 0;
}

static int hc16xx_find_mux(enum hc16xx_function func, unsigned int pin)
{
	const struct hc16xx_function_desc *fdesc = &hc16xx_functions[func];
	const struct hc16xx_mux_data *cur = fdesc->pins;

	if (func >= HC16XX_FUNCTION_COUNT)
		return -EINVAL;

	while (cur->pin >= 0) {
		if (cur->pin == pin)
			return cur->mux_sel;
		cur++;
	}

	return -EINVAL;
}

static int hc16xx_str_to_func(enum hc16xx_function *func_e, const char *func_s)
{
	int selector = 0;

	for (; selector < HC16XX_FUNCTION_COUNT; selector++) {
		if (0 == strcmp(func_s, hc16xx_functions[selector].name)) {
			*func_e = (enum hc16xx_function)selector;
			return 0;
		}
	}

	return -ENOENT;
}

static int hc16xx_str_to_pin_id(const char *pin)
{
	int len = strlen(pin);
	int base = -1;
	int offset;

	if (len < 2 || len > 3)
		return -EINVAL;

	if (pin[0] == 'l' || pin[0] == 'L') {
		base = 0;
	} else if (pin[0] == 'b' || pin[0] == 'B') {
		base = 32;
	} else if (pin[0] == 'r' || pin[0] == 'R') {
		base = 64;
	} else if (pin[0] == 't' || pin[0] == 'T') {
		base = 96;
	} else
		return -EINVAL;

	if (pin[1] < '0' || pin[1] > '9')
		return -EINVAL;

	if (len == 2)
		return base + pin[1] - '0';

	if (pin[2] < '0' || pin[2] > '9')
		return -EINVAL;

	offset  = 10 * (pin[1] - '0');
	offset += pin[2] - '0';
	if (offset >= 32)
		return -EINVAL;

	return base + offset;
}

static int hc16xx_dt_node_to_map(struct pinctrl_dev *pctldev,
				 struct device_node *np,
				 struct pinctrl_map **map,
				 unsigned int *num_maps)
{
	struct hc16xx_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	const char *function;
	const char *pin;
	enum hc16xx_function func_e;
	struct pinctrl_map *maps;
	unsigned long *config = NULL;
	unsigned int drive_strength;
	int npins;
	int nmaps;
	int ret;
	int i;

	ret = of_property_read_string(np, "function", &function);
	if (ret)
		return ret;

	if (hc16xx_str_to_func(&func_e, function)) {
		dev_err(pctl->dev,
			"OF: function %s doesn't exist\n", function);
		return -EINVAL;
	}

	npins = of_property_count_strings(np, "pins");
	if (npins <= 0)
		return -EINVAL;

	/*
	 * Optional drive-strength.
	 */
	ret = of_property_read_u32(np, "drive-strength",
				   &drive_strength);
	if (ret == -EINVAL) {
		drive_strength = 0;
	} else if (ret) {
		return ret;
	} else if (drive_strength < 1 || drive_strength > 16) {
		dev_err(pctl->dev,
			"OF: invalid drive-strength %u\n",
			drive_strength);
		return -EINVAL;
	}

	nmaps = npins + (drive_strength ? npins : 0);

	maps = kcalloc(nmaps, sizeof(*maps), GFP_KERNEL);
	if (!maps)
		return -ENOMEM;

	if (drive_strength) {
		config = kmalloc(sizeof(*config), GFP_KERNEL);
		if (!config) {
			kfree(maps);
			return -ENOMEM;
		}

		*config = pinconf_to_config_packed(
			PIN_CONFIG_DRIVE_STRENGTH,
			drive_strength);
	}

	/*
	 * MUX.
	 */
	for (i = 0; i < npins; i++) {
		int pin_id;

		ret = of_property_read_string_index(np, "pins", i, &pin);
		if (ret)
			goto err;

		pin_id = hc16xx_str_to_pin_id(pin);
		if (pin_id < 0) {
			dev_err(pctl->dev,
				"OF: invalid pin %s\n", pin);
			ret = -EINVAL;
			goto err;
		}

		if (hc16xx_find_mux(func_e, pin_id) < 0) {
			dev_err(pctl->dev,
				"OF: pin %s doesn't support function %s\n",
				pin, function);
			ret = -EINVAL;
			goto err;
		}

		maps[i].type = PIN_MAP_TYPE_MUX_GROUP;
		maps[i].data.mux.group = hc16xx_pins[pin_id].name;
		maps[i].data.mux.function = function;
	}

	/*
	 * pinconf.
	 */
	if (config) {
		for (i = 0; i < npins; i++) {
			int pin_id;

			ret = of_property_read_string_index(np, "pins",
							    i, &pin);
			if (ret)
				goto err;

			pin_id = hc16xx_str_to_pin_id(pin);
			if (pin_id < 0) {
				ret = -EINVAL;
				goto err;
			}

			maps[npins + i].type = PIN_MAP_TYPE_CONFIGS_PIN;
			maps[npins + i].data.configs.group_or_pin =
				hc16xx_pins[pin_id].name;

			maps[npins + i].data.configs.configs = config;
			maps[npins + i].data.configs.num_configs = 1;
		}
	}

	*map = maps;
	*num_maps = nmaps;

	return 0;

err:
	kfree(config);
	kfree(maps);
	return ret;
}

static void hc16xx_dt_free_map(struct pinctrl_dev *pctldev,
			       struct pinctrl_map *map,
			       unsigned int num_maps)
{
	// FIXME free configs
	//if (num_maps && map[npins].data.configs.configs)
	//	kfree(map[npins].data.configs.configs);

	kfree(map);
}

static const struct pinctrl_ops hc16xx_pinctrl_ops = {
	.get_groups_count	= pinctrl_generic_get_group_count,
	.get_group_name		= pinctrl_generic_get_group_name,
	.get_group_pins		= pinctrl_generic_get_group_pins,
	.dt_node_to_map		= hc16xx_dt_node_to_map,
	.dt_free_map		= hc16xx_dt_free_map,
};

static int hc16xx_set_mux(struct pinctrl_dev *pctldev,
			  unsigned int function,
			  unsigned int group)
{
	struct hc16xx_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	int mux;

	mux = hc16xx_find_mux(function, group);
	if (mux < 0)
		return mux;

	return hc16xx_pin_set_mux(pctl, group, mux);
}

static int hc16xx_gpio_request_enable(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range,
				      unsigned int offset)
{
	struct hc16xx_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	int mux;

	if (offset >= ARRAY_SIZE(hc16xx_pins))
		return -EINVAL;

	mux = hc16xx_find_mux(HC16XX_FUNC_GPIO, offset);
	if (mux < 0)
		return mux;

	return hc16xx_pin_set_mux(pctl, offset, mux);
}

static const struct pinmux_ops hc16xx_pinmux_ops = {
	.get_functions_count	= pinmux_generic_get_function_count,
	.get_function_name	= pinmux_generic_get_function_name,
	.get_function_groups	= pinmux_generic_get_function_groups,
	.set_mux		= hc16xx_set_mux,
	.gpio_request_enable	= hc16xx_gpio_request_enable,
	.strict			= true,
};

static int hc16xx_pinconf_get(struct pinctrl_dev *pctldev,
			      unsigned int pin,
			      unsigned long *config)
{
	struct hc16xx_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	unsigned int arg;
	int drive;

	param = pinconf_to_config_param(*config);

	switch (param) {
	case PIN_CONFIG_DRIVE_STRENGTH:
		drive = hc16xx_pin_get_drive(pctl, pin);

		/* 0..15 -> 1..16 mA */
		arg = drive + 1;

		*config = pinconf_to_config_packed(param, arg);
		return 0;

	default:
		return -ENOTSUPP;
	}
}

static int hc16xx_pinconf_set(struct pinctrl_dev *pctldev,
			      unsigned int pin,
			      unsigned long *configs,
			      unsigned int num_configs)
{
	struct hc16xx_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	unsigned int i;

	for (i = 0; i < num_configs; i++) {
		enum pin_config_param param;
		unsigned int arg;

		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_DRIVE_STRENGTH:
			if (arg < 1 || arg > 16)
				return -EINVAL;

			/* 1..16 mA -> 0..15 */
			return hc16xx_pin_set_drive(pctl, pin, arg - 1);

		default:
			return -ENOTSUPP;
		}
	}

	return 0;
}

static const struct pinconf_ops hc16xx_pinconf_ops = {
	.pin_config_get = hc16xx_pinconf_get,
	.pin_config_set = hc16xx_pinconf_set,
	.is_generic = true,
};

static int hc16xx_register_function(struct hc16xx_pinctrl *pctl,
				    const struct hc16xx_function_desc *desc)
{
	int num_groups = 0;
	const char **groups = NULL;
	const struct hc16xx_mux_data *cur;

	cur = desc->pins;
	while (cur->pin >= 0) {
		cur++;
		num_groups++;
	}

	if (num_groups == 0)
		return -EINVAL;

	groups = devm_kzalloc(pctl->dev, sizeof (*groups) * num_groups,
			      GFP_KERNEL);
	if (!groups)
		return -ENOMEM;

	cur = desc->pins;
	for (int i = 0; i < num_groups; i++, cur++)
		groups[i] = hc16xx_pins[cur->pin].name;

	return pinmux_generic_add_function(
		pctl->pctldev,
		desc->name,
		groups, num_groups, pctl
	);
}

static int hc16xx_register_groups_and_functions(struct hc16xx_pinctrl *pctl)
{
	int err;

	for (int grp = 0; grp < ARRAY_SIZE(hc16xx_pins); grp++) {
		const struct pinctrl_pin_desc *desc = &hc16xx_pins[grp];

		err = pinctrl_generic_add_group(pctl->pctldev, desc->name,
						&desc->number, 1, pctl);
		if (err < 0) {
			dev_err(pctl->dev, "can't register pin group %s %d\n",
				desc->name, err);
			return err;
		}
	}

	for (int fun = 0; fun < HC16XX_FUNCTION_COUNT; fun++) {
		const struct hc16xx_function_desc *desc = &hc16xx_functions[fun];

		err = hc16xx_register_function(pctl, desc);
		if (err < 0) {
			dev_err(pctl->dev, "can't register function %s: %d\n",
				desc->name, err);
			return err;
		}
	}
	return 0;
}

static int hc16xx_pinctrl_probe(struct platform_device *pdev)
{
	struct hc16xx_pinctrl *pctl;
	struct resource *res;
	int err;

	pctl = devm_kzalloc(&pdev->dev, sizeof(*pctl), GFP_KERNEL);
	if (!pctl)
		return -ENOMEM;

	pctl->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pctl->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pctl->base))
		return PTR_ERR(pctl->base);

	pctl->desc.name = dev_name(&pdev->dev);
	pctl->desc.owner = THIS_MODULE;

	pctl->desc.pctlops = &hc16xx_pinctrl_ops;
	pctl->desc.pmxops = &hc16xx_pinmux_ops;
	pctl->desc.confops = &hc16xx_pinconf_ops;

	pctl->desc.pins = hc16xx_pins;
	pctl->desc.npins = ARRAY_SIZE(hc16xx_pins);

	pctl->pctldev = devm_pinctrl_register(&pdev->dev,
					      &pctl->desc, pctl);
	if (IS_ERR(pctl->pctldev))
		return PTR_ERR(pctl->pctldev);

	platform_set_drvdata(pdev, pctl);

	err = hc16xx_register_groups_and_functions(pctl);
	if (err)
		return err;

	dev_info(&pdev->dev, "registered %u pins\n",
		 ARRAY_SIZE(hc16xx_pins));

	return 0;
}

static const struct of_device_id hc16xx_pinctrl_of_match[] = {
	{ .compatible = "hichip,hc16xx-pinctrl" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, hc16xx_pinctrl_of_match);

static struct platform_driver hc16xx_pinctrl_driver = {
	.driver = {
		.name = "hc16xx-pinctrl",
		.of_match_table = hc16xx_pinctrl_of_match,
	},
	.probe = hc16xx_pinctrl_probe,
};
module_platform_driver(hc16xx_pinctrl_driver);

MODULE_DESCRIPTION("HiChip HC16xx pinctrl driver");
MODULE_AUTHOR("Siarhei Volkau");
MODULE_LICENSE("GPL");
