"""Raw IR capture rig - MicroPython, runs on a separate Pico (not the
Zephyr node itself), wired to a TSOP1838 IR receiver on GP1.

Used to reverse-engineer the real Voltas remote's actual on-the-wire
behavior (e.g. whether OffTimerEnable/duration persist across unrelated
button presses) against the byte layout documented in node/lib/ir/voltas.c
- see that file's header comment for what's already been decoded from
prior captures.

Usage: run on the Pico (Thonny, or `mpremote run ir_capture.py`), point
the real remote at the TSOP, press a button. Each press prints one
"FRAME <n edges>" line followed by a raw list of mark/space durations in
microseconds, alternating mark(LOW), space(HIGH), starting from the first
mark - TSOP1838's output is active-low, so it idles HIGH and pulls LOW
during an IR mark.
"""

from machine import Pin
import time
import array

IR_PIN = 1  # GP1 - TSOP1838 OUT
MAX_EDGES = 400  # a ~90-bit Voltas frame needs ~180 edges; plenty of headroom
IDLE_GAP_US = 10000  # 10ms of silence = frame's done

buf = array.array("i", [0] * MAX_EDGES)
idx = 0
last_edge = time.ticks_us()
start_ms = time.ticks_ms()


def on_edge(pin):
    global idx, last_edge
    now = time.ticks_us()
    if idx < MAX_EDGES:
        buf[idx] = now
        idx += 1
    last_edge = now


ir = Pin(IR_PIN, Pin.IN)
ir.irq(trigger=Pin.IRQ_RISING | Pin.IRQ_FALLING, handler=on_edge)

print("Ready - point the remote at the TSOP and press a button.")

while True:
    if idx > 1 and time.ticks_diff(time.ticks_us(), last_edge) > IDLE_GAP_US:
        ir.irq(handler=None)  # pause capture while we read out the buffer
        n = idx
        durations = [time.ticks_diff(buf[i + 1], buf[i]) for i in range(n - 1)]
        idx = 0
        ir.irq(trigger=Pin.IRQ_RISING | Pin.IRQ_FALLING, handler=on_edge)
        elapsed_s = time.ticks_diff(time.ticks_ms(), start_ms) / 1000
        print("FRAME t=%.1fs %d edges" % (elapsed_s, n))
        print(durations)
    time.sleep_ms(5)
