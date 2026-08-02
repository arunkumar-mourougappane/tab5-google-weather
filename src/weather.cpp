#include "weather.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "http_json_client.h"
#include "json_arena_allocator.h"
#include "logging.h"
#include "weather_parse.h"

namespace {

constexpr const char *kHost = "https://weather.googleapis.com";

void logArenaUsage(const char *label, size_t bytesUsed, bool arenaOverflowed) {
  LOG_D("weather", "%s JSON arena: %u bytes used of %u, overflowed=%d\n", label, static_cast<unsigned>(bytesUsed),
        static_cast<unsigned>(kWeatherJsonArenaBytes), arenaOverflowed);
}

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
  } else if (httpCode < 0) {
    // Negative codes are HTTPClient's own transport-level errors (refused
    // connection, TLS handshake failure, timeout, ...), not a real
    // response from the API — always worth retrying, especially now that
    // a failed handshake forces the shared connection closed before the
    // next attempt (see getJson() above) rather than reusing whatever
    // state it left behind.
    outError = "Network error talking to the Weather API.";
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

// One TLS connection reused across all calls to weather.googleapis.com in
// this boot cycle (function-static, so it survives between getJson()
// calls), not a fresh WiFiClientSecure/HTTPClient torn down and
// reconnected per endpoint. A fresh connection per call was the original
// shape (mirroring geocode.cpp, which only ever makes one call per boot);
// on real hardware, the *third* full TLS teardown+reconnect in a row
// reliably crashed the esp-hosted SDIO driver
// (`assert failed: sdio_rx_get_buffer`) with plenty of free heap and no
// improvement from adding delay between requests — pointing at connection
// churn over the SDIO link, not resource exhaustion or timing, as the
// actual trigger. http.setReuse(true) keeps the socket open across end(),
// so the second and third requests here reuse it instead of reconnecting.
WiFiClientSecure &sharedClient() {
  static WiFiClientSecure client;
  static bool initialized = false;
  if (!initialized) {
    // setInsecure() skips TLS certificate validation — same tradeoff
    // already made for the Geocoding client in geocode.cpp.
    client.setInsecure();
    initialized = true;
  }
  return client;
}

// Shared GET + JSON-parse for all three endpoints below. Returns true and
// leaves `doc` populated with the parsed body on success (HTTP 200); on
// any other outcome fills outError/outRetryable and returns false. Wraps
// httpGetJson() (http_json_client.h) — the transport/dechunking mechanics
// are shared with geocode.cpp; this API's error-envelope interpretation
// (describeError() above) isn't, since it's specific to Google Cloud's
// error shape.
bool getJson(const String &url, JsonDocument &doc, String &outError, bool &outRetryable,
             const JsonDocument *filter = nullptr) {
  WiFiClientSecure &client = sharedClient();

  static HTTPClient http;
  http.setReuse(true);

  int httpCode = 0;
  String body;
  if (httpGetJson(http, client, url, doc, httpCode, body, filter)) {
    LOG_D("weather", "HTTP status: %d\n", httpCode);
    return true;
  }

  // httpCode <= 0 means the connection/TLS handshake itself failed (as
  // opposed to a completed request with a non-200 status) — observed on
  // real hardware paired with `esp-aes: Failed to allocate memory ...`
  // mid-handshake. A handshake that failed partway through can leave the
  // reused WiFiClientSecure's underlying socket/TLS session in a
  // half-torn-down state; the *next* call reusing that same object is
  // where the esp-hosted SDIO driver's hard assert has been seen to hit
  // (see docs/hardware.md). Force-closing here means the next attempt
  // opens a genuinely fresh connection instead of reusing whatever state
  // the failed handshake left behind.
  if (httpCode <= 0) {
    client.stop();
  }

  if (httpCode == 0) {
    outError = "Could not start the request.";
    outRetryable = true;
    LOG_E("weather", "http.begin() failed\n");
    return false;
  }

  LOG_W("weather", "HTTP status: %d\n", httpCode);
  LOG_W("weather", "body: %s\n", body.c_str());

  if (httpCode != HTTP_CODE_OK) {
    JsonDocument errDoc;
    const DeserializationError parseErr = deserializeJson(errDoc, body);
    String canonicalStatus;
    String apiMessage;
    if (!parseErr) {
      canonicalStatus = errDoc["error"]["status"] | "";
      apiMessage = errDoc["error"]["message"] | "";
    }
    describeError(httpCode, canonicalStatus, apiMessage, outError, outRetryable);
    return false;
  }

  // httpCode == HTTP_CODE_OK but the body didn't parse as JSON.
  outError = "Bad response from Google.";
  outRetryable = true;
  return false;
}

String unitsParam(bool unitsImperial) { return unitsImperial ? "IMPERIAL" : "METRIC"; }

WeatherCondition parseCondition(JsonVariantConst v) {
  WeatherCondition c;
  c.type = v["type"] | "";
  c.description = v["description"]["text"] | "";
  c.iconBaseUri = v["iconBaseUri"] | "";
  return c;
}

void appendDailyPoints(JsonArrayConst days, DailyForecastPoint out[kMaxDailyPoints], size_t &outCount) {
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
    point.nighttimeCondition = parseCondition(day["nighttimeForecast"]["weatherCondition"]);
    point.maxTemperature = day["maxTemperature"]["degrees"] | 0.0f;
    point.minTemperature = day["minTemperature"]["degrees"] | 0.0f;
    point.precipitationProbabilityPercent = day["daytimeForecast"]["precipitation"]["probability"]["percent"] | 0;
    point.nightPrecipitationProbabilityPercent =
        day["nighttimeForecast"]["precipitation"]["probability"]["percent"] | 0;
    point.sunriseTime = day["sunEvents"]["sunriseTime"] | "";
    point.sunsetTime = day["sunEvents"]["sunsetTime"] | "";
    point.moonPhase = day["moonEvents"]["moonPhase"] | "";
  }
}

}  // namespace

