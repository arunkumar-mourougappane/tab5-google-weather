// Shared GET + dechunk + JSON-parse transport, used by geocode.cpp and
// weather.cpp so the one detail that actually matters on this hardware
// doesn't have to be re-derived (or silently diverge) per caller: Google's
// APIs can return chunked transfer-encoding, and http.getStream() hands
// back the raw chunk-size markers un-decoded — confirmed on real hardware
// as the cause of an earlier "InvalidInput" JSON parse failure (see
// docs/hardware.md). http.getString() dechunks first and is what this
// helper uses.
//
// Deliberately doesn't own the HTTPClient/WiFiClientSecure — callers pass
// theirs in, since connection-reuse policy is caller-specific:
// weather.cpp reuses one connection across its three same-host calls
// (also load-bearing for an esp-hosted SDIO driver crash — see
// docs/hardware.md), while geocode.cpp only ever makes one call per boot
// and doesn't need to.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// `filter`, if non-null, is passed to deserializeJson() as a
// DeserializationOption::Filter — the DOM parser stores every field in
// the response by default regardless of whether the caller ever reads
// it, so a response with many more fields per record than the caller
// actually parses (confirmed on hardware: the Weather API's hourly
// forecast has ~20 fields/hour, most unused by fetchHourlyForecast())
// can overflow a `doc` sized against only the fields actually needed.
// Filtering at parse time keeps the parsed document (and therefore the
// arena backing it, if `doc` is a StackJsonDocument — see
// json_arena_allocator.h) sized against what's actually used, not
// everything the API happens to return.
//
// On success: returns true, `doc` populated, outHttpCode == 200.
// On failure: returns false.
//   - outHttpCode == 0 means the request itself couldn't be started
//     (http.begin() failed) — outBody is empty in that case.
//   - Otherwise outHttpCode is the HTTP status actually received (200
//     included, if the body came back but failed to parse as JSON), and
//     outBody holds whatever body was read, so the caller can apply its
//     own API-specific error-message mapping — Google's APIs use
//     different error envelopes (compare describeError() in weather.cpp
//     against describeStatus() in geocode.cpp), which is why that mapping
//     stays with each caller rather than living here.
bool httpGetJson(HTTPClient &http, WiFiClientSecure &client, const String &url, JsonDocument &doc, int &outHttpCode,
                  String &outBody, const JsonDocument *filter = nullptr);
