# VoiceBadge for M5Stack StopWatch

Firmware for turning an M5Stack StopWatch into a voice-coding BLE keyboard badge for macOS.

The badge does not act as a microphone. It behaves like a Bluetooth keyboard and sends shortcuts to the Mac. Speech recognition is handled by the Mac-side input method, such as WeChat Input.

See `CHANGELOG.md` for update history.

## Current Button Behavior

- BLE device name: `537VoiceCoding`.
- Yellow button A hold: after about 0.18 seconds, hold `Right Option` and release it when the button is released. This is designed for WeChat Input push-to-talk voice input configured as "hold Option to speak".
- Yellow button A double click: enter hands-free voice mode by holding `Right Option`; tap A again to stop dictation. In hands-free mode, a short press on blue button B also stops dictation, waits about 1.1 seconds for the text to appear, then sends `Enter`.
- Blue button B short press: send `Enter`; no vibration.
- Blue button B long press: if voice input is active, stop dictation and send `Cmd + Z` to undo the newly inserted text. If voice input ended recently, long press also sends `Cmd + Z` within an 8-second window. Otherwise it sends `Esc`. Cancel keeps the triple-vibration feedback.
- Hold blue button B while booting: enter test mode. In test mode, yellow button A types `voice badge test` to confirm that the BLE keyboard link is working.
- Yellow button A + blue button B pressed together: manually toggle the sleep clock on or off, with one vibration as confirmation.
- Touch Usagi while Ready: play a random `waiting` or `waiting2` animation with its paired PCM sound, without sending any keyboard input or vibration.

The red power button is connected to the PMIC and is not readable by the firmware. It still handles the StopWatch's hardware power behavior: quick double press to power off, long press/reset behavior as defined by the device.

## Power, Battery, and Sleep

- Auto dim: after 30 seconds without button activity, the screen brightness drops from 120 to 45. Any button press or BLE reconnect restores brightness. Voice input prevents auto dimming.
- Battery status: shown only while charging or when the battery is 20% or lower. It replaces the Ready-state top dots and refreshes about every 5 seconds. Charging shows a yellow lightning icon.
- BLE battery reporting: the firmware reports the real battery level through the BLE Battery Service, so macOS can show the badge battery level in the Bluetooth menu.
- Sleep clock: after 5 minutes without activity, the display enters a low-brightness sleep clock. BLE stays connected and the buttons still work.
- Sleep clock display: animation is hidden; the screen shows Beijing time in `HH:MM` format and an English weekday such as `THU`. The RTC is initialized at flash/build time using the `Asia/Shanghai` timezone and then keeps time on-device.
- Raise to wake: while sleeping, the IMU detects pickup/movement and wakes the display. Any button press also wakes the display.

## Display States

The screen shows a centered Usagi animation and a compact top status indicator.

- Pairing: Usagi `jumping`
- Ready: Usagi `idle`, with slow animated dots at the top. Charging or low battery replaces the dots with centered battery status.
- Voice input: Usagi `running`, with a white microphone and animated bars at the top.
- Sent: Usagi `sent`, with the sent status label.
- Cancelled/undo: Usagi `failed`, with the cancel status label.
- Ready touch action: Usagi `waiting` or `waiting2`, each with its paired PCM sound.

Generated animation assets are committed in `src/usagi_animations.*`. The helper script is `tools/generate_usagi_assets.py`.

## Weather Push

The StopWatch has no Wi-Fi in this firmware. Weather is pushed from the Mac over BLE through a custom characteristic.

Related files:

- `tools/mac_weather_push.py`: fetches weather on the Mac and writes a compact payload to the badge.
- `tools/com.voicebadge.weather.plist`: launchd example for running the weather push periodically.
- `src/weather_icons.*`: weather icons used by the sleep clock.

The helper uses the device name `537VoiceCoding` and writes to characteristic `0xFFF1`.

## Mac Setup

1. Install and enable WeChat Input.
2. Open WeChat Input settings.
3. Set the voice input push-to-talk shortcut to `Option`.
4. Open any text field, such as Notes, Codex, Claude Code, or a terminal prompt.

## Connection Test

1. Flash this firmware to the StopWatch.
2. Open macOS System Settings -> Bluetooth.
3. Find `537VoiceCoding`.
4. Connect it.
5. For the first test, boot while holding blue button B to enter test mode.
6. Open Notes and press yellow button A.
7. If `voice badge test` appears, the BLE keyboard link is working.

## Normal Use

1. Open Codex, Claude Code, a terminal, or any other text input target.
2. Make sure WeChat Input is active.
3. Put the cursor in the text field.
4. Hold yellow button A to speak; release it to finish dictation.
5. You can also double click yellow button A for hands-free voice mode, then tap A again to finish.
6. Short press blue button B to send. In hands-free voice mode, short pressing B stops dictation first, waits about 1.1 seconds, then sends.
7. Long press blue button B to cancel/undo. During voice input, cancel stops dictation and then undoes the newly inserted text. If text has already appeared, long press B can still undo it within about 8 seconds.

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
