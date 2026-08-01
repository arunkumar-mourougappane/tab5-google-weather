// Resolves a lat/lon to a UTC offset + current DST offset, via Google's
// Time Zone API. A *separate* Maps Platform API from Geocoding/Weather —
// needs its own enablement on the same key — called once, right after
// geocoding succeeds (same timing/caching pattern as geocodeLocation()
// itself: resolved once, cached in ConfigStore, not re-resolved on every
// refresh). See docs/google-weather-api.md ("Timezone").
#pragma once

#include <Arduino.h>
#include <time.h>

// `atUnixTime` matters: the Time Zone API computes whether DST applies
// *at that moment* for the given coordinates, so the two offsets
// returned are only valid as of that timestamp - not a year-round rule
// set. Pass a real current time (this project already has one by the
// time this is called; see main.cpp's boot sequence). Known
// simplification: on a long-uptime device that never reboots across a
// DST transition, these offsets go stale until the next re-resolve
// (re-provisioning, or a future periodic re-check - not built yet).
//
// Returns true and fills outRawOffsetSec/outDstOffsetSec on success -
// both in seconds, both meant to be passed straight to Arduino's
// configTime(gmtOffset_sec, daylightOffset_sec, ntpServer) unchanged.
// outTimeZoneId (e.g. "America/Los_Angeles") is informational only, for
// logging - nothing here parses or needs it.
//
// Same error-handling shape as geocodeLocation(): outError is a
// human-readable reason, outRetryable says whether a retry is worth it.
bool resolveTimezone(const String &apiKey, float lat, float lon, time_t atUnixTime, int32_t &outRawOffsetSec,
                      int32_t &outDstOffsetSec, String &outTimeZoneId, String &outError, bool &outRetryable);
