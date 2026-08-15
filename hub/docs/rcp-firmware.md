# RCP firmware: building, flashing, and the USB-Serial-JTAG fix

## The bug this section exists to prevent repeating

The single biggest time sink in this whole project was a completely silent,
non-obvious hardware misconfiguration: **`ot_rcp`'s default build puts
Spinel traffic on real UART0, on physical GPIO pins that are never wired to
the ESP32-C6's USB port.** Every diagnostic — Docker, `otbr-agent`, a
`socat` tap, even a from-scratch raw `pyserial` script — was correctly
talking to `/dev/cu.usbmodemXXXX`, and that port was correctly reaching the
chip's **USB-Serial-JTAG** peripheral. It just wasn't the peripheral the
firmware had assigned to Spinel. The RCP was never broken; every layer was
knocking on a door nothing was listening at.

Symptom trail this produces if you hit it again: `Init() at
spinel_driver.cpp:87: Failure`, or `Init() at hdlc_interface.cpp:154: No
such file or directory`, or literally zero bytes back from a raw serial
test — all while the board's ROM bootloader boot log is clearly visible
over that same port (because the ROM bootloader *does* use
USB-Serial-JTAG for its console, independent of what Spinel is configured
to use).

## The fix: build with Spinel over USB-Serial-JTAG

In `idf.py menuconfig`:

```
Component config → OpenThread → Thread Radio Co-Processor Feature
  → The RCP transport type → USB RCP
```

This sets `CONFIG_OPENTHREAD_RCP_USB_SERIAL_JTAG=y` (and clears
`CONFIG_OPENTHREAD_RCP_UART`). Its guard condition,
`depends on ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG && !OPENTHREAD_CONSOLE_TYPE_USB_SERIAL_JTAG`,
is satisfied by the example's defaults, so it's selectable without other
changes.

With this set, Spinel and the same USB port you already have plugged in
share the USB-Serial-JTAG peripheral — no rewiring, no separate USB-UART
adapter needed.

## Build and flash

From `~/iot/esp-idf/examples/openthread/ot_rcp`:

```bash
source ~/iot/esp-idf/export.sh
idf.py set-target esp32c6      # first time only
idf.py menuconfig              # apply the RCP transport change above
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash
```

## Verifying it actually works (don't trust "it flashed")

A successful flash does not mean Spinel is actually reachable — verify at
the protocol level. A minimal, dependency-light way to do this without
Docker/otbr-agent in the loop:

```bash
uv run --with pyserial python3 -c "
import serial, time
ser = serial.Serial('/dev/cu.usbmodemXXXX', 460800, timeout=1)

# Force a clean reset into RUN mode — a plain serial open does NOT do this
# the way esptool/idf.py monitor do. Without it you may see 0 bytes back
# and wrongly conclude the board is dead.
ser.setDTR(False); ser.setRTS(True); time.sleep(0.1)
ser.setRTS(False); time.sleep(0.1)
time.sleep(3)
print(ser.read(4096))  # should show ESP-ROM boot text

ser.reset_input_buffer()
frame = bytes([0x7e, 0x80, 0x01, 0x02, 0xea, 0xf0, 0x7e])  # Spinel reset
ser.write(frame); ser.flush()
time.sleep(2)
print(ser.read(256).hex())  # should NOT be empty
"
```

A working RCP replies to that reset frame with something like
`7e80060070ee747e` — decode as: `80` header, `06` = `CMD_PROP_VALUE_IS`,
`00` = property `LAST_STATUS`, `70` = `STATUS_RESET_POWER_ON`. If you want
to be fully sure it's real (not noise that happens to parse), the HDLC FCS
(CRC-16/CCITT, poly `0x8408`, init `0xFFFF`) of the frame should verify
against the trailing two bytes — see git history of this doc / session
transcript for the verification script if needed.

**Key lesson learned along the way**: if this test shows 0 bytes back
_including_ no ROM boot log, that likely means the board is sitting in
whatever DTR/RTS state a previous tool left it in (possibly the ROM
bootloader waiting for a flash tool, not the actual app) — not that the
board is dead. Always force the reset sequence above before concluding
anything about RCP behavior from a raw serial test.

## Second bug this section exists to prevent repeating: the USB transport option doesn't exist before ESP-IDF 5.5

`CONFIG_OPENTHREAD_RCP_USB_SERIAL_JTAG` (the fix above) is **not present at
all** in ESP-IDF 5.4.x's Kconfig — the `choice OPENTHREAD_RCP_TRANSPORT`
block there only has `OPENTHREAD_RCP_UART` and `OPENTHREAD_RCP_SPI` as
members. If the shared `~/iot/esp-idf` checkout gets switched to 5.4.x for
any reason (e.g. to satisfy `ot_br`, see
[troubleshooting.md](./troubleshooting.md)), rebuilding `ot_rcp` there will
**silently** re-resolve the choice back to `OPENTHREAD_RCP_UART` — no error,
no warning. Every symptom in this doc's "bug this section exists to prevent
repeating" section above reproduces exactly: ROM boot log visible, zero bytes
back from a Spinel reset frame test.

This is genuinely silent — directly editing `sdkconfig` to force
`CONFIG_OPENTHREAD_RCP_USB_SERIAL_JTAG=y` does **not** stick, even across a
real `idf.py reconfigure`. Kconfig's choice-resolution logic re-derives the
value from the (now nonexistent) menu entry and reverts it every time,
regardless of what the raw file says. The only real fix is switching the
checkout to ESP-IDF ≥5.5 (confirmed working: 5.5.5) and rebuilding from
scratch there:

```bash
cd ~/iot/esp-idf
git checkout v5.5.5
git submodule update --init --recursive
cd examples/openthread/ot_rcp
rm -rf build sdkconfig sdkconfig.old
source ~/iot/esp-idf/export.sh
idf.py set-target esp32c6
idf.py menuconfig   # re-apply the USB RCP transport setting
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash
```

Then re-verify with the pyserial reset-frame test — don't trust the build
succeeding, same as always.

As of 2026-08-16, `ot_br` has been fully retired in favor of the Pi-based
native border router (see
[border-router-native.md](./border-router-native.md)), so there's no longer
a reason to keep this checkout on 5.4.x at all — it should just live on
5.5.5 (or newer) going forward, since only `ot_rcp` needs building from it
now.

## Swapping the same board between `ot_br` and `ot_rcp`

`ot_br` and `ot_rcp` are separate example project directories, each with
their own independent `build/` and `sdkconfig`. Flashing one does not
touch or reset the other's on-disk configuration — it only changes what's
currently in the chip's flash memory. To switch back:

```bash
cd ~/iot/esp-idf/examples/openthread/ot_rcp   # or ot_br
source ~/iot/esp-idf/export.sh
idf.py -p /dev/cu.usbmodemXXXX flash
```

No need to redo `menuconfig` each time — whichever project you last
configured keeps its settings.

Note: the Thread dataset lives entirely on the host/`otbr-agent` side (see
[thread-dataset.md](./thread-dataset.md)), not on the RCP chip. Swapping
the RCP firmware back and forth has no effect on the dataset persisted on
the Docker host.
