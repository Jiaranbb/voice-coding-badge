#include "midi_player.h"

#include <Arduino.h>
#include <M5Unified.h>

namespace MidiPlayer {
namespace {

constexpr uint8_t kSpeakerChannel = 0;
constexpr uint8_t kPcmMasterVolume = 128;
constexpr uint8_t kPcmChannelVolume = 192;

const Note* activeNotes = nullptr;
size_t activeNoteCount = 0;
size_t activeNoteIndex = 0;
uint32_t nextNoteMs = 0;
bool active = false;
bool speakerReady = false;

uint8_t velocityToVolume(uint8_t velocity) {
  const uint16_t scaled = 176 + (static_cast<uint16_t>(velocity) * 79) / 127;
  return scaled > 255 ? 255 : static_cast<uint8_t>(scaled);
}

}  // namespace

void begin(uint8_t volume) {
  auto cfg = M5.Speaker.config();
  if (cfg.magnification < 8) {
    cfg.magnification = 8;
    M5.Speaker.config(cfg);
  }
  Serial.printf("[MIDI] speaker cfg pin_data_out=%d enabled=%d running=%d\n",
                static_cast<int>(cfg.pin_data_out),
                M5.Speaker.isEnabled() ? 1 : 0,
                M5.Speaker.isRunning() ? 1 : 0);
  speakerReady = M5.Speaker.begin();
  if (speakerReady) {
    M5.Speaker.setVolume(volume);
    M5.Speaker.setAllChannelVolume(255);
  }
  Serial.printf("[MIDI] speaker begin %s volume=%u\n",
                speakerReady ? "ok" : "failed",
                static_cast<unsigned>(volume));
}

void play(const Note* notes, size_t noteCount) {
  if (!speakerReady) {
    begin();
  }
  if (!speakerReady || notes == nullptr || noteCount == 0) {
    Serial.printf("[MIDI] play skipped speakerReady=%d notes=%p count=%u\n",
                  speakerReady ? 1 : 0,
                  static_cast<const void*>(notes),
                  static_cast<unsigned>(noteCount));
    return;
  }

  activeNotes = notes;
  activeNoteCount = noteCount;
  activeNoteIndex = 0;
  nextNoteMs = 0;
  active = true;
  M5.Speaker.stop(kSpeakerChannel);
  Serial.printf("[MIDI] play %u notes\n", static_cast<unsigned>(noteCount));
  update();
}

void playSample(const int16_t* samples, size_t sampleCount, uint32_t sampleRate) {
  if (!speakerReady) {
    begin();
  }
  if (!speakerReady || samples == nullptr || sampleCount == 0 || sampleRate == 0) {
    Serial.printf("[PCM] play skipped speakerReady=%d samples=%p count=%u rate=%u\n",
                  speakerReady ? 1 : 0,
                  static_cast<const void*>(samples),
                  static_cast<unsigned>(sampleCount),
                  static_cast<unsigned>(sampleRate));
    return;
  }

  active = false;
  activeNotes = nullptr;
  activeNoteCount = 0;
  activeNoteIndex = 0;
  nextNoteMs = 0;
  M5.Speaker.stop(kSpeakerChannel);
  M5.Speaker.setVolume(kPcmMasterVolume);
  M5.Speaker.setAllChannelVolume(kPcmChannelVolume);
  const bool started =
      M5.Speaker.playRaw(samples, sampleCount, sampleRate, false, 1, kSpeakerChannel, true);
  Serial.printf("[PCM] play samples=%u rate=%u volume=%u/%u started=%d\n",
                static_cast<unsigned>(sampleCount),
                static_cast<unsigned>(sampleRate),
                static_cast<unsigned>(kPcmMasterVolume),
                static_cast<unsigned>(kPcmChannelVolume),
                started ? 1 : 0);
}

void stop() {
  active = false;
  activeNotes = nullptr;
  activeNoteCount = 0;
  activeNoteIndex = 0;
  nextNoteMs = 0;
  if (speakerReady) {
    M5.Speaker.stop(kSpeakerChannel);
  }
}

void update() {
  if (!active || !speakerReady) return;

  const uint32_t now = millis();
  if (nextNoteMs != 0 && static_cast<int32_t>(now - nextNoteMs) < 0) return;

  if (activeNoteIndex >= activeNoteCount) {
    stop();
    return;
  }

  const Note& note = activeNotes[activeNoteIndex++];
  if (note.frequencyHz > 0 && note.durationMs > 0) {
    M5.Speaker.setVolume(velocityToVolume(note.velocity));
    const bool started = M5.Speaker.tone(
        static_cast<float>(note.frequencyHz), note.durationMs, kSpeakerChannel, true);
    Serial.printf("[MIDI] note freq=%u dur=%u vel=%u tone=%d\n",
                  static_cast<unsigned>(note.frequencyHz),
                  static_cast<unsigned>(note.durationMs),
                  static_cast<unsigned>(note.velocity),
                  started ? 1 : 0);
  }
  nextNoteMs = now + note.durationMs + note.gapMs;
}

bool isPlaying() {
  return active;
}

}  // namespace MidiPlayer
