#include "weather.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace {

constexpr const char *kHost = "https://weather.googleapis.com";

// Weather API errors follow Google Cloud's standard error envelope —
// {"error": {"code": <http status>, "message": "...", "status":
// "<CANONICAL_CODE>"}} — a different shape from the classic Maps
// Geocoding API's top-level `status` field (see geocode.cpp). Mapped the
// same way: a specific, human-readable reason per canonical code, plus
// whether retrying is worth it.
// https://cloud.google.com/apis/design/errors#error_codes
void describeError(int httpCode, const String &canonicalStatus, const String &apiMessage, String &outError,
                    bool &outRetryable) {
  if (canonicalStatus == "PERMISSION_DENIED" || canonicalStatus == "UNAUTHENTICATED") {
    outError = "API key rejected";
    if (apiMessage.length() > 0) {
      outError += ": ";
      outError += apiMessage;
    } else {
      outError += " - check it's valid and the Weather API is enabled for it.";
    }
    outRetryable = false;
  } else if (canonicalStatus == "RESOURCE_EXHAUSTED") {
    outError = "Google API quota exceeded for this key - try again later.";
    outRetryable = false;
  } else if (canonicalStatus == "INVALID_ARGUMENT") {
    outError = "Invalid request to the Weather API (bad location?).";
    outRetryable = false;
  } else if (canonicalStatus == "NOT_FOUND") {
    outError = "No weather data for this location.";
    outRetryable = false;
  } else if (httpCode >= 500 || canonicalStatus == "UNAVAILABLE" || canonicalStatus == "INTERNAL") {
    outError = "Google's weather server had a temporary problem.";
    outRetryable = true;
  } else {
    outError = "Unexpected response from the Weather API";
    if (canonicalStatus.length() > 0) {
      outError += " (";
      outError += canonicalStatus;
      outError += ")";
    }
    outError += ".";
    outRetryable = false;
  }
}

// Shared GET + JSON-parse for all three endpoints below. Returns true and
// leaves `doc` populated with the parsed body on success (HTTP 200); on
// any other outcome fills outError/outRetryable and returns false.
bool getJson(const String &url, JsonDocument &doc, String &outError, bool &outRetryable) {
  // setInsecure() skips TLS certificate validation — same tradeoff already
  // made for the Geocoding client in geocode.cpp, not repeated there.
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    outError = "Could not start the request.";
    outRetryable = true;
    Serial.println("[weather] http.begin() failed");
    return false;
  }

  const int code = http.GET();
  Serial.printf("[weather] HTTP status: %d\n", code);

  if (code != HTTP_CODE_OK) {
    const String body = http.getString();
    Serial.printf("[weather] body: %s\n", body.c_str());
    http.end();

    JsonDocument errDoc;
    const DeserializationError parseErr = deserializeJson(errDoc, body);
    String canonicalStatus;
    String apiMessage;
    if (!parseErr) {
      canonicalStatus = errDoc["error"]["status"] | "";
      apiMessage = errDoc["error"]["message"] | "";
    }
    describeError(code, canonicalStatus, apiMessage, outError, outRetryable);
    return false;
  }

  const DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    Serial.printf("[weather] JSON parse failed: %s\n", err.c_str());
    outError = "Bad response from Google.";
    outRetryable = true;
    return false;
  }

  return true;
}

String unitsParam(bool unitsImperial) { return unitsImperial ? "IMPERIAL" : "METRIC"; }

WeatherCondition parseCondition(JsonVariantConst v) {
  WeatherCondition c;
  c.type = v["type"] | "";
  c.description = v["description"]["text"] | "";
  c.iconBaseUri = v["iconBaseUri"] | "";
  return c;
}

}  // namespace

