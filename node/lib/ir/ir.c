#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/printk.h>
#include <zephyr/net/openthread.h>
#include <openthread/coap.h>
#include <string.h>
#include <stdlib.h>

#include "ir/ir.h"
#include "thread/thread.h"

static const struct pwm_dt_spec ir_pwm = PWM_DT_SPEC_GET(DT_ALIAS(ir_led));

static const ir_dialect_t *dialect = NULL;

#define CARRIER_PERIOD_NS 26316
#define CARRIER_DUTY_NS 8684
#define MSG_GAP_US 100000

static void carrier_on(uint32_t us)
{
	pwm_set_dt(&ir_pwm, CARRIER_PERIOD_NS, CARRIER_DUTY_NS);
	k_busy_wait(us);
}

static void carrier_off(uint32_t us)
{
	pwm_set_dt(&ir_pwm, CARRIER_PERIOD_NS, 0);
	k_busy_wait(us);
}

void ir_register_dialect(const ir_dialect_t *d)
{
	dialect = d;
}

void ir_transmit(const uint8_t *state, uint16_t nbits, bool lsb_first,
		  uint16_t header_mark_us,
		  uint16_t header_space_us,
		  uint16_t bit_mark_us,
		  uint16_t one_space_us,
		  uint16_t zero_space_us)
{
	if (header_mark_us > 0) {
		carrier_on(header_mark_us);
		carrier_off(header_space_us);
	}

	for (uint16_t n = 0; n < nbits; n++) {
		uint8_t byte_idx = n / 8;
		uint8_t bit_idx = lsb_first ? (n % 8) : (7 - n % 8);
		carrier_on(bit_mark_us);
		if ((state[byte_idx] >> bit_idx) & 1)
			carrier_off(one_space_us);
		else
			carrier_off(zero_space_us);
	}

	carrier_on(bit_mark_us);
	carrier_off(MSG_GAP_US);
}

void ir_send_command(ir_cmd_t cmd, const ir_params_t *params)
{
	if (!dialect) {
		printk("IR: no dialect registered\n");
		return;
	}

	uint8_t state[16] = {0};
	dialect->build_state(cmd, params, state, dialect->state_len);

	uint16_t nbits = dialect->nbits ? dialect->nbits
					: (uint16_t)(dialect->state_len * 8U);
	ir_transmit(state, nbits, dialect->lsb_first,
		    dialect->header_mark_us,
		    dialect->header_space_us,
		    dialect->bit_mark_us,
		    dialect->one_space_us,
		    dialect->zero_space_us);
}

/* ---------- CoAP endpoint: POST /ir ---------- */

static otCoapResource ir_resource;

static void parse_and_dispatch(const char *buf)
{
	ir_params_t p = { .temp = 24, .swing_h = false,
			  .timer_on_mins = 0, .timer_off_mins = 0 };
	const char *v;

	if (strstr(buf, "power_on")) {
		ir_send_command(IR_CMD_POWER_ON, &p);
	} else if (strstr(buf, "power_off")) {
		ir_send_command(IR_CMD_POWER_OFF, &p);
	} else if (strstr(buf, "set_temp")) {
		v = strstr(buf, "\"temp\"");
		if (v) {
			v = strchr(v, ':');
			if (v) p.temp = (uint8_t)strtol(v + 1, NULL, 10);
		}
		ir_send_command(IR_CMD_SET_TEMP, &p);
	} else if (strstr(buf, "set_swing")) {
		v = strstr(buf, "\"swing\"");
		if (v) {
			v = strchr(v, ':');
			if (v) p.swing_h = (strstr(v, "true") != NULL);
		}
		ir_send_command(IR_CMD_SET_SWING_H, &p);
	} else if (strstr(buf, "set_timer_on")) {
		v = strstr(buf, "\"mins\"");
		if (v) {
			v = strchr(v, ':');
			if (v) p.timer_on_mins = (uint16_t)strtol(v + 1, NULL, 10);
		}
		ir_send_command(IR_CMD_SET_TIMER_ON, &p);
	} else if (strstr(buf, "set_timer_off")) {
		v = strstr(buf, "\"mins\"");
		if (v) {
			v = strchr(v, ':');
			if (v) p.timer_off_mins = (uint16_t)strtol(v + 1, NULL, 10);
		}
		ir_send_command(IR_CMD_SET_TIMER_OFF, &p);
	} else {
		printk("IR: unknown CoAP cmd in: %s\n", buf);
	}
}

static void ir_coap_handler(void *ctx, otMessage *msg, const otMessageInfo *msg_info)
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

	printk("IR: /ir POST \"%s\"\n", buf);
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

int ir_init(void)
{
	if (!pwm_is_ready_dt(&ir_pwm)) {
		printk("IR: PWM not ready\n");
		return -1;
	}

	ir_resource.mUriPath = "ir";
	ir_resource.mHandler = ir_coap_handler;
	ir_resource.mContext = NULL;
	ir_resource.mNext    = NULL;
	thread_coap_add_resource(&ir_resource);

	printk("IR: ready, /ir registered\n");
	return 0;
}
