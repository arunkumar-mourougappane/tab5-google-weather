// Representative (not hardware-captured - see test_geocode/'s note on
// the same tradeoff) weather.googleapis.com response bodies, built from
// the documented schema plus the extra fields weather.cpp's own comments
// already call out as present-but-unread (dewPoint, heatIndex, windChill,
// thunderstormProbability, airPressure, visibility, sun/moon events) -
// the arena has to fit around those too, not just the handful of fields
// this project reads, same as the real bug class this project has hit
// three times now.
#include <unity.h>

#include "json_arena_allocator.h"
#include "weather_parse.h"

namespace {

// clang-format off
constexpr char kCurrentConditionsResponse[] = R"json(
{
  "currentTime": "2026-08-01T13:27:40.123456789Z",
  "timeZone": {"id": "America/Chicago"},
  "isDaytime": true,
  "weatherCondition": {
    "iconBaseUri": "https://maps.gstatic.com/weather/v1/partly_cloudy",
    "description": {"text": "Partly cloudy", "languageCode": "en"},
    "type": "PARTLY_CLOUDY"
  },
  "temperature": {"degrees": 68, "unit": "FAHRENHEIT"},
  "feelsLikeTemperature": {"degrees": 66, "unit": "FAHRENHEIT"},
  "dewPoint": {"degrees": 58, "unit": "FAHRENHEIT"},
  "heatIndex": {"degrees": 68, "unit": "FAHRENHEIT"},
  "windChill": {"degrees": 68, "unit": "FAHRENHEIT"},
  "relativeHumidity": 62,
  "uvIndex": 4,
  "precipitation": {"probability": {"percent": 20, "type": "RAIN"}, "qpf": {"quantity": 0, "unit": "MM"}},
  "thunderstormProbability": 0,
  "airPressure": {"meanSeaLevelMillibars": 1013.2},
  "wind": {
    "direction": {"degrees": 310, "cardinal": "NORTHWEST"},
    "speed": {"value": 8, "unit": "MILES_PER_HOUR"},
    "gust": {"value": 14, "unit": "MILES_PER_HOUR"}
  },
  "visibility": {"distance": 10, "unit": "MILES"},
  "cloudCover": 40
}
)json";

constexpr char kHourlyForecastResponse[] = R"json(
{
  "forecastHours": [
    {
      "interval": {"startTime": "2026-08-01T13:00:00Z", "endTime": "2026-08-01T14:00:00Z"},
      "displayDateTime": {"year": 2026, "month": 8, "day": 1, "hours": 13, "minutes": 0, "seconds": 0, "nanos": 0, "utcOffset": "-18000s"},
      "weatherCondition": {"iconBaseUri": "https://maps.gstatic.com/weather/v1/partly_cloudy", "description": {"text": "Partly cloudy"}, "type": "PARTLY_CLOUDY"},
      "temperature": {"degrees": 68, "unit": "FAHRENHEIT"},
      "feelsLikeTemperature": {"degrees": 66, "unit": "FAHRENHEIT"},
      "relativeHumidity": 62,
      "uvIndex": 4,
      "precipitation": {"probability": {"percent": 10, "type": "NONE"}, "qpf": {"quantity": 0, "unit": "MM"}},
      "wind": {"direction": {"degrees": 310, "cardinal": "NORTHWEST"}, "speed": {"value": 8, "unit": "MILES_PER_HOUR"}, "gust": {"value": 14, "unit": "MILES_PER_HOUR"}},
      "cloudCover": 40
    },
    {
      "interval": {"startTime": "2026-08-01T14:00:00Z", "endTime": "2026-08-01T15:00:00Z"},
      "displayDateTime": {"year": 2026, "month": 8, "day": 1, "hours": 14, "minutes": 0, "seconds": 0, "nanos": 0, "utcOffset": "-18000s"},
      "weatherCondition": {"iconBaseUri": "https://maps.gstatic.com/weather/v1/cloudy", "description": {"text": "Cloudy"}, "type": "CLOUDY"},
      "temperature": {"degrees": 70, "unit": "FAHRENHEIT"},
      "feelsLikeTemperature": {"degrees": 69, "unit": "FAHRENHEIT"},
      "relativeHumidity": 58,
      "uvIndex": 5,
      "precipitation": {"probability": {"percent": 15, "type": "NONE"}, "qpf": {"quantity": 0, "unit": "MM"}},
      "wind": {"direction": {"degrees": 300, "cardinal": "WEST_NORTHWEST"}, "speed": {"value": 9, "unit": "MILES_PER_HOUR"}, "gust": {"value": 15, "unit": "MILES_PER_HOUR"}},
      "cloudCover": 55
    }
  ]
}
)json";

