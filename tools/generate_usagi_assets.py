#!/usr/bin/env python3
import os
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont


SOURCE_ROOT = Path("/Users/cathy/work/writing/pet-runs/usachi-build/frames")
ASSET_CLIP_ROOT = Path("assets/usagi")
OUT_HEADER = Path("src/usagi_animations.h")
OUT_CPP = Path("src/usagi_animations.cpp")
TARGET_SIZE = (280, 303)
FONT_PATH = Path("/System/Library/Fonts/Supplemental/Tahoma.ttf")
LABEL_HEIGHT = 44
LABEL_PAD_X = 10
LABEL_COLOR = (255, 255, 255, 255)
VOICE_METER_SIZE = (166, 46)
VOICE_METER_SCALE = 4
MIC_ICON_PATH = Path(os.environ.get("MIC_ICON_PATH", "assets/mic.png"))
MIC_ICON_TARGET_HEIGHT = 40
PET_SPRITESHEET_PATH = Path(
    os.environ.get("PET_SPRITESHEET_PATH", "/Users/cathy/.codex/pets/usagi/spritesheet.webp"))
PET_CELL_SIZE = (192, 208)
PET_WAITING_ROW = 6
PET_WAITING_FRAME_COUNT = 6

LABELS = {
    "ready": {"text": "……", "font_size": 34},
    "cancelled": {"text": "?(˘•ω•˘)", "font_size": 30},
    "sent": {"text": "٩(•̤̀ᵕ•̤́๑)ᵒᵏ", "font_size": 26},
}

CLIPS = {
    "idle": {"frame_ms": 180},
    "running": {"frame_ms": 140},
    "runningLeft": {"frame_ms": 150, "asset_dir": "running-left"},
    "runningRight": {"frame_ms": 150, "asset_dir": "running-right"},
    "jumping": {"frame_ms": 140, "copy_from": "running"},
    "waiting": {"frame_ms": 150, "asset_dir": "waiting"},
    "sent": {"frame_ms": 150, "asset_dir": "sent"},
    "failed": {"frame_ms": 150, "asset_dir": "failed"},
    "waiting2": {"frame_ms": 150, "copy_from": "failed"},
}


def rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xF8) << 3) | (b >> 3)


def is_green_fringe_pixel(r: int, g: int, b: int, a: int) -> bool:
    return (
        a > 0
        and g >= 70
        and g >= max(r, b) + 24
        and g >= r * 1.25
        and g >= b * 1.25
    )


def remove_green_fringe(image: Image.Image) -> Image.Image:
    rgba = image.convert("RGBA")
    pixels = []
    for r, g, b, a in rgba.getdata():
        if is_green_fringe_pixel(r, g, b, a):
            pixels.append((0, 0, 0, 0))
        else:
            pixels.append((r, g, b, a))
    rgba.putdata(pixels)
    return rgba


def frame_values(path: Path) -> tuple[int, int, list[int]]:
    image = remove_green_fringe(Image.open(path)).resize(TARGET_SIZE, Image.Resampling.LANCZOS)
    return image_values(image)


def waiting_frame_images_from_pet() -> list[Image.Image]:
    spritesheet = Image.open(PET_SPRITESHEET_PATH).convert("RGBA")
    cell_w, cell_h = PET_CELL_SIZE
    y0 = PET_WAITING_ROW * cell_h
    frames: list[Image.Image] = []
    for index in range(PET_WAITING_FRAME_COUNT):
        x0 = index * cell_w
        frames.append(spritesheet.crop((x0, y0, x0 + cell_w, y0 + cell_h)))
    return frames


def frame_values_from_image(image: Image.Image) -> tuple[int, int, list[int]]:
    image = remove_green_fringe(image).resize(TARGET_SIZE, Image.Resampling.LANCZOS)
    width, height = image.size
    values: list[int] = []

    for r, g, b, a in image.getdata():
        if a < 255:
            r = (r * a) // 255
            g = (g * a) // 255
            b = (b * a) // 255
        values.append(rgb565(r, g, b))

    return width, height, values


def image_values(image: Image.Image) -> tuple[int, int, list[int]]:
    rgba = image.convert("RGBA")
    width, height = rgba.size
    values: list[int] = []

    for r, g, b, a in rgba.getdata():
        if a < 255:
            r = (r * a) // 255
            g = (g * a) // 255
            b = (b * a) // 255
        values.append(rgb565(r, g, b))

    return width, height, values


