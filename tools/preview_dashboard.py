#!/usr/bin/env python3
"""Host preview of the modern landscape dashboard for 1.3-inch OLED (128x64).

Mirrors src/dashboard.c, src/digits.c and src/display.c so the 128x64 frame
can be inspected without flashing. Renders an upscaled OLED-blue PNG.

Usage:
    python tools/preview_dashboard.py [freq] [duty] [on|off] [lim] [phase]
"""

import re
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import gen_digits  # noqa: E402

W, H = 128, 64
FONT_W, FONT_H = 5, 7

# Zone 1 Coordinates: Frequency
Z1_INNER_X = 5
Z1_INNER_W = 52
Z1_HEADER_Y = 4
Z1_RULE_Y = 13
Z1_NUMERAL_Y = 18

# Zone 2 Coordinates: Duty Cycle
Z2_INNER_X = 71
Z2_INNER_W = 52
Z2_HEADER_Y = 4
Z2_RULE_Y = 13
Z2_NUMERAL_Y = 21

# Zone 3 Coordinates: Dynamic Visual Progress Bar
Z3_FRAME_X = 2
Z3_FRAME_Y = 51
Z3_FRAME_W = 124
Z3_FRAME_H = 12

Z3_TRACK_X = 4
Z3_TRACK_Y = 53
Z3_TRACK_W = 120
Z3_TRACK_H = 8

PULSE_WIDTH = 8

BAYER4 = (
    (0, 8, 2, 10),
    (12, 4, 14, 6),
    (3, 11, 1, 9),
    (15, 7, 13, 5),
)

DIGITS = {
    "hero": gen_digits.build_font(34, 2),
    "mid": gen_digits.build_font(26, 1),
    "small": gen_digits.build_font(20, 1),
}


