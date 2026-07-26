#include <zephyr/kernel.h>

#include "blinky.h"

int main(void)
{
#if defined(CONFIG_BLINKY)
	init_blinky();
#endif

	return 0;
}
