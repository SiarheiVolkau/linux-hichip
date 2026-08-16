// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Driver for buttons behind serializer chips, such as
 *  74HC165.
 *
 *  Copyright (C) 2026 Siarhei Volkau <lis8215@gmail.com>
 *
 *  This file was based on: /drivers/input/keyboard/gpio_keys_polled.c
 *      Copyright (C) 2007-2010 Gabor Juhos <juhosg@openwrt.org>
 *      Copyright (C) 2010 Nuno Goncalves <nunojpg@gmail.com>
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>

#define DRV_NAME	"gpio-serializer-keys"

/* There are two distinct interfaces in the wild: 3-wire and 2-wire.
 *
 * 3-wire SNES-like interface; it has latch, clock and data lanes.
 * These lanes are all push-pull lines with fixed directions.
 * Rising-edge of the clock triggers shifting data out.
 * Falling-edge of the latch triggers latching.
 *
 * [latch]
 *      _______
 *     /       \
 * ___/         \_______________________________________________________________
 *
 * [clock]                 ______          ______          ______          _____
 *                        /      \        /      \        /      \        /
 * ______________________/        \______/        \______/        \______/
 *
 * [data]
 * ____________  ________  ______________  ______________  ______________  _____
 *             \/        \/              \/              \/              \/
 * ____________/\________/\______________/\______________/\______________/\_____
 *    x           key@0         key@1          key@2            key@3      key@4
 *    |--------|--------|-------|        |---------------|
 *      latch   sampling sampling          clock period
 *     duration  delay   delay
 *
 * 2-wire interface was found in some chinese devices with no labellings.
 * It has combined latch and data lane. The lane is open-drain bidirectional here.
 * Falling-edge of the clock triggers shifting data out.
 * Rising-edge of the latch triggers latching and data out.
 * One extra clock is requred to switch chip into input mode on latch&data lane.
 * Two-wire timings diagram:
 * [clock]
 * _____________________          ______          ______          ______
 *                      \        /      \        /      \        /      \
 *                       \______/        \______/        \______/        \______
 * [latch & data]
 * ___          _______  _______________  ______________  ______________  ______
 *    \        /       \/               \/              \/              \/
 *     \______/\_______/\_______________/\______________/\______________/\______
 *               key@0        key@1            key@2           key@3       key@4
 *    |--------|-------|-------|        |---------------|
 *      latch   sampling sampling         clock period
 *     duration  delay    delay
 */

struct gpio_serializer_keys_timings {
	u32 latch_duration_us;
	u32 sampling_delay_us;
	u32 clock_period_us;
	int num_clocks;
};

struct gpio_keys_platform_data {
	const u32 *button_codes;
	unsigned int poll_interval;
	unsigned int rep:1;
	const char *name;
	struct gpio_serializer_keys_timings timings;
};

struct gpio_serializer_keys_dev {
	struct input_dev *input;
	struct device *dev;
	const struct gpio_keys_platform_data *pdata;
	s8 *last_states;
	struct gpio_desc *latch_gpiod;
	struct gpio_desc *clock_gpiod;
	struct gpio_desc *data_gpiod;
	bool two_wire;
};

static void gpio_keys_button_event(struct gpio_serializer_keys_dev *bdev,
					 int idx, int state)
{
	u32 code;

	if (state < 0 || bdev->last_states[idx] == state)
		return;

	bdev->last_states[idx] = state;

	code = bdev->pdata->button_codes[idx];
	if (!code)
		return;

	input_event(bdev->input, EV_KEY, code, state);
}

static void gpio_serializer_keys_poll_3w(struct gpio_serializer_keys_dev *bdev)
{
	int state;
	int clock_phase2_us = bdev->pdata->timings.clock_period_us
				- bdev->pdata->timings.sampling_delay_us;

	if (clock_phase2_us < 0)
		clock_phase2_us = 0;

	gpiod_set_value_cansleep(bdev->latch_gpiod, 1);
	udelay(bdev->pdata->timings.latch_duration_us);
	gpiod_set_value_cansleep(bdev->latch_gpiod, 0);
	udelay(bdev->pdata->timings.sampling_delay_us);
	state = gpiod_get_value_cansleep(bdev->data_gpiod);
	gpio_keys_button_event(bdev, 0, state);

	for (int i = 1; i < bdev->pdata->timings.num_clocks; i++) {
		gpiod_set_value_cansleep(bdev->clock_gpiod, 1);
		udelay(bdev->pdata->timings.sampling_delay_us);
		state = gpiod_get_value_cansleep(bdev->data_gpiod);
		gpio_keys_button_event(bdev, i, state);
		gpiod_set_value_cansleep(bdev->clock_gpiod, 0);
		if (i >= bdev->pdata->timings.num_clocks - 1)
			break;
		udelay(clock_phase2_us);
	}
}

