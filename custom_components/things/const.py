"""Constants for the Things integration.

A "Things" node is any board running the firmware in this repo's node/
Zephyr app. Capabilities are a SET (a single board can mix e.g. a PIR with
a temp/humidity sensor), declared via the "caps" TXT record on the node's
SRP-registered _coap._udp service - see node/lib/thread/thread.c and
node/Kconfig's THING_CAPS option.
"""

DOMAIN = "things"

CONF_CAPS = "caps"

# Capability tags a node can declare. Keep in sync with node/Kconfig's
# THING_CAPS help text and whatever conf fragments actually set it.
CAP_AC_CLIMATE = "ac_climate"
CAP_BATTERY = "battery"

KNOWN_CAPS = [CAP_AC_CLIMATE, CAP_BATTERY]

# Which entity platforms a capability contributes. A node's forwarded
# platforms are the union across all its declared caps.
PLATFORMS_BY_CAP = {
    CAP_AC_CLIMATE: ["climate", "number"],
    CAP_BATTERY: ["sensor"],
}

COAP_PORT = 5683

# Deliberately slow - the node is a Thread Sleepy End Device on a coin cell
# (see PROGRESS.md); no reason to poll a battery reading more often than
# anyone's actually going to look at it.
BATTERY_SCAN_INTERVAL_MINUTES = 15
