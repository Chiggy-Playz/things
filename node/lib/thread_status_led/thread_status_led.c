#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#include "thread_status_led/thread_status_led.h"
#include "thread/thread.h"

static const struct gpio_dt_spec led_r = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

#define ATTACH_BLINK_PERIOD_MS  2000
#define ATTACH_BLINK_TIMEOUT_MS 60000 /* give up blinking after this; keep retrying silently */
#define CONNECTED_HOLD_MS       3000

static struct k_work_delayable blink_work;
static struct k_work_delayable connected_work;
static uint32_t blink_elapsed_ms;
static bool red_on;
static bool connected;

static void set_color(bool r, bool g, bool b)
{
	gpio_pin_set_dt(&led_r, r);
	gpio_pin_set_dt(&led_g, g);
	gpio_pin_set_dt(&led_b, b);
}

static void blink_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (connected) {
		return; /* connected sequence owns the LED now */
	}

	if (blink_elapsed_ms >= ATTACH_BLINK_TIMEOUT_MS) {
		set_color(false, false, false); /* give up blinking, save battery */
		return;
	}

	red_on = !red_on;
	set_color(red_on, false, false);
	blink_elapsed_ms += ATTACH_BLINK_PERIOD_MS;
	k_work_schedule(&blink_work, K_MSEC(ATTACH_BLINK_PERIOD_MS));
}

static void connected_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	set_color(false, false, false); /* done, LED free for whoever else wants it */
}

static void on_thread_connected(void)
{
	connected = true;
	set_color(false, false, true); /* solid blue */
	k_work_schedule(&connected_work, K_MSEC(CONNECTED_HOLD_MS));
	printk("thread_status_led: attached\n");
}

int thread_status_led_init(void)
{
	gpio_pin_configure_dt(&led_r, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_g, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_b, GPIO_OUTPUT_INACTIVE);

	k_work_init_delayable(&blink_work, blink_work_handler);
	k_work_init_delayable(&connected_work, connected_work_handler);

	thread_on_connected(on_thread_connected);
	k_work_schedule(&blink_work, K_NO_WAIT);

	return 0;
}
