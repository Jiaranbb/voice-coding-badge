#include "ha_midi.h"

namespace HaMidi {

// Converted from assets/ha.mid. The cue is intentionally short so it can play
// with the waiting animation without blocking button handling or BLE HID input.
const MidiPlayer::Note kNotes[] = {
    {587, 270, 0, 127},  // D5
    {659, 70, 0, 124},   // E5
    {740, 100, 0, 126},  // F#5
    {831, 60, 0, 124},   // G#5
};

const size_t kNoteCount = sizeof(kNotes) / sizeof(kNotes[0]);

}  // namespace HaMidi
