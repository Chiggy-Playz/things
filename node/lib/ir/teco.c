#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "ir/ir.h"
#include "ir/teco.h"

/*
 * Teco AC IR protocol (35-bit, LSB-first)
 *
 * Byte layout (bit 0 = first transmitted):
 *   [0]  bits 0-2  Mode    (auto=0, cool=1, dry=2, fan=3, heat=4)
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
 *        bit  7    Save
 *   [3]  constant 0x50
 *   [4]  constant 0x02  (only bits 0-2 transmitted → 35 bits total)
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

/* Reset state matches kTecoReset from IRremoteESP8266:
 * mode=auto, power=off, fan=auto, temp=16°C, timer=off */
static const uint8_t teco_reset[TECO_STATE_LEN] = {
	0x00,  /* Mode=auto, Power=off, Fan=auto, Swing=off, Sleep=off */
	0x20,  /* Temp=0(16°C), HalfHour=0, TensHours=1(ignored), TimerOn=0 */
	0x00,  /* UnitHours=0, Humid=0, Light=0, rsvd=0, Save=0            */
	0x50,  /* constant                                                   */
	0x02,  /* constant (only low 3 bits transmitted)                    */
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

static void teco_set_swing(uint8_t *s, bool on)
{
	s[0] = (s[0] & ~0x40U) | (on ? 0x40U : 0x00U);
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
	case IR_CMD_SET_SWING_H:
		teco_set_swing(teco_current, params->swing_h);
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
