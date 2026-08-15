#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>

/* This node's coin cell is wired directly to the 3V3/GND pins (no
 * regulator, no charge IC in between - see PROGRESS.md) - so VDD *is* the
 * battery voltage. Read via the SAADC's internal VDD channel: no extra
 * pin, no extra current path, just a brief one-shot conversion.
 *
 * Returns millivolts, or a negative errno on failure. */
int battery_read_mv(void);

/* Rough CR2032-style percentage from a millivolt reading. Coin cells hold a
 * fairly flat ~2.8-3.0V for most of their life then fall off a cliff near
 * the end - this is a coarse "fine / getting low / nearly dead" indicator,
 * not a linear fuel gauge. */
uint8_t battery_percent(int mv);

/* Sets up the ADC channel and registers the CoAP GET /battery endpoint. */
int battery_init(void);

#endif /* BATTERY_H */
