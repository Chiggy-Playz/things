#include <zephyr/kernel.h>

#include "blinky/blinky.h"
#include "ir/ir.h"
#include "thread/thread.h"

int main(void)
{
#if defined(CONFIG_BLINKY)
	init_blinky();
#endif

	thread_init();

#if defined(CONFIG_IR_VOLTAS) || defined(CONFIG_IR_TECO)
	ir_init();
#endif

	return 0;
}
