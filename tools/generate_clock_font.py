"""Generate anti-aliased clock text as RGB565 C arrays.

The large digit set is used for the HH/MM clock. The small glyph set uses the
same light system font for date and weekday text, so it stays crisp without
runtime scaling. Byte order matches generate_usagi_assets.py (drawn with
setSwapBytes(true)).
"""
from PIL import Image, ImageFont, ImageDraw

BIG_TARGET_H = 112
SMALL_TARGET_H = 30
BIG_PAD_X = 6
BIG_PAD_Y = 6
SMALL_PAD_X = 3
SMALL_PAD_Y = 3
SMALL_CHARS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ."
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


def load_font(target_h):
    for path, index, label in FONT_CANDIDATES:
        try:
            lo, hi = 20, 400
            chosen = None
            for _ in range(20):
                mid = (lo + hi) // 2
                font = ImageFont.truetype(path, mid, index=index)
                box = font.getbbox("8")
                h = box[3] - box[1]
                if abs(h - target_h) <= 1:
                    chosen = font
                    break
                if h < target_h:
                    lo = mid + 1
                else:
                    hi = mid - 1
                chosen = font
            print(f"[clock_font] using {label} ({path}#{index}) size~{chosen.size} for {target_h}px")
            return chosen
        except Exception as e:
            print(f"[clock_font] skip {label}: {e}")
    raise SystemExit("No usable clock font found")


def render_char(font, ch):
    tmp = Image.new("L", (400, 400), 0)
    d = ImageDraw.Draw(tmp)
    d.text((40, 20), ch, fill=255, font=font)
    box = tmp.getbbox()
    return tmp.crop(box)


def emit_bitmap(cpp, name, cell):
    px = cell.load()
    cpp.append(f"const uint16_t {name}[] PROGMEM = {{\n")
    for y in range(cell.height):
        row = []
        for x in range(cell.width):
            a = px[x, y]
            row.append(f"0x{rgb565(a, a, a):04X}")
        cpp.append("  " + ", ".join(row) + ",\n")
    cpp.append("};\n\n")


def make_cell(glyph, width, height):
    cell = Image.new("L", (width, height), 0)
    ox = (width - glyph.width) // 2
    oy = (height - glyph.height) // 2
    cell.paste(glyph, (ox, oy))
    return cell


def safe_name(ch):
    if ch == ".":
        return "dot"
    return ch


def main():
    big_font = load_font(BIG_TARGET_H)
    small_font = load_font(SMALL_TARGET_H)

    big_glyphs = [render_char(big_font, str(n)) for n in range(10)]
    big_cw = max(g.width for g in big_glyphs)
    big_ch = max(g.height for g in big_glyphs)
    big_w = (big_cw + BIG_PAD_X * 2 + 1) & ~1
    big_h = big_ch + BIG_PAD_Y * 2
    print(f"[clock_font] big cell = {big_w} x {big_h}")

    small_glyphs = {ch: render_char(small_font, ch) for ch in SMALL_CHARS}
    small_h = max(g.height for g in small_glyphs.values()) + SMALL_PAD_Y * 2
    print(f"[clock_font] small height = {small_h}")

    cpp = ['#include "clock_font.h"\n\n', "namespace ClockFont {\n\n"]
    total = 0

    digit_names = []
    for n, glyph in enumerate(big_glyphs):
        name = f"digit{n}"
        digit_names.append(name)
        cell = make_cell(glyph, big_w, big_h)
        emit_bitmap(cpp, name, cell)
        total += cell.width * cell.height * 2

    small_entries = []
    for ch in SMALL_CHARS:
        glyph = small_glyphs[ch]
        width = (glyph.width + SMALL_PAD_X * 2 + 1) & ~1
        name = f"small_{safe_name(ch)}"
        cell = make_cell(glyph, width, small_h)
        emit_bitmap(cpp, name, cell)
        small_entries.append((ch, width, name))
        total += cell.width * cell.height * 2

    cpp.append("const uint16_t* const kDigits[10] = {\n  ")
    cpp.append(", ".join(digit_names))
    cpp.append("\n};\n\n")

    cpp.append("const SmallGlyph kSmallGlyphs[kSmallGlyphCount] = {\n")
    for ch, width, name in small_entries:
        cpp.append(f"  {{'{ch}', {width}, {name}}},\n")
    cpp.append("};\n\n}  // namespace ClockFont\n")

    with open(OUT_CPP, "w") as f:
        f.write("".join(cpp))

    with open(OUT_H, "w") as f:
        f.write("#pragma once\n#include <Arduino.h>\n\n")
        f.write("namespace ClockFont {\n")
        f.write("struct SmallGlyph {\n")
        f.write("  char ch;\n")
        f.write("  uint8_t width;\n")
        f.write("  const uint16_t* pixels;\n")
        f.write("};\n")
        f.write(f"constexpr int kDigitW = {big_w};\n")
        f.write(f"constexpr int kDigitH = {big_h};\n")
        f.write(f"constexpr int kSmallGlyphH = {small_h};\n")
        f.write(f"constexpr int kSmallGlyphCount = {len(small_entries)};\n")
        f.write("extern const uint16_t* const kDigits[10];\n")
        f.write("extern const SmallGlyph kSmallGlyphs[kSmallGlyphCount];\n")
        f.write("}  // namespace ClockFont\n")

    print(f"[clock_font] wrote {OUT_CPP} / {OUT_H}, ~{total // 1024} KB of data")


if __name__ == "__main__":
    main()
