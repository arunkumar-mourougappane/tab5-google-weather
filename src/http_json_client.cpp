#include "http_json_client.h"

bool httpGetJson(HTTPClient &http, WiFiClientSecure &client, const String &url, JsonDocument &doc, int &outHttpCode,
                  String &outBody) {
  outHttpCode = 0;
  outBody = "";

  if (!http.begin(client, url)) {
    return false;
  }

  outHttpCode = http.GET();

  if (outHttpCode != HTTP_CODE_OK) {
    outBody = http.getString();
    http.end();
    return false;
  }

  // See http_json_client.h: getString() dechunks, getStream() doesn't.
  const String body = http.getString();
  http.end();
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    outBody = body;
    return false;
  }

  return true;
}
