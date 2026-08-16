// SPDX-License-Identifier: GPL-2.0
/*
 * HiChip HC16XX PWM driver
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

#define HC16XX_PWM_NUM_CHANNELS 6
#define HC16XX_PWM_COUNTER_MAX  GENMASK(15, 0)

#define PWM_CTRL_REG(priv) ((priv)->base + 0x00)
#define  PWM_GLOBAL_ENABLE BIT(31)
#define  PWM_CLOCK_ENABLE  BIT(30)
#define  PWM_CLKDIV_MASK   GENMASK(29, 16)
#define PWM_CHAN_CTRL_REG(priv,ch) ((priv)->base + 0x04 + (ch))
#define  PWM_CHAN_POLARITY BIT(4)
#define  PWM_CHAN_ENABLE   BIT(7)
#define PWM_CHAN_COUNTER_REG(priv,ch) ((priv)->base + 0x0c + 4 * (ch))
#define  PWM_LOW_COUNTER_MASK  GENMASK(15, 0)
#define  PWM_HIGH_COUNTER_MASK GENMASK(31, 16)

struct hc16xx_pwm {
	void __iomem *base;

	struct clk *clk;
	u32 enabled_bits;
	u32 pwm0_savestate;
};

static void hc16xx_pwm_disable(struct hc16xx_pwm *pwm,
			       unsigned int channel)
{
	u32 val;
	u32 prev = pwm->enabled_bits;

	pwm->enabled_bits &= ~BIT(channel);
	if (prev == pwm->enabled_bits)
		return;

	if (pwm->enabled_bits == 0) {
		if (channel != 0) {
			val = readb(PWM_CHAN_CTRL_REG(pwm, channel));
			val &= ~PWM_CHAN_ENABLE;
			writeb(val, PWM_CHAN_CTRL_REG(pwm, channel));
		}
		val = readb(PWM_CHAN_CTRL_REG(pwm, 0));
		val &= ~PWM_CHAN_ENABLE;
		writeb(val, PWM_CHAN_CTRL_REG(pwm, 0));

		/* clear global enable and gate the clock */
		val = readl(PWM_CTRL_REG(pwm));
		val &= ~(PWM_GLOBAL_ENABLE | PWM_CLOCK_ENABLE);
		writel(val, PWM_CTRL_REG(pwm));
	} else if (channel != 0) {
		val = readb(PWM_CHAN_CTRL_REG(pwm, channel));
		val &= ~PWM_CHAN_ENABLE;
		writeb(val, PWM_CHAN_CTRL_REG(pwm, channel));
	} else {
		/* PWM0 must remain enabled if other PWMs enabled
		 * so we just set counters to zero here to avoid
		 * unwanted pin toggling. */
		writel(0, PWM_CHAN_COUNTER_REG(pwm, 0));
	}
}

static void hc16xx_pwm_enable(struct hc16xx_pwm *pwm,
			      unsigned int channel)
{
	u32 val;
	u32 prev = pwm->enabled_bits;

	pwm->enabled_bits |= BIT(channel);
	if (prev == pwm->enabled_bits)
		return;

	if (prev == 0) {
		/* set global enable and ungate the clock */
		val = readl(PWM_CTRL_REG(pwm));
		val |= PWM_GLOBAL_ENABLE | PWM_CLOCK_ENABLE;
		writel(val, PWM_CTRL_REG(pwm));
	}
	if (channel != 0 && prev == 0) {
		/* PWM0 must be enabled if other PWMs enabled */
		writel(0, PWM_CHAN_COUNTER_REG(pwm, 0));

		val = readb(PWM_CHAN_CTRL_REG(pwm, 0));
		val |= PWM_CHAN_ENABLE;
		writeb(val, PWM_CHAN_CTRL_REG(pwm, 0));
	} else if (channel == 0) {
		writel(pwm->pwm0_savestate,
		       PWM_CHAN_COUNTER_REG(pwm, channel));
	}

	val = readb(PWM_CHAN_CTRL_REG(pwm, channel));
	val |= PWM_CHAN_ENABLE;
	writeb(val, PWM_CHAN_CTRL_REG(pwm, channel));
}

static u64 ns_to_pwm_ticks(struct hc16xx_pwm *pwm, u64 ns)
{
	unsigned long freq = clk_get_rate(pwm->clk);
	u32 div = FIELD_GET(PWM_CLKDIV_MASK, readl(PWM_CTRL_REG(pwm)));

	return DIV_ROUND_CLOSEST_ULL(ns * freq, (u64)(div + 1) * NSEC_PER_SEC);
}

static u64 pwm_ticks_to_ns(struct hc16xx_pwm *pwm, u64 ticks)
{
	unsigned long freq = clk_get_rate(pwm->clk);
	u32 div = FIELD_GET(PWM_CLKDIV_MASK, readl(PWM_CTRL_REG(pwm)));

	if (!freq)
		return 0;

	return DIV_ROUND_CLOSEST_ULL(ticks * (div + 1) * NSEC_PER_SEC, freq);
}

