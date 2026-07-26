#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>

#include "blinky.h"

static const struct gpio_dt_spec led_r = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

static const struct {
	bool r, g, b;
} colors[] = {
	{1, 0, 0}, /* red */
	{0, 1, 0}, /* green */
	{0, 0, 1}, /* blue */
	{1, 1, 0}, /* yellow */
	{0, 1, 1}, /* cyan */
	{1, 0, 1}, /* magenta */
	{1, 1, 1}, /* white */
	{0, 0, 0}, /* off */
};

static void set_color(bool r, bool g, bool b)
{
	gpio_pin_set_dt(&led_r, r);
	gpio_pin_set_dt(&led_g, g);
	gpio_pin_set_dt(&led_b, b);
}

void init_blinky(void)
{
	gpio_pin_configure_dt(&led_r, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_g, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_b, GPIO_OUTPUT_INACTIVE);

	while (1) {
		for (size_t i = 0; i < ARRAY_SIZE(colors); i++) {
			set_color(colors[i].r, colors[i].g, colors[i].b);
			k_sleep(K_MSEC(2000));
		}
	}
}
