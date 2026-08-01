// Maps the Weather API's weatherCondition.type strings (39 documented
// values plus TYPE_UNSPECIFIED - see docs/google-weather-api.md's full
// list, confirmed against Google's own reference docs) down to this
// project's icon set (docs/mockups/assets/icons/). Sun/Cloud/Rain/Moon
// mirror the entire icon vocabulary the mockups themselves define;
// everything else (MostlyClear/PartlyCloudy/Windy/LightRain/HeavyRain/
// Snow/Sleet/Hail/Thunderstorm/Fog/NightCloudy) are original designs
// added later for finer accuracy - no mockup reference, see each .svg's
// own header comment for the reasoning behind its look.
//
// Arduino-free (const char*, not the Arduino String type) so this is
// testable under `pio test -e native`, same pattern as
// geocode_parse.h/weather_parse.h/timezone_parse.h. dashboard_ui.cpp
// calls this with cur.condition.type.c_str()/day.daytimeCondition.type.c_str().
#pragma once

#include <cstring>

enum class WeatherIconBucket {
  Sun,
  MostlyClear,
  PartlyCloudy,
  Cloud,
  NightCloudy,
  Windy,
  LightRain,
  Rain,
  HeavyRain,
  Snow,
  Sleet,
  Hail,
  Thunderstorm,
  Fog,
  Moon,
};

// isDaytime only flips CLEAR/MOSTLY_CLEAR/PARTLY_CLOUDY over to the Moon
// icon, and MOSTLY_CLOUDY/CLOUDY over to NightCloudy - matches
// docs/mockups/daily-forecast.html's own sample data, where a rainy
// night still uses the rain icon, not a moon/cloud combo. DailyForecastPoint
// has no isDaytime (only daytimeCondition), so callers for the 7-day
// rows always pass true - those rows never show Moon/NightCloudy, same
// as the mockup's day-row icons.
//
// Fog is never returned here - Google's weatherCondition.type enum has
// no dedicated fog/mist value (confirmed against the full list in
// docs/google-weather-api.md), so there's no real condition string to
// bucket into it. The icon exists (generated, shown in the icon
// gallery) for a future heuristic - e.g. low visibility - not reachable
// from condition type alone today.
inline WeatherIconBucket bucketForCondition(const char *conditionType, bool isDaytime) {
  if (conditionType == nullptr) {
    return WeatherIconBucket::Cloud;
  }

  // "CLEAR" matches both CLEAR and MOSTLY_CLEAR - deliberate, both go to
  // Moon at night (docs/mockups' own design only distinguishes
  // clear-family/partly-cloudy from everything else after dark, not
  // clear-vs-mostly-clear).
  const bool isClearFamily = strstr(conditionType, "CLEAR") != nullptr;
  const bool isPartlyCloudy = strstr(conditionType, "PARTLY_CLOUDY") != nullptr;
  if (!isDaytime && (isClearFamily || isPartlyCloudy)) {
    return WeatherIconBucket::Moon;
  }

  if (strstr(conditionType, "WIND") != nullptr) {
    return WeatherIconBucket::Windy;
  }
  if (strstr(conditionType, "THUNDER") != nullptr) {
    return WeatherIconBucket::Thunderstorm;
  }
  if (strstr(conditionType, "HAIL") != nullptr) {
    return WeatherIconBucket::Hail;
  }
  // Checked before the general snow bucket - RAIN_AND_SNOW is a wintry
  // mix, not plain snow.
  if (strstr(conditionType, "RAIN_AND_SNOW") != nullptr) {
    return WeatherIconBucket::Sleet;
  }
  if (strstr(conditionType, "SNOW") != nullptr) {
    return WeatherIconBucket::Snow;
  }
  // Checked before the general rain bucket below, not after - LIGHT_RAIN/
  // LIGHT_RAIN_SHOWERS/LIGHT_TO_MODERATE_RAIN would otherwise match
  // "RAIN" first and never reach here. CHANCE_OF_SHOWERS has no "LIGHT"
  // in its name but reads the same way, so it's matched explicitly.
  if (strstr(conditionType, "LIGHT") != nullptr || strstr(conditionType, "CHANCE_OF_SHOWERS") != nullptr) {
    return WeatherIconBucket::LightRain;
  }
  // Every documented "heavy rain" variant (HEAVY_RAIN, HEAVY_RAIN_SHOWERS,
  // MODERATE_TO_HEAVY_RAIN, RAIN_PERIODICALLY_HEAVY) contains "HEAVY" -
  // by this point THUNDER/HAIL/SNOW's own heavy variants have already
  // been claimed above, so this only catches rain.
  if (strstr(conditionType, "HEAVY") != nullptr) {
    return WeatherIconBucket::HeavyRain;
  }
  if (strstr(conditionType, "RAIN") != nullptr || strstr(conditionType, "SHOWER") != nullptr ||
      strstr(conditionType, "DRIZZLE") != nullptr) {
    return WeatherIconBucket::Rain;
  }

  if (strstr(conditionType, "MOSTLY_CLEAR") != nullptr) {
    return WeatherIconBucket::MostlyClear;
  }
  if (!isDaytime && (strstr(conditionType, "MOSTLY_CLOUDY") != nullptr || strstr(conditionType, "CLOUDY") != nullptr)) {
    return WeatherIconBucket::NightCloudy;
  }
  if (isPartlyCloudy) {
    return WeatherIconBucket::PartlyCloudy;
  }
  if (isClearFamily || strstr(conditionType, "SUNNY") != nullptr) {
    return WeatherIconBucket::Sun;
  }

  // Mostly-cloudy/cloudy (daytime)/unrecognized - matches the mockups'
  // own apparent default use of the cloud icon for anything that isn't
  // clearly sun/rain/night.
  return WeatherIconBucket::Cloud;
}
