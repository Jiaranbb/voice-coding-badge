#include <Arduino.h>
#include <BleKeyboard.h>
#include <M5Unified.h>
#include "usagi_animations.h"

class DebugBleKeyboard : public BleKeyboard {
 public:
  DebugBleKeyboard(std::string deviceName, std::string deviceManufacturer, uint8_t batteryLevel)
      : BleKeyboard(deviceName, deviceManufacturer, batteryLevel) {}

 protected:
  void onConnect(BLEServer* pServer) override {
    Serial.println("[BLE] connected");
    BleKeyboard::onConnect(pServer);
  }

  void onDisconnect(BLEServer* pServer) override {
    Serial.println("[BLE] disconnected");
    BleKeyboard::onDisconnect(pServer);
  }
};

// Short name avoids macOS truncation and stale half-paired device confusion.
DebugBleKeyboard bleKeyboard("VB-Ctrl", "Jiaran", 100);

enum class UiState {
  Booting,
  WaitingForBluetooth,
  Ready,
  VoiceKeyHeld,
  Sent,
  Cancelled,
  TestMode
};

enum class StatusIndicator {
  None,
  Label,
  VoiceMeter,
  Battery
};

static UiState currentState = UiState::Booting;
static bool voiceKeyDown = false;
static bool voiceLatchMode = false;
static bool testMode = false;
static uint32_t lastActionMs = 0;
static uint32_t lastVoiceReleaseMs = 0;
static uint32_t lastUserActivityMs = 0;
static bool displayDimmed = false;
static bool lastBleConnected = false;
static bool pendingSendAfterVoice = false;
static uint32_t pendingSendMs = 0;
static bool btnAHoldVoiceStarted = false;
static bool btnALatchStartedOnThisPress = false;
static bool btnAPendingTap = false;
static uint32_t btnAPressMs = 0;
static uint32_t btnAPendingTapMs = 0;
static const UsagiAnimationClip* activeClip = nullptr;
static const UsagiStatusLabel* activeLabel = nullptr;
static const UsagiStatusAnimation* activeStatusAnimation = nullptr;
static StatusIndicator activeIndicator = StatusIndicator::None;
static uint8_t activeFrame = 0;
static uint8_t activeStatusFrame = 0;
static uint32_t lastFrameMs = 0;
static uint32_t lastStatusFrameMs = 0;
static uint32_t labelRevealStartMs = 0;
static uint32_t lastBatteryReadMs = 0;
static int32_t currentBatteryLevel = -1;
static int8_t currentChargingState = -1;
static int32_t lastDrawnBatteryLevel = -100;
static int8_t lastDrawnChargingState = -100;
static uint16_t activeLabelRevealMs = 0;
static uint16_t lastLabelVisibleWidth = 0;
static bool activeLabelLoops = false;

constexpr uint32_t kVoiceCommitDelayMs = 1100;
constexpr uint32_t kVoiceSendDelayMs = kVoiceCommitDelayMs;
constexpr uint32_t kUndoWindowMs = 8000;
constexpr uint32_t kDimAfterIdleMs = 30000;
constexpr uint32_t kBatteryRefreshMs = 5000;
constexpr uint32_t kVoiceHoldStartMs = 180;
constexpr uint32_t kVoiceDoubleClickMs = 420;
constexpr uint8_t kActiveBrightness = 120;
constexpr uint8_t kDimBrightness = 45;
constexpr uint8_t kLowBatteryLevel = 20;

constexpr uint32_t kBg = 0x000000;
constexpr uint32_t kReady = 0x1ED760;
constexpr uint32_t kVoice = 0x00A3FF;
constexpr uint32_t kSent = 0xFFD23F;
constexpr uint32_t kCancel = 0xFF4B4B;
constexpr uint32_t kMuted = 0x666666;
constexpr uint16_t kBatteryGreen = 0x07E0;
constexpr uint16_t kBatteryRed = 0xF800;
constexpr uint16_t kBatteryYellow = 0xFFE0;
constexpr int32_t kLabelY = 30;
constexpr int32_t kVoiceMeterY = 32;
constexpr int32_t kPetY = 98;
constexpr int32_t kStatusAreaH = 92;
constexpr uint16_t kReadyLabelRevealMs = 1800;

