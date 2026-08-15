# things

Wireless, coin-cell-powered IR blaster nodes (nRF52840/Zephyr) that control
ACs over Thread, driven from Home Assistant. Two AC IR dialects supported:
Voltas, Teco. One node deployed for real — `chiggy-room-climate`, a Voltas
AC in Chiggy's room.

This file is the table of contents. Read it before diving into any one doc —
the project has been through several architecture changes, and the docs
below accumulate *all* of them, not just the current one.

## Start here

1. **[`PROGRESS.md`](PROGRESS.md)** — current status, dated. This is the
   one doc that says what's actually true *right now* versus what was true
   in an earlier phase of the project. Always read this first in a new
   session, before any doc below.
2. **[`hub/docs/README.md`](hub/docs/README.md)** — border router docs,
   with their own explicit reading order and a note on which setup is
   currently active vs. retired.
3. **`node/`** — the Zephyr firmware for the battery-powered IR nodes
   itself. No separate doc yet; the code is the source of truth (`lib/ir/`
   for the Voltas/Teco protocol encoders, `lib/thread/` for Thread
   networking, `Kconfig`/`config/*.conf` for the per-build-variant options).
4. **`_archive/`** — retired earlier version of the firmware. Historical
   only, not part of the current build.

## Timeline (dated, chronological — the short version of `PROGRESS.md`)

- **2026-08-11 (evening)**: Docker/Omarchy border router setup retired in
  favor of a standalone single-chip `ot_br` build, as a temporary measure.
  Later that session: the AC node went unresponsive after being left alone
  most of a day — investigation started, not yet resolved that night.
- **2026-08-15/16 (night)**: the AC node's unresponsiveness fully
  root-caused — a worn coin cell plus the firmware having zero
  power-management wired in (~8mA constant idle draw, should be
  microamp-range). Fixes written into `node/prj.conf` and
  `node/boards/ir.overlay`, **not yet rebuilt/reflashed/re-measured** — see
  `PROGRESS.md` for the exact remaining verification steps.
- **2026-08-16**: standalone `ot_br` retired too, in favor of a native
  (no Docker) `otbr-agent` running via systemd on a Raspberry Pi (`iris`).
  Confirmed working end-to-end: RCP responds to Spinel, `otbr-agent` forms
  as Thread leader with the correct dataset, survives a real reboot, and
  shows up in Home Assistant. One real landmine hit and documented along
  the way: the USB-Serial-JTAG RCP transport doesn't exist in ESP-IDF
  5.4.x's Kconfig at all (see `hub/docs/rcp-firmware.md`) — the shared
  ESP-IDF checkout is now on 5.5.5.

## What's NOT yet done (as of 2026-08-16)

- Confirm the AC node actually attaches to the new Pi-based border router
  and that `ac on`/`ac off` work through it end-to-end.
- Rebuild/reflash/re-measure the node's power-management firmware fixes.
- Battery voltage reporting (planned, no code written).
- A real Home Assistant integration for the AC node itself, beyond raw
  `ac.sh`/`coap-client` (three options sketched, none chosen).

See `PROGRESS.md` for the full detail on all of the above.
