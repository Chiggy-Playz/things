#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/net/openthread.h>
#include <openthread/coap.h>
#include <string.h>

#include "blinky_thread/blinky_thread.h"
#include "thread/thread.h"

static const struct gpio_dt_spec led_r = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

static void set_color(bool r, bool g, bool b)
{
	gpio_pin_set_dt(&led_r, r);
	gpio_pin_set_dt(&led_g, g);
	gpio_pin_set_dt(&led_b, b);
}

/* ---------- CoAP endpoint: POST /led ---------- */

static otCoapResource led_resource;

static bool find_bool(const char *buf, const char *key)
{
	const char *v = strstr(buf, key);

	if (!v) {
		return false;
	}
	v = strchr(v, ':');
	if (!v) {
		return false;
	}
	v++; /* skip ':' */
	while (*v == ' ') {
		v++;
	}
	return strncmp(v, "true", 4) == 0;
}

static void parse_and_dispatch(const char *buf)
{
	if (strstr(buf, "\"on\"")) {
		set_color(true, true, true);
	} else if (strstr(buf, "\"off\"")) {
		set_color(false, false, false);
	} else if (strstr(buf, "\"color\"")) {
		set_color(find_bool(buf, "\"r\""), find_bool(buf, "\"g\""), find_bool(buf, "\"b\""));
	} else {
		printk("blinky_thread: unknown CoAP cmd in: %s\n", buf);
	}
}

static void led_coap_handler(void *ctx, otMessage *msg, const otMessageInfo *msg_info)
{
	ARG_UNUSED(ctx);

	if (otCoapMessageGetCode(msg) != OT_COAP_CODE_POST) {
		return;
	}

	char buf[128] = {0};
	uint16_t offset = otMessageGetOffset(msg);
	uint16_t len    = otMessageGetLength(msg) - offset;

	if (len >= sizeof(buf)) len = sizeof(buf) - 1;
	otMessageRead(msg, offset, buf, len);
	buf[len] = '\0';

	printk("blinky_thread: /led POST \"%s\"\n", buf);
	parse_and_dispatch(buf);

	if (otCoapMessageGetType(msg) == OT_COAP_TYPE_CONFIRMABLE) {
		otInstance *instance = openthread_get_default_instance();
		otMessage *resp = otCoapNewMessage(instance, NULL);
		if (!resp) return;

		otError err = otCoapMessageInitResponse(resp, msg,
				  OT_COAP_TYPE_ACKNOWLEDGMENT, OT_COAP_CODE_CHANGED);
		if (err != OT_ERROR_NONE || otCoapSendResponse(instance, resp, msg_info) != OT_ERROR_NONE) {
			otMessageFree(resp);
		}
	}
}

int blinky_thread_init(void)
{
	gpio_pin_configure_dt(&led_r, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_g, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_b, GPIO_OUTPUT_INACTIVE);

	led_resource.mUriPath = "led";
	led_resource.mHandler = led_coap_handler;
	led_resource.mContext = NULL;
	led_resource.mNext    = NULL;
	thread_coap_add_resource(&led_resource);

	printk("blinky_thread: ready, /led registered\n");
	return 0;
}
