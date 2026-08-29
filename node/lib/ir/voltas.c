/* Voltas AC IR protocol - byte layout ported from IRremoteESP8266's
 * ir_Voltas.h/.cpp (VoltasProtocol union + IRVoltas setters), verified
 * against the library's raw source directly (a first pass using a
 * summarized fetch got Power's bit position wrong - byte 2 bit 7, not
 * bit 0 - so this is checked against the actual struct, not a paraphrase).
 *
 * Byte layout (bit 0 = LSB of each byte, matches VoltasProtocol):
 *   [0]  bit  0    SwingH
 *        bits 1-7  SwingHChange (0x7D=apply the SwingH bit now,
 *                                0x19=leave physical swing state alone)
 *   [1]  bits 0-3  Mode    (Fan=0x1, Dry=0x4, Cool=0x8 - no Heat, see ir.h)
 *        bit  4    (unused)
 *        bits 5-7  FanSpeed (High=0b001, Med=0b010, Low=0b100, Auto=0b111)
 *   [2]  bits 0-2  SwingV  (0b111=on, 0b000=off - not a single bit)
 *        bit  3    Wifi    (not implemented - no WiFi module on this unit)
 *        bit  4    (unused)
 *        bit  5    Turbo
 *        bit  6    Sleep
 *        bit  7    Power
 *   [3]  bits 0-3  Temp    (temp - 16, range 0-14)
 *        bits 4-5  (unused, typically 0b01)
 *        bit  6    Econo
 *        bit  7    TempSet (unused by the reference implementation either)
 *   [4]  bits 0-5  OnTimerMins   bit 6 (unused)   bit 7 OnTimer12Hr
 *   [5]  bits 0-5  OffTimerMins  bit 6 (unused)   bit 7 OffTimer12Hr
 *   [6]  constant 0x3B
 *   [7]  bits 0-3  OnTimerHrs   bits 4-7 OffTimerHrs
 *   [8]  bits 0-4  (unused)  bit 5 Light  bit 6 OffTimerEnable  bit 7 OnTimerEnable
 *   [9]  Checksum
 *
 * Turbo/Sleep/Econo are Cool-only on real hardware - the setters below
 * silently force them off outside Cool mode, matching IRVoltas::setTurbo/
 * setSleep/setEcono exactly. Entering Dry mode forces Fan=Low and Temp=24C;
 * entering Fan mode disallows Fan=Auto (bumped to High) - both ported
 * verbatim from IRVoltas::setMode/setFan.
 */

#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "ir/ir.h"
#include "ir/voltas.h"

#define VOLTAS_STATE_LEN     10
#define VOLTAS_MIN_TEMP      16
#define VOLTAS_MAX_TEMP      30
#define VOLTAS_DRY_TEMP      24

#define VOLTAS_BIT_MARK_US   1026
#define VOLTAS_ONE_SPACE_US  2553
#define VOLTAS_ZERO_SPACE_US 554

#define VOLTAS_SWING_H_CHANGE    0x7D
#define VOLTAS_SWING_H_NO_CHANGE 0x19

#define VOLTAS_MODE_FAN   0x01
#define VOLTAS_MODE_DRY   0x04
#define VOLTAS_MODE_COOL  0x08

#define VOLTAS_FAN_HIGH 0x01
#define VOLTAS_FAN_MED  0x02
#define VOLTAS_FAN_LOW  0x04
#define VOLTAS_FAN_AUTO 0x07

#define VOLTAS_TURBO_BIT (1U << 5)
#define VOLTAS_SLEEP_BIT (1U << 6)
#define VOLTAS_POWER_BIT (1U << 7)
#define VOLTAS_ECONO_BIT (1U << 6)
#define VOLTAS_LIGHT_BIT (1U << 5)

/* Reset state: Power=off, Mode=Cool, Fan=Auto, Temp=28C, swing/turbo/
 * sleep/econo/light/timers all off - checksum byte computed at build time. */
