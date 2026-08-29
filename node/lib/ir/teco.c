#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "ir/ir.h"
#include "ir/teco.h"

/*
 * Teco AC IR protocol (35-bit, LSB-first). Bit layout verified directly
 * against IRremoteESP8266's ir_Teco.h TecoProtocol union - unlike Voltas,
 * this one has no per-mode interdependencies at all (no Dry-forces-fan-low,
 * no Cool-only-Turbo/Sleep/Econo): every setter in the reference IRTecoAc
 * class just validates its own value and writes its own bits, nothing else.
 * There's also no Turbo field in this protocol, and only one swing bit
 * (no separate H/V) - both are unsupported no-ops here.
 *
 * Byte layout (bit 0 = first transmitted):
 *   [0]  bits 0-2  Mode    (auto=0, cool=1, dry=2, fan=3 - no heat, see ir.h)
 *        bit  3    Power   (0=off, 1=on)
 *        bits 4-5  Fan     (auto=0, low=1, med=2, high=3)
 *        bit  6    Swing   (0=off, 1=on)
 *        bit  7    Sleep   (0=off, 1=on)
 *   [1]  bits 0-3  Temp    (temp - 16, range 0-14)
 *        bit  4    HalfHour
 *        bits 5-6  TensHours
 *        bit  7    TimerOn
 *   [2]  bits 0-3  UnitHours
 *        bit  4    Humid
 *        bit  5    Light
 *        bit  6    (reserved)
 *        bit  7    Save (energy-saver -> IR_CMD_SET_ECO)
 *   [3]  constant 0x50
 *   [4]  constant 0x02  (only bits 0-2 transmitted -> 35 bits total)
 */

#define TECO_STATE_LEN       5
#define TECO_NBITS           35
#define TECO_MIN_TEMP        16
#define TECO_MAX_TEMP        30

#define TECO_HDR_MARK_US     9000
#define TECO_HDR_SPACE_US    4440
#define TECO_BIT_MARK_US     620
#define TECO_ONE_SPACE_US    1650
#define TECO_ZERO_SPACE_US   580

#define TECO_MODE_AUTO 0x00
#define TECO_MODE_COOL 0x01
#define TECO_MODE_DRY  0x02
#define TECO_MODE_FAN  0x03

#define TECO_FAN_AUTO 0x00
#define TECO_FAN_LOW  0x01
#define TECO_FAN_MED  0x02
#define TECO_FAN_HIGH 0x03

#define TECO_SWING_BIT (1U << 6)
#define TECO_SLEEP_BIT (1U << 7)
#define TECO_HUMID_BIT (1U << 4)
#define TECO_LIGHT_BIT (1U << 5)
#define TECO_SAVE_BIT  (1U << 7)

/* Reset state, aligned with the Voltas default (28C/Cool/Auto fan/off)
 * rather than kTecoReset's native 16C/Auto - a fresh boot should look
 * the same regardless of which physical unit the node is driving. */
static const uint8_t teco_reset[TECO_STATE_LEN] = {
	0x00,  /* Mode=auto (overwritten below), Power=off, Fan=auto, Swing=off, Sleep=off */
	0x20,  /* Temp=0(16C, overwritten below), HalfHour=0, TensHours=1(ignored), TimerOn=0 */
	0x00,  /* UnitHours=0, Humid=0, Light=0, rsvd=0, Save=0 */
	0x50,  /* constant */
	0x02,  /* constant (only low 3 bits transmitted) */
};

static uint8_t teco_current[TECO_STATE_LEN];
static bool    teco_initialized;

static void teco_set_power(uint8_t *s, bool on)
{
	s[0] = (s[0] & ~0x08U) | (on ? 0x08U : 0x00U);
}

static void teco_set_temp(uint8_t *s, uint8_t temp)
{
	if (temp < TECO_MIN_TEMP) temp = TECO_MIN_TEMP;
	if (temp > TECO_MAX_TEMP) temp = TECO_MAX_TEMP;
	s[1] = (s[1] & ~0x0FU) | ((uint8_t)(temp - TECO_MIN_TEMP) & 0x0FU);
}

static uint8_t ir_mode_to_teco(ir_mode_t m)
{
	switch (m) {
	case IR_MODE_COOL: return TECO_MODE_COOL;
	case IR_MODE_DRY:  return TECO_MODE_DRY;
	case IR_MODE_FAN:  return TECO_MODE_FAN;
	default:           return TECO_MODE_AUTO;
	}
}

static void teco_set_mode(uint8_t *s, ir_mode_t mode)
{
	s[0] = (s[0] & ~0x07U) | (ir_mode_to_teco(mode) & 0x07U);
}

static uint8_t ir_fan_to_teco(ir_fan_t f)
{
	switch (f) {
	case IR_FAN_LOW:  return TECO_FAN_LOW;
	case IR_FAN_MED:  return TECO_FAN_MED;
	case IR_FAN_HIGH: return TECO_FAN_HIGH;
	default:          return TECO_FAN_AUTO;
	}
}

