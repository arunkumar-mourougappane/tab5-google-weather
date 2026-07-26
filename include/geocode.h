// Resolves the free-text location collected during provisioning ("City or
// ZIP") to a lat/lon, via Google's Geocoding API. This is a *separate* Maps
// Platform API from Weather — needs its own enablement on the same key —
// see docs/google-weather-api.md ("Location") for why this has to happen
// after joining real Wi-Fi (the device is offline, in AP mode, while the
// setup form is being filled out) and only once, not on every refresh.
#pragma once

#include <Arduino.h>

// Returns true and fills outLat/outLon on success.
bool geocodeLocation(const String &query, const String &apiKey, float &outLat, float &outLon);
