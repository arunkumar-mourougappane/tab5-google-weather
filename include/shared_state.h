// Mutex-protected hand-off between netTask (WiFi/provisioning-adjacent
// networking, geocode, weather fetch — may block freely) and uiTask
// (LVGL/touch — must never block on network I/O). See
// docs/firmware-architecture.md's "Concurrency" section for why these are
// split across the P4's two cores instead of sharing one task.
//
// Scoped to exactly what's needed today: boot-time status text (what
// showStatusScreen() renders). There's no dashboard UI yet to consume raw
// CurrentConditions/HourlyForecastPoint/DailyForecastPoint data, so this
// doesn't carry those — add that hand-off when a dashboard exists to
// consume it, not before.
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class SharedState {
 public:
  struct Status {
    String title;
    String subtitle;
    bool loading = false;
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

 private:
  SemaphoreHandle_t mutex_ = nullptr;
  Status status_;
  bool dirty_ = false;
};
