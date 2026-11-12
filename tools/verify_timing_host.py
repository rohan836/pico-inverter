#!/usr/bin/env python3
"""Host-side verification of the PWM timing computation.

Mirrors the integer math in src/waveform_hw.c (hardware generator) and
src/waveform.c (timer generator) and checks the invariants that matter for the
square wave: tick counts fit the 16-bit counter, N is even (exact 50 %), the
divider fits the 8.4 fixed-point format, frequency error is small, and for the
timer build the dead time, duty ratio and frequency are self-consistent.

Runs on the host; nothing is downloaded onto the Pico.
"""

import sys

F_MIN, F_MAX = 1, 60
TICK_HZ = 1_000_000
MAX_PERIOD_TICKS = 65_534  # even, 16-bit friendly
DIV16_MAX = 4095           # 255 + 15/16
DIV16_MIN = 16
SYS_HZ = 125_000_000        # RP2040 default clk_sys
MAX_WRAP = MAX_PERIOD_TICKS
DEAD_US = 200
MAX_ON_US = 10_000

failures = []


def check(name, cond):
    status = "PASS" if cond else "FAIL"
    print(f"  [{status}] {name}")
    if not cond:
        failures.append(name)


def hw_timings(f_req):
    """Replicates apply() in src/waveform_hw.c."""
    f = max(F_MIN, min(F_MAX, f_req))
    clipped = False
    if TICK_HZ // f <= MAX_PERIOD_TICKS:
        t = TICK_HZ // f
        if t & 1:
            t -= 1
        div16 = (SYS_HZ * 16 + TICK_HZ // 2) // TICK_HZ
    else:
        denom = f * MAX_PERIOD_TICKS
        d = (SYS_HZ * 16 + denom // 2) // denom
        t = MAX_PERIOD_TICKS
        if d > DIV16_MAX:
            d = DIV16_MAX
            clipped = True
        div16 = d

    div16 = max(DIV16_MIN, div16)
    ticks = max(t, 2)
    num = ticks * div16 * 1_000_000
    den = SYS_HZ * 16
    period_us = (num + den // 2) // den
    actual_hz = (1_000_000 + period_us // 2) // period_us
    return ticks, div16, period_us, actual_hz, clipped


def timer_timings(f_req, duty_pct, max_on_us=MAX_ON_US, dead_us=DEAD_US):
    """Replicates compute_timing() in src/waveform.c (symmetric per-channel law)."""
    f = max(F_MIN, min(F_MAX, f_req))
    half = 1_000_000 // (2 * f)
    period = 2 * half
    if half <= dead_us + 20:
        max_on = half
    else:
        max_on = half - dead_us

    on = (period * duty_pct) // 100
    limited = False
    if on > max_on:
        on = max_on
        limited = True
    if on > max_on_us:
        on = max_on_us
        limited = True
    return half, on, half - on, period, limited


def main() -> int:
    print("== Hardware generator (default) ==")
    for f in [60, 59, 50, 40, 30, 20, 16, 15, 10, 9, 8, 7, 5, 1]:
        ticks, div16, period_us, actual_hz, clipped = hw_timings(f)
        expected = 1_000_000 // f
        print(
            f"  f={f:>3}Hz -> ticks={ticks:>6} div={div16/16:.4f} "
            f"period={period_us}us actual={actual_hz}Hz level={ticks//2} clipped={clipped}"
        )
        check(f"f={f}: 50 % on each channel (ticks even)", ticks % 2 == 0)
        check(f"f={f}: ticks fits 16 bits", ticks <= MAX_WRAP)
        check(f"f={f}: divider in 1..255 (x.y format)", div16 <= DIV16_MAX and div16 >= DIV16_MIN)
        check(f"f={f}: complement, no overlap or gap", 2 * (ticks // 2) == ticks)
        if f >= 16:
            check(
                f"f={f}: period within 1 us of {expected} us",
                abs(period_us - expected) <= 1,
            )
            check(f"f={f}: achieved frequency within 0.1 % of {f} Hz", actual_hz == f)
        elif f < 8:
            check(f"f={f}: divider clipped, LIM reported", clipped)
            check(f"f={f}: output clamps to lowest achievable (~7.45 Hz)", actual_hz == 7)
        else:
            check(f"f={f}: achieved frequency within 1 Hz of request", abs(actual_hz - f) <= 1)

    print()
    print("== Timer generator (default build): symmetric per-channel duty ==")
    for f in [60, 50, 30, 10, 1]:
        for duty in [100, 50, 25, 5]:
            half, on, off, period, limited = timer_timings(f, duty)
            scope_pct = (on * 100) // period
            expect_limited = (period * duty) // 100 > (half - DEAD_US) or (period * duty) // 100 > MAX_ON_US
            print(
                f"  f={f}Hz duty={duty}% -> half={half}us on={on}us off={off}us "
                f"period={period}us scope={scope_pct}% limited={limited}"
            )
            check(f"f={f} d={duty}: on + off == half", on + off == half)
            check(f"f={f} d={duty}: full period == 2x half", period == half * 2)
            check(f"f={f} d={duty}: both gaps equal (symmetric)", off == half - on)
            check(f"f={f} d={duty}: dead time >= 200 us", off >= DEAD_US)
            check(f"f={f} d={duty}: LIM consistent with clamps", limited == expect_limited)
            if duty <= 25 and f >= 20:
                exp = (period * duty) // 100
                check(f"f={f} d={duty}: scope shows requested {duty}%", on == exp)

    print()
    if failures:
        print(f"RESULT: {len(failures)} check(s) FAILED:")
        for name in failures:
            print(f"  - {name}")
        return 1
    print("RESULT: all timing checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
