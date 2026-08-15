# Project status — read this first in a new session

Last updated: 2026-08-16 (early morning, continued from the night session).
Supersedes the 2026-08-11 version entirely
— that session's active problem is resolved, and the architecture described
there (standalone `ot_br`) is no longer what's running. Trust this over
anything you remember from a summarized/compacted history.

**If you're touching the border router side**, read
[`hub/docs/README.md`](hub/docs/README.md) next — it has the correct doc
reading order. Don't go straight to a specific doc in there; several
describe retired setups and it's not obvious which from the filenames alone.

## The big picture

Wireless, coin-cell-powered IR blaster nodes (nRF52840/Zephyr) controlling
ACs via Home Assistant over Thread. Two AC IR dialects supported: Voltas,
Teco. One node currently deployed for real: hostname
`chiggy-room-climate`, controls a Voltas AC in Chiggy's room.

## Current architecture (as of 2026-08-16 — changed completely since 08-11, don't assume the old docs)

**Border router**: native (no Docker) `otbr-agent`/`ot-ctl`, running via
systemd on a Raspberry Pi (`iris.local` / mDNS host `iris`), talking to an
ESP32-C6 RCP over `/dev/ttyACM0`. Full setup, including *why* native instead
of Docker (the Pi's boot drive is a tiny ~3.7G USB stick), is in
[`hub/docs/border-router-native.md`](hub/docs/border-router-native.md).

Both the **standalone `ot_br` firmware** (what was running on 08-11) and the
**Docker/Omarchy setup** (documented in `border-router-docker.md`, from
before that) are now fully retired. Omarchy is no longer part of this
project at all. Don't suggest either as a first resort.

**Confirmed working, verified with hard evidence, not just "looks right"**:
- RCP responds correctly to a raw Spinel reset frame (`7e80060070ee747e` —
  decodes as `STATUS_RESET_POWER_ON`) over `/dev/ttyACM0` on the Pi.
- `otbr-agent` running via systemd (`enable`d, confirmed to survive a real
  `sudo reboot` — service and Thread dataset both came back on their own).
- `sudo ot-ctl state` → `leader`, `sudo ot-ctl dataset active` → matches
  `node/lib/thread/thread.c`'s hardcoded credentials exactly (channel 15,
  ext PAN ID `2c66561f1a0734af`, mesh-local prefix `fd00:db8:a0:0::/64`,
  etc).
- Visible in Home Assistant's Thread integration (shows as
  `OpenThread BorderRouter #D68E` / `otf68dd64d93b0d68e.local.` — that's
  expected, not the Pi's own hostname; see `border-router-native.md`'s last
  section for why).
- `vcgencmd get_throttled` on the Pi → `0x0`. Power supply to the Pi itself
  is not a concern.

**One real landmine hit and fixed along the way, worth knowing about even if
not touching this again soon**: the shared `~/iot/esp-idf` checkout was on
5.4.2 (left over from when it needed to satisfy `ot_br`). `ot_rcp`'s
USB-Serial-JTAG Spinel transport **does not exist in ESP-IDF 5.4.x's
Kconfig at all** — selecting it (even by hand-editing `sdkconfig` and
forcing a real `idf.py reconfigure`) silently no-ops, and the choice
re-resolves to plain UART every time. The checkout is now on **5.5.5** and
this is fixed. Full details in `hub/docs/rcp-firmware.md`. Since `ot_br` is
retired, there's no reason to ever move this checkout back to 5.4.x.