void buzz(uint8_t level, uint16_t ms) {
  M5.Power.setVibration(level);
  delay(ms);
  M5.Power.setVibration(0);
}

void buzzPattern(uint8_t count) {
  for (uint8_t i = 0; i < count; ++i) {
    buzz(120, 45);
    delay(65);
  }
}

void drawStage() {
  M5.Display.fillScreen(kBg);
  lastDrawnBatteryLevel = -100;
  lastDrawnChargingState = -100;
}

void clearStatusArea() {
  M5.Display.fillRect(0, 0, M5.Display.width(), kStatusAreaH, kBg);
  lastLabelVisibleWidth = 0;
  lastDrawnBatteryLevel = -100;
  lastDrawnChargingState = -100;
}

void updateBatterySnapshot(bool force = false) {
  const uint32_t now = millis();
  if (!force && now - lastBatteryReadMs < kBatteryRefreshMs) return;

  lastBatteryReadMs = now;
  currentBatteryLevel = M5.Power.getBatteryLevel();
  const auto charging = M5.Power.isCharging();
  currentChargingState = charging == m5::Power_Class::is_charging_t::is_charging
                             ? 1
                             : charging == m5::Power_Class::is_charging_t::is_discharging ? 0 : -1;
}

bool shouldShowBatteryStatus() {
  return currentChargingState == 1 ||
         (currentBatteryLevel >= 0 && currentBatteryLevel <= kLowBatteryLevel);
}

void drawBatteryIndicator(bool force = false) {
  if (activeIndicator != StatusIndicator::Battery) return;

  updateBatterySnapshot(force);
  if (!force && currentBatteryLevel == lastDrawnBatteryLevel &&
      currentChargingState == lastDrawnChargingState) {
    return;
  }

  lastDrawnBatteryLevel = currentBatteryLevel;
  lastDrawnChargingState = currentChargingState;

  const int32_t screenW = M5.Display.width();
  const int32_t totalW = 92;
  const int32_t x = (screenW - totalW) / 2;
  const int32_t y = kLabelY + 6;
  const int32_t iconX = x;
  const int32_t iconY = y + 1;
  const int32_t iconW = 34;
  const int32_t iconH = 18;
  const int32_t fillMax = iconW - 6;
  const int32_t pctX = iconX + iconW + 9;

  M5.Display.fillRect(x - 4, y - 4, totalW + 8, 28, kBg);
  M5.Display.drawRoundRect(iconX, iconY, iconW, iconH, 3, 0xFFFF);
  M5.Display.fillRect(iconX + iconW, iconY + 6, 3, 7, 0xFFFF);

  if (currentBatteryLevel >= 0) {
    const uint16_t color = currentChargingState == 1 ? kBatteryGreen : kBatteryRed;
    const int32_t fillW = (fillMax * currentBatteryLevel) / 100;
    if (fillW > 0) {
      M5.Display.fillRect(iconX + 3, iconY + 3, fillW, iconH - 6, color);
    }
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(0xFFFF, kBg);
    M5.Display.setCursor(pctX, y + 2);
    M5.Display.printf("%2d%%", static_cast<int>(currentBatteryLevel));
  } else {
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(kMuted, kBg);
    M5.Display.setCursor(pctX, y + 2);
    M5.Display.print("--");
  }

  if (currentChargingState == 1) {
    M5.Display.drawLine(iconX + 17, iconY + 3, iconX + 12, iconY + 10, kBatteryYellow);
    M5.Display.drawLine(iconX + 12, iconY + 10, iconX + 18, iconY + 10, kBatteryYellow);
    M5.Display.drawLine(iconX + 18, iconY + 10, iconX + 13, iconY + 16, kBatteryYellow);
  }
}

