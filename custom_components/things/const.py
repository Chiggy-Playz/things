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

# Human-readable labels for the same tags, kept in sync with strings.json's
# "thing_caps" selector options by hand - used anywhere caps need to be
# shown to a user rather than matched against in code (e.g. the discovery
# confirm dialog), so nobody has to read raw snake_case tag names.
CAP_LABELS = {
    CAP_AC_CLIMATE: "AC / climate control",
    CAP_BATTERY: "Battery level sensor",
}

# Separate from CONF_CAPS on purpose - caps says which entity platforms a
# node needs, node_config says how an already-declared capability should
# behave for THIS physical unit (e.g. which swing axis has a real motor).
# Parsed from the "config" TXT key (node/Kconfig's THING_CONFIG option) -
# same comma-separated style as "caps", but key=value pairs instead of bare
# tags, e.g. "swing=v".
CONF_NODE_CONFIG = "node_config"

CONF_SWING_AXIS = "swing"
SWING_AXIS_H = "h"
SWING_AXIS_V = "v"
KNOWN_SWING_AXES = [SWING_AXIS_H, SWING_AXIS_V]


def parse_node_config(raw: str) -> dict[str, str]:
    """Parse a "key=value,key=value" TXT value into a dict.

    Tolerant of stray whitespace and empty segments (e.g. "" or a trailing
    comma) since this is untrusted-ish data coming off the network.
    """
    result: dict[str, str] = {}
    for part in raw.split(","):
        part = part.strip()
        if not part or "=" not in part:
            continue
        key, _, value = part.partition("=")
        result[key.strip()] = value.strip()
    return result

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
