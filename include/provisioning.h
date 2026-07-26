// First-run setup: the device opens its own access point + a small web
// server (see docs/mockups/provisioning.html for the design this
// implements) and blocks until a phone/laptop submits Wi-Fi credentials, a
// location, and a Google Weather API key. No internet access happens here —
// the device is in AP mode, not joined to any network yet — geocoding the
// location happens later, once, after the caller reboots into station mode
// (see geocode.h and docs/google-weather-api.md).
#pragma once

#include "config_store.h"

// Blocks (servicing DNS/HTTP/LVGL itself) until setup completes, then
// returns. Caller is expected to restart the device afterward — this
// function doesn't do it itself, so callers can log/display first.
void runProvisioning(ConfigStore &config);
