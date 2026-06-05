#include <Arduino.h>
#include <BleKeyboard.h>
#include <M5Unified.h>
#include <time.h>
#include <string.h>
#include "usagi_animations.h"
#include "clock_font.h"
#include "weather_icons.h"
#include "midi_player.h"
#include "ha_audio.h"
#include "wula_audio.h"
#include <esp_system.h>
#include <NimBLEDevice.h>

// ---- Weather pushed from the Mac over a custom BLE characteristic ----
// The Mac (which has internet) fetches the temperature and writes it here, so
// the watch can show weather without any WiFi of its own.
static char gWeatherTemp[16] = {0};  // integer degrees C as text, e.g. "24" / "-3"
static int gWeatherCond = -1;        // condition code (see WeatherCond), -1 = unknown
static uint32_t gWeatherMs = 0;      // millis() of last write (0 = never received)
static volatile bool gWeatherDirty = false;  // set on receive -> force a clock redraw
static portMUX_TYPE gWeatherMux = portMUX_INITIALIZER_UNLOCKED;

// Condition codes the Mac sends after a '|': 0 clear, 1 partly cloudy, 2 cloudy,
// 3 rain, 4 snow, 5 thunderstorm, 6 fog.
class WeatherCharCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    std::string v = c->getValue();  // "<temp>|<cond>", cond optional
    const size_t bar = v.find('|');
    const std::string t = (bar == std::string::npos) ? v : v.substr(0, bar);
    const int cond = (bar == std::string::npos) ? -1 : atoi(v.substr(bar + 1).c_str());
    portENTER_CRITICAL(&gWeatherMux);
    strncpy(gWeatherTemp, t.c_str(), sizeof(gWeatherTemp) - 1);
    gWeatherTemp[sizeof(gWeatherTemp) - 1] = '\0';
    gWeatherCond = cond;
    gWeatherMs = millis();
    portEXIT_CRITICAL(&gWeatherMux);
    gWeatherDirty = true;
    Serial.printf("[Weather] temp='%s' cond=%d\n", gWeatherTemp, cond);
  }
};
static WeatherCharCallbacks gWeatherCallbacks;

// Time pushed from the Mac (which always has the correct clock). The RTC on this
// board loses time on power-off and can power up with garbage, so the Mac re-syncs
// it over BLE. Parsed on the BLE task but applied from the main loop so all I2C
// (RTC/IMU/PMIC) stays single-threaded.
static int gTimeY, gTimeMo, gTimeD, gTimeH, gTimeMi, gTimeS, gTimeW;
static volatile bool gTimePending = false;

class TimeCharCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    std::string v = c->getValue();  // "YYYY-MM-DD HH:MM:SS W" (W = 0 Sun .. 6 Sat)
    int Y, Mo, D, H, Mi, S, W = 0;
    if (sscanf(v.c_str(), "%d-%d-%d %d:%d:%d %d", &Y, &Mo, &D, &H, &Mi, &S, &W) >= 6 &&
        Y >= 2024 && Mo >= 1 && Mo <= 12 && D >= 1 && D <= 31) {
      portENTER_CRITICAL(&gWeatherMux);
      gTimeY = Y; gTimeMo = Mo; gTimeD = D; gTimeH = H; gTimeMi = Mi; gTimeS = S; gTimeW = W;
      gTimePending = true;
      portEXIT_CRITICAL(&gWeatherMux);
      Serial.printf("[Time] received %04d-%02d-%02d %02d:%02d:%02d\n", Y, Mo, D, H, Mi, S);
    }
  }
};
static TimeCharCallbacks gTimeCallbacks;

// Forward declarations for the idle-sleep / raise-to-wake helpers, which are
// defined after the drawing routines but referenced earlier.
void wakeDisplay();
void enterDisplaySleep();

class DebugBleKeyboard : public BleKeyboard {
 public:
  DebugBleKeyboard(std::string deviceName, std::string deviceManufacturer, uint8_t batteryLevel)
      : BleKeyboard(deviceName, deviceManufacturer, batteryLevel) {}

