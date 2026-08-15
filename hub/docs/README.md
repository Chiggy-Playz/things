# hub/ — Thread Border Router

This directory documents the border router side of the project: an ESP32-C6
acting as a Radio Co-Processor (RCP), paired with a full `ot-br-posix`
border router running as the actual host software.

**Read `PROGRESS.md` at the repo root first, always** — it's the top-level
"what's currently true" doc and says which of the setups below is actually
running right now. The docs in this directory accumulate every setup ever
tried (some retired, some active) — don't assume the most recently-added doc
or the most detailed one is the current one without checking `PROGRESS.md`.

No firmware/config files live in this repo yet for the border router side —
this is pure documentation of a working setup that currently exists outside
this repo. If/when that gets migrated into the repo proper, it should live
under `hub/`.

## Read these in order

1. [architecture.md](./architecture.md) — why RCP + host border router at
   all, not the single-chip `ot_br` example (`ot_br` is now fully retired,
   this doc is background/history, not something to act on).
2. [rcp-firmware.md](./rcp-firmware.md) — building/flashing the ESP32-C6 RCP
   firmware. Read this regardless of which host setup you're using below —
   the RCP side is shared. **Contains a real landmine**: the USB-Serial-JTAG
   transport fix requires ESP-IDF ≥5.5; it silently reverts to broken on
   5.4.x with zero error message. Don't skip this doc assuming "it's already
   flashed, I don't need to read it."
3. **Pick ONE of these two, based on what `PROGRESS.md` says is currently
   active — don't read both as if they're both live:**
   - [border-router-native.md](./border-router-native.md) — **the currently
     active setup** (as of 2026-08-16): native `otbr-agent`/`ot-ctl`
     binaries extracted from the official Docker image, run via systemd on
     a Raspberry Pi. No Docker on the target device at all.
   - [border-router-docker.md](./border-router-docker.md) — the Docker
     Compose approach (previously ran on Omarchy). **Currently NOT in use**
     — kept because it's a legitimate alternative if the constraints ever
     favor it again (e.g. a host with real disk/CPU to spare), and because
     `border-router-native.md` explicitly builds on top of it (same image,
     same Dockerfile, just extracted rather than run in-container).
4. [thread-dataset.md](./thread-dataset.md) — configuring the border router
   with the same Thread network credentials the Zephyr nodes expect. Same
   credentials regardless of which host setup above you used.
5. [troubleshooting.md](./troubleshooting.md) — symptom-indexed reference
   for everything that's gone wrong so far, across all of the above. Check
   this before spending time re-diagnosing something from scratch.

## Current known-good state — see `PROGRESS.md` at the repo root

Don't duplicate the live status here; it goes stale immediately and this
file is not what gets read first in a new session. `PROGRESS.md` is the one
source of truth for "what's actually running right now."
