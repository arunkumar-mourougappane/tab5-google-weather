// The Dashboard screen (docs/mockups/dashboard.html) - the kiosk's
// default, always-on screen once the first weather fetch succeeds. See
// src/dashboard_ui.cpp for the layout; this header is deliberately thin,
// same convention as boot_ui.h.
#pragma once

#include "shared_state.h"

// Builds the screen on first call (module-level statics, not rebuilt on
// every call - see dashboard_ui.cpp's top comment for why), then updates
// every value in place on this and every subsequent call. Safe to call
// repeatedly with fresh data each time a refresh completes.
void showDashboardScreen(const SharedState::DashboardSnapshot &data);

// Cheap per-tick refresh of just the clock and "Synced Xm ago" label -
// call this every uiTask tick once the dashboard has been shown at least
// once, independent of whether new data has arrived. No-op if the
// dashboard hasn't been built yet.
void refreshDashboardClock(const SharedState::DashboardSnapshot &data);
