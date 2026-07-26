#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "ir/ir.h"
#include "ir/voltas.h"

#define VOLTAS_STATE_LEN     10
#define VOLTAS_MIN_TEMP      16
#define VOLTAS_MAX_TEMP      30

#define VOLTAS_BIT_MARK_US   1026
#define VOLTAS_ONE_SPACE_US  2553
#define VOLTAS_ZERO_SPACE_US 554

#define VOLTAS_SWING_H_CHANGE    0x7D
#define VOLTAS_SWING_H_NO_CHANGE 0x19

static const uint8_t voltas_default_on[VOLTAS_STATE_LEN] = {
	0x33, 0xE8, 0x80, 0x18, 0x3B, 0x3B, 0x3B, 0x11, 0x00, 0x8A
};

static const uint8_t voltas_default_off[VOLTAS_STATE_LEN] = {
	0x33, 0x28, 0x00, 0x17, 0x3B, 0x3B, 0x3B, 0x11, 0x00, 0xCB
};

static uint8_t voltas_current[VOLTAS_STATE_LEN];
static bool voltas_initialized = false;

static uint8_t voltas_calc_checksum(const uint8_t *state)
{
	uint8_t sum = 0;
	for (int i = 0; i < VOLTAS_STATE_LEN - 1; i++) {
		sum += state[i];
	}
	return ~sum;
}

static void voltas_set_temp(uint8_t *state, uint8_t temp)
{
	if (temp < VOLTAS_MIN_TEMP) temp = VOLTAS_MIN_TEMP;
	if (temp > VOLTAS_MAX_TEMP) temp = VOLTAS_MAX_TEMP;
	state[3] = (state[3] & 0xF0) | (temp - VOLTAS_MIN_TEMP);
}

static void voltas_set_swing_h(uint8_t *state, bool on)
{
	state[0] = (VOLTAS_SWING_H_CHANGE << 1) | (on ? 1 : 0);
}

static void voltas_set_timer_on(uint8_t *state, uint16_t mins)
{
	if (mins == 0) {
		state[8] &= ~(1 << 7);
		return;
	}
	uint16_t hrs = (mins / 60) + 1;
	state[4] = (state[4] & 0x40) | (mins % 60);
	state[4] |= ((hrs / 12) & 1) << 7;
	state[7] = (state[7] & 0xF0) | (hrs % 12);
	state[8] |= (1 << 7);
}

static void voltas_set_timer_off(uint8_t *state, uint16_t mins)
{
	if (mins == 0) {
		state[8] &= ~(1 << 6);
		return;
	}
	uint16_t hrs = (mins / 60) + 1;
	state[5] = (state[5] & 0x40) | (mins % 60);
	state[5] |= ((hrs / 12) & 1) << 7;
	state[7] = (state[7] & 0x0F) | ((hrs % 12) << 4);
	state[8] |= (1 << 6);
}

static void voltas_build_state(ir_cmd_t cmd, const ir_params_t *params,
				uint8_t *state, size_t len)
{
	if (!voltas_initialized) {
		memcpy(voltas_current, voltas_default_on, VOLTAS_STATE_LEN);
		voltas_initialized = true;
	}

	switch (cmd) {
	case IR_CMD_POWER_ON:
		memcpy(voltas_current, voltas_default_on, VOLTAS_STATE_LEN);
		break;
	case IR_CMD_POWER_OFF:
		memcpy(voltas_current, voltas_default_off, VOLTAS_STATE_LEN);
		break;
	case IR_CMD_SET_TEMP:
		voltas_set_temp(voltas_current, params->temp);
		break;
	case IR_CMD_SET_SWING_H:
		voltas_set_swing_h(voltas_current, params->swing_h);
		break;
	case IR_CMD_SET_TIMER_ON:
		voltas_set_timer_on(voltas_current, params->timer_on_mins);
		break;
	case IR_CMD_SET_TIMER_OFF:
		voltas_set_timer_off(voltas_current, params->timer_off_mins);
		break;
	default:
		printk("Voltas: unknown command %d\n", cmd);
		return;
	}

	voltas_current[VOLTAS_STATE_LEN - 1] = voltas_calc_checksum(voltas_current);
	memcpy(state, voltas_current, VOLTAS_STATE_LEN);
}

static const ir_dialect_t voltas_dialect = {
	.build_state     = voltas_build_state,
	.state_len       = VOLTAS_STATE_LEN,
	.header_mark_us  = 0,
	.header_space_us = 0,
	.bit_mark_us     = VOLTAS_BIT_MARK_US,
	.one_space_us    = VOLTAS_ONE_SPACE_US,
	.zero_space_us   = VOLTAS_ZERO_SPACE_US,
};

static int voltas_init(void)
{
	ir_register_dialect(&voltas_dialect);
	printk("Voltas: dialect registered\n");
	return 0;
}

SYS_INIT(voltas_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
