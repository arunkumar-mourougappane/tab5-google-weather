// First-run setup: the device opens its own access point + a small web
// server (see docs/mockups/provisioning.html for the design this
// implements) and blocks until a phone/laptop submits Wi-Fi credentials, a
// location, and a Google Weather API key. No internet access happens here —
// the device is in AP mode, not joined to any network yet — geocoding the
// location happens later, once, after rebooting into station mode
// (see geocode.h and docs/google-weather-api.md).
#pragma once

#include "config_store.h"

// Blocks (servicing DNS/HTTP/LVGL itself) until setup completes, shows the
// success screen for a couple of seconds, then reboots the device itself
// via ESP.restart() — this call never returns. Self-contained so "setup
// complete" always ends in a restart regardless of caller, rather than
// depending on the caller to do it correctly afterward.
[[noreturn]] void runProvisioning(ConfigStore &config);