static void gpio_serializer_keys_poll_2w(struct gpio_serializer_keys_dev *bdev)
{
	int state;
	int clock_phase2_us = bdev->pdata->timings.clock_period_us
				- bdev->pdata->timings.sampling_delay_us;

	if (clock_phase2_us < 0)
		clock_phase2_us = 0;

	gpiod_set_value_cansleep(bdev->data_gpiod, 1);
	udelay(bdev->pdata->timings.latch_duration_us);
	gpiod_set_value_cansleep(bdev->data_gpiod, 0);
	udelay(bdev->pdata->timings.sampling_delay_us);
	state = gpiod_get_value_cansleep(bdev->data_gpiod);
	gpio_keys_button_event(bdev, 0, state);

	for (int i = 1; i < bdev->pdata->timings.num_clocks; i++) {
		gpiod_set_value_cansleep(bdev->clock_gpiod, 1);
		udelay(bdev->pdata->timings.sampling_delay_us);
		state = gpiod_get_value_cansleep(bdev->data_gpiod);
		gpio_keys_button_event(bdev, i, state);
		gpiod_set_value_cansleep(bdev->clock_gpiod, 0);
		udelay(clock_phase2_us);
	}
	/* Last clock cycle, prepares HW to detect next latch pulse */
	gpiod_set_value_cansleep(bdev->clock_gpiod, 1);
	udelay(bdev->pdata->timings.sampling_delay_us);
	gpiod_set_value_cansleep(bdev->clock_gpiod, 0);
}

static void gpio_serializer_keys_poll(struct input_dev *input)
{
	struct gpio_serializer_keys_dev *bdev = input_get_drvdata(input);

	if (bdev->two_wire) {
		gpio_serializer_keys_poll_2w(bdev);
	} else {
		gpio_serializer_keys_poll_3w(bdev);
	}

	input_sync(input);
}

static int gpio_serializer_keys_open(struct input_dev *input)
{
	struct gpio_serializer_keys_dev *bdev = input_get_drvdata(input);

	input_set_poll_interval(input, bdev->pdata->poll_interval);

	memset(bdev->last_states, -1, bdev->pdata->timings.num_clocks);
	/* report initial state of the buttons */
	gpio_serializer_keys_poll(input);

	return 0;
}

static void gpio_serializer_keys_close(struct input_dev *input)
{
	input_set_poll_interval(input, 0);
}

