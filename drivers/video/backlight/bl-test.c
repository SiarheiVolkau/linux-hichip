// SPDX-License-Identifier: GPL-2.0
/*
 * Generic backlight API test driver
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <uapi/linux/fb.h>

#define BL_TEST_DELAY_MS	2000

struct bl_test {
	struct backlight_device *backlight;
};

static void bl_test_power(struct backlight_device *bl, bool on)
{
	bl->props.power = on ? FB_BLANK_UNBLANK : FB_BLANK_POWERDOWN;
	backlight_update_status(bl);
}

static void bl_test_set_brightness(struct backlight_device *bl,
				   unsigned int brightness)
{
	bl->props.brightness = brightness;
	backlight_update_status(bl);
}

static int bl_test_probe(struct platform_device *pdev)
{
	struct bl_test *test;
	struct device_node *bl_np;
	struct backlight_device *bl;
	unsigned int max, middle;

	bl_np = of_parse_phandle(pdev->dev.of_node, "backlight", 0);
	if (!bl_np)
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "missing backlight property\n");

	bl = of_find_backlight_by_node(bl_np);
	of_node_put(bl_np);

	if (!bl)
		return dev_err_probe(&pdev->dev, -EPROBE_DEFER,
				     "backlight not available\n");

	test = devm_kzalloc(&pdev->dev, sizeof(*test), GFP_KERNEL);
	if (!test) {
		put_device(&bl->dev);
		return -ENOMEM;
	}

	test->backlight = bl;
	platform_set_drvdata(pdev, test);

	max = bl->props.max_brightness;
	middle = max / 2;

	dev_info(&pdev->dev, "brightness range: 0..%u\n", max);

	/* Disabled */
	dev_info(&pdev->dev, "test: disabled\n");
	bl_test_power(bl, false);
	msleep(BL_TEST_DELAY_MS);

	/* Minimum brightness */
	dev_info(&pdev->dev, "test: minimum brightness (%u)\n", 0);
	bl_test_power(bl, true);
	bl_test_set_brightness(bl, 0);
	msleep(BL_TEST_DELAY_MS);

	/* Maximum brightness */
	dev_info(&pdev->dev, "test: maximum brightness (%u)\n", max);
	bl_test_set_brightness(bl, max);
	msleep(BL_TEST_DELAY_MS);

	/* Middle brightness */
	dev_info(&pdev->dev, "test: middle brightness (%u)\n", middle);
	bl_test_set_brightness(bl, middle);
	msleep(BL_TEST_DELAY_MS);

	/* Leave the backlight disabled */
	dev_info(&pdev->dev, "test: disabled\n");
	bl_test_power(bl, false);

	dev_info(&pdev->dev, "backlight test complete\n");

	return 0;
}

static void bl_test_remove(struct platform_device *pdev)
{
	struct bl_test *test = platform_get_drvdata(pdev);

	bl_test_power(test->backlight, false);
	put_device(&test->backlight->dev);
}

static const struct of_device_id bl_test_of_match[] = {
	{ .compatible = "backlight-test" },
	{ }
};
MODULE_DEVICE_TABLE(of, bl_test_of_match);

static struct platform_driver bl_test_driver = {
	.probe = bl_test_probe,
	.remove = bl_test_remove,
	.driver = {
		.name = "bl-test",
		.of_match_table = bl_test_of_match,
	},
};
module_platform_driver(bl_test_driver);

MODULE_DESCRIPTION("Generic backlight API test driver");
MODULE_AUTHOR("Siarhei Volkau <lis8215@gmail.com>");
MODULE_LICENSE("GPL");
