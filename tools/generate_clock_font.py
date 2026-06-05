"""Generate big anti-aliased clock digits (0-9) as RGB565 C arrays.

Renders white digits on black with a light system font at a size built-in lgfx
fonts can't reach smoothly, and emits src/clock_font.{h,cpp}.
Byte order matches generate_usagi_assets.py (drawn with setSwapBytes(true)).
"""
from PIL import Image, ImageFont, ImageDraw

TARGET_H = 112          # desired digit pixel height
PAD_X = 6
PAD_Y = 6
OUT_H = "src/clock_font.h"
OUT_CPP = "src/clock_font.cpp"

FONT_CANDIDATES = [
    ("/System/Library/Fonts/HelveticaNeue.ttc", 12, "Helvetica Neue Thin"),
    ("/System/Library/Fonts/HelveticaNeue.ttc", 7, "Helvetica Neue Light"),
    ("/System/Library/Fonts/Avenir Next.ttc", 10, "Avenir Next Ultra Light"),
    ("/System/Library/Fonts/SFNS.ttf", 0, "SF NS"),
]


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xF8) << 3) | (b >> 3)


def load_font():
    for path, index, label in FONT_CANDIDATES:
        # binary-search a px size so "8" is ~TARGET_H tall
        try:
            lo, hi = 40, 400
            chosen = None
            for _ in range(20):
                mid = (lo + hi) // 2
                f = ImageFont.truetype(path, mid, index=index)
                box = f.getbbox("8")
                h = box[3] - box[1]
                if abs(h - TARGET_H) <= 1:
                    chosen = f
                    break
                if h < TARGET_H:
                    lo = mid + 1
                else:
                    hi = mid - 1
                chosen = f
            print(f"[clock_font] using {label} ({path}#{index}) size~{chosen.size}")
            return chosen
        except Exception as e:
            print(f"[clock_font] skip {label}: {e}")
    raise SystemExit("No usable clock font found")


def render_digit(font, ch):
    # Render onto a generous canvas, then crop to ink bbox.
    tmp = Image.new("L", (400, 400), 0)
    d = ImageDraw.Draw(tmp)
    d.text((40, 20), ch, fill=255, font=font)
    box = tmp.getbbox()
    return tmp.crop(box)  # grayscale alpha, cropped tight


def main():
    font = load_font()
    glyphs = [render_digit(font, str(n)) for n in range(10)]
    cw = max(g.width for g in glyphs)
    ch = max(g.height for g in glyphs)
    cell_w = (cw + PAD_X * 2 + 1) & ~1
    cell_h = ch + PAD_Y * 2
    print(f"[clock_font] cell = {cell_w} x {cell_h}")

    cpp = ['#include "clock_font.h"\n\n', "namespace ClockFont {\n\n"]
    names = []
    total = 0
    for n, g in enumerate(glyphs):
        cell = Image.new("L", (cell_w, cell_h), 0)
        # center horizontally, baseline-align by centering vertically too
        ox = (cell_w - g.width) // 2
        oy = (cell_h - g.height) // 2
        cell.paste(g, (ox, oy))
        px = cell.load()
        name = f"digit{n}"
        names.append(name)
        cpp.append(f"const uint16_t {name}[] PROGMEM = {{\n")
        for y in range(cell_h):
            row = []
            for x in range(cell_w):
                a = px[x, y]
                row.append(f"0x{rgb565(a, a, a):04X}")
            cpp.append("  " + ", ".join(row) + ",\n")
        cpp.append("};\n\n")
        total += cell_w * cell_h * 2

    cpp.append("const uint16_t* const kDigits[10] = {\n  ")
    cpp.append(", ".join(names))
    cpp.append("\n};\n\n}  // namespace ClockFont\n")

    with open(OUT_CPP, "w") as f:
        f.write("".join(cpp))

    with open(OUT_H, "w") as f:
        f.write("#pragma once\n#include <Arduino.h>\n\n")
        f.write("namespace ClockFont {\n")
        f.write(f"constexpr int kDigitW = {cell_w};\n")
        f.write(f"constexpr int kDigitH = {cell_h};\n")
        f.write("extern const uint16_t* const kDigits[10];\n")
        f.write("}  // namespace ClockFont\n")

    print(f"[clock_font] wrote {OUT_CPP} / {OUT_H}, ~{total // 1024} KB of data")


if __name__ == "__main__":
    main()