static struct gpio_keys_platform_data *
gpio_serializer_get_devtree_pdata(struct device *dev)
{
	struct gpio_keys_platform_data *pdata;
	u32 *button;
	u32 nclocks;
	int ret;

	ret = device_property_read_u32(dev, "num-clocks", &nclocks);
	if (ret < 0)
		return ERR_PTR(ret);

	ret = device_get_child_node_count(dev);
	if (ret == 0)
		return ERR_PTR(-EINVAL);

	pdata = devm_kzalloc(dev, sizeof(*pdata) + nclocks * sizeof(*button),
			     GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	button = (u32 *)(pdata + 1);

	pdata->button_codes = button;
	pdata->timings.num_clocks = nclocks;

	/* SNES defaults */
	pdata->timings.latch_duration_us = 12;
	pdata->timings.clock_period_us   =  6;
	pdata->timings.sampling_delay_us =  3;
	device_property_read_u32(dev, "latch-duration",
				 &pdata->timings.latch_duration_us);
	device_property_read_u32(dev, "clock-period",
				 &pdata->timings.clock_period_us);
	device_property_read_u32(dev, "sampling-delay",
				 &pdata->timings.sampling_delay_us);

	pdata->rep = device_property_present(dev, "autorepeat");
	device_property_read_u32(dev, "poll-interval", &pdata->poll_interval);

	device_property_read_string(dev, "label", &pdata->name);

	device_for_each_child_node_scoped(dev, child) {
		u32 reg;

		if (fwnode_property_read_u32(child, "reg", &reg)) {
			dev_err(dev, "button without reg property\n");
			return ERR_PTR(-EINVAL);
		}
		if (reg >= nclocks) {
			dev_err(dev, "invalid reg property %d,"
				" must be in range 0-%d\n", reg, nclocks - 1);
			return ERR_PTR(-EINVAL);
		}
		if (button[reg]) {
			dev_err(dev, "duplicate key@%d\n", reg);
			return ERR_PTR(-EINVAL);
		}
		if (fwnode_property_read_u32(child, "linux,code",
					     &button[reg])) {
			dev_err(dev, "button without keycode\n");
			return ERR_PTR(-EINVAL);
		}
	}

	return pdata;
}

static const struct of_device_id gpio_serializer_keys_of_match[] = {
	{ .compatible = "gpio-serializer-keys", },
	{ },
};
MODULE_DEVICE_TABLE(of, gpio_serializer_keys_of_match);

static int gpio_serializer_keys_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fwnode_handle *node = of_fwnode_handle(dev->of_node);
	const struct gpio_keys_platform_data *pdata;
	struct gpio_serializer_keys_dev *bdev;
	struct input_dev *input;
	int error;
	int i;

	pdata = gpio_serializer_get_devtree_pdata(dev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);

	if (!pdata->poll_interval) {
		dev_err(dev, "missing poll_interval value\n");
		return -EINVAL;
	}

	bdev = devm_kzalloc(dev, sizeof(*bdev) + pdata->timings.num_clocks,
			    GFP_KERNEL);
	if (!bdev) {
		dev_err(dev, "no memory for private data\n");
		return -ENOMEM;
	}

	bdev->latch_gpiod = devm_fwnode_gpiod_get_optional(
		dev, node, "latch", GPIOD_OUT_LOW, DRV_NAME"-latch");
	if (IS_ERR(bdev->latch_gpiod))
		return dev_err_probe(dev, PTR_ERR(bdev->latch_gpiod),
				     "failed to get latch-gpio\n");

	bdev->clock_gpiod = devm_fwnode_gpiod_get(
		dev, node, "clock", GPIOD_OUT_LOW, DRV_NAME"-clock");
	if (IS_ERR(bdev->clock_gpiod))
		return dev_err_probe(dev, PTR_ERR(bdev->clock_gpiod),
				     "failed to get clock-gpio\n");

	bdev->data_gpiod = devm_fwnode_gpiod_get(
		dev, node, "data", GPIOD_OUT_LOW, DRV_NAME"-data");
	if (IS_ERR(bdev->data_gpiod))
		return dev_err_probe(dev, PTR_ERR(bdev->data_gpiod),
				     "failed to get data-gpio\n");
	bdev->two_wire = !bdev->latch_gpiod;

	input = devm_input_allocate_device(dev);
	if (!input) {
		dev_err(dev, "no memory for input device\n");
		return -ENOMEM;
	}

	input_set_drvdata(input, bdev);

	input->name = pdata->name ?: pdev->name;
	input->phys = DRV_NAME"/input0";

	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;

	input->open = gpio_serializer_keys_open;
	input->close = gpio_serializer_keys_close;

	__set_bit(EV_KEY, input->evbit);
	if (pdata->rep)
		__set_bit(EV_REP, input->evbit);

	for (i = 0; i < pdata->timings.num_clocks; i++) {
		u32 button = pdata->button_codes[i];

		if (button)
			input_set_capability(input, EV_KEY, button);
	}

	bdev->input = input;
	bdev->dev = dev;
	bdev->pdata = pdata;
	bdev->last_states = (s8 *)(bdev + 1);

	error = input_setup_polling(input, gpio_serializer_keys_poll);
	if (error) {
		dev_err(dev, "unable to set up polling, err=%d\n", error);
		return error;
	}

	error = input_register_device(input);
	if (error) {
		dev_err(dev, "unable to register polled device, err=%d\n",
			error);
		return error;
	}

	return 0;
}

static struct platform_driver gpio_serializer_keys_driver = {
	.probe	= gpio_serializer_keys_probe,
	.driver	= {
		.name	= DRV_NAME,
		.of_match_table = gpio_serializer_keys_of_match,
	},
};
module_platform_driver(gpio_serializer_keys_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Siarhei Volkau <lis8215@gmail.com>");
MODULE_DESCRIPTION("Buttons Serializer with GPIO interface driver");
MODULE_ALIAS("platform:" DRV_NAME);