 protected:
  // Called after the HID services are set up but before advertising starts —
  // the right spot to register our custom weather characteristic.
  void onStarted(BLEServer* pServer) override {
    NimBLEService* svc = pServer->createService(NimBLEUUID((uint16_t)0xFFF0));
    NimBLECharacteristic* ch = svc->createCharacteristic(
        NimBLEUUID((uint16_t)0xFFF1), NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    ch->setCallbacks(&gWeatherCallbacks);
    NimBLECharacteristic* tch = svc->createCharacteristic(
        NimBLEUUID((uint16_t)0xFFF2), NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    tch->setCallbacks(&gTimeCallbacks);
    svc->start();
    Serial.println("[Weather] GATT 0xFFF0 ready (weather 0xFFF1 / time 0xFFF2)");
  }

  void onConnect(BLEServer* pServer) override {
    Serial.println("[BLE] connected");
    BleKeyboard::onConnect(pServer);
    // Keep advertising so a second central (the Mac weather helper) can connect
    // alongside the HID host. NimBLE allows up to 3 simultaneous connections.
    NimBLEDevice::startAdvertising();
  }

  void onDisconnect(BLEServer* pServer) override {
    Serial.println("[BLE] disconnected");
    BleKeyboard::onDisconnect(pServer);
    NimBLEDevice::startAdvertising();
  }
};

// Short name avoids macOS truncation and stale half-paired device confusion.
DebugBleKeyboard bleKeyboard("537VoiceCoding", "Jiaran", 100);

enum class UiState {
  Booting,
  WaitingForBluetooth,
  Ready,
  VoiceKeyHeld,
  Sent,
  Cancelled,
  PetAction,
  PetSwipeLeft,
  PetSwipeRight,
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
static uint32_t voiceStartMs = 0;
static bool testMode = false;
static uint32_t lastActionMs = 0;
static uint32_t lastVoiceReleaseMs = 0;
static uint32_t lastUserActivityMs = 0;
static bool displayDimmed = false;
static bool displayAsleep = false;
static uint32_t lastImuPollMs = 0;
static uint32_t lastPanelRecoveryMs = 0;
static float lastAccelX = 0.0f;
static float lastAccelY = 0.0f;
static float lastAccelZ = 0.0f;
static bool accelBaselineValid = false;
static int lastDrawnClockHm = -1;
static uint32_t sleepEnterMs = 0;
static bool comboToggleHandled = false;
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
static int16_t lastReportedBleBatteryLevel = -1;
static uint16_t activeLabelRevealMs = 0;
static uint16_t lastLabelVisibleWidth = 0;
static bool activeLabelLoops = false;
static bool petTouchArmed = true;
static bool petTouchActive = false;
static bool petGestureConsumed = false;
static int16_t petTouchStartX = 0;
static int16_t petTouchStartY = 0;

constexpr uint32_t kVoiceCommitDelayMs = 1100;
constexpr uint32_t kVoiceSendDelayMs = kVoiceCommitDelayMs;
constexpr uint32_t kUndoWindowMs = 8000;
constexpr uint32_t kDimAfterIdleMs = 30000;
constexpr uint32_t kSleepAfterIdleMs = 300000;   // 5 min no activity -> screen off (sleep)
constexpr uint32_t kImuPollIntervalMs = 120;     // raise-to-wake IMU poll cadence while asleep
constexpr uint32_t kPanelRecoveryIntervalMs = 60000;  // heal AMOLED colour/window drift while idle
constexpr float kWakeMotionThreshold = 0.30f;    // g; sum of |accel delta| that counts as "picked up"
constexpr uint8_t kSleepClockBrightness = 32;    // dim brightness for the always-on sleep clock
constexpr uint32_t kRaiseWakeArmDelayMs = 1500;  // settle time before raise-to-wake arms after sleeping
constexpr uint32_t kWeatherStaleMs = 3UL * 60 * 60 * 1000;  // weather older than 3h -> fall back to weekday
constexpr uint32_t kBatteryRefreshMs = 5000;
constexpr uint32_t kVoiceHoldStartMs = 180;
constexpr uint32_t kVoiceDoubleClickMs = 420;
constexpr uint32_t kMaxVoiceHoldMs = 30000;  // watchdog: auto-release voice/latch so it can never stick
constexpr uint32_t kPetTouchActionMs = 1200;
constexpr int16_t kPetSwipeMinDx = 54;
constexpr int16_t kPetSwipeMaxDy = 96;
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
constexpr int32_t kPetTouchPadding = 16;
constexpr int32_t kStatusAreaH = 92;
constexpr uint16_t kReadyLabelRevealMs = 1800;

int32_t evenCenterX(uint16_t width) {
  return ((M5.Display.width() - width) / 2) & ~1;
}

uint16_t evenWidth(uint16_t width) {
  return width & ~1;
}

void finishImagePush() {
  M5.Display.setSwapBytes(false);
}

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

void playWaitingCue() {
  MidiPlayer::playSample(HaAudio::kSamples, HaAudio::kSampleCount, HaAudio::kSampleRate);
}

void playWaiting2Cue() {
  MidiPlayer::playSample(WulaAudio::kSamples, WulaAudio::kSampleCount, WulaAudio::kSampleRate);
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

  // Report the real battery level over BLE (Battery Service 0x2A19) so the host
  // (e.g. macOS Bluetooth menu) shows the true charge instead of the initial 100%.
  if (currentBatteryLevel >= 0) {
    const uint8_t bleLevel = currentBatteryLevel > 100 ? 100 : currentBatteryLevel;
    if (bleLevel != lastReportedBleBatteryLevel) {
      bleKeyboard.setBatteryLevel(bleLevel);
      lastReportedBleBatteryLevel = bleLevel;
    }
  }
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
  lastPanelRecoveryMs = dimmed ? millis() - kPanelRecoveryIntervalMs : millis();
}

void noteUserActivity() {
  lastUserActivityMs = millis();
  wakeDisplay();
  setDisplayDimmed(false);
}

void updatePowerSaving() {
  if (voiceKeyDown) return;

  const uint32_t now = millis();
  const uint32_t idle = now - lastUserActivityMs;
  if (!displayDimmed && idle >= kDimAfterIdleMs) {
    setDisplayDimmed(true);
  }
  if (!displayAsleep && idle >= kSleepAfterIdleMs) {
    enterDisplaySleep();
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

  const uint16_t visibleWidth = evenWidth(getLabelVisibleWidth());
  if (!force && visibleWidth == lastLabelVisibleWidth) return;
  lastLabelVisibleWidth = visibleWidth;

  const uint16_t clearW = evenWidth(activeLabel->width);
  const int32_t x = evenCenterX(clearW);
  M5.Display.fillRect(x, kLabelY, clearW, activeLabel->height, kBg);
  if (visibleWidth == 0) return;

  M5.Display.setSwapBytes(true);
  for (uint16_t row = 0; row < activeLabel->height; ++row) {
    const uint16_t* rowPixels = activeLabel->pixels + (row * activeLabel->width);
    M5.Display.pushImage(x, kLabelY + row, visibleWidth, 1, rowPixels);
  }
  finishImagePush();
}

void drawVoiceMeter(bool force = false) {
  if (activeIndicator != StatusIndicator::VoiceMeter || activeStatusAnimation == nullptr ||
      activeStatusAnimation->frameCount == 0) {
    return;
  }

  const uint32_t now = millis();
  if (!force && now - lastStatusFrameMs < activeStatusAnimation->frameMs) return;

  const uint16_t width = evenWidth(activeStatusAnimation->width);
  const int32_t x = evenCenterX(width);
  const uint16_t* frame = activeStatusAnimation->frames[activeStatusFrame];
  M5.Display.setSwapBytes(true);
  M5.Display.pushImage(
      x, kVoiceMeterY, width, activeStatusAnimation->height, frame);
  finishImagePush();

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
  const uint16_t width = evenWidth(activeClip->width);
  const int32_t x = evenCenterX(width);
  M5.Display.setSwapBytes(true);
  M5.Display.pushImage(x, kPetY, width, activeClip->height, frame);
  finishImagePush();

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

static const char* kWeekdayNames[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char* kMonthNames[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                      "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// Weather-condition icon (centred at ix,iy) drawn from the colour Apple-emoji
// bitmaps. Code: 0 clear (sun day / moon night), 1 partly, 2 cloudy, 3 rain,
// 4 snow, 5 thunderstorm, 6 fog (reuses cloudy). -1/unknown -> clear.
void drawWeatherIcon(int ix, int iy, int cond, bool day) {
  const uint16_t* bmp;
  switch (cond) {
    case 1: bmp = WeatherIcons::partly; break;
    case 2:
    case 6: bmp = WeatherIcons::cloudy; break;
    case 3: bmp = WeatherIcons::rain; break;
    case 4: bmp = WeatherIcons::snow; break;
    case 5: bmp = WeatherIcons::storm; break;
    default: bmp = day ? WeatherIcons::sun : WeatherIcons::moon; break;
  }
  const int w = evenWidth(WeatherIcons::kIconW);
  const int h = WeatherIcons::kIconH;
  const int x = (ix - w / 2) & ~1;
  M5.Display.setSwapBytes(true);
  M5.Display.pushImage(x, iy - h / 2, w, h, bmp);
  finishImagePush();
}

// Draw "<num>°C" centred at (cx, y) with Font4 and a small raised degree ring.
void drawSleepTemp(const char* num, int cx, int y) {
  M5.Display.setFont(&fonts::Font4);
  M5.Display.setTextSize(1);
  const int wn = M5.Display.textWidth(num);
  const int wc = M5.Display.textWidth("C");
  const int ringR = 4, gap1 = 3, gap2 = 4;
  const int total = wn + gap1 + ringR * 2 + gap2 + wc;
  const int x = cx - total / 2;
  M5.Display.setTextColor(0xFFFFFF, kBg);
  M5.Display.setTextDatum(middle_left);
  M5.Display.drawString(num, x, y);
  const int ringCx = x + wn + gap1 + ringR;
  M5.Display.drawCircle(ringCx, y - 7, ringR, 0xFFFFFF);
  M5.Display.drawString("C", ringCx + ringR + gap2, y);
}

// Sleep "always-on" clock: shows Beijing time HH:MM plus the weekday (English),
// no animation. Redraws only when the minute changes to save power.
void drawSleepClock(bool force = false) {
  if (!displayAsleep) return;

  int hh = -1, mm = -1, mon = -1, day = -1, wd = -1;
  if (M5.Rtc.isEnabled()) {
    const auto dt = M5.Rtc.getDateTime();  // RTC holds Beijing local time
    hh = dt.time.hours;
    mm = dt.time.minutes;
    mon = dt.date.month;
    day = dt.date.date;
    wd = dt.date.weekDay;
  }

  const int key = (hh >= 0) ? (hh * 60 + mm) : -2;
  if (!force && key == lastDrawnClockHm && !gWeatherDirty) return;
  lastDrawnClockHm = key;
  gWeatherDirty = false;

  M5.Display.fillScreen(kBg);

  // Save the exact prior text state and restore it on exit, so the normal UI
  // (which relies on the default font / its own text size) is never disturbed.
  const auto savedFont = M5.Display.getFont();
  const auto savedDatum = M5.Display.getTextDatum();
  const float savedSizeX = M5.Display.getTextSizeX();
  const float savedSizeY = M5.Display.getTextSizeY();

  // Left column: big rounded stacked hours over minutes, drawn from the
  // pre-rendered anti-aliased digit bitmaps (smooth, no upscaling jaggies).
  const int dw = evenWidth(ClockFont::kDigitW);
  const int dh = ClockFont::kDigitH;
  const int lx = 158;            // horizontal centre of the two-digit block
  const int x0 = (lx - dw) & ~1; // left digit
  const int x1 = lx & ~1;        // right digit
  const int yH = 116;            // hours row top
  const int yM = yH + dh + 6;    // minutes row top
  if (hh >= 0) {
    M5.Display.setSwapBytes(true);
    M5.Display.pushImage(x0, yH, dw, dh, ClockFont::kDigits[(hh / 10) % 10]);
    M5.Display.pushImage(x1, yH, dw, dh, ClockFont::kDigits[hh % 10]);
    M5.Display.pushImage(x0, yM, dw, dh, ClockFont::kDigits[(mm / 10) % 10]);
    M5.Display.pushImage(x1, yM, dw, dh, ClockFont::kDigits[mm % 10]);
    finishImagePush();
  }

  // Read the weather snapshot once (pushed from the Mac over BLE).
  char wtemp[16];
  int wcond;
  uint32_t wms;
  portENTER_CRITICAL(&gWeatherMux);
  strncpy(wtemp, gWeatherTemp, sizeof(wtemp));
  wcond = gWeatherCond;
  wms = gWeatherMs;
  portEXIT_CRITICAL(&gWeatherMux);
  wtemp[sizeof(wtemp) - 1] = '\0';
  const bool haveWeather =
      wms != 0 && wtemp[0] != '\0' && (millis() - wms) < kWeatherStaleMs;

  // Right column: weather/day-night icon, date, and temperature (or weekday).
  const int rx = 356;
  drawWeatherIcon(rx, 158, haveWeather ? wcond : -1, hh >= 6 && hh < 18);

  M5.Display.setFont(&fonts::Font4);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(0xFFFFFF, kBg);
  if (mon >= 1) {
    // Date MM·DD with a small raised centre dot.
    char mmb[3], ddb[3];
    std::snprintf(mmb, sizeof(mmb), "%02d", mon);
    std::snprintf(ddb, sizeof(ddb), "%02d", day);
    M5.Display.fillCircle(rx, 250, 2, 0xFFFFFF);
    M5.Display.setTextDatum(middle_right);
    M5.Display.drawString(mmb, rx - 7, 250);
    M5.Display.setTextDatum(middle_left);
    M5.Display.drawString(ddb, rx + 7, 250);
  }

  if (haveWeather) {
    drawSleepTemp(wtemp, rx, 300);
  } else {
    const char* wstr = (wd >= 0 && wd < 7) ? kWeekdayNames[wd] : "";
    M5.Display.setFont(&fonts::Font4);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(0xFFFFFF, kBg);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString(wstr, rx, 300);
  }

  M5.Display.setFont(savedFont);
  M5.Display.setTextSize(savedSizeX, savedSizeY);
  M5.Display.setTextDatum(savedDatum);
}

// Enter the low-power sleep state after a long idle period (or manual power-key
// click). BLE stays connected so the keyboard keeps working; the screen dims to
// a low-brightness always-on clock instead of going fully dark.
void enterDisplaySleep() {
  if (displayAsleep) return;
  MidiPlayer::stop();
  displayAsleep = true;
  sleepEnterMs = millis();
  lastDrawnClockHm = -1;        // force a fresh clock draw
  M5.Display.setBrightness(kSleepClockBrightness);
  drawSleepClock(true);
  accelBaselineValid = false;  // re-establish the motion baseline on next poll
}

// Wake the screen back up and force a full redraw of the current UI. The text
// state is left untouched (drawSleepClock restores it), so the normal UI renders
// exactly as before.
void wakeDisplay() {
  if (!displayAsleep) return;
  displayAsleep = false;
  lastDrawnClockHm = -1;
  // Re-assert the panel registers (incl. RGB/BGR colour order) without a hard
  // reset or clearing. This heals the occasional "everything turns blue" glitch
  // so toggling sleep + waking recovers colour without a reboot.
  M5.Display.init_without_reset();
  M5.Display.setRotation(0);
  M5.Display.setBrightness(kActiveBrightness);
  drawStage();
  drawStatusIndicator(true);
  drawAnimationFrame(true);
  drawBatteryIndicator(true);
  lastPanelRecoveryMs = millis();
}

void recoverDisplayPanelIfIdle() {
  if (displayAsleep || !displayDimmed || currentState != UiState::Ready || voiceKeyDown ||
      pendingSendAfterVoice) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastPanelRecoveryMs < kPanelRecoveryIntervalMs) return;
  lastPanelRecoveryMs = now;

  // The StopWatch AMOLED occasionally drifts into a bad colour/window state
  // after long idle animation. Re-assert the panel state and fully redraw while
  // idle; this mimics the manual power-button recover without interrupting input.
  M5.Display.wakeup();
  M5.Display.init_without_reset();
  M5.Display.setRotation(0);
  M5.Display.setSwapBytes(false);
  M5.Display.setBrightness(kDimBrightness);
  drawStage();
  drawStatusIndicator(true);
  drawAnimationFrame(true);
  drawBatteryIndicator(true);
}

// Raise-to-wake: while asleep, poll the IMU and wake when the device is moved
// (e.g. picked up). Falls back to no-op if the IMU is unavailable; buttons
// still wake the screen via noteUserActivity().
void updateRaiseToWake() {
  if (!displayAsleep || !M5.Imu.isEnabled()) return;

  const uint32_t now = millis();
  // Grace period after entering sleep: let the button press / hand motion settle
  // before arming raise-to-wake, otherwise a manual power-key sleep wakes instantly.
  if (now - sleepEnterMs < kRaiseWakeArmDelayMs) {
    accelBaselineValid = false;
    return;
  }
  if (now - lastImuPollMs < kImuPollIntervalMs) return;
  lastImuPollMs = now;

  float ax = 0.0f, ay = 0.0f, az = 0.0f;
  if (!M5.Imu.getAccel(&ax, &ay, &az)) return;

  if (!accelBaselineValid) {
    lastAccelX = ax;
    lastAccelY = ay;
    lastAccelZ = az;
    accelBaselineValid = true;
    return;
  }

  const float motion =
      fabsf(ax - lastAccelX) + fabsf(ay - lastAccelY) + fabsf(az - lastAccelZ);
  lastAccelX = ax;
  lastAccelY = ay;
  lastAccelZ = az;

  if (motion >= kWakeMotionThreshold) {
    noteUserActivity();  // wakes the display + redraws
  }
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
  MidiPlayer::stop();
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
      showAnimatedState(UsagiAnimations::sent, &UsagiAnimations::labelSent);
      break;
    case UiState::Cancelled:
      showAnimatedState(UsagiAnimations::failed, &UsagiAnimations::labelCancelled);
      break;
    case UiState::PetAction:
      if (esp_random() & 1) {
        showAnimatedState(UsagiAnimations::waiting);
        playWaitingCue();
      } else {
        showAnimatedState(UsagiAnimations::waiting2);
        playWaiting2Cue();
      }
      break;
    case UiState::PetSwipeLeft:
      showAnimatedState(UsagiAnimations::runningLeft);
      break;
    case UiState::PetSwipeRight:
      showAnimatedState(UsagiAnimations::runningRight);
      break;
    case UiState::TestMode:
      showAnimatedState(UsagiAnimations::idle);
      break;
  }
}

bool isPetSwipeState() {
  return currentState == UiState::PetSwipeLeft || currentState == UiState::PetSwipeRight;
}

bool isTouchInsidePet(int16_t x, int16_t y) {
  if (activeClip == nullptr) return false;

  const int32_t petX = evenCenterX(activeClip->width);
  const int32_t left = petX - kPetTouchPadding;
  const int32_t right = petX + activeClip->width + kPetTouchPadding;
  const int32_t top = kPetY - kPetTouchPadding;
  const int32_t bottom = kPetY + activeClip->height + kPetTouchPadding;
  return x >= left && x <= right && y >= top && y <= bottom;
}

void handleReadyPetTouch() {
  if ((currentState != UiState::Ready && !isPetSwipeState()) || voiceKeyDown || pendingSendAfterVoice ||
      displayAsleep || M5.Display.touch() == nullptr) {
    return;
  }

  m5gfx::touch_point_t touchPoint;
  const bool touching = M5.Display.getTouch(&touchPoint, 1) > 0;

  if (isPetSwipeState()) {
    if (touching) {
      noteUserActivity();
      return;
    }

    petTouchArmed = false;
    petTouchActive = false;
    petGestureConsumed = false;
    setState(UiState::Ready);
    return;
  }

  if (!touching) {
    if (petTouchActive && !petGestureConsumed && petTouchArmed) {
      petTouchArmed = false;
      noteUserActivity();
      lastActionMs = millis();
      setState(UiState::PetAction);
    } else {
      petTouchArmed = true;
    }
    petTouchActive = false;
    petGestureConsumed = false;
    return;
  }

  if (!petTouchActive) {
    if (!petTouchArmed || !isTouchInsidePet(touchPoint.x, touchPoint.y)) {
      return;
    }
    petTouchActive = true;
    petGestureConsumed = false;
    petTouchStartX = touchPoint.x;
    petTouchStartY = touchPoint.y;
    noteUserActivity();
    return;
  }

  if (petGestureConsumed) {
    return;
  }

  const int16_t dx = touchPoint.x - petTouchStartX;
  const int16_t dy = touchPoint.y - petTouchStartY;
  if (abs(dx) < kPetSwipeMinDx || abs(dy) > kPetSwipeMaxDy) {
    return;
  }

  petGestureConsumed = true;
  petTouchArmed = false;
  noteUserActivity();
  lastActionMs = millis();
  setState(dx < 0 ? UiState::PetSwipeLeft : UiState::PetSwipeRight);
}

void tapKey(uint8_t key) {
  if (!bleKeyboard.isConnected()) return;
  bleKeyboard.press(key);
  delay(60);
  bleKeyboard.release(key);
}

void undoLastInput() {
  if (!bleKeyboard.isConnected()) return;
  // GUI text fields (Notes, browsers): Cmd+Z undoes the just-dictated text.
  bleKeyboard.press(KEY_LEFT_GUI);
  bleKeyboard.press('z');
  delay(80);
  bleKeyboard.releaseAll();
  delay(40);
  // Terminals / consoles (Claude Code) ignore Cmd+Z, so also clear the input line
  // with Ctrl+U (kill line). It's a no-op in standard macOS text fields, so the
  // two contexts don't interfere.
  bleKeyboard.press(KEY_LEFT_CTRL);
  bleKeyboard.press('u');
  delay(80);
  bleKeyboard.releaseAll();
}

void typeTestText() {
  if (!bleKeyboard.isConnected()) return;
  bleKeyboard.print("voice badge test");
}

void pressVoiceShortcut(uint8_t buzzCount = 1) {
  if (!bleKeyboard.isConnected() || voiceKeyDown) return;

  MidiPlayer::stop();
  // WeChat Input voice input is configured on this Mac as the right Option (Alt) key.
  bleKeyboard.press(KEY_RIGHT_ALT);
  voiceKeyDown = true;
  voiceStartMs = millis();
  lastActionMs = millis();
  noteUserActivity();
  setState(UiState::VoiceKeyHeld);
  if (buzzCount > 0) buzzPattern(buzzCount);
}

void releaseVoiceShortcut() {
  if (!voiceKeyDown) return;

  bleKeyboard.release(KEY_RIGHT_ALT);
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

  // Safety net: if a push-to-talk release event was missed (BLE/button glitch),
  // release as soon as the yellow key is physically back up. (Latch mode is
  // hands-free, so it's excluded — it ends on a separate click.)
  if (voiceKeyDown && !voiceLatchMode && !M5.BtnA.isPressed()) {
    releaseVoiceShortcut();
    clearYellowButtonGesture();
  }
}

#ifndef BUILD_BJ_YEAR
#define BUILD_BJ_YEAR 2026
#define BUILD_BJ_MON 1
#define BUILD_BJ_MDAY 1
#define BUILD_BJ_HOUR 0
#define BUILD_BJ_MIN 0
#define BUILD_BJ_SEC 0
#define BUILD_BJ_WDAY 0
#endif

// Seed the RTC with the build's Beijing (UTC+8) wall-clock time the first time
// the device boots (RTC never set). Afterwards the RX8130 keeps time on its own.
void initClockSource() {
  if (!M5.Rtc.isEnabled()) {
    Serial.println("[VoiceBadge] RTC unavailable; sleep clock will show --:--");
    return;
  }
  const auto d = M5.Rtc.getDate();
  // Re-seed when the RTC is unset OR holds garbage (power-up junk like year 2084,
  // month 0). The Mac re-syncs the real time over BLE shortly after, anyway.
  const bool bad = d.year < 2024 || d.year > 2099 || d.month < 1 || d.month > 12 ||
                   d.date < 1 || d.date > 31;
  if (bad) {
    struct tm bt = {};
    bt.tm_year = BUILD_BJ_YEAR - 1900;
    bt.tm_mon = BUILD_BJ_MON - 1;
    bt.tm_mday = BUILD_BJ_MDAY;
    bt.tm_hour = BUILD_BJ_HOUR;
    bt.tm_min = BUILD_BJ_MIN;
    bt.tm_sec = BUILD_BJ_SEC;
    bt.tm_wday = BUILD_BJ_WDAY;
    M5.Rtc.setDateTime(&bt);
    Serial.printf("[VoiceBadge] RTC invalid; seeded Beijing time %04d-%02d-%02d %02d:%02d:%02d\n",
                  (int)BUILD_BJ_YEAR, (int)BUILD_BJ_MON, (int)BUILD_BJ_MDAY,
                  (int)BUILD_BJ_HOUR, (int)BUILD_BJ_MIN, (int)BUILD_BJ_SEC);
  }
  const auto now = M5.Rtc.getDateTime();
  Serial.printf("[VoiceBadge] RTC now (Beijing) %04d-%02d-%02d %02d:%02d:%02d wday=%d\n",
                now.date.year, now.date.month, now.date.date, now.time.hours,
                now.time.minutes, now.time.seconds, now.date.weekDay);
}

// Apply a time pushed from the Mac (from the main loop, so RTC I2C stays single
// threaded). Keeps the clock correct even after a power-off RTC reset.
void applyPendingTime() {
  if (!gTimePending) return;
  int Y, Mo, D, H, Mi, S, W;
  portENTER_CRITICAL(&gWeatherMux);
  Y = gTimeY; Mo = gTimeMo; D = gTimeD; H = gTimeH; Mi = gTimeMi; S = gTimeS; W = gTimeW;
  gTimePending = false;
  portEXIT_CRITICAL(&gWeatherMux);
  if (!M5.Rtc.isEnabled()) return;
  struct tm t = {};
  t.tm_year = Y - 1900;
  t.tm_mon = Mo - 1;
  t.tm_mday = D;
  t.tm_hour = H;
  t.tm_min = Mi;
  t.tm_sec = S;
  t.tm_wday = (W >= 0 && W < 7) ? W : 0;
  M5.Rtc.setDateTime(&t);
  lastDrawnClockHm = -1;  // force the clock to redraw with the corrected time
  gWeatherDirty = true;
  Serial.printf("[Time] RTC set to %04d-%02d-%02d %02d:%02d:%02d\n", Y, Mo, D, H, Mi, S);
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.internal_spk = true;
  M5.begin(cfg);
  Serial.begin(115200);
  Serial.println("[VoiceBadge] boot");
  M5.Display.setRotation(0);
  M5.Display.setBrightness(kActiveBrightness);
  MidiPlayer::begin();
  lastUserActivityMs = millis();

  setState(UiState::Booting);
  delay(300);

  M5.update();
  testMode = M5.BtnB.isPressed();
  M5.BtnB.setHoldThresh(700);

  bleKeyboard.begin();
  Serial.println("[VoiceBadge] BLE keyboard advertising as 537VoiceCoding");
  Serial.printf("[VoiceBadge] IMU (raise-to-wake) %s\n",
                M5.Imu.isEnabled() ? "enabled" : "NOT available");
  initClockSource();
  if (testMode) {
    setState(UiState::TestMode);
  } else {
    setState(UiState::WaitingForBluetooth);
  }
}

void loop() {
  M5.update();
  MidiPlayer::update();
  applyPendingTime();

  const bool bleConnected = bleKeyboard.isConnected();
  if (bleConnected && !lastBleConnected) {
    noteUserActivity();
  }
  lastBleConnected = bleConnected;

  // Yellow + Blue pressed together toggles the sleep clock. (The red/power key is
  // wired straight to the PMIC and isn't reported to firmware on this board, so
  // we use a two-button combo instead.) A short buzz confirms the toggle.
  const bool bothButtonsDown = M5.BtnA.isPressed() && M5.BtnB.isPressed();
  if (bothButtonsDown && !comboToggleHandled) {
    comboToggleHandled = true;
    if (voiceKeyDown) releaseVoiceShortcut();
    clearYellowButtonGesture();
    clearPendingSendAfterVoice();
    buzz(120, 40);
    if (displayAsleep) {
      noteUserActivity();  // wake
    } else {
      lastUserActivityMs = millis();
      enterDisplaySleep();  // sleep
    }
  } else if (!bothButtonsDown) {
    comboToggleHandled = false;
  }

  // While asleep, only the always-on clock and wake detection run. Returning
  // early keeps the voice/UI drawing (setState, animations) from painting over
  // the clock. A single front-button press or movement wakes; both-button combo
  // is handled above (and must not double-trigger a wake here).
  if (displayAsleep) {
    updateRaiseToWake();
    if (!bothButtonsDown && (M5.BtnA.wasPressed() || M5.BtnB.wasPressed())) {
      noteUserActivity();
    } else {
      drawSleepClock();
    }
    delay(20);
    return;
  }

  if (!bleConnected) {
    if (voiceKeyDown) {
      bleKeyboard.releaseAll();
      voiceKeyDown = false;
      voiceLatchMode = false;
    }
    clearPendingSendAfterVoice();
    clearYellowButtonGesture();
    if (!testMode) setState(UiState::WaitingForBluetooth);
    updatePowerSaving();
    if (displayAsleep) {
      drawSleepClock();
    } else {
      drawStatusIndicator();
      drawAnimationFrame();
      drawBatteryIndicator();
    }
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

  // While the two-button combo is held (e.g. right after it woke the screen),
  // don't treat the held buttons as voice/send — just keep the normal UI up.
  if (bothButtonsDown || comboToggleHandled) {
    updatePowerSaving();
    refreshReadyStatusIndicator();
    drawStatusIndicator();
    drawAnimationFrame();
    drawBatteryIndicator();
    delay(20);
    return;
  }

  handleYellowButton();
  updatePendingSendAfterVoice();
  handleReadyPetTouch();

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

  // Watchdog: never let voice/latch stay engaged forever (e.g. forgotten latch,
  // or a lost release). Auto-release after a max hold so it always returns to idle.
  if (voiceKeyDown && millis() - voiceStartMs > kMaxVoiceHoldMs) {
    releaseVoiceShortcut();
    clearYellowButtonGesture();
  }

  const uint32_t actionReturnMs = currentState == UiState::PetAction ? kPetTouchActionMs : 1200;
  if (!isPetSwipeState() && millis() - lastActionMs > actionReturnMs && !voiceKeyDown) {
    setState(UiState::Ready);
  }

  updatePowerSaving();
  recoverDisplayPanelIfIdle();
  if (displayAsleep) {
    drawSleepClock();
  } else {
    refreshReadyStatusIndicator();
    drawStatusIndicator();
    drawAnimationFrame();
    drawBatteryIndicator();
  }
  delay(20);
}
