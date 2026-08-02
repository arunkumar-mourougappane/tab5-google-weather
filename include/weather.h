// Client for weather.googleapis.com — current conditions, hourly forecast,
// and daily forecast, per docs/google-weather-api.md. A separate Maps
// Platform API/key-enablement from the Geocoding API in geocode.h; called
// on a refresh timer against the lat/lon ConfigStore resolved once at
// first boot, not re-geocoded per call.
//
// Fixed-capacity output arrays (kMaxHourlyPoints/kMaxDailyPoints), not
// std::vector — the counts are small and known ahead of time (we only ever
// ask for 16 hours / 7 days), so a caller-owned array plus an out-count
// avoids a heap allocation per refresh for no real benefit.
#pragma once

#include <Arduino.h>

// 16, not 8 - the Dashboard's compact strip only ever shows 8
// (dashboard_ui.cpp's own kMaxDashboardHourCells caps that independently),
// but the Hourly-detail screen (hourly_detail_ui.cpp) needs the fuller
// 16-hour view docs/mockups/hourly-detail.html calls for.
constexpr size_t kMaxHourlyPoints = 16;
constexpr size_t kMaxDailyPoints = 7;

struct WeatherCondition {
  String type;         // e.g. "CLEAR", "RAIN" — see Weather API's WeatherCondition.type enum.
  String description;  // Human-readable, localized by Google (we don't localize it further).
  String iconBaseUri;   // Base URI; caller appends a size/format suffix per Google's docs.
};

struct CurrentConditions {
  String currentTime;
  bool isDaytime = false;
  WeatherCondition condition;
  float temperature = 0.0f;
  float feelsLikeTemperature = 0.0f;
  int relativeHumidity = 0;
  float uvIndex = 0.0f;
  int precipitationProbabilityPercent = 0;
  float windSpeed = 0.0f;
  String windDirectionCardinal;
  float windGust = 0.0f;
  int cloudCoverPercent = 0;
};

struct HourlyForecastPoint {
  String displayDateTime;
  bool isDaytime = true;  // A 16-hour span often crosses sunset/sunrise -
                           // hourly_detail_ui.cpp's per-hour icons need
                           // this to pick Moon/NightCloudy correctly via
                           // weather_icon.h's bucketForCondition().
  WeatherCondition condition;
  float temperature = 0.0f;
  int precipitationProbabilityPercent = 0;
};

struct DailyForecastPoint {
  String displayDate;
  WeatherCondition daytimeCondition;
  float maxTemperature = 0.0f;
  float minTemperature = 0.0f;
  int precipitationProbabilityPercent = 0;
};

// Each fetch* call is a single-shot HTTPS request, same shape as
// geocodeLocation(): returns true and fills the output on success; on
// failure fills outError with a specific, human-readable reason and
// outRetryable with whether a retry is worth it (true for transient
// network/server errors, false for ones a retry can't fix — bad key,
// permission denied, quota exhausted).
bool fetchCurrentConditions(const String &apiKey, float lat, float lon, bool unitsImperial,
                             CurrentConditions &out, String &outError, bool &outRetryable);

// Writes at most kMaxHourlyPoints entries into out, sets outCount to how
// many were actually written.
bool fetchHourlyForecast(const String &apiKey, float lat, float lon, bool unitsImperial,
                          HourlyForecastPoint out[kMaxHourlyPoints], size_t &outCount, String &outError,
                          bool &outRetryable);

// Writes at most kMaxDailyPoints entries into out, sets outCount to how
// many were actually written.
bool fetchDailyForecast(const String &apiKey, float lat, float lon, bool unitsImperial,
                         DailyForecastPoint out[kMaxDailyPoints], size_t &outCount, String &outError,
                         bool &outRetryable);
