#include <zephyr/kernel.h>
#include <zephyr/net/openthread.h>
#include <openthread/coap.h>
#include <openthread/dataset.h>
#include <openthread/dns.h>
#include <openthread/error.h>
#include <openthread/instance.h>
#include <openthread/thread.h>
#include <openthread/ip6.h>
#include <openthread/link.h>
#include <openthread/logging.h>
#include <openthread/srp_client.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/printk.h>
#include <stdio.h>
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

static thread_connected_cb_t connected_cb;
static bool notified_connected;

void thread_on_connected(thread_connected_cb_t cb)
{
	connected_cb = cb;
}

/* Debug convenience: UDP-announce non-link-local addresses to a fixed
 * host (see CONFIG_THREAD_ANNOUNCE_ADDR). No-op if unset. */
static void announce_address(const uint8_t *b)
{
	if (sizeof(CONFIG_THREAD_ANNOUNCE_ADDR) <= 1) {
		return; /* empty string */
	}

	char msg[80];
	snprintf(msg, sizeof(msg),
		 "%s: %02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
		 CONFIG_THREAD_HOSTNAME,
		 b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
		 b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);

	struct sockaddr_in6 dst = {0};
	dst.sin6_family = AF_INET6;
	dst.sin6_port = htons(CONFIG_THREAD_ANNOUNCE_PORT);
	if (zsock_inet_pton(AF_INET6, CONFIG_THREAD_ANNOUNCE_ADDR, &dst.sin6_addr) != 1) {
		return;
	}

	int sock = zsock_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		return;
	}
	zsock_sendto(sock, msg, strlen(msg), 0, (struct sockaddr *)&dst, sizeof(dst));
	zsock_close(sock);
}

static void on_thread_state_changed(otChangedFlags flags, void *ctx)
{
	ARG_UNUSED(ctx);
	otInstance *instance = openthread_get_default_instance();

	if (flags & OT_CHANGED_THREAD_ROLE) {
		otDeviceRole role = otThreadGetDeviceRole(instance);

		printk("Thread: role -> %s\n", role_str(role));

		if (!notified_connected && role != OT_DEVICE_ROLE_DISABLED &&
		    role != OT_DEVICE_ROLE_DETACHED) {
			notified_connected = true;
			if (connected_cb) {
				connected_cb();
			}
		}
	}

	if (flags & OT_CHANGED_IP6_ADDRESS_ADDED) {
		const otNetifAddress *addr = otIp6GetUnicastAddresses(instance);
		for (; addr; addr = addr->mNext) {
			const uint8_t *b = addr->mAddress.mFields.m8;
			printk("Thread: addr %02x%02x:%02x%02x:%02x%02x:%02x%02x:"
			       "%02x%02x:%02x%02x:%02x%02x:%02x%02x\n",
			       b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
			       b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);

			if (!(b[0] == 0xfe && b[1] == 0x80)) {
				announce_address(b);
			}
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

/* Buffer must persist for as long as the SRP client uses it. */
static const char srp_hostname[] = CONFIG_THREAD_HOSTNAME;

static void srp_client_auto_start_cb(const otSockAddr *server_addr, void *ctx)
{
	ARG_UNUSED(ctx);
	printk("SRP: client %s (host \"%s\")\n",
	       server_addr ? "started" : "stopped", srp_hostname);
}

/* Advertises what this node actually supports, so a generic Home Assistant
 * integration can discover it via zeroconf and create the right entities
 * without any per-node-type integration code. A single "caps" key, since a
 * node can have more than one capability (e.g. a future board mixing a PIR
 * with a temp/humidity sensor) - see CONFIG_THING_CAPS's help text. Empty
 * string is still a valid (zero-length) TXT value if CONFIG_THING_CAPS is
 * unset for a given build variant. */
static const otDnsTxtEntry caps_txt_entry = {
	.mKey = "caps",
	.mValue = (const uint8_t *)CONFIG_THING_CAPS,
	.mValueLength = sizeof(CONFIG_THING_CAPS) - 1, /* exclude the NUL */
};

/* SRP client only ever sends an "SRP Update" once a host name, a host
 * address, AND at least one service are all set - otherwise it sits
 * "started" forever without ever transmitting anything. This service
 * registration is what actually triggers registration; without it,
 * otSrpClientEnableAutoStartMode() firing "started" is a no-op. Advertises
 * the CoAP endpoint this node already runs. */
static otSrpClientService coap_service = {
	.mName = "_coap._udp",
	.mInstanceName = srp_hostname,
	.mPort = OT_DEFAULT_COAP_PORT,
	.mTxtEntries = &caps_txt_entry,
	.mNumTxtEntries = 1,
};

static void srp_client_callback(otError error, const otSrpClientHostInfo *host_info,
				 const otSrpClientService *services,
				 const otSrpClientService *removed_services, void *ctx)
{
	ARG_UNUSED(host_info);
	ARG_UNUSED(services);
	ARG_UNUSED(removed_services);
	ARG_UNUSED(ctx);
	printk("SRP: update result: %s\n", otThreadErrorToString(error));
}

static void start_srp_client(otInstance *instance)
{
	otSrpClientSetHostName(instance, srp_hostname);
	otSrpClientEnableAutoHostAddress(instance);
	otSrpClientSetCallback(instance, srp_client_callback, NULL);
	otSrpClientAddService(instance, &coap_service);
	otSrpClientEnableAutoStartMode(instance, srp_client_auto_start_cb, NULL);
}

int thread_init(void)
{
	otInstance *instance = openthread_get_default_instance();

	openthread_mutex_lock();

	configure_dataset(instance);

	otSetStateChangedCallback(instance, on_thread_state_changed, NULL);

	otCoapStart(instance, OT_DEFAULT_COAP_PORT);

	start_srp_client(instance);

	openthread_mutex_unlock();

	/* 2026-08-16: was otIp6SetEnabled(instance, true) +
	 * otThreadSetEnabled(instance, true) called directly here. That
	 * skips Zephyr's own openthread_run() (modules/openthread/openthread.c),
	 * which is the ONLY place CONFIG_OPENTHREAD_MTD_SED's sleepy behavior
	 * actually gets applied - it sets otLinkModeConfig.mRxOnWhenIdle=false
	 * and otLinkSetPollPeriod() right before enabling Thread. Without that,
	 * the device attaches as a normal always-listening child regardless of
	 * CONFIG_OPENTHREAD_MTD_SED/MTD_SED_POLL_PERIOD - the radio just never
	 * gets told it's allowed to sleep between polls. This, not PM/QSPI/USB,
	 * is the real explanation for the ~7.5mA that survived every other fix
	 * (see PROGRESS.md). openthread_run() takes its own lock internally, so
	 * it must run outside the mutex held above; it reads the dataset already
	 * committed by configure_dataset() and enables Thread itself.
	 */
	openthread_run();

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