static int hc16xx_pwm_check_timings(struct hc16xx_pwm *pwm,
				    const struct pwm_state *state)
{
	u64 ticks = ns_to_pwm_ticks(pwm, state->period);
	u64 duty_ticks = ns_to_pwm_ticks(pwm, state->duty_cycle);

	if (ticks == 0)
		return -ERANGE;
	if (ticks < duty_ticks)
		return -EINVAL;
	if (duty_ticks > HC16XX_PWM_COUNTER_MAX)
		return -ERANGE;
	if ((ticks - duty_ticks) > HC16XX_PWM_COUNTER_MAX)
		return -ERANGE;

	return 0;
}

static void hc16xx_pwm_set_timings(struct hc16xx_pwm *pwm,
				   unsigned int channel,
				   const struct pwm_state *state)
{
	u64 ticks = ns_to_pwm_ticks(pwm, state->period);
	u64 duty_ticks = ns_to_pwm_ticks(pwm, state->duty_cycle);
	u32 regval;

	regval  = FIELD_PREP(PWM_LOW_COUNTER_MASK, ticks - duty_ticks);
	regval |= FIELD_PREP(PWM_HIGH_COUNTER_MASK, duty_ticks);

	if (channel == 0)
		pwm->pwm0_savestate = regval;
	else
		writel(regval, PWM_CHAN_COUNTER_REG(pwm, channel));

	regval = readb(PWM_CHAN_CTRL_REG(pwm, channel));
	if (state->polarity == PWM_POLARITY_NORMAL)
		regval &= ~PWM_CHAN_POLARITY;
	else
		regval |= PWM_CHAN_POLARITY;
	writeb(regval, PWM_CHAN_CTRL_REG(pwm, channel));
}

static int hc16xx_pwm_apply(struct pwm_chip *chip,
			    struct pwm_device *pwm,
			    const struct pwm_state *state)
{
	struct hc16xx_pwm *priv = pwmchip_get_drvdata(chip);
	unsigned int channel = pwm->hwpwm;
	int ret;

	if (channel >= HC16XX_PWM_NUM_CHANNELS)
		return -EINVAL;

	ret = hc16xx_pwm_check_timings(priv, state);
	if (ret)
		return ret;

	if (pwm->state.enabled)
		hc16xx_pwm_disable(priv, channel);

	hc16xx_pwm_set_timings(priv, channel, state);

	if (state->enabled)
		hc16xx_pwm_enable(priv, channel);

	return 0;
}

static int hc16xx_pwm_get_state(struct pwm_chip *chip,
				struct pwm_device *pwm,
				struct pwm_state *state)
{
	struct hc16xx_pwm *priv = pwmchip_get_drvdata(chip);
	unsigned int channel = pwm->hwpwm;
	u32 duty_ticks, period_ticks, val;

	if (channel == 0)
		val = priv->pwm0_savestate;
	else
		val = readl(PWM_CHAN_COUNTER_REG(priv, channel));
	duty_ticks = FIELD_GET(PWM_HIGH_COUNTER_MASK, val);
	period_ticks = FIELD_GET(PWM_LOW_COUNTER_MASK, val) + duty_ticks;
	state->period = pwm_ticks_to_ns(priv, period_ticks);
	state->duty_cycle = pwm_ticks_to_ns(priv, duty_ticks);

	state->enabled = !!(priv->enabled_bits & BIT(channel));

	val = readb(PWM_CHAN_CTRL_REG(priv, channel));
	val &= PWM_CHAN_POLARITY;
	state->polarity = val ? PWM_POLARITY_INVERSED : PWM_POLARITY_NORMAL;

	return 0;
}

static const struct pwm_ops hc16xx_pwm_ops = {
	.apply = hc16xx_pwm_apply,
	.get_state = hc16xx_pwm_get_state,
};

static int hc16xx_pwm_probe(struct platform_device *pdev)
{
	struct hc16xx_pwm *priv;
	struct pwm_chip *chip;
	struct resource *res;
	int ret;

	chip = devm_pwmchip_alloc(&pdev->dev, HC16XX_PWM_NUM_CHANNELS,
				  sizeof(*priv));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	priv = pwmchip_get_drvdata(chip);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	chip->ops = &hc16xx_pwm_ops;
	chip->atomic = true;

	platform_set_drvdata(pdev, priv);

	ret = devm_pwmchip_add(&pdev->dev, chip);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register PWM chip\n");

	return 0;
}

static const struct of_device_id hc16xx_pwm_of_match[] = {
	{
		.compatible = "hichip,hc16xx-pwm",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, hc16xx_pwm_of_match);

static struct platform_driver hc16xx_pwm_driver = {
	.probe = hc16xx_pwm_probe,
	.driver = {
		.name = "hc16xx-pwm",
		.of_match_table = hc16xx_pwm_of_match,
	},
};
module_platform_driver(hc16xx_pwm_driver);

MODULE_DESCRIPTION("HiChip HC16XX PWM driver");
MODULE_AUTHOR("Siarhei Volkau <lis8215@gmail.com>");
MODULE_LICENSE("GPL");