**Second landmine hit and fixed (2026-08-16, later the same night)**: kernel
IPv6 forwarding was disabled on the Pi (`net.ipv6.conf.all.forwarding=0`,
Debian's default) — Thread attach worked fine regardless, but any real
LAN↔mesh traffic (the node's UDP announce to hermes, presumably `ac on`/
`ac off` from hermes too) silently never arrived, since `ip6tables` being
`ACCEPT` doesn't matter if the kernel isn't forwarding at all. Fixed
persistently (`/etc/sysctl.d/99-thread-forwarding.conf`) plus a
defense-in-depth line in `otbr-agent-start.sh` itself. **Confirmed fixed
with hard evidence**: node's UDP announce landed on hermes for the first
time this session immediately after. Full detail in
`hub/docs/border-router-native.md`.

**Still open, actively being investigated**: the node's own SRP client logs
`SRP: client started (host "chiggy-room-climate")` on boot (confirmed via
serial console — see `node/config/debug_console.conf` for how to get this
visibility back), meaning it did discover the BR's SRP server via Network
Data. But `sudo ot-ctl srp server host` on the BR stays empty indefinitely
— no registration ever actually lands, and there's zero log evidence on the
BR side of ever receiving an SRP registration attempt. Unlike the
forwarding issue above, this isn't a cross-interface problem — SRP traffic
goes directly to the BR's own mesh-local address, entirely within the
mesh. Root cause not yet found.

**RESOLVED (2026-08-16, early morning)**: hermes→node CoAP commands (`ac
on`/`ac off`, `/led` on `blinky_thread`) were unreliable — every attempt
sent, retried 4x over ~70-85s, then gave up with zero response. **Root
cause: nothing to do with `otbr-agent`, the mesh, or `ip6tables` at all —
hermes simply had no IPv6 route to the node's OMR prefix
(`fd21:6d0:845:1::/64`)**, so its CoAP packets went out hermes's normal
internet default gateway and vanished before ever reaching iris.

Confirmed step by step:
- `tcpdump -i wpan0 -n` on iris during a failed attempt from hermes showed
  **zero packets from hermes ever arriving** — only otbr-agent's own
  routine `ff02::1` multicast advertisements. This proved the failure was
  upstream of `wpan0`/iris entirely, not a mesh-forwarding or firewall
  problem (confirming the "not yet checked" item below, but pointing away
  from iris rather than at it).
- Same test repeated from this Mac (LAN, same network as iris) **worked
  immediately** — LED turned on. This isolated the problem to hermes
  specifically, not the border router.
- On hermes: `ip -6 route show` had no route at all to
  `fd21:6d0:845:1::/64`. Root cause:
  `net.ipv6.conf.enp2s0.accept_ra_rt_info_max_plen` (the **per-interface**
  value, not `conf.all.*`) was `0`, so hermes was ignoring the Route
  Information Option in iris's Router Advertisement that says "route this
  prefix via me". Setting `conf.all.*` alone (which is what
  `sudo sysctl -w net.ipv6.conf.all.accept_ra_rt_info_max_plen=64` sets)
  did **not** propagate to the already-existing `enp2s0` value — on this
  kernel the per-interface value has to be set explicitly for it to take
  effect on real traffic.

**Fix, applied and persisted on hermes**:
```bash
sudo sysctl -w net.ipv6.conf.enp2s0.accept_ra_rt_info_max_plen=64
echo 'net.ipv6.conf.enp2s0.accept_ra_rt_info_max_plen=64' | sudo tee /etc/sysctl.d/99-ra-route-info.conf
sudo sysctl --system
sudo rdisc6 enp2s0   # forces a fresh RA immediately instead of waiting for iris's next periodic one
```
After this, `ip -6 route show` on hermes shows
`fd21:6d0:845:1::/64 via fe80::dea6:32ff:fe0e:ed71 dev enp2s0 proto ra`, and
`ac on`/`ac off`/`/led` color changes from hermes worked 3/3 (confirmed by
physically watching the LED turn on, off, then red).

