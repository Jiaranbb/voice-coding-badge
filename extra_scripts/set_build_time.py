"""Inject the current Beijing (UTC+8) wall-clock time as build defines.

The firmware uses these to seed the RTC on first boot, so the sleep clock shows
Beijing time regardless of the host machine's timezone.
"""
import datetime

Import("env")  # noqa: F821  (provided by PlatformIO)

try:
    from zoneinfo import ZoneInfo
    now = datetime.datetime.now(ZoneInfo("Asia/Shanghai"))
except Exception:
    # Fallback: compute UTC+8 manually if tz database is unavailable.
    now = datetime.datetime.utcnow() + datetime.timedelta(hours=8)

# Python isoweekday(): Mon=1..Sun=7 -> RTC/tm convention Sun=0..Sat=6
wday = now.isoweekday() % 7

env.Append(CPPDEFINES=[
    ("BUILD_BJ_YEAR", now.year),
    ("BUILD_BJ_MON", now.month),
    ("BUILD_BJ_MDAY", now.day),
    ("BUILD_BJ_HOUR", now.hour),
    ("BUILD_BJ_MIN", now.minute),
    ("BUILD_BJ_SEC", now.second),
    ("BUILD_BJ_WDAY", wday),
])

print("[set_build_time] Beijing build time: %s (wday=%d)"
      % (now.strftime("%Y-%m-%d %H:%M:%S"), wday))
