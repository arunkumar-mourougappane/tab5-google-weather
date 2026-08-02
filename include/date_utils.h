// Small, pure (Arduino String-only, no LVGL) date-formatting helpers
// shared across screens - same reasoning that moved IconSize/
// iconForBucket() into dashboard_icon_lookup.h: this is real logic
// (Zeller's congruence), not a 3-line per-file UI helper like
// makeLabel/makeRow/makeColumn, which stay duplicated per screen file
// by this project's convention.
#pragma once

#include <Arduino.h>

// Zeller's congruence on a "YYYY-MM-DD" ISO date (see weather.cpp's
// DailyForecastPoint::displayDate) - no date/time library needed.
// Returns 0=Sunday..6=Saturday, or -1 if isoDate isn't well-formed.
inline int weekdayFromIsoDate(const String &isoDate) {
  if (isoDate.length() != 10) {
    return -1;
  }
  int year = isoDate.substring(0, 4).toInt();
  int month = isoDate.substring(5, 7).toInt();
  const int day = isoDate.substring(8, 10).toInt();
  if (month < 3) {
    month += 12;
    year -= 1;
  }
  const int k = year % 100;
  const int j = year / 100;
  const int h = (day + (13 * (month + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
  return (h + 6) % 7;  // Zeller's h is 0=Saturday - rotate to 0=Sunday.
}

// weekday: 0=Sunday..6=Saturday (weekdayFromIsoDate()'s return value).
// Falls back to "?" for an out-of-range/-1 input rather than indexing
// past the array.
inline const char *weekdayAbbrev(int weekday) {
  static const char *const kAbbrev[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  return (weekday >= 0 && weekday < 7) ? kAbbrev[weekday] : "?";
}

// month: 1=January..12=December (displayDate's own "MM", not 0-indexed).
inline const char *monthAbbrev(int month) {
  static const char *const kAbbrev[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  return (month >= 1 && month <= 12) ? kAbbrev[month - 1] : "?";
}
