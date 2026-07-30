// Boot-time status screen rendering — pure LVGL/UI concerns, no networking.
#pragma once

#include <WiFi.h>

// Reused across every boot-time status screen (connecting, geocoding,
// errors, and the final placeholder) so we're not creating a new lv_obj
// screen per state. `loading` controls only the spinner, matching the
// mockup's boot/sync (spinner) vs. offline (static icon, not built here)
// distinction.
void showStatusScreen(const char *title, const char *subtitle, bool loading);

// Maps WiFi.status() to a reason a non-developer can act on, rather than
// one generic "could not join" message for every case — a wrong password
// and an out-of-range network need different fixes, both different from a
// plain timeout.
const char *describeWifiStatus(wl_status_t status);
