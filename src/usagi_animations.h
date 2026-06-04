#pragma once

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
extern const UsagiAnimationClip jumping;
extern const UsagiAnimationClip waiting;
extern const UsagiAnimationClip failed;

extern const UsagiStatusLabel labelReady;
extern const UsagiStatusLabel labelCancelled;
extern const UsagiStatusLabel labelSent;
extern const UsagiStatusAnimation voiceMeter;
}  // namespace UsagiAnimations