bool fetchCurrentConditions(const String &apiKey, float lat, float lon, bool unitsImperial,
                             CurrentConditions &out, String &outError, bool &outRetryable) {
  String url = String(kHost) + "/v1/currentConditions:lookup?key=" + apiKey + "&location.latitude=" +
               String(lat, 6) + "&location.longitude=" + String(lon, 6) + "&unitsSystem=" + unitsParam(unitsImperial);

  // buildCurrentConditionsFilter() (weather_parse.h): the API returns far
  // more fields per response than this function reads (dewPoint,
  // heatIndex, airPressure, ... — confirmed on hardware), and the DOM
  // parser stores all of them by default. Filtering to just what's read
  // below keeps the parsed document small enough for the stack arena
  // regardless of how many extra fields Google's response happens to
  // include. Shared with test/test_weather/ so a test parsing a real
  // captured response exercises the exact filter and arena size
  // production uses.
  JsonDocument filter;
  buildCurrentConditionsFilter(filter);

  StackJsonDocument<kWeatherJsonArenaBytes> stackDoc;
  JsonDocument &doc = stackDoc.doc();
  if (!getJson(url, doc, outError, outRetryable, &filter)) {
    return false;
  }
  logArenaUsage("current", stackDoc.bytesUsed(), stackDoc.overflowed());

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

  LOG_I("weather", "current: %s, %d%% (feels %d), humidity %d%%\n", out.condition.type.c_str(),
        static_cast<int>(out.temperature), static_cast<int>(out.feelsLikeTemperature), out.relativeHumidity);
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

  // buildHourlyForecastFilter() (weather_parse.h) - see its comment.
  JsonDocument filter;
  buildHourlyForecastFilter(filter);

  StackJsonDocument<kWeatherJsonArenaBytes> stackDoc;
  JsonDocument &doc = stackDoc.doc();
  if (!getJson(url, doc, outError, outRetryable, &filter)) {
    return false;
  }
  logArenaUsage("hourly", stackDoc.bytesUsed(), stackDoc.overflowed());

  JsonArrayConst hours = doc["forecastHours"].as<JsonArrayConst>();
  for (JsonVariantConst hour : hours) {
    if (outCount >= kMaxHourlyPoints) {
      break;
    }
    HourlyForecastPoint &point = out[outCount++];
    point.displayDateTime = hour["displayDateTime"]["hours"] | "";
    point.isDaytime = hour["isDaytime"] | true;
    point.condition = parseCondition(hour["weatherCondition"]);
    point.temperature = hour["temperature"]["degrees"] | 0.0f;
    point.precipitationProbabilityPercent = hour["precipitation"]["probability"]["percent"] | 0;
  }

  LOG_I("weather", "hourly: %u points\n", static_cast<unsigned>(outCount));
  return true;
}

