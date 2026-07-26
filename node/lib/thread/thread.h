#ifndef THREAD_H
#define THREAD_H

#include <openthread/coap.h>

int thread_init(void);
void thread_coap_add_resource(otCoapResource *resource);

#endif /* THREAD_H */