constexpr char kDailyForecastResponse[] = R"json(
{
  "forecastDays": [
    {
      "interval": {"startTime": "2026-08-01T05:00:00Z", "endTime": "2026-08-02T05:00:00Z"},
      "displayDate": {"year": 2026, "month": 8, "day": 1},
      "daytimeForecast": {
        "weatherCondition": {"iconBaseUri": "https://maps.gstatic.com/weather/v1/partly_cloudy", "description": {"text": "Partly cloudy"}, "type": "PARTLY_CLOUDY"},
        "relativeHumidity": 55,
        "uvIndex": 6,
        "precipitation": {"probability": {"percent": 20, "type": "RAIN"}, "qpf": {"quantity": 0, "unit": "MM"}},
        "wind": {"direction": {"degrees": 310, "cardinal": "NORTHWEST"}, "speed": {"value": 8, "unit": "MILES_PER_HOUR"}, "gust": {"value": 14, "unit": "MILES_PER_HOUR"}},
        "cloudCover": 40
      },
      "nighttimeForecast": {
        "weatherCondition": {"iconBaseUri": "https://maps.gstatic.com/weather/v1/clear", "description": {"text": "Clear"}, "type": "CLEAR"},
        "relativeHumidity": 70,
        "uvIndex": 0,
        "precipitation": {"probability": {"percent": 5, "type": "NONE"}, "qpf": {"quantity": 0, "unit": "MM"}},
        "wind": {"direction": {"degrees": 280, "cardinal": "WEST"}, "speed": {"value": 5, "unit": "MILES_PER_HOUR"}, "gust": {"value": 9, "unit": "MILES_PER_HOUR"}},
        "cloudCover": 10
      },
      "maxTemperature": {"degrees": 74, "unit": "FAHRENHEIT"},
      "minTemperature": {"degrees": 58, "unit": "FAHRENHEIT"},
      "feelsLikeMaxTemperature": {"degrees": 73, "unit": "FAHRENHEIT"},
      "feelsLikeMinTemperature": {"degrees": 58, "unit": "FAHRENHEIT"},
      "sunEvents": {"sunriseTime": "2026-08-01T11:15:00Z", "sunsetTime": "2026-08-02T01:20:00Z"},
      "moonEvents": {"moonPhase": "WANING_GIBBOUS"}
    },
    {
      "interval": {"startTime": "2026-08-02T05:00:00Z", "endTime": "2026-08-03T05:00:00Z"},
      "displayDate": {"year": 2026, "month": 8, "day": 2},
      "daytimeForecast": {
        "weatherCondition": {"iconBaseUri": "https://maps.gstatic.com/weather/v1/cloudy", "description": {"text": "Cloudy"}, "type": "CLOUDY"},
        "relativeHumidity": 60,
        "uvIndex": 5,
        "precipitation": {"probability": {"percent": 40, "type": "RAIN"}, "qpf": {"quantity": 2, "unit": "MM"}},
        "wind": {"direction": {"degrees": 290, "cardinal": "WEST_NORTHWEST"}, "speed": {"value": 10, "unit": "MILES_PER_HOUR"}, "gust": {"value": 18, "unit": "MILES_PER_HOUR"}},
        "cloudCover": 65
      },
      "nighttimeForecast": {
        "weatherCondition": {"iconBaseUri": "https://maps.gstatic.com/weather/v1/cloudy", "description": {"text": "Cloudy"}, "type": "CLOUDY"},
        "relativeHumidity": 68,
        "uvIndex": 0,
        "precipitation": {"probability": {"percent": 30, "type": "RAIN"}, "qpf": {"quantity": 1, "unit": "MM"}},
        "wind": {"direction": {"degrees": 285, "cardinal": "WEST"}, "speed": {"value": 7, "unit": "MILES_PER_HOUR"}, "gust": {"value": 12, "unit": "MILES_PER_HOUR"}},
        "cloudCover": 50
      },
      "maxTemperature": {"degrees": 72, "unit": "FAHRENHEIT"},
      "minTemperature": {"degrees": 56, "unit": "FAHRENHEIT"},
      "feelsLikeMaxTemperature": {"degrees": 71, "unit": "FAHRENHEIT"},
      "feelsLikeMinTemperature": {"degrees": 56, "unit": "FAHRENHEIT"},
      "sunEvents": {"sunriseTime": "2026-08-02T11:16:00Z", "sunsetTime": "2026-08-03T01:19:00Z"},
      "moonEvents": {"moonPhase": "WANING_GIBBOUS"}
    }
  ],
  "nextPageToken": "representative-page-token-000"
}
)json";
// clang-format on

