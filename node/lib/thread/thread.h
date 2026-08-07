#ifndef THREAD_H
#define THREAD_H

#include <openthread/coap.h>

typedef void (*thread_connected_cb_t)(void);

int thread_init(void);
void thread_coap_add_resource(otCoapResource *resource);

/* Calls cb exactly once, the first time this device attaches to the
 * Thread network (role becomes child/router/leader). */
void thread_on_connected(thread_connected_cb_t cb);

#endif /* THREAD_H */
