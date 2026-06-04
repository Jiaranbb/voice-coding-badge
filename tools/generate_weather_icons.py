"""Render Apple colour weather emoji into RGB565 C arrays for the watch.

Emoji are composited onto black (the watch background) and stored in the same
byte order as generate_usagi_assets.py (drawn with setSwapBytes(true)).
"""
from PIL import Image, ImageFont, ImageDraw

TARGET = 60            # icon cell size (px)
STRIKE = 96            # Apple Color Emoji bitmap strike to render from
EMOJI_FONT = "/System/Library/Fonts/Apple Color Emoji.ttc"
OUT_H = "src/weather_icons.h"
OUT_CPP = "src/weather_icons.cpp"

# name -> emoji
ICONS = [
    ("sun", "☀️"),       # ☀️ clear day
    ("moon", "\U0001F319"),        # 🌙 clear night
    ("partly", "⛅"),          # ⛅ partly cloudy
    ("cloudy", "☁️"),    # ☁️ cloudy
    ("rain", "\U0001F327️"),  # 🌧️ rain
    ("snow", "❄️"),      # ❄️ snow
    ("storm", "⛈️"),     # ⛈️ thunderstorm
]


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xF8) << 3) | (b >> 3)


def render(font, em):
    img = Image.new("RGBA", (STRIKE + 20, STRIKE + 20), (0, 0, 0, 0))
    ImageDraw.Draw(img).text((10, 10), em, font=font, embedded_color=True)
    img = img.crop(img.getbbox())
    s = TARGET / max(img.width, img.height)
    img = img.resize((round(img.width * s), round(img.height * s)), Image.LANCZOS)
    cell = Image.new("RGB", (TARGET, TARGET), (0, 0, 0))
    cell.paste(img, ((TARGET - img.width) // 2, (TARGET - img.height) // 2), img)
    return cell


def main():
    font = ImageFont.truetype(EMOJI_FONT, STRIKE)
    cpp = ['#include "weather_icons.h"\n\n', "namespace WeatherIcons {\n\n"]
    names = []
    for name, em in ICONS:
        cell = render(font, em)
        px = cell.load()
        names.append(name)
        cpp.append(f"const uint16_t {name}[] PROGMEM = {{\n")
        for y in range(TARGET):
            row = [f"0x{rgb565(*px[x, y]):04X}" for x in range(TARGET)]
            cpp.append("  " + ", ".join(row) + ",\n")
        cpp.append("};\n\n")
    cpp.append("}  // namespace WeatherIcons\n")

    with open(OUT_CPP, "w") as f:
        f.write("".join(cpp))
    with open(OUT_H, "w") as f:
        f.write("#pragma once\n#include <Arduino.h>\n\n")
        f.write("namespace WeatherIcons {\n")
        f.write(f"constexpr int kIconW = {TARGET};\nconstexpr int kIconH = {TARGET};\n")
        for n in names:
            f.write(f"extern const uint16_t {n}[];\n")
        f.write("}  // namespace WeatherIcons\n")
    print(f"wrote {OUT_CPP}/{OUT_H}: {len(names)} icons, ~{len(names)*TARGET*TARGET*2//1024} KB")


if __name__ == "__main__":
    main()
