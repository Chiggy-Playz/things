#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "blinky.h"

#define LED_NODE DT_ALIAS(led0)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

void init_blinky(void)
{
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);

	while (1) {
		gpio_pin_toggle_dt(&led);
		k_sleep(K_MSEC(500));
	}
}
