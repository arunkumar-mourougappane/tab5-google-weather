// Pure JSON-parsing surface for resolveTimezone() (see timezone_lookup.h)
// - no Arduino/HTTPClient dependency, only ArduinoJson. Exists so
// test/test_timezone/ can exercise the exact filter + arena size
// production uses against a real captured response body. See
// geocode_parse.h for the same pattern applied to geocode.cpp.
#pragma once

#include <ArduinoJson.h>

// Response is tiny (dstOffset, rawOffset, status, timeZoneId,
// timeZoneName - five scalar fields, no arrays, no nesting), which made
// 768 look generous - wrong. Confirmed on ESP32 hardware: it overflowed
// after only 16 bytes used, meaning a single allocation request for
// >750 bytes failed, not a gradual buildup from the actual field data -
// ArduinoJson v7's variant pool grows in one fixed-size chunk when it
// needs more room, not incrementally per field, so the arena has to
// clear that chunk threshold regardless of how small the parsed data is.
//
// A follow-up bump to 2048 turned out to still be a guess, not a
// measurement: test/test_timezone/ (native, 64-bit host) needed >4096
// bytes for this exact body - ArduinoJson's internal bookkeeping is
// pointer/size_t-sized, so a 64-bit host genuinely needs more arena for
// the same JSON than a 32-bit ESP32 does. 6144 clears the native
// requirement with margin, which makes it a safe (if not necessarily
// minimal) value for the real 32-bit target too - matches
// weather.cpp's kWeatherJsonArenaBytes, already hardware-proven for
// meaningfully larger responses. Re-check against real ESP32 LOG_D
// "bytesUsed" output (timezone_lookup.cpp) if this ever needs
// tightening - native numbers are an upper-bound sanity check, not a
// substitute for that.
constexpr size_t kTimezoneJsonArenaBytes = 6144;

inline void buildTimezoneFilter(JsonDocument &filter) {
  filter["status"] = true;
  filter["errorMessage"] = true;
  filter["rawOffset"] = true;
  filter["dstOffset"] = true;
  filter["timeZoneId"] = true;
}
