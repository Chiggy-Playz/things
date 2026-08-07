#ifndef BLINKY_THREAD_H
#define BLINKY_THREAD_H

/*
 * Debug RGB LED control over Thread CoAP — POST /led:
 *   {"cmd":"on"}                      -> white
 *   {"cmd":"off"}                     -> off
 *   {"cmd":"color","r":bool,"g":bool,"b":bool}
 * The attach-status blink/solid-blue sequence lives in
 * thread_status_led (global, on by default) — this module only owns
 * the LED once that handoff happens.
 */
int blinky_thread_init(void);

#endif /* BLINKY_THREAD_H */
