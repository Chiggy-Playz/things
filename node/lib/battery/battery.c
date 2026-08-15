#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/net/openthread.h>
#include <openthread/coap.h>
#include <openthread/message.h>
#include <stdio.h>

#include "battery/battery.h"
#include "thread/thread.h"

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
	!DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "battery: no ADC io-channels in devicetree - see boards/xiao_ble_nrf52840.overlay"
#endif

/* Single channel: VDD itself (see battery.h for why - coin cell is wired
 * straight to 3V3/GND, no regulator in between). */
static const struct adc_dt_spec vbatt = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

int battery_read_mv(void)
{
	uint16_t raw;
	struct adc_sequence sequence = {
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};
	int32_t val_mv;
	int rc;

	rc = adc_sequence_init_dt(&vbatt, &sequence);
	if (rc < 0) {
		return rc;
	}

	rc = adc_read_dt(&vbatt, &sequence);
	if (rc < 0) {
		return rc;
	}

	val_mv = raw;
	rc = adc_raw_to_millivolts_dt(&vbatt, &val_mv);
	if (rc < 0) {
		return rc;
	}

	return val_mv;
}

/* CR2032 discharge is flat for most of its life, then falls off a cliff -
 * piecewise-linear interpolation between a handful of known points. This is
 * a coarse "fine / getting low / nearly dead" indicator, not a fuel gauge. */
uint8_t battery_percent(int mv)
{
	static const struct {
		int mv;
		uint8_t pct;
	} curve[] = {
		{ 3000, 100 },
		{ 2900, 80 },
		{ 2800, 60 },
		{ 2700, 40 },
		{ 2500, 20 },
		{ 2200, 5 },
		{ 2000, 0 },
	};

	if (mv >= curve[0].mv) {
		return 100;
	}
	if (mv <= curve[ARRAY_SIZE(curve) - 1].mv) {
		return 0;
	}

	for (size_t i = 0; i < ARRAY_SIZE(curve) - 1; i++) {
		int hi_mv = curve[i].mv, lo_mv = curve[i + 1].mv;
		uint8_t hi_pct = curve[i].pct, lo_pct = curve[i + 1].pct;

		if (mv <= hi_mv && mv > lo_mv) {
			return lo_pct + (uint32_t)(mv - lo_mv) * (hi_pct - lo_pct) /
					       (hi_mv - lo_mv);
		}
	}

	return 0; /* unreachable */
}

static otCoapResource battery_resource;

static void battery_coap_handler(void *ctx, otMessage *msg, const otMessageInfo *msg_info)
{
	ARG_UNUSED(ctx);

	if (otCoapMessageGetCode(msg) != OT_COAP_CODE_GET) {
		return;
	}

	int mv = battery_read_mv();
	otInstance *instance = openthread_get_default_instance();
	otMessage *resp = otCoapNewMessage(instance, NULL);

	if (!resp) {
		return;
	}

	otCoapType resp_type = (otCoapMessageGetType(msg) == OT_COAP_TYPE_CONFIRMABLE)
					? OT_COAP_TYPE_ACKNOWLEDGMENT
					: OT_COAP_TYPE_NON_CONFIRMABLE;

	if (otCoapMessageInitResponse(resp, msg, resp_type, OT_COAP_CODE_CONTENT) !=
	    OT_ERROR_NONE) {
		otMessageFree(resp);
		return;
	}

	otCoapMessageAppendContentFormatOption(resp, OT_COAP_OPTION_CONTENT_FORMAT_JSON);
	otCoapMessageSetPayloadMarker(resp);

	char body[64];
	int len;

	if (mv < 0) {
		len = snprintf(body, sizeof(body), "{\"error\":%d}", mv);
	} else {
		len = snprintf(body, sizeof(body), "{\"mv\":%d,\"percent\":%u}", mv,
			       battery_percent(mv));
	}

	if (otMessageAppend(resp, body, (uint16_t)len) != OT_ERROR_NONE) {
		otMessageFree(resp);
		return;
	}

	if (otCoapSendResponse(instance, resp, msg_info) != OT_ERROR_NONE) {
		otMessageFree(resp);
	}
}

int battery_init(void)
{
	if (!adc_is_ready_dt(&vbatt)) {
		printk("Battery: ADC not ready\n");
		return -1;
	}

	int rc = adc_channel_setup_dt(&vbatt);

	if (rc < 0) {
		printk("Battery: channel setup failed (%d)\n", rc);
		return rc;
	}

	battery_resource.mUriPath = "battery";
	battery_resource.mHandler = battery_coap_handler;
	battery_resource.mContext = NULL;
	battery_resource.mNext = NULL;
	thread_coap_add_resource(&battery_resource);

	printk("Battery: ready, /battery registered\n");
	return 0;
}
