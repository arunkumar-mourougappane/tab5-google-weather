// Mutex-protected hand-off between netTask (WiFi/provisioning-adjacent
// networking, geocode, weather fetch — may block freely) and uiTask
// (LVGL/touch — must never block on network I/O). See
// docs/firmware-architecture.md's "Concurrency" section for why these are
// split across the P4's two cores instead of sharing one task.
//
// Two independent payloads, same mutex, same dirty-flag-per-tryConsume
// pattern: Status (boot-time text, shown before the first successful
// weather fetch and on hard failures) and DashboardSnapshot (real weather
// data, once it exists — see dashboard_ui.h). Not two classes/mutexes:
// this project has exactly one netTask->uiTask hand-off need, just two
// shapes of payload for it.
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "weather.h"

class SharedState {
 public:
  struct Status {
    String title;
    String subtitle;
    bool loading = false;
  };

  struct DashboardSnapshot {
    CurrentConditions current;
    HourlyForecastPoint hourly[kMaxHourlyPoints];
    size_t hourlyCount = 0;
    DailyForecastPoint daily[kMaxDailyPoints];
    size_t dailyCount = 0;
    // For "Synced Xm ago" — wall-clock, not millis(): valid as soon as
    // this is published since NTP sync (main.cpp) already requires WiFi,
    // same as the fetch that produced this snapshot.
    time_t syncedAtUnix = 0;
    // Carried from ConfigStore rather than read there directly by
    // dashboard_ui.cpp: both are fixed at provisioning time (or, for
    // displayLocation, at first successful geocode) and never change
    // mid-session, so copying them once per snapshot (cheap - this struct
    // already copies two small fixed-size arrays) keeps dashboard_ui.cpp's
    // only dependency on data flowing through SharedState, not a second,
    // separate ConfigStore reference.
    //
    // displayLocation is ConfigStore's resolved city+", "+state (e.g.
    // "Bellevue, WA") when geocoding returned both, falling back to the
    // raw locationQuery text the user typed at setup (which might just be
    // a ZIP code) when it didn't - see main.cpp's fetchDashboardSnapshot().
    String displayLocation;
    bool unitsImperial = true;
    // ConfigStore's resolved rawOffsetSec + dstOffsetSec (see
    // timezone_lookup.h), or 0 (UTC) if not yet resolved. Added straight
    // to a UTC epoch at display time (dashboard_ui.cpp's
    // refreshDashboardClock()) rather than touching the system TZ/NTP
    // config - see that function's comment for why.
    int32_t utcOffsetSec = 0;
  };

  // Creates the underlying mutex. Call once, before either task starts.
  void begin();

  // netTask calls this whenever what should be on screen changes.
  void publishStatus(const String &title, const String &subtitle, bool loading);

  // uiTask calls this every tick. Returns true and fills `out` only if the
  // status has changed since the last successful call — so uiTask can
  // skip re-rendering (tearing down and rebuilding the whole screen isn't
  // free) when nothing's new.
  bool tryConsumeStatus(Status &out);

  // netTask calls this after every successful weather refresh.
  void publishDashboard(const DashboardSnapshot &snapshot);

  // uiTask calls this every tick. Same dirty-flag contract as
  // tryConsumeStatus() above.
  bool tryConsumeDashboard(DashboardSnapshot &out);

 private:
  SemaphoreHandle_t mutex_ = nullptr;
  Status status_;
  bool statusDirty_ = false;
  DashboardSnapshot dashboard_;
  bool dashboardDirty_ = false;
};
