#ifndef IR_H
#define IR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Shared across dialects - each dialect's build_state maps these onto its
 * own on-the-wire values (which differ per protocol; e.g. Voltas and Teco
 * encode "fan high" as completely different bit patterns). */
typedef enum {
	IR_MODE_AUTO,
	IR_MODE_COOL,
	IR_MODE_DRY,
	IR_MODE_FAN,
	/* No IR_MODE_HEAT - Indian split/window ACs are cooling-only, and
	 * neither dialect's real hardware does anything useful with it even
	 * though both protocols technically have a bit for it. */
} ir_mode_t;

typedef enum {
	IR_FAN_AUTO,
	IR_FAN_LOW,
	IR_FAN_MED,
	IR_FAN_HIGH,
} ir_fan_t;

typedef enum {
	IR_CMD_POWER_ON,
	IR_CMD_POWER_OFF,
	IR_CMD_SET_TEMP,
	IR_CMD_SET_MODE,
	IR_CMD_SET_FAN,
	IR_CMD_SET_SWING_H,
	IR_CMD_SET_SWING_V,
	IR_CMD_SET_TURBO,
	IR_CMD_SET_SLEEP,
	IR_CMD_SET_ECO,
	IR_CMD_SET_LIGHT,
	IR_CMD_SET_HUMID,   /* Teco-only - no Voltas equivalent */
	IR_CMD_SET_TIMER_ON,
	IR_CMD_SET_TIMER_OFF,
} ir_cmd_t;

typedef struct {
	uint8_t   temp;
	ir_mode_t mode;
	ir_fan_t  fan;
	bool      swing_h;
	bool      swing_v;
	bool      turbo;
	bool      sleep;
	bool      eco;
	bool      light;
	bool      humid;
	uint16_t  timer_on_mins;
	uint16_t  timer_off_mins;
} ir_params_t;

typedef struct {
	void (*build_state)(ir_cmd_t cmd, const ir_params_t *params,
			     uint8_t *state_out, size_t state_len);
	size_t   state_len;
	uint16_t nbits;            /* 0 = state_len * 8 */
	bool     lsb_first;        /* false = MSB first (default) */
	uint16_t header_mark_us;   /* 0 = no header */
	uint16_t header_space_us;  /* 0 = no header */
	uint16_t bit_mark_us;
	uint16_t one_space_us;
	uint16_t zero_space_us;
} ir_dialect_t;

int  ir_init(void);
void ir_send_command(ir_cmd_t cmd, const ir_params_t *params);
void ir_register_dialect(const ir_dialect_t *dialect);
void ir_transmit(const uint8_t *state, uint16_t nbits, bool lsb_first,
		  uint16_t header_mark_us,
		  uint16_t header_space_us,
		  uint16_t bit_mark_us,
		  uint16_t one_space_us,
		  uint16_t zero_space_us);

#endif /* IR_H */
