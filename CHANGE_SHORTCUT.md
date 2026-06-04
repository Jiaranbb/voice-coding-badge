# VoiceBadge 改快捷键和刷固件说明

当前固件把 Badge 做成一个 BLE 蓝牙键盘。

## 改语音触发快捷键

语音触发快捷键在 `src/main.cpp` 里：

```cpp
void pressVoiceShortcut(uint8_t buzzCount = 1) {
  ...
  bleKeyboard.press(KEY_LEFT_CTRL);
  ...
}

void releaseVoiceShortcut() {
  ...
  bleKeyboard.release(KEY_LEFT_CTRL);
  ...
}
```

如果微信输入法「按住说话」设置为单独 `左 Ctrl`，不用改。

如果要改成 `左 Shift + 左 Ctrl`，把上面两处改成：

```cpp
bleKeyboard.press(KEY_LEFT_SHIFT);
bleKeyboard.press(KEY_LEFT_CTRL);
```

以及：

```cpp
bleKeyboard.release(KEY_LEFT_SHIFT);
bleKeyboard.release(KEY_LEFT_CTRL);
```

常用键名在 `BleKeyboard.h` 里，包括：

- `KEY_LEFT_CTRL`
- `KEY_LEFT_SHIFT`
- `KEY_LEFT_ALT`
- `KEY_LEFT_GUI`，也就是 Mac 的 Command
- `KEY_RETURN`
- `KEY_ESC`

## 编译

需要先安装 PlatformIO。进入项目目录后运行：

```bash
platformio run
```

## 刷入 StopWatch

用 USB-C 连接 Badge，然后查看串口：

```bash
platformio device list
```

刷入：

```bash
platformio run -t upload --upload-port /dev/cu.usbmodem14201
```

如果串口不是 `/dev/cu.usbmodem14201`，换成 `platformio device list` 里看到的 USB 串口。

## 使用

- 开机：短按电源键一次
- 关机：快速双击电源键
- 黄键 A：按住说话
- 黄键 A 双击：免按住说话
- 蓝键 B 短按：发送
- 蓝键 B 长按：取消/撤销
