#pragma once
#include <Arduino.h>

namespace ClockFont {
struct SmallGlyph {
  char ch;
  uint8_t width;
  const uint16_t* pixels;
};
constexpr int kDigitW = 92;
constexpr int kDigitH = 124;
constexpr int kSmallGlyphH = 38;
constexpr int kSmallGlyphCount = 37;
extern const uint16_t* const kDigits[10];
extern const SmallGlyph kSmallGlyphs[kSmallGlyphCount];
}  // namespace ClockFont