def label_image(text: str, font_size: int) -> Image.Image:
    font = ImageFont.truetype(str(FONT_PATH), font_size)
    scratch = Image.new("RGBA", (1, 1), (0, 0, 0, 0))
    draw = ImageDraw.Draw(scratch)
    bbox = draw.textbbox((0, 0), text, font=font)
    text_width = bbox[2] - bbox[0]
    text_height = bbox[3] - bbox[1]
    width = text_width + LABEL_PAD_X * 2

    image = Image.new("RGBA", (width, LABEL_HEIGHT), (0, 0, 0, 255))
    draw = ImageDraw.Draw(image)
    x = (width - text_width) / 2 - bbox[0]
    y = (LABEL_HEIGHT - text_height) / 2 - bbox[1]
    draw.text((x, y), text, font=font, fill=LABEL_COLOR)
    return image


def draw_round_bar(draw: ImageDraw.ImageDraw,
                   x: int,
                   center_y: int,
                   height: int,
                   width: int,
                   scale: int) -> None:
    left = (x - width / 2) * scale
    top = (center_y - height / 2) * scale
    right = (x + width / 2) * scale
    bottom = (center_y + height / 2) * scale
    draw.rounded_rectangle((left, top, right, bottom), radius=(width * scale) / 2, fill=LABEL_COLOR)


def mic_icon_image() -> Image.Image:
    source = Image.open(MIC_ICON_PATH).convert("RGBA")
    alpha = source.getchannel("A")
    luminance = source.convert("L")
    opacity = ImageChops.multiply(alpha, luminance)
    bbox = opacity.point(lambda value: 255 if value > 20 else 0).getbbox()
    if bbox is None:
        raise RuntimeError(f"Cannot find white mic pixels in {MIC_ICON_PATH}")

    cropped = source.crop(bbox)
    cropped_opacity = opacity.crop(bbox)
    scale = MIC_ICON_TARGET_HEIGHT / cropped.height
    target_size = (max(1, round(cropped.width * scale)), MIC_ICON_TARGET_HEIGHT)

    icon = Image.new("RGBA", cropped.size, (255, 255, 255, 0))
    icon.putalpha(cropped_opacity)
    return icon.resize(target_size, Image.Resampling.LANCZOS)


def voice_meter_image(step: int) -> Image.Image:
    width, height = VOICE_METER_SIZE
    scale = VOICE_METER_SCALE
    image = Image.new("RGBA", (width * scale, height * scale), (0, 0, 0, 255))
    draw = ImageDraw.Draw(image)

    center_x = width // 2
    center_y = height // 2

    levels = [
        (10, 18, 26, 16),
        (16, 26, 18, 10),
        (24, 16, 10, 26),
        (14, 22, 28, 18),
        (26, 18, 14, 24),
        (18, 10, 24, 28),
    ]
    offsets = (34, 48, 62, 76)

    for index, offset in enumerate(offsets):
        draw_round_bar(draw, center_x - offset, center_y, levels[step][index], 2, scale)
        draw_round_bar(draw, center_x + offset, center_y, levels[(step + 3) % 6][index], 2, scale)

    source_mic = mic_icon_image()
    mic = source_mic.resize(
        (round(source_mic.width * scale), round(source_mic.height * scale)), Image.Resampling.LANCZOS)
    mic_x = center_x * scale - mic.width // 2
    mic_y = center_y * scale - mic.height // 2
    image.alpha_composite(mic, (mic_x, mic_y))

    return image.resize(VOICE_METER_SIZE, Image.Resampling.LANCZOS)


def append_values(chunks: list[str], values: list[int]) -> None:
    for start in range(0, len(values), 16):
        row = values[start : start + 16]
        chunks.append("  " + ", ".join(f"0x{value:04X}" for value in row) + ",\n")


def write_header() -> None:
    OUT_HEADER.write_text(
        """#pragma once

#include <Arduino.h>

struct UsagiAnimationClip {
  const uint16_t* const* frames;
  uint8_t frameCount;
  uint16_t width;
  uint16_t height;
  uint16_t frameMs;
};

struct UsagiStatusLabel {
  const uint16_t* pixels;
  uint16_t width;
  uint16_t height;
};

struct UsagiStatusAnimation {
  const uint16_t* const* frames;
  uint8_t frameCount;
  uint16_t width;
  uint16_t height;
  uint16_t frameMs;
};

namespace UsagiAnimations {
extern const UsagiAnimationClip idle;
extern const UsagiAnimationClip running;
extern const UsagiAnimationClip runningLeft;
extern const UsagiAnimationClip runningRight;
extern const UsagiAnimationClip jumping;
extern const UsagiAnimationClip waiting;
extern const UsagiAnimationClip sent;
extern const UsagiAnimationClip failed;
extern const UsagiAnimationClip waiting2;

extern const UsagiStatusLabel labelReady;
extern const UsagiStatusLabel labelCancelled;
extern const UsagiStatusLabel labelSent;
extern const UsagiStatusAnimation voiceMeter;
}  // namespace UsagiAnimations
""",
        encoding="utf-8",
    )