void setDisplayDimmed(bool dimmed) {
  if (displayDimmed == dimmed) return;
  M5.Display.setBrightness(dimmed ? kDimBrightness : kActiveBrightness);
  displayDimmed = dimmed;
}

void noteUserActivity() {
  lastUserActivityMs = millis();
  setDisplayDimmed(false);
}

void updatePowerSaving() {
  if (voiceKeyDown) return;

  const uint32_t now = millis();
  if (!displayDimmed && now - lastUserActivityMs >= kDimAfterIdleMs) {
    setDisplayDimmed(true);
  }
}

uint16_t getLabelVisibleWidth() {
  if (activeLabel == nullptr || activeLabel->pixels == nullptr) return 0;
  if (activeLabelRevealMs == 0) return activeLabel->width;

  uint32_t elapsed = millis() - labelRevealStartMs;
  if (activeLabelLoops) {
    elapsed %= activeLabelRevealMs;
  }
  if (elapsed >= activeLabelRevealMs) return activeLabel->width;

  return (activeLabel->width * elapsed) / activeLabelRevealMs;
}

void drawStatusLabel(bool force = false) {
  if (activeIndicator != StatusIndicator::Label || activeLabel == nullptr ||
      activeLabel->pixels == nullptr) {
    return;
  }

  const uint16_t visibleWidth = getLabelVisibleWidth();
  if (!force && visibleWidth == lastLabelVisibleWidth) return;
  lastLabelVisibleWidth = visibleWidth;

  const int32_t x = (M5.Display.width() - activeLabel->width) / 2;
  M5.Display.fillRect(x, kLabelY, activeLabel->width, activeLabel->height, kBg);
  if (visibleWidth == 0) return;

  const bool previousSwap = M5.Display.getSwapBytes();
  M5.Display.setSwapBytes(true);
  for (uint16_t row = 0; row < activeLabel->height; ++row) {
    const uint16_t* rowPixels = activeLabel->pixels + (row * activeLabel->width);
    M5.Display.pushImage(x, kLabelY + row, visibleWidth, 1, rowPixels);
  }
  M5.Display.setSwapBytes(previousSwap);
}

void drawVoiceMeter(bool force = false) {
  if (activeIndicator != StatusIndicator::VoiceMeter || activeStatusAnimation == nullptr ||
      activeStatusAnimation->frameCount == 0) {
    return;
  }

  const uint32_t now = millis();
  if (!force && now - lastStatusFrameMs < activeStatusAnimation->frameMs) return;

  const int32_t x = (M5.Display.width() - activeStatusAnimation->width) / 2;
  const uint16_t* frame = activeStatusAnimation->frames[activeStatusFrame];
  const bool previousSwap = M5.Display.getSwapBytes();
  M5.Display.setSwapBytes(true);
  M5.Display.pushImage(
      x, kVoiceMeterY, activeStatusAnimation->width, activeStatusAnimation->height, frame);
  M5.Display.setSwapBytes(previousSwap);

  activeStatusFrame = (activeStatusFrame + 1) % activeStatusAnimation->frameCount;
  lastStatusFrameMs = now;
}

void drawStatusIndicator(bool force = false) {
  if (activeIndicator == StatusIndicator::Label) {
    drawStatusLabel(force);
  } else if (activeIndicator == StatusIndicator::VoiceMeter) {
    drawVoiceMeter(force);
  } else if (activeIndicator == StatusIndicator::Battery) {
    drawBatteryIndicator(force);
  }
}

