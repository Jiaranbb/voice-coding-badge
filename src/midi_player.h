#pragma once

#include <stddef.h>
#include <stdint.h>

namespace MidiPlayer {

struct Note {
  uint16_t frequencyHz;
  uint16_t durationMs;
  uint16_t gapMs;
  uint8_t velocity;
};

void begin(uint8_t volume = 128);
void play(const Note* notes, size_t noteCount);
void playSample(const int16_t* samples, size_t sampleCount, uint32_t sampleRate);
void stop();
void update();
bool isPlaying();

}  // namespace MidiPlayer