def write_cpp() -> None:
    chunks: list[str] = [
        '#include "usagi_animations.h"\n\n',
        "#include <pgmspace.h>\n\n",
        "namespace UsagiAnimations {\n\n",
    ]

    generated_clips: dict[str, tuple[list[str], int, int]] = {}

    for clip_name, config in CLIPS.items():
        copy_from = config.get("copy_from")
        if copy_from is not None:
            if copy_from not in generated_clips:
                raise RuntimeError(f"Cannot copy frames from unknown clip {copy_from}")
            source_symbols, width, height = generated_clips[copy_from]
            chunks.append(f"const uint16_t* const {clip_name}_frames[] PROGMEM = {{\n")
            chunks.append("  " + ", ".join(source_symbols) + "\n")
            chunks.append("};\n\n")
            chunks.append(
                f"const UsagiAnimationClip {clip_name} = "
                f"{{{clip_name}_frames, {len(source_symbols)}, {width}, {height}, {config['frame_ms']}}};\n\n"
            )
            generated_clips[clip_name] = (source_symbols, width, height)
            continue

        asset_dir = config.get("asset_dir")
        if asset_dir is not None:
            frame_sources = sorted((ASSET_CLIP_ROOT / asset_dir).glob("*.png"))
        elif clip_name == "waiting" and PET_SPRITESHEET_PATH.exists():
            frame_sources = waiting_frame_images_from_pet()
        else:
            frame_sources = sorted((SOURCE_ROOT / clip_name).glob("*.png"))

        if not frame_sources:
            raise RuntimeError(f"No frames found for {clip_name}")

        width = height = None
        frame_symbols: list[str] = []

        for index, source in enumerate(frame_sources):
            if isinstance(source, Image.Image):
                frame_width, frame_height, values = frame_values_from_image(source)
            else:
                frame_width, frame_height, values = frame_values(source)
            width = width or frame_width
            height = height or frame_height
            if width != frame_width or height != frame_height:
                raise RuntimeError(f"Inconsistent frame size in {clip_name}")

            symbol = f"{clip_name}_{index:02d}"
            frame_symbols.append(symbol)
            chunks.append(f"const uint16_t {symbol}[] PROGMEM = {{\n")
            append_values(chunks, values)
            chunks.append("};\n\n")

        chunks.append(f"const uint16_t* const {clip_name}_frames[] PROGMEM = {{\n")
        chunks.append("  " + ", ".join(frame_symbols) + "\n")
        chunks.append("};\n\n")
        chunks.append(
            f"const UsagiAnimationClip {clip_name} = "
            f"{{{clip_name}_frames, {len(frame_symbols)}, {width}, {height}, {config['frame_ms']}}};\n\n"
        )
        generated_clips[clip_name] = (frame_symbols, width, height)

    for label_name, config in LABELS.items():
        width, height, values = image_values(label_image(config["text"], config["font_size"]))
        symbol = f"label_{label_name}_pixels"
        chunks.append(f"const uint16_t {symbol}[] PROGMEM = {{\n")
        append_values(chunks, values)
        chunks.append("};\n\n")

        export_name = "label" + "".join(part.title() for part in label_name.split("_"))
        chunks.append(f"const UsagiStatusLabel {export_name} = {{{symbol}, {width}, {height}}};\n\n")

    meter_symbols: list[str] = []
    meter_width = meter_height = None
    for index in range(6):
        width, height, values = image_values(voice_meter_image(index))
        meter_width = meter_width or width
        meter_height = meter_height or height
        symbol = f"voice_meter_{index:02d}"
        meter_symbols.append(symbol)
        chunks.append(f"const uint16_t {symbol}[] PROGMEM = {{\n")
        append_values(chunks, values)
        chunks.append("};\n\n")

    chunks.append("const uint16_t* const voice_meter_frames[] PROGMEM = {\n")
    chunks.append("  " + ", ".join(meter_symbols) + "\n")
    chunks.append("};\n\n")
    chunks.append(
        f"const UsagiStatusAnimation voiceMeter = "
        f"{{voice_meter_frames, {len(meter_symbols)}, {meter_width}, {meter_height}, 120}};\n\n"
    )

    chunks.append("}  // namespace UsagiAnimations\n")
    OUT_CPP.write_text("".join(chunks), encoding="utf-8")


def main() -> None:
    write_header()
    write_cpp()
    print(f"Wrote {OUT_HEADER} and {OUT_CPP}")


if __name__ == "__main__":
    main()
