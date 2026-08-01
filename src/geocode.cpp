#include "geocode.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "geocode_parse.h"
#include "http_json_client.h"
#include "json_arena_allocator.h"
#include "logging.h"

namespace {

String urlEncode(const String &s) {
  String out;
  out.reserve(s.length() * 3);
  char buf[4];
  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else if (c == ' ') {
      out += '+';
    } else {
      snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(c));
      out += buf;
    }
  }
  return out;
}

// Maps Google's `status` field to something a non-developer can act on.
// See https://developers.google.com/maps/documentation/geocoding/requests-geocoding#StatusCodes
void describeStatus(const char *status, const char *apiErrorMessage, String &outError, bool &outRetryable) {
  if (strcmp(status, "ZERO_RESULTS") == 0) {
    outError = "No results for that city/ZIP - check it was entered correctly during setup.";
    outRetryable = false;
  } else if (strcmp(status, "REQUEST_DENIED") == 0) {
    outError = "API key rejected";
    if (apiErrorMessage[0] != '\0') {
      outError += ": ";
      outError += apiErrorMessage;
    } else {
      outError += " - check it's valid and the Geocoding API is enabled for it.";
    }
    outRetryable = false;
  } else if (strcmp(status, "OVER_QUERY_LIMIT") == 0 || strcmp(status, "OVER_DAILY_LIMIT") == 0) {
    outError = "Google API quota exceeded for this key - try again later.";
    outRetryable = false;
  } else if (strcmp(status, "INVALID_REQUEST") == 0) {
    outError = "Invalid location query.";
    outRetryable = false;
  } else if (strcmp(status, "UNKNOWN_ERROR") == 0) {
    outError = "Google's geocoding server had a temporary problem.";
    outRetryable = true;
  } else {
    outError = "Unexpected response (";
    outError += status;
    outError += ").";
    outRetryable = false;
  }
}

}  // namespace

bool geocodeLocation(const String &query, const String &apiKey, float &outLat, float &outLon, String &outCity,
                      String &outState, String &outError, bool &outRetryable) {
  // NOTE: setInsecure() skips TLS certificate validation. Acceptable for a
  // personal-use scaffold talking to a fixed, well-known Google endpoint;
  // the honest fix (pinning Google's root CA) is a follow-up, not done
  // here — see docs/google-weather-api.md for the parallel note about the
  // API key itself living in flash rather than behind a proxy.
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  const String url = "https://maps.googleapis.com/maps/api/geocode/json?address=" + urlEncode(query) +
                      "&key=" + apiKey;

  // buildGeocodeFilter() (geocode_parse.h): keeps the parsed document
  // sized against what this function actually reads rather than the
  // full response - see kGeocodeJsonArenaBytes's comment for why this
  // matters here specifically. Shared with test/test_geocode/ so a test
  // parsing a real captured response exercises the exact filter and
  // arena size production uses, not a parallel copy that could drift.
  JsonDocument filter;
  buildGeocodeFilter(filter);

  // One-shot call, no connection reuse needed (unlike weather.cpp, which
  // makes three back-to-back calls to the same host) — a fresh
  // HTTPClient/WiFiClientSecure per call is fine here. See
  // http_json_client.h for why the transport itself is shared.
  StackJsonDocument<kGeocodeJsonArenaBytes> stackDoc;
  JsonDocument &doc = stackDoc.doc();
  int httpCode = 0;
  String body;
  if (!httpGetJson(http, client, url, doc, httpCode, body, &filter)) {
    if (httpCode == 0) {
      outError = "Could not start the request.";
      outRetryable = true;
      LOG_E("geocode", "http.begin() failed\n");
      return false;
    }

    LOG_W("geocode", "HTTP status: %d\n", httpCode);
    LOG_W("geocode", "body: %s\n", body.c_str());
    // JsonDocument::memoryUsage() would be the obvious way to check this,
    // but it's deprecated in this ArduinoJson version and unconditionally
    // returns 0 — see json_arena_allocator.h. Logged here too (not just
    // the success path below) since a parse failure with httpCode == 200
    // and a body that looks complete/valid is exactly what an arena
    // overflow looks like — this line is what would have made that
    // diagnosable immediately instead of needing a hardware log to spot.
    LOG_D("geocode", "JSON arena: %u bytes used of %u, overflowed=%d\n",
          static_cast<unsigned>(stackDoc.bytesUsed()), static_cast<unsigned>(kGeocodeJsonArenaBytes),
          stackDoc.overflowed());

    if (httpCode != HTTP_CODE_OK) {
      outError = "Network error talking to Google (HTTP ";
      outError += httpCode;
      outError += ").";
      outRetryable = true;
      return false;
    }

    // httpCode == HTTP_CODE_OK but the body didn't parse as JSON.
    outError = "Bad response from Google.";
    outRetryable = true;
    return false;
  }

  LOG_D("geocode", "HTTP status: %d\n", httpCode);
  LOG_D("geocode", "JSON arena: %u bytes used of %u, overflowed=%d\n", static_cast<unsigned>(stackDoc.bytesUsed()),
        static_cast<unsigned>(kGeocodeJsonArenaBytes), stackDoc.overflowed());

  const char *status = doc["status"] | "";
  LOG_D("geocode", "status field: \"%s\"\n", status);
  if (strcmp(status, "OK") != 0) {
    const char *apiErrorMessage = doc["error_message"] | "";
    LOG_W("geocode", "error_message: \"%s\"\n", apiErrorMessage);
    describeStatus(status, apiErrorMessage, outError, outRetryable);
    return false;
  }

  outLat = doc["results"][0]["geometry"]["location"]["lat"] | 0.0f;
  outLon = doc["results"][0]["geometry"]["location"]["lng"] | 0.0f;

  JsonArrayConst components = doc["results"][0]["address_components"].as<JsonArrayConst>();
  outCity = findAddressComponent(components, "locality", /*useShortName=*/false);
  outState = findAddressComponent(components, "administrative_area_level_1", /*useShortName=*/true);

  LOG_I("geocode", "parsed lat=%ld.%03ld lon=%ld.%03ld (x1000 int, avoids relying on printf %%f)\n",
        static_cast<long>(outLat * 1000) / 1000, labs(static_cast<long>(outLat * 1000)) % 1000,
        static_cast<long>(outLon * 1000) / 1000, labs(static_cast<long>(outLon * 1000)) % 1000);
  LOG_I("geocode", "city=\"%s\" state=\"%s\"\n", outCity.c_str(), outState.c_str());
  return true;
}