bool fetchDailyForecast(const String &apiKey, float lat, float lon, bool unitsImperial,
                         DailyForecastPoint out[kMaxDailyPoints], size_t &outCount, String &outError,
                         bool &outRetryable) {
  outCount = 0;

  // Split into two page requests (first 4 days, then the rest) instead of
  // one 7-day call. The daily forecast has the widest per-entry payload of
  // the three endpoints (day+night split, sun/moon events) and is always
  // the 3rd HTTPS request in this app's boot sequence — on real hardware
  // that combination crashed the esp-hosted SDIO driver
  // (`assert failed: sdio_rx_get_buffer`) partway through reading the
  // response body, even with the connection reused and a settle delay
  // beforehand (see docs/hardware.md). Halving the body size is the next
  // thing to try per that doc's notes; if this still crashes, the response
  // size isn't the actual variable either and the SDIO driver itself needs
  // investigating upstream.
  constexpr size_t kFirstPageDays = 4;

  const String baseUrl = String(kHost) + "/v1/forecast/days:lookup?key=" + apiKey + "&location.latitude=" +
                          String(lat, 6) + "&location.longitude=" + String(lon, 6) +
                          "&unitsSystem=" + unitsParam(unitsImperial) + "&days=" + String(kMaxDailyPoints);

  // buildDailyForecastFilter() (weather_parse.h) - see its comment.
  JsonDocument filter;
  buildDailyForecastFilter(filter);

  // page-1's JsonDocument/body String are scoped to this block so they're
  // freed before page 2's request, not held alive across it. Once, on real
  // hardware, page 2 hit `esp-aes: Failed to allocate memory for start
  // alignment buffer` mid-TLS-decrypt (a tiny DMA scratch allocation
  // failing) despite ESP.getFreeHeap() reporting >60KB free beforehand —
  // free heap isn't the same as an unfragmented block being available, and
  // leaving page 1's parsed JSON resident through page 2's TLS handshake
  // was unnecessary peak memory pressure at exactly the moment that
  // allocation needs to succeed. Now that both documents are stack-arena
  // backed (see json_arena_allocator.h) rather than heap-backed, this
  // scoping also means page 1's buffer is off the stack before page 2's
  // even exists, not just "freed" in a heap sense.
  String pageToken;
  {
    StackJsonDocument<kWeatherJsonArenaBytes> stackDoc;
    JsonDocument &doc = stackDoc.doc();
    if (!getJson(baseUrl + "&pageSize=" + String(kFirstPageDays), doc, outError, outRetryable, &filter)) {
      return false;
    }
    logArenaUsage("daily page 1", stackDoc.bytesUsed(), stackDoc.overflowed());
    appendDailyPoints(doc["forecastDays"].as<JsonArrayConst>(), out, outCount);
    pageToken = doc["nextPageToken"] | "";
  }

  if (pageToken.length() > 0 && outCount < kMaxDailyPoints) {
    delay(500);  // beat between the two calls, same idea as settleWifiLink() in main.cpp

    StackJsonDocument<kWeatherJsonArenaBytes> stackDoc2;
    JsonDocument &doc2 = stackDoc2.doc();
    String secondError;
    bool secondRetryable = true;
    const String url2 = baseUrl + "&pageSize=" + String(kMaxDailyPoints - outCount) + "&pageToken=" + pageToken;
    if (getJson(url2, doc2, secondError, secondRetryable, &filter)) {
      logArenaUsage("daily page 2", stackDoc2.bytesUsed(), stackDoc2.overflowed());
      appendDailyPoints(doc2["forecastDays"].as<JsonArrayConst>(), out, outCount);
    } else {
      // A partial (4-day) forecast beats none — don't fail the whole call
      // over the second page.
      LOG_E("weather", "daily page 2 failed: %s\n", secondError.c_str());
    }
  }

  LOG_I("weather", "daily: %u points\n", static_cast<unsigned>(outCount));
  return true;
}
