#include "timezone_lookup.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "http_json_client.h"
#include "json_arena_allocator.h"
#include "logging.h"
#include "timezone_parse.h"

namespace {

// Time Zone API uses the same top-level `status` field (not the
// error/status-envelope split Weather API uses) as Geocoding - same
// canonical status values, same describeStatus() shape as geocode.cpp's,
// duplicated rather than shared since the two files' error text differs
// slightly in what's being looked up ("that city/ZIP" vs "that
// location's timezone").
void describeStatus(const char *status, const char *errorMessage, String &outError, bool &outRetryable) {
  if (strcmp(status, "ZERO_RESULTS") == 0) {
    outError = "No timezone found for that location.";
    outRetryable = false;
  } else if (strcmp(status, "REQUEST_DENIED") == 0) {
    outError = "API key rejected";
    if (errorMessage[0] != '\0') {
      outError += ": ";
      outError += errorMessage;
    } else {
      outError += " - check it's valid and the Time Zone API is enabled for it.";
    }
    outRetryable = false;
  } else if (strcmp(status, "OVER_QUERY_LIMIT") == 0 || strcmp(status, "OVER_DAILY_LIMIT") == 0) {
    outError = "Google API quota exceeded for this key - try again later.";
    outRetryable = false;
  } else if (strcmp(status, "INVALID_REQUEST") == 0) {
    outError = "Invalid timezone request.";
    outRetryable = false;
  } else if (strcmp(status, "UNKNOWN_ERROR") == 0) {
    outError = "Google's timezone server had a temporary problem.";
    outRetryable = true;
  } else {
    outError = "Unexpected response (";
    outError += status;
    outError += ").";
    outRetryable = false;
  }
}

}  // namespace

bool resolveTimezone(const String &apiKey, float lat, float lon, time_t atUnixTime, int32_t &outRawOffsetSec,
                      int32_t &outDstOffsetSec, String &outTimeZoneId, String &outError, bool &outRetryable) {
  // See geocode.cpp's identical note: acceptable for a personal-use
  // scaffold talking to a fixed, well-known Google endpoint.
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = "https://maps.googleapis.com/maps/api/timezone/json?location=" + String(lat, 6) + "," +
               String(lon, 6) + "&timestamp=" + String(static_cast<long>(atUnixTime)) + "&key=" + apiKey;

  // buildTimezoneFilter()/kTimezoneJsonArenaBytes (timezone_parse.h):
  // shared with test/test_timezone/ so a test parsing a real captured
  // response exercises the exact filter and arena size production uses.
  JsonDocument filter;
  buildTimezoneFilter(filter);

  StackJsonDocument<kTimezoneJsonArenaBytes> stackDoc;
  JsonDocument &doc = stackDoc.doc();
  int httpCode = 0;
  String body;
  if (!httpGetJson(http, client, url, doc, httpCode, body, &filter)) {
    if (httpCode == 0) {
      outError = "Could not start the request.";
      outRetryable = true;
      LOG_E("timezone", "http.begin() failed\n");
      return false;
    }

    LOG_W("timezone", "HTTP status: %d\n", httpCode);
    LOG_W("timezone", "body: %s\n", body.c_str());
    LOG_D("timezone", "JSON arena: %u bytes used of %u, overflowed=%d\n",
          static_cast<unsigned>(stackDoc.bytesUsed()), static_cast<unsigned>(kTimezoneJsonArenaBytes),
          stackDoc.overflowed());

    if (httpCode != HTTP_CODE_OK) {
      outError = "Network error talking to Google (HTTP ";
      outError += httpCode;
      outError += ").";
      outRetryable = true;
      return false;
    }

    outError = "Bad response from Google.";
    outRetryable = true;
    return false;
  }

  LOG_D("timezone", "HTTP status: %d\n", httpCode);
  LOG_D("timezone", "JSON arena: %u bytes used of %u, overflowed=%d\n", static_cast<unsigned>(stackDoc.bytesUsed()),
        static_cast<unsigned>(kTimezoneJsonArenaBytes), stackDoc.overflowed());

  const char *status = doc["status"] | "";
  LOG_D("timezone", "status field: \"%s\"\n", status);
  if (strcmp(status, "OK") != 0) {
    const char *errorMessage = doc["errorMessage"] | "";
    LOG_W("timezone", "errorMessage: \"%s\"\n", errorMessage);
    describeStatus(status, errorMessage, outError, outRetryable);
    return false;
  }

  outRawOffsetSec = doc["rawOffset"] | 0;
  outDstOffsetSec = doc["dstOffset"] | 0;
  outTimeZoneId = doc["timeZoneId"] | "";
  LOG_I("timezone", "resolved %s: rawOffset=%ld dstOffset=%ld\n", outTimeZoneId.c_str(),
        static_cast<long>(outRawOffsetSec), static_cast<long>(outDstOffsetSec));
  return true;
}