This also retroactively explains why the earlier "outbound direction
works fine" observation didn't contradict this: the node's own UDP
announce/SRP registration are *mesh→LAN*, which only needed iris's kernel
forwarding (fixed earlier the same night). This bug was specifically about
a *LAN host* needing to learn a route *into* the mesh's OMR prefix via RA —
a genuinely new requirement introduced by real Linux-kernel routing
(`otbr-agent`) that the old embedded `ot_br` firmware never needed, since
it had no real IP routing stack at all. So the original hypothesis ("new
architecture's real IP routing surface") was directionally right, just
pointing at the wrong host (hermes, not iris).

**Second part, also RESOLVED (2026-08-16, same session, ~15 min later)**:
even after the hermes route fix, `coap-client`/`ac.sh` never received the
CoAP ACK back — every attempt still showed retransmissions and a
client-side timeout, **despite the command demonstrably executing on the
node every time** (LED changes confirmed visually, 3/3, including one
"instantly turns off on the very first send" observation that proved the
*forward* path was fast and healthy — the retries were 100% about the
response, not the command).

Diagnosed with a `tcpdump -i any -n port 5683` capture on iris spanning
*both* `wpan0` and `wlan0` during one request: it showed the node's 4-byte
ACK arriving on `wpan0` (`In`) every single time, but **never** going back
out `wlan0`. So the node and the mesh side were completely fine — the
drop was specifically in iris's mesh→LAN forwarding, the mirror image of
the bug already fixed earlier that night.

**Root cause: a direct, unintended side effect of that earlier fix.**
Setting `net.ipv6.conf.all.forwarding=1` (to fix the *first* bug) makes
Linux treat the interface as a router — and by default, a router
**stops accepting Router Advertisements** on that interface
(`accept_ra=1` means "accept RAs only if forwarding is disabled";
`accept_ra=2` is needed to keep accepting them regardless). The moment
forwarding was turned on, `wlan0` silently lost the ability to
(re-)acquire a global IPv6 address/route from the real LAN router. It
didn't fail immediately — iris was still using an address/route it had
learned *before* forwarding was enabled (which is why the UDP-announce
test passed right after that earlier fix) — it only became visible once
that old lease's lifetime expired with nothing renewing it, leaving
`wlan0` with just a link-local address and literally no route to hermes
(`ip -6 route get <hermes-addr>` → `Network is unreachable`).

**Fix, confirmed working, currently LIVE-ONLY / NOT YET PERSISTED** (the
user deliberately wants to watch for side effects before making this
survive a reboot — same class of bug as caused this in the first place):
```bash
sudo sysctl -w net.ipv6.conf.wlan0.accept_ra=2
sudo rdisc6 wlan0   # forces a fresh RA immediately (needs `ndisc6`)
```
After this, `wlan0` picked up real global addresses (including a genuinely
globally-routed one, `2406:b400:71:7b22:.../64` — not just a private
prefix) and a default route via RA. Full round trip re-tested from hermes
immediately after: CoAP ACK (`t:ACK c:2.04`) received, `coap-client` exit
code `0`, no retransmissions, LED confirmed on. Re-checked the *original*
forwarding fix wasn't itself broken by this change: `forwarding` still `1`
on `all`/`wpan0`/`wlan0`, `OT_FORWARD_INGRESS` ACCEPT rule still counting
packets, LED still toggles reliably — no regression.

**Not yet done** (holding off intentionally): persist
`net.ipv6.conf.wlan0.accept_ra=2` to `/etc/sysctl.d/` and add it next to
the existing `forwarding=1` defense-in-depth line in
`otbr-agent-start.sh` — right now this resets on iris's next reboot, and
the original CoAP-response-lost symptom would silently return once the
freshly-relearned address/route eventually expires again (same delayed-
failure pattern as before). Two things worth actually checking before
persisting, not just waiting and hoping:
1. Router's own IPv6 firewalling — `wlan0` having a real global address
   with iris's `ip6tables INPUT` chain at default `ACCEPT`, zero rules,
   means iris is more directly internet-reachable than before this
   session. Acceptable on this home network, per the user, but worth
   knowing if this Pi ever moves somewhere less trusted.
2. iris now has a real default route out `wlan0` it didn't meaningfully
   have before — shouldn't matter for Thread traffic specifically, but
   worth remembering if iris ever does anything else network-facing.

Test commands used throughout, for reference:
```bash
# resolve + test with full CoAP debug output
ADDR=$(avahi-resolve -6 -n <hostname>.local | awk '{print $2}')
coap-client -m post -e '{"cmd":"power_on"}' -v 7 "coap://[$ADDR]/ir"

# check whether the BR's own log shows the request arriving at all
sudo journalctl -u otbr-agent --no-pager --since "<time>" --until "<time>" \
  | grep -viE "BorderRouting|MulticastDns--: Adding host|TrelPeerTable|TrelDiscoverer|Advertisement"

# check firewall/routing state
sudo ip6tables -L OT_FORWARD_INGRESS -v -n --line-numbers
sudo ipset list otbr-ingress-allow-dst
ip -6 route get <node-address>
```

## The AC node: original problem (found 2026-08-11, root-caused 2026-08-15/16)

The 08-11 session's "active problem" (`ac on` stopped responding after being
away most of a day) is **resolved** — root cause found, not the mesh, not
the border router.

**What it actually was**: a worn coin cell with high internal resistance,
compounded by the node's firmware never actually behaving as a Thread
Sleepy End Device despite being configured as one — `thread_init()` called
`otIp6SetEnabled()`/`otThreadSetEnabled()` directly instead of Zephyr's
`openthread_run()`, which is the only place `CONFIG_OPENTHREAD_MTD_SED`'s
`mRxOnWhenIdle=false` + poll period actually get applied (see
`modules/openthread/openthread.c` in the Zephyr tree). The radio was just
listening continuously the whole time. Measured via a proper in-series
ammeter: **~8mA constant draw at idle**, not the microamp-range a sleepy end
device should show. `220mAh (typical CR2032) ÷ 8mA ≈ 27-28 hours` — matches
the original symptom's timeline (worked before sleep, dead ~24h later,
untouched) almost exactly.