void drawAnimationFrame(bool force = false) {
  if (activeClip == nullptr || activeClip->frameCount == 0) return;

  const uint32_t now = millis();
  if (!force && now - lastFrameMs < activeClip->frameMs) return;

  const uint16_t* frame = activeClip->frames[activeFrame];
  const int32_t x = (M5.Display.width() - activeClip->width) / 2;
  const bool previousSwap = M5.Display.getSwapBytes();
  M5.Display.setSwapBytes(true);
  M5.Display.pushImage(x, kPetY, activeClip->width, activeClip->height, frame);
  M5.Display.setSwapBytes(previousSwap);

  activeFrame = (activeFrame + 1) % activeClip->frameCount;
  lastFrameMs = now;
}

void showAnimatedState(const UsagiAnimationClip& clip,
                       const UsagiStatusLabel* label = nullptr,
                       uint16_t labelRevealMs = 0,
                       bool labelLoops = false,
                       StatusIndicator indicator = StatusIndicator::Label) {
  activeClip = &clip;
  activeLabel = label;
  activeStatusAnimation = nullptr;
  activeIndicator = indicator == StatusIndicator::Battery
                        ? StatusIndicator::Battery
                        : label == nullptr ? StatusIndicator::None : indicator;
  activeLabelRevealMs = labelRevealMs;
  activeLabelLoops = labelLoops;
  labelRevealStartMs = millis();
  lastLabelVisibleWidth = 0;
  activeFrame = 0;
  activeStatusFrame = 0;
  lastFrameMs = 0;
  lastStatusFrameMs = 0;
  drawStage();
  drawStatusIndicator(true);
  drawAnimationFrame(true);
  drawBatteryIndicator(true);
}

void showReadyState() {
  updateBatterySnapshot(true);
  if (shouldShowBatteryStatus()) {
    showAnimatedState(UsagiAnimations::idle, nullptr, 0, false, StatusIndicator::Battery);
  } else {
    showAnimatedState(
        UsagiAnimations::idle, &UsagiAnimations::labelReady, kReadyLabelRevealMs, true);
  }
}

void refreshReadyStatusIndicator() {
  if (currentState != UiState::Ready || voiceKeyDown) return;

  updateBatterySnapshot();
  const bool wantsBattery = shouldShowBatteryStatus();
  const bool showingBattery = activeIndicator == StatusIndicator::Battery;

  if (wantsBattery == showingBattery) return;

  clearStatusArea();
  if (wantsBattery) {
    activeLabel = nullptr;
    activeStatusAnimation = nullptr;
    activeIndicator = StatusIndicator::Battery;
  } else {
    activeLabel = &UsagiAnimations::labelReady;
    activeStatusAnimation = nullptr;
    activeIndicator = StatusIndicator::Label;
    activeLabelRevealMs = kReadyLabelRevealMs;
    activeLabelLoops = true;
    labelRevealStartMs = millis();
    lastLabelVisibleWidth = 0;
  }
  drawStatusIndicator(true);
}

void showVoiceState() {
  activeClip = &UsagiAnimations::running;
  activeLabel = nullptr;
  activeStatusAnimation = &UsagiAnimations::voiceMeter;
  activeIndicator = StatusIndicator::VoiceMeter;
  activeFrame = 0;
  activeStatusFrame = 0;
  lastFrameMs = 0;
  lastStatusFrameMs = 0;
  drawStage();
  drawStatusIndicator(true);
  drawAnimationFrame(true);
  drawBatteryIndicator(true);
}

void setState(UiState state) {
  if (state == currentState && activeClip != nullptr) return;
  currentState = state;

  switch (state) {
    case UiState::Booting:
      showAnimatedState(UsagiAnimations::idle);
      break;
    case UiState::WaitingForBluetooth:
      showAnimatedState(UsagiAnimations::jumping);
      break;
    case UiState::Ready:
      showReadyState();
      break;
    case UiState::VoiceKeyHeld:
      showVoiceState();
      break;
    case UiState::Sent:
      showAnimatedState(UsagiAnimations::waiting, &UsagiAnimations::labelSent);
      break;
    case UiState::Cancelled:
      showAnimatedState(UsagiAnimations::failed, &UsagiAnimations::labelCancelled);
      break;
    case UiState::TestMode:
      showAnimatedState(UsagiAnimations::idle);
      break;
  }
}

