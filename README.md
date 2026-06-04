# Voice Coding Badge

Firmware for turning an M5Stack StopWatch into a BLE keyboard badge for voice coding on macOS.

The badge does not act as a microphone. It sends keyboard shortcuts to the Mac, and the Mac-side input method handles speech recognition.

## Hardware

- M5Stack StopWatch
- USB-C cable for flashing
- macOS with WeChat Input configured for voice input

## Current Behavior

- BLE device name: `VB-Ctrl`
- Yellow button A hold: hold `Left Ctrl` to trigger push-to-talk voice input.
- Yellow button A double click: start hands-free voice mode by holding `Left Ctrl`; tap A again to stop.
- Blue button B short press: send `Enter`.
- Blue button B short press while voice mode is active: stop voice input, wait about 1.1 seconds, then send `Enter`.
- Blue button B long press: cancel/undo with `Cmd + Z` when possible; otherwise send `Esc`.
- Hold blue button B while booting: test mode. In test mode, button A types `voice badge test`.
- Auto dim: lowers screen brightness after 30 seconds without button activity.
- Battery status: shown only while charging or when battery is 20% or lower.

## Display States

- Pairing: Usagi `jumping`
- Ready: Usagi `idle` with animated dots
- Voice input: Usagi `running` with mic meter
- Sent: Usagi `waiting`
- Cancelled: Usagi `failed`

Generated animation assets are committed in `src/usagi_animations.*`. The helper script is `tools/generate_usagi_assets.py`.

## Mac Setup

1. Install and enable WeChat Input.
2. Open WeChat Input settings.
3. Set the voice input push-to-talk shortcut to `Ctrl`.
4. Open any text field, such as Notes, Codex, or a terminal prompt.

## Build

Install PlatformIO, then run:

```bash
platformio run
```

## Flash

Connect the StopWatch over USB-C, then check the serial port:

```bash
platformio device list
```

Flash the firmware:

```bash
platformio run -t upload --upload-port /dev/cu.usbmodem14201
```

If your serial port is different, replace `/dev/cu.usbmodem14201` with the port shown by `platformio device list`.

## Change Shortcuts

See `CHANGE_SHORTCUT.md`.

## Dependencies

This project uses the PlatformIO configuration from the M5Stack StopWatch documentation:

- `platform = espressif32 @ 6.12.0`
- `board = esp32s3box`
- `framework = arduino`
- `M5Unified`
- `M5GFX`
- `M5PM1`
- `M5IOE1`

It also uses `ESP32 BLE Keyboard` and `NimBLE-Arduino`.
