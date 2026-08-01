// Pure JSON-parsing surface for geocodeLocation() (see geocode.h) - no
// Arduino/HTTPClient dependency, only ArduinoJson. Exists so
// test/test_geocode/ can exercise the exact filter + arena size + field
// lookup production code uses against a real captured response body,
// without needing network I/O or the Arduino String type at all.
// geocode.cpp includes this too (not a parallel copy), so a test passing
// here is a real guarantee about what ships, not just about this file.
#pragma once

#include <ArduinoJson.h>

#include <cstring>

// Sized against a real ZIP-code query response (5 address_components,
// bounds+viewport+location geometry, postcode_localities, place_id,
// formatted_address — confirmed overflowing an earlier, smaller size on
// real hardware).
//
// 3072 (the original hardware-confirmed bump) turned out to still be
// under test/test_geocode/'s native (64-bit host) requirement of >4096
// bytes for a representative response - ArduinoJson's internal
// bookkeeping is pointer/size_t-sized, so a 64-bit host genuinely needs
// more arena for the same JSON than 32-bit ESP32 does. 6144 clears that
// with margin and matches weather.cpp's kWeatherJsonArenaBytes (already
// hardware-proven for larger responses), making it a safe upper bound
// for the real target even though it's not necessarily the minimum.
// The tests that actually validate the real 32-bit number are
// `pio test -e tab5` (on-device), not `pio test -e native` - see
// platformio.ini's [env:native] comment.
constexpr size_t kGeocodeJsonArenaBytes = 6144;

// Keeps the parsed document sized against what geocodeLocation() actually
// reads (status/error_message/first result's lat+lng+address_components)
// rather than the full response, which includes several fields this
// project never uses.
inline void buildGeocodeFilter(JsonDocument &filter) {
  filter["status"] = true;
  filter["error_message"] = true;
  filter["results"][0]["geometry"]["location"]["lat"] = true;
  filter["results"][0]["geometry"]["location"]["lng"] = true;
  // Index 0 in an ArduinoJson filter applies to every element of the
  // array, not just the first.
  filter["results"][0]["address_components"][0]["long_name"] = true;
  filter["results"][0]["address_components"][0]["short_name"] = true;
  filter["results"][0]["address_components"][0]["types"][0] = true;
}

// address_components is an array of {long_name, short_name, types[]}
// entries with no fixed order or fixed count - finds the one whose
// `types` includes `type` and returns its long_name (city) or short_name
// (state abbreviation). Returns a pointer into `components`'s own
// backing document (valid exactly as long as that document is), or ""
// (not nullptr) if no entry has this type - some rural/ambiguous queries
// omit locality or administrative_area_level_1 entirely, which callers
// treat as "unknown," not an error.
inline const char *findAddressComponent(JsonArrayConst components, const char *type, bool useShortName) {
  for (JsonVariantConst component : components) {
    for (JsonVariantConst t : component["types"].as<JsonArrayConst>()) {
      if (strcmp(t.as<const char *>(), type) == 0) {
        return useShortName ? (component["short_name"] | "") : (component["long_name"] | "");
      }
    }
  }
  return "";
}
