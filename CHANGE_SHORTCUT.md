# Change Shortcuts and Flash Firmware

The current firmware makes the badge act as a BLE keyboard.

## Change the Voice Shortcut

The voice shortcut is defined in `src/main.cpp`:

```cpp
void pressVoiceShortcut(uint8_t buzzCount = 1) {
  ...
  bleKeyboard.press(KEY_RIGHT_ALT);
  ...
}

void releaseVoiceShortcut() {
  ...
  bleKeyboard.release(KEY_RIGHT_ALT);
  ...
}
```

On macOS, `KEY_RIGHT_ALT` is the right Option key. If your input method uses right Option for push-to-talk, no change is needed.

If you want to use `Left Shift + Left Ctrl`, change the press logic to:

```cpp
bleKeyboard.press(KEY_LEFT_SHIFT);
bleKeyboard.press(KEY_LEFT_CTRL);
```

And change the release logic to:

```cpp
bleKeyboard.release(KEY_LEFT_SHIFT);
bleKeyboard.release(KEY_LEFT_CTRL);
```

Common key constants include:

- `KEY_LEFT_CTRL`
- `KEY_LEFT_SHIFT`
- `KEY_LEFT_ALT`
- `KEY_RIGHT_ALT`
- `KEY_LEFT_GUI`, which is Command on macOS
- `KEY_RETURN`
- `KEY_ESC`

## Build

Install PlatformIO, then run:

```bash
platformio run
```

## Flash the StopWatch

Connect the badge over USB-C, then list serial ports:

```bash
platformio device list
```

Flash:

```bash
platformio run -t upload --upload-port /dev/cu.usbmodem14201
```

If your port is different, replace `/dev/cu.usbmodem14201` with the USB serial port shown by `platformio device list`.

## Use

- Power on: short press the power button once.
- Power off: quick double press the power button.
- Yellow button A: hold to speak.
- Yellow button A double click: hands-free voice mode.
- Blue button B short press: send.
- Blue button B long press: cancel or undo.
- Yellow button A + blue button B: toggle sleep clock.