Confirmed via direct observation, not just inference: watched the dying
cell's voltage collapse to 0V and partially recover repeatedly under load
(classic brownout-reset loop from a cell too weak to sustain boot+attach
current), then confirmed a **fresh** cell attached cleanly with no such
collapse — narrowing it to the battery, not a firmware crash-loop, mesh
issue, or the border router (which was independently confirmed healthy and
sitting 30cm from the node during testing, ruling out RF/distance too).

Secondary, real finding along the way: this board's onboard `BQ25101`
lithium-charging IC (designed for a rechargeable Li-ion cell) shows a
consistent ~0.35-0.45V loss between the raw coin cell and the actual rail
delivered to the nRF52840 — a real hardware/chemistry mismatch (coin cell
vs. what this board's power path expects), compounding the primary cause
but not the root cause by itself.

### Node firmware changes — REBUILT, REFLASHED, RE-MEASURED, RESOLVED (2026-08-16)

The first hypothesis (`CONFIG_PM=y`/`CONFIG_PM_DEVICE=y` as "the fix") turned
out to be **completely wrong** on this SoC, caught by rebuilding and
re-measuring rather than trusting the Kconfig assignment: nRF52 never
selects `HAS_PM` (see `soc/nordic/nrf52/Kconfig`), so `CONFIG_PM` is silently
forced back to `n` regardless of `prj.conf` — that's the literal
"assigned y, but got n" warning that kicked off this whole investigation.
Both lines were removed.

What followed was a real bisection across several genuine (but ultimately
not-the-cause) fixes, each verified by rebuild + reflash + remeasure rather
than assumed:
1. **Console off** (`CONFIG_SERIAL/CONSOLE/UART_CONSOLE=n`) + dropping dead
   `CONFIG_PM`: ~8mA → 7.44mA. Real (~470uA-class), not the story.
2. **QSPI flash suspend** (`CONFIG_PM_DEVICE_RUNTIME=y` +
   `zephyr,pm-device-runtime-auto` on `node/boards/xiao_ble_nrf52840.overlay`'s
   `p25q16h` node — the onboard QSPI NOR is unused by this app, settings live
   on internal flash): 7.44mA → 7.45mA. No effect.
3. **USB device stack disabled** (`CONFIG_USB_DEVICE_STACK_NEXT=n`,
   `CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT=n` — xiao_ble's console actually
   routes over USB CDC-ACM by devicetree default, a separate subsystem from
   `CONFIG_SERIAL`/`CONFIG_CONSOLE` entirely): 7.45mA → 7.5mA. No effect.
4. **Isolation test** (`blinky` build, no Thread/radio at all): idle dropped
   to **~5uA**, ~200uA per LED — proved the board/SoC/Zephyr baseline was
   already clean, and fixes 1-3 were real but not the dominant cause.
5. **Isolation test 2** (`blinky_thread` build, Thread+radio active, zero
   IR/PWM code): still ~7.5mA — isolated the problem to OpenThread/radio
   specifically, not the IR blaster code.
6. **Actual root cause**, found by reading `modules/openthread/openthread.c`:
   `thread_init()` (`node/lib/thread/thread.c`) called
   `otIp6SetEnabled()`/`otThreadSetEnabled()` directly instead of Zephyr's
   `openthread_run()`. Only `openthread_run()` applies the SED-specific
   `otThreadSetLinkMode()` (`mRxOnWhenIdle=false`) + `otLinkSetPollPeriod()`
   before enabling Thread — calling the raw APIs skips that step entirely,
   so the device was attaching as a normal always-listening child no matter
   what `CONFIG_OPENTHREAD_MTD_SED` said. Fixed by replacing the two direct
   calls with `openthread_run()`.

**Confirmed working on real hardware, both test build and the actual
production firmware**: idle current now cycles **10-90uA** (the expected
sleepy-poll wake/sleep pattern, ~500ms period), briefly rising to ~300uA
when handling an actual CoAP command. `220mAh ÷ ~30uA average ≈ 300+ days` —
vs. the original ~27-28 hours. Node has been rebuilt and reflashed with all
of the above; ready to go back in service.

Also still true, unrelated to power and not yet addressed: `node/prj.conf`
now disables the console entirely (both UART and USB-CDC paths), so
`CONFIG_PRINTK=y` (from `config/climate.conf`) has no backend left — those
`printk()` calls silently no-op. Harmless, but worth knowing if debug output
is ever needed again (`config/debug_console.conf` re-enables UART/console,
though not the USB path — that fragment predates this finding and may need
a matching USB counterpart if USB-console debugging is ever wanted).

### Battery voltage reporting — implemented and verified working (2026-08-16)

Node's coin cell is wired directly to `3V3`/`GND` (no regulator, no BQ25101
in that path) — so VDD *is* the battery voltage. Read via the nRF52840
SAADC's internal VDD channel (`NRF_SAADC_VDD`, internal 0.6V ref + 1/6 gain,
`node/boards/xiao_ble_nrf52840.overlay`) — no extra pin, no extra current
draw beyond the one-shot conversion. (The board's own onboard divider on
`P0.14`/`P0.31` taps `VBAT`, the charge IC's input net, which is unconnected
in this wiring — doesn't apply here.)

New module `node/lib/battery/battery.c`: `battery_read_mv()` +
`battery_percent()` (rough CR2032 curve — flat ~2.8-3.0V most of its life
then falls off a cliff, piecewise-linear between a handful of points, not a
real fuel gauge — worth retuning once a cell's actually been watched
declining over months). Registers `GET /battery` over CoAP, JSON response.

Confirmed on the real node:
```
$ coap-client -m get -N "coap://[$ADDR]/battery"
{"mv":3304,"percent":100}
```
Stable across repeated reads (3303-3306mV), cross-checked against the coin
cell being fresh. 3.3V is right at the top of a fresh CR2032's nominal range
(~3.0-3.3V open-circuit), consistent with 100%. Can also be sanity-checked
independently with a plain multimeter across `3V3`/`GND` (or directly at the
coin cell holder terminals) — same net, no regulator in between, so the two
should read the same value.

### Also still true from before, unrelated to tonight

Uncommitted working-tree state (not yet committed, still pending):
`node/Kconfig`'s `THREAD_ANNOUNCE_ADDR` defaults to hermes's address instead
of blank; `node/config/blinky_thread.conf` had its now-redundant override
stripped; new `node/config/chiggy_room.conf` carries the node's hostname.

### IR commands supported (via `POST /ir`, JSON body — see `node/lib/ir/ir.c`)

| Command | Trigger substring + field | Notes |
|---|---|---|
| Power on | `"power_on"` | |
| Power off | `"power_off"` | |
| Set temp | `"set_temp"` + `"temp":N` | 16–30°C clamped both dialects |
| Set swing | `"set_swing"` + `"swing":true/false` | |
| Timer on | `"set_timer_on"` + `"mins":N` | schedules delayed power-on |
| Timer off | `"set_timer_off"` + `"mins":N` | Teco ignores the mins value, always just clears the timer — only Voltas supports real delayed-off. |

Voltas timer minimum granularity is ~1 hour (confirmed matches real remote +
reference implementation — not a bug). Sub-hour delayed on/off would need a
software delay (`sleep 300 && ac off`), not the AC's own IR timer.

## Server-side scripts (on hermes, in `~/things/`)

Still not committed to this repo. **Confirmed done** (2026-08-14): converted
to a systemd unit, `ac-listener.service`, enabled and running continuously
since 2026-08-14 11:43:12 (verified via `systemctl status` — real PID under
the service's cgroup, not a leftover `nohup` process). Survives hermes
reboots now; no longer the old fragile `nohup python3 listener.py &`
approach.

- **`listener.py`** — UDP listener on `('::', 5555)`, parses
  `announce_address()`'s `"<hostname>: <addr>"`, filters the node's
  mesh-local prefix, writes the surviving OMR address to
  `/home/chiggy/things/.ac_node_addr`.
- **`ac.sh`** (symlinked `~/.local/bin/ac`) — `coap-client` POST to
  `coap://[addr]/ir`. `ac on`, `ac off`, `ac temp <N>`, `ac swing on|off`,
  `ac timer on|off [mins]`. Needs `libcoap2-bin` (not `libcoap3-bin` — this
  Debian release doesn't have it).
- `sudo sysctl -w net.ipv6.conf.all.accept_ra_rt_info_max_plen=64` needed on
  hermes for the OMR route to be honored — still only set live, not in
  `/etc/sysctl.d/`, will reset on hermes reboot if not made persistent.

## Discussed but NOT yet implemented (no code written)

- **Proper Home Assistant integration** for the AC node itself (currently
  nothing beyond raw `ac.sh`/`coap-client`; HA only sees the border router,
  which is infra-level, not a device). Three options sketched, none chosen:
  1. HA `command_line` platform wrapping `ac.sh`.
  2. MQTT + HA discovery bridge.
  3. Custom `custom_components/` integration via `aiocoap`.

## Reference docs

`hub/docs/` — see `hub/docs/README.md` for the reading order and which
setups are active vs. retired. Don't read the individual docs in whatever
order they're listed by filename; the README explicitly tells you which is
current.
