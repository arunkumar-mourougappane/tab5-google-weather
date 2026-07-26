#include "geocode.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

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

}  // namespace

bool geocodeLocation(const String &query, const String &apiKey, float &outLat, float &outLon) {
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

  if (!http.begin(client, url)) {
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    return false;
  }

  const char *status = doc["status"] | "";
  if (strcmp(status, "OK") != 0) {
    return false;
  }

  outLat = doc["results"][0]["geometry"]["location"]["lat"] | 0.0f;
  outLon = doc["results"][0]["geometry"]["location"]["lng"] | 0.0f;
  return true;
}
