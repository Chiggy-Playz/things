#include <zephyr/kernel.h>

#include "battery/battery.h"
#include "blinky/blinky.h"
#include "blinky_thread/blinky_thread.h"
#include "ir/ir.h"
#include "thread/thread.h"
#include "thread_status_led/thread_status_led.h"

int main(void)
{
#if defined(CONFIG_BLINKY)
	init_blinky();
#endif

	thread_init();
	battery_init();

#if defined(CONFIG_THREAD_STATUS_LED)
	thread_status_led_init();
#endif

#if defined(CONFIG_BLINKY_THREAD)
	blinky_thread_init();
#endif

#if defined(CONFIG_IR_VOLTAS) || defined(CONFIG_IR_TECO)
	ir_init();
#endif

	return 0;
}
