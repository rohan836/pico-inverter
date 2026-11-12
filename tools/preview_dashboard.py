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


def render(freq=60, duty=25, on=False, limited=False, phase=0):
    f = Frame()
    f.rect(0, 0, 128, 64)
    f.fill_rect(64, 3, 1, 41)
    f.fill_rect(1, 46, 126, 1)
    f.text(6, 5, "FREQ HZ")
    f.text(70, 5, "DUTY %")
    value = str(freq)
    f.digits(4 + (57 - digits_width(value, "hero")) // 2, 17, value, "hero")
    value = str(duty)
    f.digits(68 + (56 - digits_width(value, "mid")) // 2, 20, value, "mid")
    f.text(6, 51, "PWM")
    f.text(32, 51, "ON" if on else "OFF")
    if limited:
        f.text(103, 51, "LIM")
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
