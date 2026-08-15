# Why RCP + host border router, not single-chip `ot_br`

## The three genuinely different things ESP-IDF offers

It's easy to conflate these — they look similar but are architecturally
distinct:

- **`ot_br`** (`examples/openthread/ot_br`) — a barebones, single-chip
  example. One ESP32 runs WiFi, the 802.15.4 radio, and the full border
  router stack (including `otbr-agent`-equivalent logic) all in one process.
  No separate host needed.
- **`ot_rcp`** (`examples/openthread/ot_rcp`) — radio-only firmware. The
  ESP32 does nothing but expose the 802.15.4 radio over Spinel; a separate
  host runs the actual border router software (`otbr-agent`) and talks to
  the chip as a dumb radio peripheral.
- **`esp-thread-br`** — Espressif's own full two-chip host+RCP SDK (host
  software + RCP firmware, both ESP-IDF-based). Not used here.
- **`ot-br-posix`** — the OpenThread project's own Linux-native reference
  border router implementation. This is what we run as the host software,
  paired with `ot_rcp` as the radio.

## Why we moved off `ot_br`

The single-chip `ot_br` example was the first thing tried, and it mostly
worked — but hit two real, unresolved-on-that-platform problems:

1. **WiFi/802.15.4 radio coexistence caused WiFi reconnect failures.**
   Under real-world conditions (router not adjacent), the shared radio
   coexistence between WiFi and Thread caused repeated "WiFi Connect failed
   N times" leading to a crash in `example_wifi_sta_do_connect`. There's a
   documented mitigation (`WiFi Maximum Retry` raised to 1000), but it's a
   workaround for a structural limitation, not a fix.

2. **No working SRP + mDNS Advertising Proxy.** `ot_br` is built with
   `OPENTHREAD_CONFIG_SRP_SERVER_ADVERTISING_PROXY_ENABLE` defaulting to 0,
   and this isn't exposed via Kconfig on that example. Without the
   Advertising Proxy, individual Thread nodes' SRP-registered hostnames
   never get bridged onto the LAN's mDNS, so `<hostname>.local` never
   resolves from a normal machine on the network.

Both of these are inherent to trying to do everything (WiFi radio, 802.15.4
radio, and full border router logic) in one single-chip, resource-constrained
process. The real fix is separating concerns: dumb radio on the chip,
full-featured border router software on a real Linux host with proper
resources and a maintained network stack.

## Why `ot-br-posix` over `esp-thread-br`

`esp-thread-br` (Espressif's own two-chip SDK) was considered, but
`ot-br-posix` is the actual upstream OpenThread project's reference
implementation — more actively maintained, better documented, and it's what
most real Thread border router products (including commercial ones) are
ultimately built on. Running it directly, via Docker, on a normal Linux box
avoids being downstream of Espressif's own packaging of the same underlying
software.

## `ot_br` still has a role

We didn't delete `ot_br` — it's kept around as a **temporary, standalone
fallback**: flash it onto the same ESP32-C6 board when you need a
self-contained border router without the Docker/host setup running (e.g.
testing something quickly, or the Docker host is unavailable). See
[rcp-firmware.md](./rcp-firmware.md) for how to swap the same board between
`ot_br` and `ot_rcp` without losing either build's configuration.