template <size_t N>
bool parseWithFilter(const char *body, void (*buildFilter)(JsonDocument &), StackJsonDocument<N> &stackDoc) {
  JsonDocument filter;
  buildFilter(filter);
  const DeserializationError err =
      deserializeJson(stackDoc.doc(), body, DeserializationOption::Filter(filter));
  return !err && !stackDoc.overflowed();
}

void test_current_conditions_parses_without_overflow() {
  StackJsonDocument<kWeatherJsonArenaBytes> stackDoc;
  TEST_ASSERT_TRUE(parseWithFilter(kCurrentConditionsResponse, buildCurrentConditionsFilter, stackDoc));

  JsonDocument &doc = stackDoc.doc();
  TEST_ASSERT_EQUAL_FLOAT(68.0f, doc["temperature"]["degrees"] | 0.0f);
  TEST_ASSERT_EQUAL_FLOAT(66.0f, doc["feelsLikeTemperature"]["degrees"] | 0.0f);
  TEST_ASSERT_EQUAL_INT(62, doc["relativeHumidity"] | 0);
  TEST_ASSERT_EQUAL_STRING("PARTLY_CLOUDY", doc["weatherCondition"]["type"] | "");
  TEST_ASSERT_EQUAL_STRING("Partly cloudy", doc["weatherCondition"]["description"]["text"] | "");
  TEST_ASSERT_EQUAL_STRING("NORTHWEST", doc["wind"]["direction"]["cardinal"] | "");
}

void test_hourly_forecast_parses_without_overflow() {
  StackJsonDocument<kWeatherJsonArenaBytes> stackDoc;
  TEST_ASSERT_TRUE(parseWithFilter(kHourlyForecastResponse, buildHourlyForecastFilter, stackDoc));

  JsonArrayConst hours = stackDoc.doc()["forecastHours"].as<JsonArrayConst>();
  size_t count = 0;
  for (JsonVariantConst hour : hours) {
    count++;
  }
  TEST_ASSERT_EQUAL_size_t(2, count);

  JsonVariantConst first = hours[0];
  TEST_ASSERT_EQUAL_INT(13, first["displayDateTime"]["hours"] | -1);
  TEST_ASSERT_EQUAL_FLOAT(68.0f, first["temperature"]["degrees"] | 0.0f);
  TEST_ASSERT_EQUAL_INT(10, first["precipitation"]["probability"]["percent"] | -1);
}

void test_daily_forecast_parses_without_overflow() {
  StackJsonDocument<kWeatherJsonArenaBytes> stackDoc;
  TEST_ASSERT_TRUE(parseWithFilter(kDailyForecastResponse, buildDailyForecastFilter, stackDoc));

  JsonDocument &doc = stackDoc.doc();
  JsonArrayConst days = doc["forecastDays"].as<JsonArrayConst>();
  size_t count = 0;
  for (JsonVariantConst day : days) {
    count++;
  }
  TEST_ASSERT_EQUAL_size_t(2, count);

  JsonVariantConst first = days[0];
  TEST_ASSERT_EQUAL_INT(2026, first["displayDate"]["year"] | 0);
  TEST_ASSERT_EQUAL_INT(8, first["displayDate"]["month"] | 0);
  TEST_ASSERT_EQUAL_INT(1, first["displayDate"]["day"] | 0);
  TEST_ASSERT_EQUAL_FLOAT(74.0f, first["maxTemperature"]["degrees"] | 0.0f);
  TEST_ASSERT_EQUAL_FLOAT(58.0f, first["minTemperature"]["degrees"] | 0.0f);
  TEST_ASSERT_EQUAL_STRING("representative-page-token-000", doc["nextPageToken"] | "");
}

}  // namespace

// pio test -e native builds a plain executable (int main()); pio test
// -e tab5-test builds for the Arduino framework, which supplies its own
// main.cpp expecting setup()/loop() instead - same tests either way,
// just a different entry point per target.
#ifdef ARDUINO
#include <Arduino.h>

void setup() {
  delay(2000);  // let the board's USB-serial connection settle before
                // Unity starts writing output, or the first few lines
                // are lost - PlatformIO's own recommended pattern.
  UNITY_BEGIN();
  RUN_TEST(test_current_conditions_parses_without_overflow);
  RUN_TEST(test_hourly_forecast_parses_without_overflow);
  RUN_TEST(test_daily_forecast_parses_without_overflow);
  UNITY_END();
}

void loop() {}
#else
int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_current_conditions_parses_without_overflow);
  RUN_TEST(test_hourly_forecast_parses_without_overflow);
  RUN_TEST(test_daily_forecast_parses_without_overflow);
  return UNITY_END();
}
#endif
