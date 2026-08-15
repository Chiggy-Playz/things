# Project status — read this first in a new session

Last updated: 2026-08-16 (night). Supersedes the 2026-08-11 version entirely
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

**Still open (2026-08-16, unresolved after significant investigation)**:
hermes→node CoAP commands (`ac on`/`ac off`, tested directly and via a
`/led` test on `blinky_thread`) are unreliable — every attempt sends,
retries 4x over ~70-85s (CoAP's own exponential backoff), then gives up
with **zero response ever received**, no matter what. The user specifically
recalls this direction working when the old standalone `ot_br` (ESP32,
Docker/Omarchy-free) was flashed — so this is very likely something
specific to the new native-`otbr-agent`-on-Linux architecture, not a
Thread/CoAP-inherent limitation. `ot_br` is embedded firmware with its own
internal WiFi↔802.15.4 routing — no Linux kernel, no `ip6tables`, no `wpan0`
TUN device at all. `otbr-agent` is a real Linux daemon relying on the
kernel's actual IP stack/netfilter/TUN — a genuinely new, more complex
surface area that didn't exist before.

**Ruled out so far, with hard evidence**:
- App-level code — identical failure on two completely different, simple
  CoAP handlers (`/ir` and `/led`).
- `ip6tables` policy — `OT_FORWARD_INGRESS` chain confirmed `ACCEPT`s this
  traffic (rule counter incremented at least once), node's OMR prefix
  confirmed present in `otbr-ingress-allow-dst` ipset.
- Kernel routing decision — `ip -6 route get <node-addr>` correctly
  resolves to `dev wpan0`.
- `trel://` radio link — removed entirely from `otbr-agent-start.sh`
  (this node has zero TREL support, was copied from the original Docker
  script unnecessarily) — **did not fix it**, ruled out as sole cause.
  5/5 repeated test attempts after removal still failed identically.

**Not yet checked**: whether the packet actually reaches the `wpan0`
interface at the kernel level at all — `tcpdump` isn't installed on `iris`
yet (`sudo apt install tcpdump`, then capture on `wpan0` during a test
attempt). Everything checked so far relies on `otbr-agent`'s *own* log,
which only shows what it chooses to log — if the packet reaches `wpan0` but
otbr-agent silently fails to process it without logging anything, tcpdump
would be the way to actually see that. This is the natural next step.
Also worth checking: whether the `otbr-ingress-allow-dst`/`deny-src` ipsets
are dynamically managed by otbr-agent (re-evaluated periodically) rather
than static — if so, there could be brief windows where the destination
isn't actually allow-listed, causing intermittent drops.

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
compounded by the node's firmware having **zero power-management
infrastructure** wired in — no `CONFIG_PM`, no `CONFIG_PM_DEVICE` — meaning
nothing was ever suspending the radio/peripherals between uses. Measured via
a proper in-series ammeter: **~8mA constant draw at idle**, not the
microamp-range a sleepy end device should show. `220mAh (typical CR2032) ÷
8mA ≈ 27-28 hours` — matches the original symptom's timeline (worked before
sleep, dead ~24h later, untouched) almost exactly.

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

### Node firmware changes (made 2026-08-15/16 night) — NOT YET REBUILT/REFLASHED/VERIFIED

`node/prj.conf`:
- `CONFIG_RESET_ON_FATAL_ERROR=y` — was unset; any fault previously caused a
  silent permanent halt (no watchdog either) requiring a manual power cycle.
- `CONFIG_PM=y`, `CONFIG_PM_DEVICE=y` — was completely absent; this is the
  actual fix for the ~8mA idle draw, assuming the radio driver in this SDK
  properly implements device-PM suspend (documented as generally true for
  Zephyr's nRF5 support, not independently re-verified here).
- `CONFIG_SERIAL=n`, `CONFIG_CONSOLE=n`, `CONFIG_UART_CONSOLE=n` — this node
  never has a physical console connected (reverted early in the project);
  Nordic's own power-optimization docs measure ~470uA saved by disabling
  this. New `node/config/debug_console.conf` fragment re-enables it when
  actually needed for debugging — add it as an extra Kconfig fragment, don't
  hand-edit `prj.conf` back and forth.

`node/boards/ir.overlay`: added a `sleep` pinctrl state for the IR LED's PWM
pin — required once `CONFIG_PM_DEVICE=y` was added; without it the build
fails with `static assertion failed: ".../pwm@... defined without sleep
state"`. Zephyr enforces this at compile time for any PM-managed peripheral.

**Important gap**: none of the above has been rebuilt, reflashed to the
actual node, or re-measured for real current draw since being written. The
theory (worn cell + zero PM = the whole story) is well-supported by
observation, but the *fix* is unverified. Do this before considering the
node problem fully closed:
1. Rebuild `chiggy_room_climate_controller`, reflash.
2. Re-run the same in-series ammeter test from tonight — expect idle draw
   to drop from ~8mA toward microamp range if `CONFIG_PM`/`CONFIG_PM_DEVICE`
   worked as expected.
3. If it's still high, the radio driver may not implement device-PM suspend
   properly in this SDK version — would need further investigation, not
   assumed fixed just because the Kconfig is now set.

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

- **Battery voltage reporting**: plan agreed (SAADC internal-reference
  mode, periodic timer, extend the UDP announce message, update
  `listener.py`). Given tonight's findings, sampling voltage specifically
  right after a TX burst (not just at idle) would directly catch the
  load-dependent sag that was the real story — worth designing it that way
  when this gets picked up, not just periodic idle sampling.
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