void tapKey(uint8_t key) {
  if (!bleKeyboard.isConnected()) return;
  bleKeyboard.press(key);
  delay(60);
  bleKeyboard.release(key);
}

void undoLastInput() {
  if (!bleKeyboard.isConnected()) return;
  bleKeyboard.press(KEY_LEFT_GUI);
  bleKeyboard.press('z');
  delay(80);
  bleKeyboard.releaseAll();
}

void typeTestText() {
  if (!bleKeyboard.isConnected()) return;
  bleKeyboard.print("voice badge test");
}

void pressVoiceShortcut(uint8_t buzzCount = 1) {
  if (!bleKeyboard.isConnected() || voiceKeyDown) return;

  // WeChat Input voice input is configured on this Mac as Ctrl.
  bleKeyboard.press(KEY_LEFT_CTRL);
  voiceKeyDown = true;
  lastActionMs = millis();
  noteUserActivity();
  setState(UiState::VoiceKeyHeld);
  if (buzzCount > 0) buzzPattern(buzzCount);
}

void releaseVoiceShortcut() {
  if (!voiceKeyDown) return;

  bleKeyboard.release(KEY_LEFT_CTRL);
  voiceKeyDown = false;
  voiceLatchMode = false;
  lastVoiceReleaseMs = millis();
  lastActionMs = millis();
  noteUserActivity();
  setState(UiState::Ready);
}

void scheduleSendAfterVoice() {
  pendingSendAfterVoice = true;
  pendingSendMs = millis() + kVoiceSendDelayMs;
}

void clearPendingSendAfterVoice() {
  pendingSendAfterVoice = false;
  pendingSendMs = 0;
}

void updatePendingSendAfterVoice() {
  if (!pendingSendAfterVoice || voiceKeyDown || !bleKeyboard.isConnected()) {
    return;
  }
  if (static_cast<int32_t>(millis() - pendingSendMs) < 0) return;

  clearPendingSendAfterVoice();
  tapKey(KEY_RETURN);
  setState(UiState::Sent);
  lastActionMs = millis();
  noteUserActivity();
}

void clearYellowButtonGesture() {
  btnAHoldVoiceStarted = false;
  btnALatchStartedOnThisPress = false;
  btnAPendingTap = false;
  btnAPressMs = 0;
  btnAPendingTapMs = 0;
}

void handleYellowButton() {
  const uint32_t now = millis();

  if (M5.BtnA.wasPressed()) {
    noteUserActivity();
    clearPendingSendAfterVoice();

    if (btnAPendingTap && now - btnAPendingTapMs <= kVoiceDoubleClickMs) {
      btnAPendingTap = false;
      btnAHoldVoiceStarted = false;
      btnALatchStartedOnThisPress = true;
      btnAPressMs = 0;
      voiceLatchMode = true;
      pressVoiceShortcut(2);
      return;
    }

    btnAPressMs = now;
    btnAHoldVoiceStarted = false;
    btnALatchStartedOnThisPress = false;
  }

  if (M5.BtnA.isPressed() && !voiceLatchMode && !btnAHoldVoiceStarted &&
      btnAPressMs != 0 && now - btnAPressMs >= kVoiceHoldStartMs) {
    btnAPendingTap = false;
    btnAHoldVoiceStarted = true;
    pressVoiceShortcut();
  }

  if (M5.BtnA.wasReleased()) {
    noteUserActivity();

    if (voiceLatchMode) {
      if (btnALatchStartedOnThisPress) {
        btnALatchStartedOnThisPress = false;
        btnAPressMs = 0;
        return;
      }

      releaseVoiceShortcut();
      clearYellowButtonGesture();
      return;
    }

    if (btnAHoldVoiceStarted) {
      releaseVoiceShortcut();
      clearYellowButtonGesture();
      return;
    }
    btnAPendingTap = true;
    btnAPendingTapMs = now;
  }

  if (btnAPendingTap && now - btnAPendingTapMs > kVoiceDoubleClickMs) {
    btnAPendingTap = false;
  }
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);
  Serial.begin(115200);
  Serial.println("[VoiceBadge] boot");
  M5.Display.setRotation(0);
  M5.Display.setBrightness(kActiveBrightness);
  lastUserActivityMs = millis();

  setState(UiState::Booting);
  delay(300);

  M5.update();
  testMode = M5.BtnB.isPressed();
  M5.BtnB.setHoldThresh(700);

  bleKeyboard.begin();
  Serial.println("[VoiceBadge] BLE keyboard advertising as VB-Ctrl");
  if (testMode) {
    setState(UiState::TestMode);
  } else {
    setState(UiState::WaitingForBluetooth);
  }
}