static const uint8_t voltas_reset[VOLTAS_STATE_LEN] = {
	0x33, 0xE8, 0x00, 0x1C, 0x00, 0x00, 0x3B, 0x00, 0x00, 0x00
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

static uint8_t voltas_get_mode(const uint8_t *s)
{
	return s[1] & 0x0FU;
}

static uint8_t voltas_get_fan_raw(const uint8_t *s)
{
	return (s[1] >> 5) & 0x07U;
}

static bool voltas_get_turbo(const uint8_t *s)
{
	return (s[2] & VOLTAS_TURBO_BIT) != 0;
}

static bool voltas_get_sleep(const uint8_t *s)
{
	return (s[2] & VOLTAS_SLEEP_BIT) != 0;
}

static bool voltas_get_econo(const uint8_t *s)
{
	return (s[3] & VOLTAS_ECONO_BIT) != 0;
}

static void voltas_set_temp(uint8_t *s, uint8_t temp)
{
	if (temp < VOLTAS_MIN_TEMP) temp = VOLTAS_MIN_TEMP;
	if (temp > VOLTAS_MAX_TEMP) temp = VOLTAS_MAX_TEMP;
	s[3] = (s[3] & ~0x0FU) | ((temp - VOLTAS_MIN_TEMP) & 0x0FU);
}

/* Sets the raw FanSpeed bits, applying the "Auto unavailable in Fan mode"
 * rule from IRVoltas::setFan. */
static void voltas_set_fan_raw(uint8_t *s, uint8_t fan)
{
	if (fan == VOLTAS_FAN_AUTO && voltas_get_mode(s) == VOLTAS_MODE_FAN) {
		fan = VOLTAS_FAN_HIGH;
	}
	s[1] = (s[1] & ~0xE0U) | ((fan & 0x07U) << 5);
}

static uint8_t ir_fan_to_voltas(ir_fan_t f)
{
	switch (f) {
	case IR_FAN_LOW:  return VOLTAS_FAN_LOW;
	case IR_FAN_MED:  return VOLTAS_FAN_MED;
	case IR_FAN_HIGH: return VOLTAS_FAN_HIGH;
	default:          return VOLTAS_FAN_AUTO;
	}
}

static void voltas_set_fan(uint8_t *s, ir_fan_t f)
{
	voltas_set_fan_raw(s, ir_fan_to_voltas(f));
}

/* Turbo/Sleep/Econo are Cool-only on real hardware - forced off otherwise. */
static void voltas_set_turbo(uint8_t *s, bool on)
{
	if (on && voltas_get_mode(s) == VOLTAS_MODE_COOL) {
		s[2] |= VOLTAS_TURBO_BIT;
	} else {
		s[2] &= ~VOLTAS_TURBO_BIT;
	}
}

static void voltas_set_sleep(uint8_t *s, bool on)
{
	if (on && voltas_get_mode(s) == VOLTAS_MODE_COOL) {
		s[2] |= VOLTAS_SLEEP_BIT;
	} else {
		s[2] &= ~VOLTAS_SLEEP_BIT;
	}
}

static void voltas_set_econo(uint8_t *s, bool on)
{
	if (on && voltas_get_mode(s) == VOLTAS_MODE_COOL) {
		s[3] |= VOLTAS_ECONO_BIT;
	} else {
		s[3] &= ~VOLTAS_ECONO_BIT;
	}
}

static uint8_t ir_mode_to_voltas(ir_mode_t m)
{
	switch (m) {
	case IR_MODE_DRY: return VOLTAS_MODE_DRY;
	case IR_MODE_FAN: return VOLTAS_MODE_FAN;
	default:          return VOLTAS_MODE_COOL; /* AUTO has no Voltas
						      * equivalent - IRVoltas
						      * falls back to Cool for
						      * any unrecognized mode,
						      * same here. */
	}
}

static void voltas_set_mode(uint8_t *s, ir_mode_t mode)
{
	uint8_t m = ir_mode_to_voltas(mode);
	s[1] = (s[1] & ~0x0FU) | (m & 0x0FU);

	switch (m) {
	case VOLTAS_MODE_FAN:
		voltas_set_fan_raw(s, voltas_get_fan_raw(s)); /* re-validate */
		break;
	case VOLTAS_MODE_DRY:
		voltas_set_fan_raw(s, VOLTAS_FAN_LOW);
		voltas_set_temp(s, VOLTAS_DRY_TEMP);
		break;
	default:
		break;
	}
	/* Re-apply against the new mode so leaving Cool clears them. */
	voltas_set_turbo(s, voltas_get_turbo(s));
	voltas_set_sleep(s, voltas_get_sleep(s));
	voltas_set_econo(s, voltas_get_econo(s));
}

static void voltas_set_power(uint8_t *s, bool on)
{
	if (on) {
		s[2] |= VOLTAS_POWER_BIT;
	} else {
		s[2] &= ~VOLTAS_POWER_BIT;
	}
}

static void voltas_set_swing_h(uint8_t *s, bool on)
{
	s[0] = (VOLTAS_SWING_H_CHANGE << 1) | (on ? 1 : 0);
}

static void voltas_set_swing_v(uint8_t *s, bool on)
{
	s[2] = (s[2] & ~0x07U) | (on ? 0x07U : 0x00U);
}

static void voltas_set_light(uint8_t *s, bool on)
{
	if (on) {
		s[8] |= VOLTAS_LIGHT_BIT;
	} else {
		s[8] &= ~VOLTAS_LIGHT_BIT;
	}
}

static void voltas_set_timer_on(uint8_t *s, uint16_t mins)
{
	if (mins == 0) {
		s[8] &= ~(1 << 7);
		return;
	}
	uint16_t hrs = (mins / 60) + 1;
	s[4] = (s[4] & 0x40) | (mins % 60);
	s[4] |= ((hrs / 12) & 1) << 7;
	s[7] = (s[7] & 0xF0) | (hrs % 12);
	s[8] |= (1 << 7);
}

static void voltas_set_timer_off(uint8_t *s, uint16_t mins)
{
	if (mins == 0) {
		s[8] &= ~(1 << 6);
		return;
	}
	uint16_t hrs = (mins / 60) + 1;
	s[5] = (s[5] & 0x40) | (mins % 60);
	s[5] |= ((hrs / 12) & 1) << 7;
	s[7] = (s[7] & 0x0F) | ((hrs % 12) << 4);
	s[8] |= (1 << 6);
}

static void voltas_build_state(ir_cmd_t cmd, const ir_params_t *params,
				uint8_t *state, size_t len)
{
	if (!voltas_initialized) {
		memcpy(voltas_current, voltas_reset, VOLTAS_STATE_LEN);
		voltas_initialized = true;
	}

	switch (cmd) {
	case IR_CMD_POWER_ON:
		voltas_set_power(voltas_current, true);
		break;
	case IR_CMD_POWER_OFF:
		voltas_set_power(voltas_current, false);
		break;
	case IR_CMD_SET_TEMP:
		voltas_set_temp(voltas_current, params->temp);
		break;
	case IR_CMD_SET_MODE:
		voltas_set_mode(voltas_current, params->mode);
		break;
	case IR_CMD_SET_FAN:
		voltas_set_fan(voltas_current, params->fan);
		break;
	case IR_CMD_SET_SWING_H:
		voltas_set_swing_h(voltas_current, params->swing_h);
		break;
	case IR_CMD_SET_SWING_V:
		voltas_set_swing_v(voltas_current, params->swing_v);
		break;
	case IR_CMD_SET_TURBO:
		voltas_set_turbo(voltas_current, params->turbo);
		break;
	case IR_CMD_SET_SLEEP:
		voltas_set_sleep(voltas_current, params->sleep);
		break;
	case IR_CMD_SET_ECO:
		voltas_set_econo(voltas_current, params->eco);
		break;
	case IR_CMD_SET_LIGHT:
		voltas_set_light(voltas_current, params->light);
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