static void teco_set_fan(uint8_t *s, ir_fan_t f)
{
	s[0] = (s[0] & ~0x30U) | ((ir_fan_to_teco(f) & 0x03U) << 4);
}

static void teco_set_swing(uint8_t *s, bool on)
{
	s[0] = (s[0] & ~TECO_SWING_BIT) | (on ? TECO_SWING_BIT : 0);
}

static void teco_set_sleep(uint8_t *s, bool on)
{
	s[0] = (s[0] & ~TECO_SLEEP_BIT) | (on ? TECO_SLEEP_BIT : 0);
}

static void teco_set_humid(uint8_t *s, bool on)
{
	s[2] = (s[2] & ~TECO_HUMID_BIT) | (on ? TECO_HUMID_BIT : 0);
}

static void teco_set_light(uint8_t *s, bool on)
{
	s[2] = (s[2] & ~TECO_LIGHT_BIT) | (on ? TECO_LIGHT_BIT : 0);
}

static void teco_set_save(uint8_t *s, bool on)
{
	s[2] = (s[2] & ~TECO_SAVE_BIT) | (on ? TECO_SAVE_BIT : 0);
}

/* Teco has one timer (power-state toggle).  mins=0 clears it; max 24 h. */
static void teco_set_timer(uint8_t *s, uint16_t mins)
{
	if (mins == 0) {
		s[1] &= ~0x80U;
		return;
	}
	if (mins > 24U * 60U) mins = 24U * 60U;
	uint8_t hours = (uint8_t)(mins / 60U);
	uint8_t tens  = hours / 10U;
	uint8_t unit  = hours % 10U;
	uint8_t half  = ((mins % 60U) >= 30U) ? 1U : 0U;
	s[1] = (s[1] & 0x0FU) | (uint8_t)(half << 4) | (uint8_t)((tens & 0x03U) << 5) | 0x80U;
	s[2] = (s[2] & 0xF0U) | (unit & 0x0FU);
}

static void teco_build_state(ir_cmd_t cmd, const ir_params_t *params,
			      uint8_t *state, size_t len)
{
	ARG_UNUSED(len);

	if (!teco_initialized) {
		memcpy(teco_current, teco_reset, TECO_STATE_LEN);
		teco_set_mode(teco_current, IR_MODE_COOL);
		teco_set_fan(teco_current, IR_FAN_AUTO);
		teco_set_temp(teco_current, 28);
		teco_initialized = true;
	}

	switch (cmd) {
	case IR_CMD_POWER_ON:
		teco_set_power(teco_current, true);
		break;
	case IR_CMD_POWER_OFF:
		teco_set_power(teco_current, false);
		break;
	case IR_CMD_SET_TEMP:
		teco_set_temp(teco_current, params->temp);
		break;
	case IR_CMD_SET_MODE:
		teco_set_mode(teco_current, params->mode);
		break;
	case IR_CMD_SET_FAN:
		teco_set_fan(teco_current, params->fan);
		break;
	case IR_CMD_SET_SWING_H:
		teco_set_swing(teco_current, params->swing_h);
		break;
	case IR_CMD_SET_SWING_V:
	case IR_CMD_SET_TURBO:
		printk("Teco: cmd %d not supported by this protocol\n", cmd);
		return;
	case IR_CMD_SET_SLEEP:
		teco_set_sleep(teco_current, params->sleep);
		break;
	case IR_CMD_SET_ECO:
		teco_set_save(teco_current, params->eco);
		break;
	case IR_CMD_SET_LIGHT:
		teco_set_light(teco_current, params->light);
		break;
	case IR_CMD_SET_HUMID:
		teco_set_humid(teco_current, params->humid);
		break;
	case IR_CMD_SET_TIMER_ON:
		teco_set_timer(teco_current, params->timer_on_mins);
		break;
	case IR_CMD_SET_TIMER_OFF:
		teco_set_timer(teco_current, 0);
		break;
	default:
		printk("Teco: unknown command %d\n", cmd);
		return;
	}

	memcpy(state, teco_current, TECO_STATE_LEN);
}

static const ir_dialect_t teco_dialect = {
	.build_state     = teco_build_state,
	.state_len       = TECO_STATE_LEN,
	.nbits           = TECO_NBITS,
	.lsb_first       = true,
	.header_mark_us  = TECO_HDR_MARK_US,
	.header_space_us = TECO_HDR_SPACE_US,
	.bit_mark_us     = TECO_BIT_MARK_US,
	.one_space_us    = TECO_ONE_SPACE_US,
	.zero_space_us   = TECO_ZERO_SPACE_US,
};

static int teco_init(void)
{
	ir_register_dialect(&teco_dialect);
	printk("Teco: dialect registered\n");
	return 0;
}

SYS_INIT(teco_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