void loop() {
  M5.update();

  const bool bleConnected = bleKeyboard.isConnected();
  if (bleConnected && !lastBleConnected) {
    noteUserActivity();
  }
  lastBleConnected = bleConnected;

  if (!bleConnected) {
    if (voiceKeyDown) {
      bleKeyboard.releaseAll();
      voiceKeyDown = false;
      voiceLatchMode = false;
    }
    clearPendingSendAfterVoice();
    clearYellowButtonGesture();
    if (!testMode) setState(UiState::WaitingForBluetooth);
    drawStatusIndicator();
    drawAnimationFrame();
    drawBatteryIndicator();
    updatePowerSaving();
    delay(20);
    return;
  }

  if (testMode) {
    setState(UiState::TestMode);
    if (M5.BtnA.wasPressed()) {
      noteUserActivity();
      typeTestText();
      buzzPattern(1);
    }
    if (M5.BtnB.wasPressed()) {
      noteUserActivity();
      tapKey(KEY_RETURN);
    }
    drawStatusIndicator();
    drawAnimationFrame();
    drawBatteryIndicator();
    updatePowerSaving();
    delay(20);
    return;
  }

  handleYellowButton();
  updatePendingSendAfterVoice();

  if (M5.BtnB.wasHold()) {
    noteUserActivity();
    clearPendingSendAfterVoice();
    clearYellowButtonGesture();
    const bool recentVoiceInput =
        lastVoiceReleaseMs != 0 && (millis() - lastVoiceReleaseMs <= kUndoWindowMs);

    if (voiceKeyDown) {
      releaseVoiceShortcut();
      delay(kVoiceCommitDelayMs);
      undoLastInput();
      lastVoiceReleaseMs = 0;
    } else if (recentVoiceInput) {
      undoLastInput();
      lastVoiceReleaseMs = 0;
    } else {
      tapKey(KEY_ESC);
    }
    setState(UiState::Cancelled);
    buzzPattern(3);
    lastActionMs = millis();
  } else if (M5.BtnB.wasClicked()) {
    noteUserActivity();
    clearYellowButtonGesture();
    if (voiceKeyDown) {
      releaseVoiceShortcut();
      scheduleSendAfterVoice();
      drawStatusIndicator();
      drawAnimationFrame();
      drawBatteryIndicator();
      updatePowerSaving();
      delay(20);
      return;
    }
    clearPendingSendAfterVoice();
    tapKey(KEY_RETURN);
    setState(UiState::Sent);
    lastActionMs = millis();
  }

  if (millis() - lastActionMs > 1200 && !voiceKeyDown) {
    setState(UiState::Ready);
  }

  refreshReadyStatusIndicator();
  drawStatusIndicator();
  drawAnimationFrame();
  drawBatteryIndicator();
  updatePowerSaving();
  delay(20);
}
