# Changelog

## 2026-06-05

- Added a second Ready-state Usagi touch action, `waiting2`, reusing the `failed`
  animation frames as a separate named clip.
- Added the `wula` PCM sound effect and bound it to the new `waiting2` touch
  action.
- Trimmed the final 0.2 seconds from the `wula` PCM clip to remove trailing
  noise. The final playback length is about 0.82 seconds.
- Updated Ready-state touch behavior so tapping Usagi randomly plays either
  `waiting` with the existing `ha` PCM sound or `waiting2` with the new `wula`
  PCM sound.
- Added AMOLED display recovery safeguards for long idle sessions:
  - image pushes now use even-aligned X coordinates and widths for the StopWatch
    AMOLED panel constraints;
  - byte-swap state is reset after image pushes;
  - the display panel is periodically reasserted and redrawn while dimmed and
    idle to recover from rare blue-tint or shifted-image glitches.
- Added Ready-state Usagi swipe gestures:
  - hold and swipe left to loop `running-left` until release;
  - hold and swipe right to loop `running-right` until release.
- Verified the firmware with `platformio run` and on-device USB flashing.