def load_font():
    text = (ROOT / "src" / "font.c").read_text(encoding="utf-8")
    rows = re.findall(r"\{\s*((?:0x[0-9A-Fa-f]{2}\s*,\s*){4}0x[0-9A-Fa-f]{2})\s*\}", text)
    return [[int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", r)] for r in rows]


FONT = load_font()


class Frame:
    def __init__(self):
        self.px = [[False] * W for _ in range(H)]

    def pixel(self, x, y, on=True):
        if 0 <= x < W and 0 <= y < H:
            self.px[y][x] = on

    def fill_rect(self, x, y, w, h, on=True):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.pixel(xx, yy, on)

    def rect(self, x, y, w, h, on=True):
        if w <= 0 or h <= 0:
            return
        self.fill_rect(x, y, w, 1, on)
        self.fill_rect(x, y + h - 1, w, 1, on)
        self.fill_rect(x, y, 1, h, on)
        self.fill_rect(x + w - 1, y, 1, h, on)

    def text(self, x, y, s, scale=1):
        for ch in s:
            code = ord(ch.upper())
            if code < 32 or code > 127:
                code = ord("?")
            bits = FONT[code - 32]
            for gy in range(FONT_H):
                for gx in range(FONT_W):
                    if (bits[gx] >> gy) & 1:
                        for yy in range(scale):
                            for xx in range(scale):
                                self.pixel(x + gx * scale + xx, y + gy * scale + yy, True)
            x += (FONT_W + 1) * scale

    def digits(self, x, y, s, name, on=True):
        font = DIGITS[name]
        for ch in s:
            if not ch.isdigit():
                continue
            g = font["glyphs"][int(ch)]
            for py in range(g["h"]):
                for px in range(g["w"]):
                    cov = g["cov"][py * g["w"] + px]
                    if cov == 0:
                        continue
                    sx, sy = x + px, y + py
                    if cov >= 224 or BAYER4[sy & 3][sx & 3] < (cov >> 4):
                        self.pixel(sx, sy, on)
            x += g["w"] + font["tracking"]


def digits_width(s, name):
    font = DIGITS[name]
    widths = [font["glyphs"][int(ch)]["w"] for ch in s if ch.isdigit()]
    return sum(widths) + font["tracking"] * max(0, len(widths) - 1)


def draw_datum_brackets(f, left, top, right, bottom):
    f.pixel(left, top, True)
    f.pixel(left + 1, top, True)
    f.pixel(left, top + 1, True)

    f.pixel(right, top, True)
    f.pixel(right - 1, top, True)
    f.pixel(right, top + 1, True)

    f.pixel(left, bottom, True)
    f.pixel(left + 1, bottom, True)
    f.pixel(left, bottom - 1, True)

    f.pixel(right, bottom, True)
    f.pixel(right - 1, bottom, True)
    f.pixel(right, bottom - 1, True)


def ping_pong(t, span):
    if span <= 0:
        return 0
    period = 2 * span
    m = t % period
    return m if m < span else (span - (m - span))


def draw_dynamic_progress_bar(f, duty_pct, on, limited, phase):
    # 1. Precision outer frame
    f.rect(Z3_FRAME_X, Z3_FRAME_Y, Z3_FRAME_W, Z3_FRAME_H, True)

    # 2. Scale calibration ticks on upper and lower frame rails (25%, 50%, 75%)
    for q in range(1, 4):
        tx = Z3_TRACK_X + (Z3_TRACK_W * q) // 4
        f.pixel(tx, Z3_FRAME_Y - 1, True)
        f.pixel(tx, Z3_FRAME_Y + Z3_FRAME_H, True)

    # 3. Inactive (PWM OFF) State: technical dot-dash resting track
    if not on:
        for dx in range(6, 121, 6):
            f.fill_rect(Z3_TRACK_X + dx - 4, Z3_TRACK_Y + 3, 3, 2, True)
        return

    # 4. Active (PWM ON) State: energized proportional fill
    fill_w = max(1, min(Z3_TRACK_W, (duty_pct * Z3_TRACK_W) // 100))
    f.fill_rect(Z3_TRACK_X, Z3_TRACK_Y, fill_w, Z3_TRACK_H, True)

    # Scale division notches cut through the active fill
    for q in range(1, 4):
        tx = Z3_TRACK_X + (Z3_TRACK_W * q) // 4
        if tx < Z3_TRACK_X + fill_w - 1:
            f.fill_rect(tx, Z3_TRACK_Y, 1, Z3_TRACK_H, False)

    # Crisp demarcation on leading edge
    if fill_w >= 4:
        f.fill_rect(Z3_TRACK_X + fill_w - 2, Z3_TRACK_Y, 2, Z3_TRACK_H, False)

    # Dynamic kinetic wavefront sweep (motion along energized vector)
    if fill_w > 12:
        span = fill_w - (PULSE_WIDTH + 2)
        pos = ping_pong(phase, span)
        f.fill_rect(Z3_TRACK_X + pos, Z3_TRACK_Y + 1, PULSE_WIDTH, Z3_TRACK_H - 2, False)

    # Visual limit indicator: warning hazard striae at the end-cap headroom
    if limited:
        for hx in range(116, 123, 2):
            f.fill_rect(hx, Z3_TRACK_Y, 1, Z3_TRACK_H, True)


def render(freq=60, duty=25, on=False, limited=False, phase=0):
    f = Frame()

    # Zone 1: Frequency Pod
    draw_datum_brackets(f, 2, 2, 59, 45)
    f.text(Z1_INNER_X, Z1_HEADER_Y, "FREQ", 1)
    f.text(Z1_INNER_X + Z1_INNER_W - 11, Z1_HEADER_Y, "HZ", 1)
    f.fill_rect(Z1_INNER_X, Z1_RULE_Y, Z1_INNER_W, 1, True)

    s_freq = str(freq)
    freq_w = digits_width(s_freq, "hero")
    freq_x = Z1_INNER_X + (Z1_INNER_W - freq_w) // 2
    f.digits(freq_x, Z1_NUMERAL_Y, s_freq, "hero")

    # Zone 2: Duty Cycle Pod
    draw_datum_brackets(f, 68, 2, 125, 45)
    f.text(Z2_INNER_X, Z2_HEADER_Y, "DUTY", 1)
    f.text(Z2_INNER_X + Z2_INNER_W - 5, Z2_HEADER_Y, "%", 1)
    f.fill_rect(Z2_INNER_X, Z2_RULE_Y, Z2_INNER_W, 1, True)

    s_duty = str(duty)
    duty_w = digits_width(s_duty, "mid")
    duty_x = Z2_INNER_X + (Z2_INNER_W - duty_w) // 2
    f.digits(duty_x, Z2_NUMERAL_Y, s_duty, "mid")

    # Inter-zone Divider
    for dy in range(4, 45, 2):
        f.pixel(63, dy, True)

    # Zone 3: Dynamic Visual Progress Bar
    draw_dynamic_progress_bar(f, duty, on, limited, phase)

    return f


def to_image(f, zoom=6):
    img = Image.new("RGB", (W, H), (10, 14, 20))  # OLED dark background
    for y in range(H):
        for x in range(W):
            if f.px[y][x]:
                img.putpixel((x, y), (135, 225, 255))  # OLED crisp emissive cyan/white
    return img.resize((W * zoom, H * zoom), Image.NEAREST)


def main(argv):
    freq = int(argv[1]) if len(argv) > 1 else 60
    duty = int(argv[2]) if len(argv) > 2 else 50
    on = (argv[3].lower() != "off") if len(argv) > 3 else False
    limited = (argv[4].lower() == "lim") if len(argv) > 4 else False
    phase = int(argv[5]) if len(argv) > 5 else 0

    f = render(freq, duty, on, limited, phase)
    suffix = f"{freq}hz_{duty}pct_{'on' if on else 'off'}"
    if limited:
        suffix += "_lim"
    out = ROOT / "build" / f"dashboard_{suffix}.png"
    out.parent.mkdir(exist_ok=True)
    to_image(f).save(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main(sys.argv)
