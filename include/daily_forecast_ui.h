// The 7-Day-forecast screen (docs/mockups/daily-forecast.html) - reached
// by tapping the Dashboard's "7-DAY OUTLOOK" section (dashboard_ui.cpp).
// See src/daily_forecast_ui.cpp for the layout; this header is
// deliberately thin, same convention as dashboard_ui.h/hourly_detail_ui.h.
#pragma once

#include <lvgl.h>

#include "shared_state.h"

// Builds the screen on first call (module-level statics, not rebuilt on
// every call - same build-once/update-in-place convention
// dashboard_ui.cpp/hourly_detail_ui.cpp use), then updates every value in
// place on this and every subsequent call. Does NOT itself call
// lv_screen_load() past the first build - navigation-triggered callers
// (dashboard_ui.cpp's tap handler) must call lv_screen_load() on
// dailyForecastScreenObj() themselves immediately after, every time; see
// that accessor's own comment for why (this is a lesson learned from a
// real bug in the Hourly-detail screen, not a hypothetical).
void showDailyForecastScreen(const SharedState::DashboardSnapshot &data);

// Cheap per-tick refresh of just the clock - call this every uiTask tick
// once this screen has been shown at least once, independent of whether
// new data has arrived. No-op if the screen hasn't been built yet.
void refreshDailyForecastClock(const SharedState::DashboardSnapshot &data);

// Lets main.cpp's uiTaskFn know whether it's safe to push a data refresh
// into this screen (via showDailyForecastScreen()/refreshDailyForecastClock())
// without accidentally building it before the user has ever navigated
// here.
bool dailyForecastScreenBuilt();

// Accessor for the screen object, mirroring dashboard_ui.h's
// dashboardScreenObj()/hourly_detail_ui.h's hourlyDetailScreenObj().
// Navigation-triggered callers must call lv_screen_load() on this
// themselves after showDailyForecastScreen() updates the widgets -
// showDailyForecastScreen() only loads the screen on its very first
// (build) call, since its other call site is main.cpp's background
// refresh, which must never yank the user onto this screen just because
// new data arrived. Null until this screen has been built at least once.
lv_obj_t *dailyForecastScreenObj();
