#ifndef THREAD_STATUS_LED_H
#define THREAD_STATUS_LED_H

/*
 * Global boot-status indicator on the onboard RGB LED, present on any
 * build with Thread enabled (i.e. every build except CONFIG_BLINKY):
 *  - blinks red every 2s while attaching (gives up after 60s to save
 *    battery if it never attaches; Thread keeps retrying silently)
 *  - solid blue for 3s once attached, then goes dark
 * Enabled by default (CONFIG_THREAD_STATUS_LED) — disable per build
 * type in its conf fragment if not wanted.
 */
int thread_status_led_init(void);

#endif /* THREAD_STATUS_LED_H */
