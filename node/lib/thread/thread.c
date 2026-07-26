#include <zephyr/kernel.h>
#include <zephyr/net/openthread.h>
#include <openthread/coap.h>
#include <openthread/dataset.h>
#include <openthread/instance.h>
#include <openthread/thread.h>
#include <openthread/ip6.h>
#include <openthread/link.h>
#include <openthread/logging.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include "thread/thread.h"

/* ---------- Thread credentials ---------- */

static const uint8_t k_network_key[OT_NETWORK_KEY_SIZE] = {
	0x86, 0xe9, 0x9d, 0x7a, 0x56, 0xd3, 0x36, 0x05,
	0xc0, 0x87, 0x65, 0x8a, 0xf3, 0xd9, 0x0c, 0xdc,
};

static const uint8_t k_ext_pan_id[OT_EXT_PAN_ID_SIZE] = {
	0x2c, 0x66, 0x56, 0x1f, 0x1a, 0x07, 0x34, 0xaf,
};

static const uint8_t k_pskc[OT_PSKC_MAX_SIZE] = {
	0x0c, 0x88, 0x79, 0xe3, 0xea, 0xa0, 0xf2, 0x6f,
	0xbd, 0x4b, 0x47, 0xf6, 0xfa, 0xbe, 0x00, 0x45,
};

/* fd00:db8:a0:0::/64 → fd00:0db8:00a0:0000 */
static const uint8_t k_ml_prefix[OT_MESH_LOCAL_PREFIX_SIZE] = {
	0xfd, 0x00, 0x0d, 0xb8, 0x00, 0xa0, 0x00, 0x00,
};

static const char *role_str(otDeviceRole role)
{
	switch (role) {
	case OT_DEVICE_ROLE_DISABLED:  return "disabled";
	case OT_DEVICE_ROLE_DETACHED:  return "detached";
	case OT_DEVICE_ROLE_CHILD:     return "child";
	case OT_DEVICE_ROLE_ROUTER:    return "router";
	case OT_DEVICE_ROLE_LEADER:    return "leader";
	default:                       return "unknown";
	}
}

static void on_thread_state_changed(otChangedFlags flags, void *ctx)
{
	ARG_UNUSED(ctx);
	otInstance *instance = openthread_get_default_instance();

	if (flags & OT_CHANGED_THREAD_ROLE) {
		printk("Thread: role -> %s\n", role_str(otThreadGetDeviceRole(instance)));
	}

	if (flags & OT_CHANGED_IP6_ADDRESS_ADDED) {
		const otNetifAddress *addr = otIp6GetUnicastAddresses(instance);
		for (; addr; addr = addr->mNext) {
			const uint8_t *b = addr->mAddress.mFields.m8;
			printk("Thread: addr %02x%02x:%02x%02x:%02x%02x:%02x%02x:"
			       "%02x%02x:%02x%02x:%02x%02x:%02x%02x\n",
			       b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
			       b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
		}
	}
}

static void configure_dataset(otInstance *instance)
{
	otOperationalDataset ds = {0};

	ds.mChannel = 15;
	ds.mComponents.mIsChannelPresent = true;

	ds.mPanId = 0x173A;
	ds.mComponents.mIsPanIdPresent = true;

	strncpy(ds.mNetworkName.m8, "OpenThread", OT_NETWORK_NAME_MAX_SIZE);
	ds.mComponents.mIsNetworkNamePresent = true;

	memcpy(ds.mExtendedPanId.m8, k_ext_pan_id, OT_EXT_PAN_ID_SIZE);
	ds.mComponents.mIsExtendedPanIdPresent = true;

	memcpy(ds.mNetworkKey.m8, k_network_key, OT_NETWORK_KEY_SIZE);
	ds.mComponents.mIsNetworkKeyPresent = true;

	memcpy(ds.mPskc.m8, k_pskc, OT_PSKC_MAX_SIZE);
	ds.mComponents.mIsPskcPresent = true;

	memcpy(ds.mMeshLocalPrefix.m8, k_ml_prefix, OT_MESH_LOCAL_PREFIX_SIZE);
	ds.mComponents.mIsMeshLocalPrefixPresent = true;

	otDatasetSetActive(instance, &ds);
}

int thread_init(void)
{
	otInstance *instance = openthread_get_default_instance();

	openthread_mutex_lock();

	configure_dataset(instance);

	otSetStateChangedCallback(instance, on_thread_state_changed, NULL);

	otCoapStart(instance, OT_DEFAULT_COAP_PORT);

	otIp6SetEnabled(instance, true);
	otThreadSetEnabled(instance, true);

	openthread_mutex_unlock();

	printk("Thread: started, CoAP on port %d\n", OT_DEFAULT_COAP_PORT);
	return 0;
}

void thread_coap_add_resource(otCoapResource *resource)
{
	otInstance *instance = openthread_get_default_instance();

	openthread_mutex_lock();
	otCoapAddResource(instance, resource);
	openthread_mutex_unlock();
}
