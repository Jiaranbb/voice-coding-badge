#!/usr/bin/env python3
"""Fetch the local weather (free, no API key) and push it to the StopWatch over BLE.

The watch has no WiFi; this Mac-side helper does the internet part and writes a
short "<temp>|<cond>" string to the device's custom characteristic. Run it on a
timer (see com.voicebadge.weather.plist) every ~15 minutes.

Deps: pip3 install bleak   (CoreBluetooth on macOS)
"""
import asyncio
import json
import re
import sys
import time
import urllib.parse
import urllib.request

from bleak import BleakScanner, BleakClient

DEVICE_NAME = "537VoiceCoding"
CHAR_UUID = "0000fff1-0000-1000-8000-00805f9b34fb"  # 0xFFF1
# Primary source: a macOS Shortcut that returns "<temp>|<conditions>" from the
# system Weather (WeatherKit, same as the Weather app). Most accurate + no key.
SHORTCUT_NAME = "VBWeather"
# Fallback location for wttr.in if the Shortcut isn't set up and the Mac's own
# CoreLocation can't be read (e.g. background launchd run): Binjiang, Hangzhou.
FALLBACK_LOCATION = "30.2084,120.2107"  # Hangzhou Binjiang

# Map wttr.in condition text -> device condition code.
# 0 clear, 1 partly cloudy, 2 cloudy, 3 rain, 4 snow, 5 thunderstorm, 6 fog
COND_RULES = [
    ("thunder", 5), ("storm", 5),
    ("snow", 4), ("sleet", 4), ("blizzard", 4), ("ice", 4),
    ("rain", 3), ("drizzle", 3), ("shower", 3),
    ("fog", 6), ("mist", 6), ("haze", 6),
    ("partly", 1),
    ("cloud", 2), ("overcast", 2),
    ("sunny", 0), ("clear", 0),
]


def map_condition(text: str) -> int:
    t = text.lower()
    for kw, code in COND_RULES:
        if kw in t:
            return code
    return 0


def get_mac_location(timeout_s: float = 6.0):
    """Best-effort Mac location via CoreLocation; returns 'lat,lon' or None."""
    try:
        import time
        from CoreLocation import CLLocationManager, kCLLocationAccuracyKilometer
        from Foundation import NSRunLoop, NSDate
    except Exception:
        return None
    try:
        mgr = CLLocationManager.alloc().init()
        if hasattr(mgr, "requestWhenInUseAuthorization"):
            mgr.requestWhenInUseAuthorization()
        mgr.setDesiredAccuracy_(kCLLocationAccuracyKilometer)
        mgr.startUpdatingLocation()
        start = time.time()
        while time.time() - start < timeout_s:
            NSRunLoop.currentRunLoop().runUntilDate_(NSDate.dateWithTimeIntervalSinceNow_(0.3))
            loc = mgr.location()
            if loc is not None:
                c = loc.coordinate()
                if c.latitude or c.longitude:
                    return f"{c.latitude:.4f},{c.longitude:.4f}"
        return None
    except Exception:
        return None


# WMO weather code -> device condition (0 clear,1 partly,2 cloudy,3 rain,4 snow,5 storm,6 fog)
WMO_TO_COND = {
    0: 0, 1: 0, 2: 1, 3: 2,
    45: 6, 48: 6,
    51: 3, 53: 3, 55: 3, 56: 3, 57: 3,
    61: 3, 63: 3, 65: 3, 66: 3, 67: 3,
    71: 4, 73: 4, 75: 4, 77: 4,
    80: 3, 81: 3, 82: 3, 85: 4, 86: 4,
    95: 5, 96: 5, 99: 5,
}


def fetch_open_meteo(lat, lon):
    url = ("https://api.open-meteo.com/v1/forecast"
           f"?latitude={lat}&longitude={lon}&current=temperature_2m,weather_code")
    for attempt in range(3):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req, timeout=20) as r:
                cur = json.loads(r.read().decode("utf-8", "replace"))["current"]
            temp = str(int(round(float(cur["temperature_2m"]))))
            return temp, WMO_TO_COND.get(int(cur["weather_code"]), 0)
        except Exception as e:
            if attempt == 2:
                print(f"[weather] open-meteo failed: {e}", file=sys.stderr)
            time.sleep(1.5)
    return None


def fetch_wttr(loc):
    url = f"https://wttr.in/{urllib.parse.quote(loc)}?format=j1"
    req = urllib.request.Request(url, headers={"User-Agent": "curl/8"})
    with urllib.request.urlopen(req, timeout=20) as r:
        cur = json.loads(r.read().decode("utf-8", "replace"))["current_condition"][0]
    temp_c = str(int(round(float(cur["temp_C"]))))
    desc = cur.get("weatherDesc", [{}])[0].get("value", "")
    return temp_c, map_condition(desc)


def fetch_weather():
    loc = get_mac_location() or FALLBACK_LOCATION
    source = "mac-location" if loc != FALLBACK_LOCATION else "fallback(Hangzhou Binjiang)"
    lat, lon = (p.strip() for p in loc.split(","))
    r = fetch_open_meteo(lat, lon)
    if r:
        print(f"[weather] location={loc} ({source}) source=open-meteo")
        return r
    r = fetch_wttr(loc)  # last resort
    print(f"[weather] location={loc} ({source}) source=wttr.in")
    return r


async def push(payload: str) -> bool:
    dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=15)
    if not dev:
        print(f"[weather] device '{DEVICE_NAME}' not found (advertising?)", file=sys.stderr)
        return False
    async with BleakClient(dev) as client:
        await client.write_gatt_char(CHAR_UUID, payload.encode(), response=False)
    print(f"[weather] pushed '{payload}' to {DEVICE_NAME}")
    return True


def main():
    temp, cond = fetch_weather()
    payload = f"{temp}|{cond}"
    ok = asyncio.run(push(payload))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