bool fetchCurrentConditions(const String &apiKey, float lat, float lon, bool unitsImperial,
                             CurrentConditions &out, String &outError, bool &outRetryable) {
  String url = String(kHost) + "/v1/currentConditions:lookup?key=" + apiKey + "&location.latitude=" +
               String(lat, 6) + "&location.longitude=" + String(lon, 6) + "&unitsSystem=" + unitsParam(unitsImperial);

  JsonDocument doc;
  if (!getJson(url, doc, outError, outRetryable)) {
    return false;
  }

  out.currentTime = doc["currentTime"] | "";
  out.isDaytime = doc["isDaytime"] | false;
  out.condition = parseCondition(doc["weatherCondition"]);
  out.temperature = doc["temperature"]["degrees"] | 0.0f;
  out.feelsLikeTemperature = doc["feelsLikeTemperature"]["degrees"] | 0.0f;
  out.relativeHumidity = doc["relativeHumidity"] | 0;
  out.uvIndex = doc["uvIndex"] | 0.0f;
  out.precipitationProbabilityPercent = doc["precipitation"]["probability"]["percent"] | 0;
  out.windSpeed = doc["wind"]["speed"]["value"] | 0.0f;
  out.windDirectionCardinal = doc["wind"]["direction"]["cardinal"] | "";
  out.windGust = doc["wind"]["gust"]["value"] | 0.0f;
  out.cloudCoverPercent = doc["cloudCover"] | 0;

  Serial.printf("[weather] current: %s, %d%% (feels %d), humidity %d%%\n", out.condition.type.c_str(),
                static_cast<int>(out.temperature), static_cast<int>(out.feelsLikeTemperature),
                out.relativeHumidity);
  return true;
}

bool fetchHourlyForecast(const String &apiKey, float lat, float lon, bool unitsImperial,
                          HourlyForecastPoint out[kMaxHourlyPoints], size_t &outCount, String &outError,
                          bool &outRetryable) {
  outCount = 0;

  String url = String(kHost) + "/v1/forecast/hours:lookup?key=" + apiKey + "&location.latitude=" +
               String(lat, 6) + "&location.longitude=" + String(lon, 6) +
               "&unitsSystem=" + unitsParam(unitsImperial) + "&hours=" + String(kMaxHourlyPoints) +
               "&pageSize=" + String(kMaxHourlyPoints);

  JsonDocument doc;
  if (!getJson(url, doc, outError, outRetryable)) {
    return false;
  }

  JsonArrayConst hours = doc["forecastHours"].as<JsonArrayConst>();
  for (JsonVariantConst hour : hours) {
    if (outCount >= kMaxHourlyPoints) {
      break;
    }
    HourlyForecastPoint &point = out[outCount++];
    point.displayDateTime = hour["displayDateTime"]["hours"] | "";
    point.condition = parseCondition(hour["weatherCondition"]);
    point.temperature = hour["temperature"]["degrees"] | 0.0f;
    point.precipitationProbabilityPercent = hour["precipitation"]["probability"]["percent"] | 0;
  }

  Serial.printf("[weather] hourly: %u points\n", static_cast<unsigned>(outCount));
  return true;
}

bool fetchDailyForecast(const String &apiKey, float lat, float lon, bool unitsImperial,
                         DailyForecastPoint out[kMaxDailyPoints], size_t &outCount, String &outError,
                         bool &outRetryable) {
  outCount = 0;

  String url = String(kHost) + "/v1/forecast/days:lookup?key=" + apiKey + "&location.latitude=" +
               String(lat, 6) + "&location.longitude=" + String(lon, 6) +
               "&unitsSystem=" + unitsParam(unitsImperial) + "&days=" + String(kMaxDailyPoints) +
               "&pageSize=" + String(kMaxDailyPoints);

  JsonDocument doc;
  if (!getJson(url, doc, outError, outRetryable)) {
    return false;
  }

  JsonArrayConst days = doc["forecastDays"].as<JsonArrayConst>();
  for (JsonVariantConst day : days) {
    if (outCount >= kMaxDailyPoints) {
      break;
    }
    DailyForecastPoint &point = out[outCount++];
    const int year = day["displayDate"]["year"] | 0;
    const int month = day["displayDate"]["month"] | 0;
    const int dayOfMonth = day["displayDate"]["day"] | 0;
    char dateBuf[11];
    snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", year, month, dayOfMonth);
    point.displayDate = dateBuf;
    point.daytimeCondition = parseCondition(day["daytimeForecast"]["weatherCondition"]);
    point.maxTemperature = day["maxTemperature"]["degrees"] | 0.0f;
    point.minTemperature = day["minTemperature"]["degrees"] | 0.0f;
    point.precipitationProbabilityPercent = day["daytimeForecast"]["precipitation"]["probability"]["percent"] | 0;
  }

  Serial.printf("[weather] daily: %u points\n", static_cast<unsigned>(outCount));
  return true;
}
