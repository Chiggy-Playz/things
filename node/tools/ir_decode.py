"""Decodes raw mark/space captures from ir_capture.py's log output into
Voltas protocol bytes/fields, per the byte layout documented in
node/lib/ir/voltas.c's header comment.

Usage: python3 ir_decode.py [path/to/ir.log]  (defaults to /tmp/ir.log)
"""

from __future__ import annotations

import ast
import re
import sys

FRAME_RE = re.compile(r"FRAME t=([\d.]+)s (\d+) edges")
LONG_SPACE_THRESHOLD_US = 1500  # clear gap between ~550us zeros and ~2550us ones
VOLTAS_STATE_LEN = 10


def decode_frame(durations: list[int]) -> list[int]:
    """durations alternates mark, space, mark, space, ... (first is a mark,
    no header pulse in this protocol). Bits come from spaces: long = 1,
    short = 0. MSB-first per byte (matches ir_dialect_t's lsb_first=false
    default for Voltas)."""
    spaces = durations[1::2]
    bits = [1 if s > LONG_SPACE_THRESHOLD_US else 0 for s in spaces]

    state = []
    for byte_start in range(0, len(bits) - 7, 8):
        byte_bits = bits[byte_start : byte_start + 8]
        value = 0
        for bit in byte_bits:
            value = (value << 1) | bit
        state.append(value)
    return state


def describe(state: list[int]) -> str:
    if len(state) < VOLTAS_STATE_LEN:
        return f"  (only {len(state)} bytes decoded, expected {VOLTAS_STATE_LEN} - check capture)"

    swing_h = state[0] & 0x01
    swing_h_change = state[0] >> 1

    mode = state[1] & 0x0F
    fan = (state[1] >> 5) & 0x07

    swing_v = state[2] & 0x07
    wifi = (state[2] >> 3) & 0x01
    turbo = (state[2] >> 5) & 0x01
    sleep = (state[2] >> 6) & 0x01
    power = (state[2] >> 7) & 0x01

    temp = (state[3] & 0x0F) + 16
    econo = (state[3] >> 6) & 0x01

    on_not24hr = state[4] & 0x01
    on_12hr = (state[4] >> 7) & 0x01

    off_not24hr = state[5] & 0x01
    off_12hr = (state[5] >> 7) & 0x01

    on_hrs = state[7] & 0x0F
    off_hrs = (state[7] >> 4) & 0x0F

    light = (state[8] >> 5) & 0x01
    off_timer_enable = (state[8] >> 6) & 0x01
    on_timer_enable = (state[8] >> 7) & 0x01

    hex_bytes = " ".join(f"{b:02X}" for b in state)

    return (
        f"  bytes: {hex_bytes}\n"
        f"  power={power} mode=0x{mode:X} fan=0x{fan:X} temp={temp}C\n"
        f"  swingH={swing_h} (change=0x{swing_h_change:02X}) swingV=0x{swing_v:X}"
        f" turbo={turbo} sleep={sleep} econo={econo} light={light} wifi_bit={wifi}\n"
        f"  OnTimer:  enable={on_timer_enable} not24hr={on_not24hr} 12hr={on_12hr} hrs_nibble={on_hrs}\n"
        f"  OffTimer: enable={off_timer_enable} not24hr={off_not24hr} 12hr={off_12hr} hrs_nibble={off_hrs}"
    )


def main() -> None:
    path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ir.log"
    with open(path) as f:
        lines = f.readlines()

    i = 0
    frame_num = 0
    while i < len(lines):
        m = FRAME_RE.search(lines[i])
        if m:
            elapsed_s, n_edges = m.group(1), m.group(2)
            durations = ast.literal_eval(lines[i + 1].strip())
            state = decode_frame(durations)
            frame_num += 1
            print(f"--- Frame {frame_num} (t={elapsed_s}s, {n_edges} edges) ---")
            print(describe(state))
            print()
            i += 2
        else:
            i += 1


if __name__ == "__main__":
    main()
