#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqreturn.h>
#include <linux/sched_clock.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/version.h>

#define TIMER_CNT_REG 0x00
#define TIMER_CMP_REG 0x04
#define TIMER_CTL_REG 0x08

#define TIMER_CTL_EN	BIT(2)
#define TIMER_CTL_INT	BIT(3)
#define TIMER_CTL_IER	BIT(4)

static void __iomem *timer_base;

static void hc16xx_timer_stop(void)
{
	u8 val = readb(timer_base + TIMER_CTL_REG);

	writeb(val & ~TIMER_CTL_EN, timer_base + TIMER_CTL_REG);
}

static void hc16xx_timer_setup(unsigned long delay)
{
	writel((readl(timer_base + TIMER_CNT_REG) + delay),
		timer_base + TIMER_CMP_REG);
}

static void hc16xx_timer_start(void)
{
	u8 val = readb(timer_base + TIMER_CTL_REG);

	writeb(val | TIMER_CTL_EN, timer_base + TIMER_CTL_REG);
}

static void hc16xx_timer_enable_irq(void)
{
	u8 val = readb(timer_base + TIMER_CTL_REG);

	writeb(val | TIMER_CTL_IER, timer_base + TIMER_CTL_REG);
}

static int hc16xx_clkevt_shutdown(struct clock_event_device *evt)
{
	hc16xx_timer_stop();
	return 0;
}

static int hc16xx_clkevt_next_event(unsigned long evt,
				   struct clock_event_device *unused)
{
	hc16xx_timer_stop();
	hc16xx_timer_setup(evt);
	hc16xx_timer_start();
	return 0;
}

static u32 evt_cmp, evt_cnt;
static u8 evt_ctl;
static void hc16xx_clkevt_suspend(struct clock_event_device *evt)
{
	evt_cnt	= readl(timer_base + TIMER_CNT_REG);
	evt_cmp	= readl(timer_base + TIMER_CMP_REG);
	evt_ctl	= readb(timer_base + TIMER_CTL_REG);
}

static void hc16xx_clkevt_resume(struct clock_event_device *evt)
{
	writel(evt_cmp, timer_base + TIMER_CMP_REG);
	writel(evt_cnt,	timer_base + TIMER_CNT_REG);
	writeb(evt_ctl, timer_base + TIMER_CTL_REG);
}

static struct clock_event_device hc16xx_clockevent = {
	.name = "timer4",
	.rating = 300,
	.features = CLOCK_EVT_FEAT_ONESHOT,
	.set_state_shutdown = hc16xx_clkevt_shutdown,
	.set_state_oneshot = hc16xx_clkevt_shutdown,
	.tick_resume = hc16xx_clkevt_shutdown,
	.set_next_event = hc16xx_clkevt_next_event,
	.suspend = hc16xx_clkevt_suspend,
	.resume = hc16xx_clkevt_resume,
};


static irqreturn_t hc16xx_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = (struct clock_event_device *)dev_id;
	u8 val;

	/* clear interrupt flag */
	val = readb(timer_base + TIMER_CTL_REG);
	if (val & TIMER_CTL_INT) {
		writeb(val | TIMER_CTL_INT, timer_base + TIMER_CTL_REG);

		evt->event_handler(evt);
	}

	return IRQ_HANDLED;
}

static u64 notrace hc16xx_timer_sched_read(void)
{
	return readl(timer_base + TIMER_CNT_REG);
}

static int __init hc16xx_timer_init(struct device_node *node)
{
	int ret, irq;
	unsigned long timer_rate;
	struct clk *clk;

	timer_base = of_iomap(node, 0);
	if (!timer_base)
		panic("Can't map registers");

	irq = irq_of_parse_and_map(node, 0);
	if (irq <= 0)
		panic("Can't parse IRQ");

	clk = of_clk_get_by_name(node, "counting");
	if (IS_ERR(clk))
		panic("failed to get timer clock\n");
	timer_rate = clk_get_rate(clk);
	clk_prepare_enable(clk);

	/* set clk div 16*/
	timer_rate = timer_rate / 16;

	/* Make sure timer is stopped before playing with interrupts */
	hc16xx_timer_stop();

	sched_clock_register(hc16xx_timer_sched_read, 32, timer_rate);

	clocksource_mmio_init(timer_base + TIMER_CNT_REG, node->name,
			      timer_rate, 300, 32, clocksource_mmio_readl_up);

	hc16xx_clockevent.cpumask = cpu_possible_mask;
	hc16xx_clockevent.irq = irq;

	clockevents_config_and_register(&hc16xx_clockevent, timer_rate,
			0x50, UINT_MAX);

	ret = request_irq(irq, hc16xx_timer_interrupt,
			  IRQF_TIMER | IRQF_IRQPOLL, "hc16xx_timer",
			  &hc16xx_clockevent);
	if (ret)
		pr_warn("failed to setup irq %d\n", irq);

	/* Enable timer interrupt */
	hc16xx_timer_enable_irq();
	return 0;
}

TIMER_OF_DECLARE(sb_timer, "hichip,hc16xx-timer", hc16xx_timer_init);

