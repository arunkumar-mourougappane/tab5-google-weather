// Resolves the free-text location collected during provisioning ("City or
// ZIP") to a lat/lon, via Google's Geocoding API. This is a *separate* Maps
// Platform API from Weather — needs its own enablement on the same key —
// see docs/google-weather-api.md ("Location") for why this has to happen
// after joining real Wi-Fi (the device is offline, in AP mode, while the
// setup form is being filled out) and only once, not on every refresh.
#pragma once

#include <Arduino.h>

// Returns true and fills outLat/outLon on success.
//
// On failure, fills outError with a human-readable reason — including
// Google's own `status` field (OK, ZERO_RESULTS, OVER_QUERY_LIMIT,
// OVER_DAILY_LIMIT, REQUEST_DENIED, INVALID_REQUEST, UNKNOWN_ERROR — see
// https://developers.google.com/maps/documentation/geocoding/requests-geocoding#StatusCodes)
// mapped to something a non-developer can act on, not the raw code — and
// outRetryable with whether trying again is worth it: true for transient
// failures (network errors, UNKNOWN_ERROR), false for ones that won't
// change without the user fixing something (bad key, bad query, quota).
bool geocodeLocation(const String &query, const String &apiKey, float &outLat, float &outLon,
                      String &outError, bool &outRetryable);
