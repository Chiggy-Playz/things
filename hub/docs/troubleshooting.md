# Troubleshooting index

Quick-reference by symptom. Most of these link to fuller writeups in the
other docs — this file exists so a future search for an error string finds
something.

| Symptom | Cause | Detail |
|---|---|---|
| `Init() at spinel_driver.cpp:87: Failure` | RCP built with Spinel on real UART0, not wired to the USB port you're using | [rcp-firmware.md](./rcp-firmware.md) |
| `Init() at hdlc_interface.cpp:154: No such file or directory` | Same as above, or (if device path is genuinely correct) a `--privileged` Docker container not sharing a live `/dev` namespace — use `-v /dev:/dev` bind mount instead | [rcp-firmware.md](./rcp-firmware.md) |
| Raw `pyserial` test shows 0 bytes back, including no ROM boot log | Board sitting in whatever DTR/RTS state a previous tool left it in — force a reset (`setRTS(True)` then `False`) before reading | [rcp-firmware.md](./rcp-firmware.md) |
| `otPlatRadioSetMacKey() at radio.cpp:964: InvalidArgument` | Real upstream 2-day compatibility gap in OpenThread's `keyIdMode` wire format (July 24 → Aug 6 2026) | [border-router-docker.md](./border-router-docker.md) |
| Logs show `RADIO_URL:` / `TUN_INTERFACE_NAME:` when you set `OT_RCP_DEVICE` etc. | Wrong Docker image — `openthread/otbr` is test-only, use `openthread/border-router` | [border-router-docker.md](./border-router-docker.md) |
| Container boot-loops with `ipset ... Set cannot be destroyed: it is in use by a kernel component` | Leftover host-level `iptables`/`ipset` state from an ungracefully-stopped prior container (`network_mode: host` means this state is on the real host, not container-scoped) | [border-router-docker.md](./border-router-docker.md) |
| Border router works internally (forms network, RCP responds) but never becomes visible to `avahi-browse`/Home Assistant | `OT_INFRA_IF` pointed at a nonexistent/wrong interface (e.g. `wlan0` when the real interface is `enp12s0`) | [border-router-docker.md](./border-router-docker.md) |
| `avahi-browse -a` shows nothing for the border router, but `ss -ulnp \| grep 5353` shows it's bound | Expected — this image has no `avahi-daemon` at all, uses its own built-in mDNS publisher. Separately, `DnssdServer` traffic to `ff02::fb` in the logs is Thread-radio-side mDNS, not the LAN-facing border agent advertisement | [border-router-docker.md](./border-router-docker.md) |
| `ot-ctl state` stuck looping `Attach attempt N failed` / `attachstate idle -> start` | Dataset built via `dataset clear` + individual fields, missing Active Timestamp/Security Policy — use `dataset init new` then override fields instead | [thread-dataset.md](./thread-dataset.md) |
| `find_bool()`-style JSON parsing bug: a field like `"r":false,"g":true` gets `r` read as `true` | `strstr(v, "true")` scans past field boundaries — use a bounded `strncmp(v, "true", 4)` after skipping whitespace instead | fixed in `node/lib/blinky_thread/blinky_thread.c`; consider Zephyr's `json_obj_parse` for anything more complex |
| Gitignore pattern like `build*/*` doesn't match nested `node/build/` | Patterns containing an internal slash anchor to the repo root, not "any depth" — prefix with `**/` | `.gitignore` at repo root |
| WiFi Guru Meditation crash / "WiFi Connect failed 11 times" (only relevant if reviving `ot_br`) | WiFi/802.15.4 coexistence-induced disconnects exceeding the default max-retry of 6 | raise `Example Connection Configuration → WiFi Maximum Retry` to 1000; see [architecture.md](./architecture.md) for why we moved off `ot_br` anyway |
| `ot_rcp` builds fine, flashes fine, but ROM boot log is visible while a Spinel reset-frame test gets 0 bytes back — even after the USB-transport fix was already applied once | ESP-IDF checked out at 5.4.x, where `CONFIG_OPENTHREAD_RCP_USB_SERIAL_JTAG` doesn't exist in Kconfig at all — the choice silently re-resolves to UART, even if you hand-edit `sdkconfig` and force a real `reconfigure` | [rcp-firmware.md](./rcp-firmware.md) |
| `ot-ctl <anything>` → `connect session failed: Permission denied` (native/systemd setup, not Docker) | `/run/openthread-wpan0.sock` is root-owned, no write access for other users; `otbr-agent` runs as root but `ot-ctl` was run as a plain user | run `ot-ctl` via `sudo`; [border-router-native.md](./border-router-native.md) |
| `otbr-agent` exits immediately with `Vendor name must be set.` | `--vendor-name`/`--model-name` are required flags, easy to miss because `otbr-agent --help \| head -5` truncates before showing them | [border-router-native.md](./border-router-native.md) |
| Home Assistant shows the border router as `otf68dd64d93b0d68e.local.` / `OpenThread BorderRouter #D68E`, not the host's real mDNS name (e.g. `iris.local`) | Expected — `otbr-agent` advertises its Border Agent identity from its own internally-generated Extended Address (`ot-ctl extaddr`), independent of the host machine's hostname, by design | [border-router-native.md](./border-router-native.md) |
| `git clone --recursive` of `ot-br-posix` (or the build itself) fails or the shell reports issues partway through on a Raspberry Pi | Almost certainly disk space, not a network blip — check `df -H` before assuming otherwise. A from-source build (mbedtls submodule alone is ~200MB) does not fit on a small (~3-4G) boot drive | build the image on a machine with real disk/CPU instead, extract just the `otbr-agent`/`ot-ctl` binaries, transfer those; [border-router-native.md](./border-router-native.md) |
| Thread attach works fine (node shows up in `child table`/`neighbor table`), but nothing the node sends (UDP announce, etc.) ever reaches anything on the LAN — BR log shows the packet arriving (`MeshForwarder-: Received ... dst:[LAN-address]`) but never a corresponding "Sent" | Kernel IPv6 forwarding disabled (`cat /proc/sys/net/ipv6/conf/all/forwarding` → `0`) — `ip6tables` FORWARD policy being `ACCEPT` is not sufficient on its own; Debian ships with forwarding off by default and the original Docker `run` script never explicitly enables it either | [border-router-native.md](./border-router-native.md) |
| Node attaches, `SRP: client started (host "...")` prints on the node's own console, but `sudo ot-ctl srp server host` on the BR stays empty forever, no matter how long you wait | `otSrpClientAddService()` was never called anywhere in `node/lib/thread/thread.c` — per the real OpenThread header docs, the SRP client only ever sends an update once host name, host address, AND at least one service are all set. Missing the service means it sits "started" but never transmits anything, ever | fixed 2026-08-16, `node/lib/thread/thread.c` — registers `_coap._udp` |
| `.local` resolves fine via `avahi-resolve`/`ping6` from a Mac, but fails (`Name or service not known`) from a Debian/Linux box that has `avahi-daemon`/`libnss-mdns` installed | `/etc/nsswitch.conf`'s `hosts:` line uses `mdns4_minimal` — the `4` means IPv4-only; a Thread node's record is IPv6-only (AAAA, no A record), so it's correctly "not found" by an IPv4-only resolver module | either add `mdns6_minimal` to nsswitch.conf (system-wide change), or sidestep NSS entirely with `avahi-resolve -6 -n <hostname>.local` (no system config touched) |
| CoAP commands **from the LAN to a node** (`ac on`, etc.) reliably fail — send, retry 4x over ~70-85s, give up, zero response — while the **reverse** direction (node's own UDP announce, SRP registration) works fine | Unresolved as of 2026-08-16. Ruled out: app code (fails identically on two unrelated handlers), `ip6tables` policy, kernel routing decision, the `trel://` radio link. BR's own log never shows the destination address at all during a failed attempt — request isn't reaching `otbr-agent`'s own processing despite firewall/routing saying it should. See `border-router-native.md`'s "Known unresolved issue" section for the full ruled-out list and next steps (`tcpdump` on `wpan0` is the next untried check) | [border-router-native.md](./border-router-native.md) |

## Dead ends — don't retry these without a new reason to

- **Raspberry Pi 4B + this specific 16GB SD card**: hits `cmd 371a0010
  status 1fff0001` on boot, a known unresolved upstream "SD Card Killer"
  bug. Not fixed by re-imaging. **Resolved 2026-08-16**: booting from a USB
  drive instead of the SD card — exactly the workaround this doc already
  named — works fine (the Pi, `iris`, has been running this way since).
  Not a dead end anymore, just don't use that SD card for booting.
- **Building `ot-br-posix` from source directly on a resource/disk-
  constrained device** (e.g. a Pi with a small boot drive): the clone alone
  (mbedtls submodule ~200MB) plus the actual compile does not fit on a
  ~3-4G drive. Don't retry this without first checking `df -H` — if it's
  tight, build elsewhere and extract the binaries instead; see
  [border-router-native.md](./border-router-native.md).
- **EndeavourOS, if the system hasn't been updated in ~a year**: pacman
  keyring/package-rename conflicts (`linux-firmware-nvidia`,
  `gcc-libs`/`libgcc`) cascade badly. `pacman-key --init/--populate` plus
  `--overwrite '*'` gets partway there but this specific box was abandoned
  as a dead end after repeated new conflicts. If revisited, budget for a
  full `pacman -Syyu --overwrite '*'` rather than targeted package fixes.
- **Docker Desktop on Mac for anything needing `--network host`**: as of
  this writing, Docker Desktop for Mac's VM-based architecture doesn't
  support real host networking the way Linux Docker does — this is a
  structural limitation, not a missing flag. (USB/IP passthrough does work
  since Docker Desktop 4.35+, if only USB access is needed, but that's a
  separate feature from host networking.)
- **Patching the vendored RCP source to loosen the `keyIdMode` check**:
  considered and explicitly rejected in favor of fixing it host-side (build
  `ot-br-posix` from source with the submodule bumped past the fix
  commit). Don't revisit without discussing first — the RCP-side patch
  would need to also *normalize* the value before it's used downstream
  (Security Control encoding), not just relax the check, and even then
  it's patching vendored third-party source rather than using the real
  upstream fix.
- **`ot_br` doesn't currently build on this ESP-IDF checkout** — see
  exact versions and the failure below. Not yet fixed; don't assume it
  just works if you come back to it.

## `ot_br` build failure: exact versions, for reproducing/fixing later

Attempted 2026-08-11, using the same `~/iot/esp-idf` checkout as the
working `ot_rcp` build:

- ESP-IDF: `5.5.5` (`git log -1` in `~/iot/esp-idf` shows commit `b774170`)
- Vendored OpenThread submodule: commit `b678a4f63b6f9397d1a0fa8f31e5b8e0271a4d00`,
  dated 2026-07-03 (same one `ot_rcp` uses successfully — this is not an
  `ot_rcp`-vs-`ot_br` version split, both examples share the same
  checkout)
- `ot_br/main/idf_component.yml` pins `espressif/esp_ot_cli_extension:
  "~2.0.0"` — after clearing the stale lock (`rm -rf managed_components
  dependencies.lock`) this re-resolved to the latest matching version,
  **2.0.3**, and the failure persisted, so this is not a stale-lock
  problem.

**Failure**: link-time `undefined reference to 'otCliOutputFormat'` and
`undefined reference to 'otCliSetUserCommands'`, from
`esp_ot_cli_extension` v2.0.3's own source
(`esp_ot_tcp_socket.c`, `esp_ot_cli_extension.c`, `esp_ot_curl.c`,
`esp_ot_heap_diag.c`, `esp_ot_ip.c`). These two symbols don't exist
anywhere in the currently-vendored OpenThread's CLI module — a real
upstream API mismatch between `esp_ot_cli_extension` 2.0.3 and this
OpenThread commit, not something a lock-file refresh can fix.

**Why `ot_rcp` is unaffected**: it never links this component at all
(`CONFIG_OPENTHREAD_CONSOLE_ENABLE=n` in the RCP build, so no CLI, so no
dependency on `esp_ot_cli_extension`). This is purely an `ot_br`-specific
problem.

**Not yet done, options for whoever picks this back up**:
1. Comment out the one call site pulling this component in —
   `main/esp_ot_br.c:126` (`esp_cli_custom_command_init();`) and its
   `#include "esp_ot_cli_extension.h"` at line 31 — then remove the
   `espressif/esp_ot_cli_extension` entry from `main/idf_component.yml`.
   This only removes optional extra CLI commands (curl, raw TCP socket,
   heap diagnostics) — not needed for border router functionality. This
   is editing Espressif's example app code, not vendored OpenThread
   library source, but was left undone pending explicit go-ahead since it
   is still a source edit.
2. Check if a newer `esp_ot_cli_extension` release (past 2.0.3, if one
   exists by the time this is revisited) has caught up with the current
   OpenThread CLI API.
3. Pin `ot_br`'s OpenThread submodule to an older commit matching what
   `esp_ot_cli_extension` 2.0.3 actually expects — not investigated, and
   would put `ot_br` on a different OpenThread version than `ot_rcp`.

## Process lessons (not technical, but keep doing these)

- **Verify claims with hard evidence, not "looks plausible."** The
  `SetMacKey` bug was root-caused by actually reading the real upstream
  commits (dates, PR numbers, diffs) rather than guessing from symptoms —
  this is what turned "some version mismatch, probably" into an exact,
  two-day compatibility window with a named fix commit.
- **Don't trust which Docker image/entrypoint is actually running from
  memory** — `docker buildx imagetools inspect` and `docker exec ... which
  <binary>` settle it directly instead of re-reading GitHub source and
  assuming it matches what's deployed.
- **A file existing inside an image's filesystem doesn't mean it's what
  runs.** The s6-overlay border-router scripts were present in
  `openthread/otbr` (the wrong image) too — only checking the actual
  `Config.Entrypoint` revealed what really executes by default.
