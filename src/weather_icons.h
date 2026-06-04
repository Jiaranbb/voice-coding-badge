#pragma once
#include <Arduino.h>

namespace WeatherIcons {
constexpr int kIconW = 60;
constexpr int kIconH = 60;
extern const uint16_t sun[];
extern const uint16_t moon[];
extern const uint16_t partly[];
extern const uint16_t cloudy[];
extern const uint16_t rain[];
extern const uint16_t snow[];
extern const uint16_t storm[];
}  // namespace WeatherIcons
