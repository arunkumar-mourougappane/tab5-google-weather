// Pure JSON-parsing surface for weather.cpp's three fetch* functions
// (see weather.h) - no Arduino/HTTPClient dependency, only ArduinoJson.
// Exists so test/test_weather/ can exercise the exact filters + arena
// size production uses against real captured response bodies. See
// geocode_parse.h for the same pattern applied to geocode.cpp.
#pragma once

#include <ArduinoJson.h>

// Sized against real bytesUsed() numbers logged on hardware (see
// weather.cpp's own fetch* calls) rather than assumed. Bumped from 6144
// to 8192 when test/test_weather/'s daily-forecast fixture (the
// largest of the three endpoints - day+night split, sun/moon events)
// needed >6144 bytes natively (64-bit host - see
// geocode_parse.h's kGeocodeJsonArenaBytes comment for why native's
// number runs higher than the real 32-bit ESP32 target's). Re-check
// against `pio test -e tab5` (on-device) / weather.cpp's own LOG_D
// "bytesUsed" output if this ever needs tightening.
constexpr size_t kWeatherJsonArenaBytes = 12288;

// Marks the subset of a weatherCondition object this project actually
// reads - used to build each endpoint's filter below.
inline void filterWeatherCondition(JsonObject condition) {
  condition["type"] = true;
  condition["description"]["text"] = true;
  condition["iconBaseUri"] = true;
}

inline void buildCurrentConditionsFilter(JsonDocument &filter) {
  filter["currentTime"] = true;
  filter["isDaytime"] = true;
  filterWeatherCondition(filter["weatherCondition"].to<JsonObject>());
  filter["temperature"]["degrees"] = true;
  filter["feelsLikeTemperature"]["degrees"] = true;
  filter["relativeHumidity"] = true;
  filter["uvIndex"] = true;
  filter["precipitation"]["probability"]["percent"] = true;
  filter["wind"]["speed"]["value"] = true;
  filter["wind"]["direction"]["cardinal"] = true;
  filter["wind"]["gust"]["value"] = true;
  filter["cloudCover"] = true;
}

// See fetchCurrentConditions()'s filter comment in weather.cpp - the
// same DOM-parses-everything issue is worse per-record here (~20
// fields/hour in the raw response vs. the handful read below), and 8
// records made it overflow the arena on real hardware before this
// filter was added.
inline void buildHourlyForecastFilter(JsonDocument &filter) {
  JsonObject hour = filter["forecastHours"][0].to<JsonObject>();
  hour["displayDateTime"]["hours"] = true;
  hour["isDaytime"] = true;
  filterWeatherCondition(hour["weatherCondition"].to<JsonObject>());
  hour["temperature"]["degrees"] = true;
  hour["precipitation"]["probability"]["percent"] = true;
}

// See fetchCurrentConditions()'s filter comment in weather.cpp - the
// daily forecast's day+night split and sun/moon events make its
// per-record field count the largest of the three endpoints; filtering
// matters here even more than for hourly.
inline void buildDailyForecastFilter(JsonDocument &filter) {
  filter["nextPageToken"] = true;
  JsonObject day = filter["forecastDays"][0].to<JsonObject>();
  day["displayDate"]["year"] = true;
  day["displayDate"]["month"] = true;
  day["displayDate"]["day"] = true;
  filterWeatherCondition(day["daytimeForecast"]["weatherCondition"].to<JsonObject>());
  filterWeatherCondition(day["nighttimeForecast"]["weatherCondition"].to<JsonObject>());
  day["maxTemperature"]["degrees"] = true;
  day["minTemperature"]["degrees"] = true;
  day["daytimeForecast"]["precipitation"]["probability"]["percent"] = true;
  day["nighttimeForecast"]["precipitation"]["probability"]["percent"] = true;
  day["sunEvents"]["sunriseTime"] = true;
  day["sunEvents"]["sunsetTime"] = true;
  day["moonEvents"]["moonPhase"] = true;
}
