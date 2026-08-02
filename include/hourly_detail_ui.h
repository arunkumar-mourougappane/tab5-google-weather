// The Hourly-detail screen (docs/mockups/hourly-detail.html) - reached by
// tapping the Dashboard's "HOURLY" section (dashboard_ui.cpp). See
// src/hourly_detail_ui.cpp for the layout; this header is deliberately
// thin, same convention as dashboard_ui.h/boot_ui.h.
#pragma once

#include <lvgl.h>

#include "shared_state.h"

// Builds the screen on first call (module-level statics, not rebuilt on
// every call - same build-once/update-in-place convention
// dashboard_ui.cpp uses), then updates every value in place on this and
// every subsequent call, and lv_screen_load()s it active. Safe to call
// repeatedly with fresh data each time a refresh completes.
void showHourlyDetailScreen(const SharedState::DashboardSnapshot &data);

// Cheap per-tick refresh of just the clock - call this every uiTask tick
// once this screen has been shown at least once, independent of whether
// new data has arrived. No-op if the screen hasn't been built yet.
void refreshHourlyDetailClock(const SharedState::DashboardSnapshot &data);

// Lets main.cpp's uiTaskFn know whether it's safe to push a data refresh
// into this screen (via showHourlyDetailScreen()/refreshHourlyDetailClock())
// without accidentally building - and lv_screen_load()-ing to - it before
// the user has ever navigated here. showHourlyDetailScreen() itself calls
// lv_screen_load() on first build, same as showDashboardScreen() does;
// unlike the Dashboard, this screen must never be built eagerly.
bool hourlyDetailScreenBuilt();

// Accessor for the screen object, mirroring dashboard_ui.h's
// dashboardScreenObj(). Navigation-triggered callers (dashboard_ui.cpp's
// tap handler) must call lv_screen_load() on this themselves after
// showHourlyDetailScreen() updates the widgets - showHourlyDetailScreen()
// only loads the screen on its very first (build) call, since its other
// call site is main.cpp's background refresh, which must never yank the
// user onto this screen just because new data arrived. Null until this
// screen has been built at least once.
lv_obj_t *hourlyDetailScreenObj();
