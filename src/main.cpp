// Firmware for the Tab5 weather dashboard.
//
// Boot flow: bring up display + touch (docs/rendering.md) -> if not yet
// provisioned, run first-run setup (AP + web page, docs/mockups/
// provisioning.html) and reboot -> split into two FreeRTOS tasks pinned to
// the P4's two cores (docs/firmware-architecture.md's "Concurrency"
// section): uiTask (core 0) owns LVGL/touch and never blocks on network
// I/O; netTask (core 1) joins the saved Wi-Fi, geocodes the saved location
// once if needed (docs/google-weather-api.md), then loops fetching current
// conditions/hourly/daily (src/weather.cpp) on a refresh timer — free to
// block, since it's off the render path. The two hand off boot-status text
// and dashboard data via SharedState.
//
// This file is orchestration only — display/LVGL plumbing lives in
// display.{h,cpp}, boot-time screen rendering in boot_ui.{h,cpp}, the
// dashboard itself in dashboard_ui.{h,cpp}. See
// docs/firmware-architecture.md for the reasoning behind that split.

#include <M5Unified.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <time.h>

#include "boot_ui.h"
#include "config_store.h"
#include "daily_forecast_ui.h"
#include "dashboard_ui.h"
#include "display.h"
#include "geocode.h"
#include "hourly_detail_ui.h"
#include "logging.h"
#include "provisioning.h"
#include "shared_state.h"
#include "timezone_lookup.h"
#include "weather.h"

namespace {

ConfigStore configStore;
SharedState sharedState;

// Diagnostic, kept permanently rather than ripped out post-investigation:
// helped rule out uiTaskFn/netTaskFn's own stack allocations as the cause
// of a since-identified SDIO driver assert (see docs/hardware.md's
// "Regressed, then identified as a known, unresolved upstream bug"
// section) — largest_dma was identical before task creation and at the
// start of each task, pointing away from our own code and toward the
// upstream esp-hosted issue documented there. Left in place since it's
// cheap and the upstream bug is still open — useful if it resurfaces.
void logHeapStats(const char *label) {
  LOG_D("heap", "%s: free=%u largest_internal=%u largest_dma=%u\n", label, ESP.getFreeHeap(),
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
}

// A boot-time system clock that already reads as a real date (not the
// 1970 epoch, or close to it, that an unset clock shows) means an
// earlier session's NTP sync survived this reset. ESP-IDF's settimeofday()
// — which the SNTP client calls internally once synced, so this needs no
// explicit "write" step of our own — persists the synced time into
// RTC-retained memory as a side effect, the same mechanism deep-sleep
// wake relies on for time continuity, and that's documented to survive a
// plain software reset (esp_restart()) or panic reboot.
//
// Does NOT survive a brownout-triggered reset on this hardware, though —
// confirmed on real hardware, contradicting what this comment originally
// claimed: a boot immediately following a brownout reset (`E BOD:
// Brownout detector was triggered` in the log) still read as an unset
// clock here, meaning the BOD reset path on this chip resets the RTC
// domain too, unlike a plain SW/panic reset. Not something worth working
// around — this function still does exactly what its name says (checks
// whether the clock looks valid, from whatever cause), it just won't
// fire as often across this project's brownout-heavy test cycles as
// originally assumed. A genuine power-on reset (unplugged/battery
// disconnected) also reads as invalid, as expected.
bool rtcTimeLooksValid() {
  // 2024-01-01T00:00:00Z: comfortably before this project existed, and
  // comfortably after what an unset clock reads as (either the 1970
  // epoch exactly, or a small offset from it based on uptime alone) —
  // any real synced time will be well past this floor for years.
  constexpr time_t kSaneEpochFloor = 1704067200;
  return time(nullptr) >= kSaneEpochFloor;
}

// ---------------------------------------------------------------------
// netTask (core 1): WiFi connect, one-time geocode, weather fetch. Every
// wait here is a plain delay() — no pumpLvgl() calls, unlike the old
// single-task boot sequence — because uiTask is independently pumping
// LVGL on the other core the whole time; this task is free to block.
// ---------------------------------------------------------------------

void connectWifiOrRetryBoot() {
  sharedState.publishStatus("Connecting to Wi-Fi", "Joining \"" + configStore.wifiSsid() + "\"...",
                             /*loading=*/true);

  WiFi.mode(WIFI_STA);
  WiFi.begin(configStore.wifiSsid().c_str(), configStore.wifiPassword().c_str());

  constexpr uint32_t kWifiTimeoutMs = 20000;
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiTimeoutMs) {
    delay(50);
  }

  const wl_status_t status = WiFi.status();
  if (status != WL_CONNECTED) {
    LOG_E("net", "WiFi.status() = %d\n", status);
    sharedState.publishStatus("Could not join Wi-Fi", describeWifiStatus(status), /*loading=*/false);
    delay(4000);
    ESP.restart();
  }

  // Confirms WiFi association is actually healthy at this point — the
  // esp-aes/TLS-handshake failures logged around HTTPS calls (see
  // docs/hardware.md) happen inside ssl_starttls_handshake(), which only
  // runs after the underlying TCP socket already connected, so those
  // failures shouldn't mean WiFi itself is bad. Logging this here (and
  // again in logWifiState() below, before each fetch) makes that visible
  // in the log instead of having to infer it.
  LOG_I("net", "WiFi connected: ssid=%s ip=%s rssi=%d dBm\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
        WiFi.RSSI());
}

// NTP sync, once, right after WiFi comes up — purely so log timestamps
// show real wall-clock time instead of just uptime (see logging.h). UTC
// only: this project doesn't currently resolve a timezone for the
// provisioned location (the Weather API's response does include a
// timeZone.id, e.g. "America/Chicago", but converting IANA zone names to
// a POSIX TZ string needs a lookup table this project doesn't have — out
// of scope for what's just a logging nicety). Blocks this task for up to
// 10s if NTP is unreachable, same shape as every other network wait in
// this file — fine since uiTask keeps rendering independently regardless.
void syncTimeOverNtp() {
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  struct tm timeInfo;
  if (getLocalTime(&timeInfo, 10000)) {
    logSetTimeSynced(true);
    LOG_I("net", "NTP time synced: %04d-%02d-%02d %02d:%02d:%02d UTC\n", timeInfo.tm_year + 1900,
          timeInfo.tm_mon + 1, timeInfo.tm_mday, timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);
  } else {
    LOG_W("net", "NTP sync timed out - log timestamps will show uptime instead of wall-clock time\n");
  }
}

// Called right before each weather fetch (from settleWifiLink()) so a
// mid-session WiFi drop between requests shows up in the log too, not
// just the initial join.
void logWifiState() {
  const wl_status_t status = WiFi.status();
  LOG_D("net", "WiFi state: status=%d (%s) rssi=%d dBm\n", status, status == WL_CONNECTED ? "connected" : "NOT connected",
        WiFi.RSSI());
}

void resolveLocationIfNeeded() {
  // hasLocation() alone isn't enough to skip: a device provisioned before
  // city()/state() existed has lat/lon cached in NVS but no city/state
  // (ConfigStore::begin() reads those keys as "" since they were never
  // written). Re-geocoding once backfills both - after that they're
  // cached in NVS same as lat/lon, and this returns immediately every
  // boot like before. Checking city OR state (not just city) covers the
  // rare real address that only has one of the two; checking neither
  // would mean re-geocoding every boot forever for it instead of once.
  if (configStore.hasLocation() && (configStore.city().length() > 0 || configStore.state().length() > 0)) {
    return;
  }

  sharedState.publishStatus("Finding your location", configStore.locationQuery(), /*loading=*/true);

  // geocodeLocation() is a single-shot HTTPS call with no retry of its own
  // (see geocode.cpp) — a request fired immediately after WiFi.status()
  // first reports WL_CONNECTED can still fail transiently (DNS/TLS not
  // fully settled yet), the same class of "just-changed radio state isn't
  // instantly ready" issue seen with WiFi scanning. Without a retry here,
  // one such blip showed the "Could not resolve" screen even though the
  // location would have resolved fine moments later. Only retries when
  // geocodeLocation() itself says it's worth it — a bad API key or a
  // location Google genuinely can't find won't change on attempt 2 or 3,
  // so those show their specific reason immediately instead of wasting a
  // second first.
  float lat = 0.0f;
  float lon = 0.0f;
  String city;
  String state;
  String error;
  bool retryable = true;
  bool resolved = false;
  for (int attempt = 0; attempt < 3 && !resolved && retryable; attempt++) {
    if (attempt > 0) {
      delay(500);
    }
    resolved =
        geocodeLocation(configStore.locationQuery(), configStore.apiKey(), lat, lon, city, state, error, retryable);
  }

  if (resolved) {
    configStore.saveLocation(lat, lon, city, state);
    return;
  }

  sharedState.publishStatus("Could not resolve that location", error, /*loading=*/false);
  delay(4000);
}

// Prefers the resolved "City, ST" (from geocodeLocation()'s
// address_components, saved once by resolveLocationIfNeeded()) over the
// raw text the user typed at setup, which might just be a ZIP code - city
// alone (no state) still beats that; state alone without a city isn't
// useful on its own, so that case falls back to the raw query too. Only
// meaningful after resolveLocationIfNeeded() has succeeded (callers
// already gate on configStore.hasLocation() before reaching here).
String resolvedDisplayLocation() {
  if (configStore.city().length() > 0) {
    String display = configStore.city();
    if (configStore.state().length() > 0) {
      display += ", " + configStore.state();
    }
    return display;
  }
  return configStore.locationQuery();
}

// Resolves once (same caching pattern as resolveLocationIfNeeded() - and
// depends on it having already succeeded, since it needs lat/lon), not
// re-resolved on every refresh. Not fatal on failure: the dashboard just
// shows UTC (SharedState::DashboardSnapshot::utcOffsetSec defaults to 0)
// until this succeeds on a later boot - unlike a bad location, a bad
// timezone lookup shouldn't block showing weather at all, so this
// doesn't publish a status-screen error the way resolveLocationIfNeeded()
// does.
void resolveTimezoneIfNeeded() {
  if (configStore.hasTimezone()) {
    return;
  }

  int32_t rawOffsetSec = 0;
  int32_t dstOffsetSec = 0;
  String timeZoneId;
  String error;
  bool retryable = true;
  bool resolved = false;
  for (int attempt = 0; attempt < 3 && !resolved && retryable; attempt++) {
    if (attempt > 0) {
      delay(500);
    }
    resolved = resolveTimezone(configStore.apiKey(), configStore.latitude(), configStore.longitude(), time(nullptr),
                                rawOffsetSec, dstOffsetSec, timeZoneId, error, retryable);
  }

  if (resolved) {
    configStore.saveTimezone(rawOffsetSec, dstOffsetSec);
  } else {
    LOG_W("net", "timezone resolution failed: %s (dashboard clock will show UTC)\n", error.c_str());
  }
}

// Gives the esp-hosted SDIO link (ESP32-P4 <-> C6 co-processor, see
// docs/hardware.md) a moment to settle between back-to-back HTTPS requests.
// Same shape as the pauses already used for the "just-changed radio state
// isn't instantly ready" class of issue in resolveLocationIfNeeded() and
// the WiFi-scan retry loop — firing HTTPS requests immediately back-to-back
// crashed the SDIO driver with `assert failed: sdio_rx_get_buffer` on real
// hardware; this hasn't been root-caused, just worked around.
//
// 750ms wasn't enough on its own: with the shared/reused HTTPClient (see
// weather.cpp's sharedClient()), the driver still asserted on the 3rd
// back-to-back request, this time partway through reading the response body
// rather than at connect time — pointing at sustained SDIO RX volume, not
// just reconnect churn, as a second trigger for the same assert. Bumped to
// give the link more recovery time between requests; if this specific
// assert resurfaces even with this delay, that's evidence the real fix
// needs to shrink the daily-forecast response (largest of the three) rather
// than just wait longer. (It did — see fetchDailyForecast()'s page split.)
constexpr uint32_t kWifiSettleMs = 2000;

void settleWifiLink() {
  // Total free heap alone doesn't explain the esp-aes DMA-alignment-buffer
  // allocation failure seen once on real hardware (free heap was >60KB both
  // times) — logging the largest contiguous internal/DMA-capable block too,
  // since that's what a tiny scratch-buffer allocation actually needs and
  // fragmentation could starve it even with plenty of free heap overall.
  LOG_D("net", "free heap: %u (largest internal block: %u, largest DMA block: %u)\n", ESP.getFreeHeap(),
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
  logWifiState();
  delay(kWifiSettleMs);
}

// How often netTask refetches once the dashboard is up. 10 min matches
// docs/google-weather-api.md's explicit recommendation ("far more
// frequent than the underlying forecast data changes", ~13k calls/month
// combined across all three endpoints, comfortably inside the free
// tier's 10k calls/SKU/month).
constexpr uint32_t kRefreshIntervalMs = 10 * 60 * 1000;

// Fetches current conditions, hourly, and daily in one pass. Returns true
// (and fills `out`) only if current conditions succeeded — hourly/daily
// failures are logged but don't block showing at least the current
// reading, since a dashboard with a working "now" card and an empty
// hourly/daily section is still far more useful than reverting to the
// status screen. Same single-shot-with-limited-retry shape already used
// by resolveLocationIfNeeded() for each of the three calls.
bool fetchDashboardSnapshot(SharedState::DashboardSnapshot &out) {
  settleWifiLink();

  String error;
  bool retryable = true;
  bool fetched = false;
  for (int attempt = 0; attempt < 3 && !fetched && retryable; attempt++) {
    if (attempt > 0) {
      delay(500);
    }
    fetched = fetchCurrentConditions(configStore.apiKey(), configStore.latitude(), configStore.longitude(),
                                      configStore.unitsImperial(), out.current, error, retryable);
  }
  if (!fetched) {
    LOG_E("net", "current conditions failed: %s\n", error.c_str());
    return false;
  }

  settleWifiLink();
  String hourlyError;
  bool hourlyRetryable = true;
  if (!fetchHourlyForecast(configStore.apiKey(), configStore.latitude(), configStore.longitude(),
                            configStore.unitsImperial(), out.hourly, out.hourlyCount, hourlyError,
                            hourlyRetryable)) {
    LOG_E("net", "hourly forecast failed: %s\n", hourlyError.c_str());
  }

  settleWifiLink();
  String dailyError;
  bool dailyRetryable = true;
  if (!fetchDailyForecast(configStore.apiKey(), configStore.latitude(), configStore.longitude(),
                           configStore.unitsImperial(), out.daily, out.dailyCount, dailyError, dailyRetryable)) {
    LOG_E("net", "daily forecast failed: %s\n", dailyError.c_str());
  }

  out.syncedAtUnix = time(nullptr);
  out.displayLocation = resolvedDisplayLocation();
  out.unitsImperial = configStore.unitsImperial();
  // Defaults to 0 (UTC) via ConfigStore's own defaults if
  // resolveTimezoneIfNeeded() hasn't succeeded yet - see
  // dashboard_ui.cpp's refreshDashboardClock(), which just adds this to
  // the current UTC epoch rather than touching the system TZ/NTP config.
  out.utcOffsetSec = configStore.timezoneRawOffsetSec() + configStore.timezoneDstOffsetSec();

  LOG_I("net", "dashboard snapshot: %d%s, %u hourly, %u daily\n", static_cast<int>(out.current.temperature),
        out.unitsImperial ? "F" : "C", static_cast<unsigned>(out.hourlyCount), static_cast<unsigned>(out.dailyCount));
  return true;
}

void netTaskFn(void *) {
  logHeapStats("netTaskFn start");
  connectWifiOrRetryBoot();
  // Skipped if setup() already found a valid RTC-persisted time from an
  // earlier session (see rtcTimeLooksValid()) — no reason to spend a
  // network round-trip re-confirming a clock that's almost certainly
  // still correct. Known simplification: this means clock drift across
  // many resets without an intervening power loss is never corrected,
  // only re-synced from scratch after one. Acceptable for what's
  // primarily a log-readability feature, not a precision timekeeping one.
  if (!logTimeIsSynced()) {
    syncTimeOverNtp();
  }
  resolveLocationIfNeeded();

  // Location resolution can fail (bad API key, no results for the query,
  // transient network issue exhausting its retries — see
  // resolveLocationIfNeeded()) and previously wasn't checked here: the
  // weather fetch ran anyway, silently using ConfigStore's (0, 0)
  // defaults instead of a resolved location, showing a real but
  // meaningless forecast for the middle of the ocean with no obvious
  // indication anything was wrong. resolveLocationIfNeeded() already
  // published a "Could not resolve" status that stays on screen since
  // nothing overwrites it below — nothing useful left to do this boot.
  if (!configStore.hasLocation()) {
    vTaskDelete(nullptr);
  }

  // Depends on lat/lon from resolveLocationIfNeeded() above, so must run
  // after it. Not a hard requirement like location - see
  // resolveTimezoneIfNeeded()'s own comment.
  resolveTimezoneIfNeeded();

  // resolvedDisplayLocation() (not locationQuery()): by this point
  // resolveLocationIfNeeded() has already succeeded (guaranteed by the
  // hasLocation() check above), so the resolved "City, ST" is already
  // known - no reason to show the raw ZIP/text the user typed here just
  // because it's earlier in boot than the dashboard itself.
  sharedState.publishStatus("Getting weather", resolvedDisplayLocation(), /*loading=*/true);

  // Ongoing refresh loop, not one-shot: this is the dashboard's actual
  // data source now, not just a boot-time proof of the weather client.
  // On failure, log and leave whatever's already on screen (the status
  // screen on the very first attempt, or the last good dashboard on any
  // later one) rather than overwriting it with an error — a transient
  // failure at a 10-minute cadence shouldn't be alarming (see
  // docs/google-weather-api.md). Richer offline/stale-data UX is
  // docs/mockups/status.html, a later phase.
  for (;;) {
    SharedState::DashboardSnapshot snapshot;
    if (fetchDashboardSnapshot(snapshot)) {
      sharedState.publishDashboard(snapshot);
    }
    delay(kRefreshIntervalMs);
  }
}

// ---------------------------------------------------------------------
// uiTask (core 0): LVGL/touch only. Never calls into WiFi/HTTP — the only
// thing it does with netTask's output is render whatever SharedState last
// published.
// ---------------------------------------------------------------------

void uiTaskFn(void *) {
  logHeapStats("uiTaskFn start");
  // Set once the dashboard has been shown at least once, so the clock/
  // sync-age labels can keep ticking every loop without waiting on a new
  // fetch — cheap (two label-text-set calls), unlike showDashboardScreen()
  // itself which is only worth calling when the data actually changed.
  bool dashboardShown = false;
  SharedState::DashboardSnapshot lastDashboard;

  for (;;) {
    M5.update();
    pumpLvgl();

    SharedState::Status status;
    if (sharedState.tryConsumeStatus(status)) {
      showStatusScreen(status.title.c_str(), status.subtitle.c_str(), status.loading);
    }

    if (sharedState.tryConsumeDashboard(lastDashboard)) {
      showDashboardScreen(lastDashboard);
      // hourlyDetailScreenBuilt() guards against eagerly building (and
      // lv_screen_load()-ing to) hourly_detail_ui.cpp's screen before the
      // user has ever tapped into it - once they have, this keeps it
      // fresh in place if left open across a refresh, same principle as
      // showDashboardScreen() above already relies on (lv_screen_load()
      // only fires on that screen's own first build).
      if (hourlyDetailScreenBuilt()) {
        showHourlyDetailScreen(lastDashboard);
      }
      // Same guard, same reasoning, for the 7-Day-forecast screen.
      if (dailyForecastScreenBuilt()) {
        showDailyForecastScreen(lastDashboard);
      }
      dashboardShown = true;
    } else if (dashboardShown) {
      refreshDashboardClock(lastDashboard);
      if (hourlyDetailScreenBuilt()) {
        refreshHourlyDetailClock(lastDashboard);
      }
      if (dailyForecastScreenBuilt()) {
        refreshDailyForecastClock(lastDashboard);
      }
    }

    delay(5);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);

  // Checked immediately, independent of WiFi/provisioning below — if an
  // earlier session's NTP sync survived this reset (see
  // rtcTimeLooksValid()'s comment), every log line from here on can use a
  // real timestamp, and netTaskFn won't need to spend a network
  // round-trip re-confirming it later.
  if (rtcTimeLooksValid()) {
    logSetTimeSynced(true);
    LOG_I("main", "RTC time from a prior session looks valid, skipping NTP sync this boot\n");
  }

  auto cfg = M5.config();
  M5.begin(cfg);

  initDisplay();
  sharedState.begin();

  configStore.begin();

  // Provisioning runs here, synchronously, before the task split below —
  // not on netTask. It owns LVGL directly (its own screens, its own
  // lv_tick_inc()/lv_timer_handler() pump loop — see provisioning.cpp),
  // which would race with uiTask's independent pumping of the same LVGL
  // state from a different core. It's a one-time, single-task first-boot
  // flow anyway, so there's no benefit to splitting it — only the ongoing
  // WiFi-connect/geocode/weather-fetch flow below needs to.
  if (!configStore.isProvisioned()) {
    // Never returns: runProvisioning() reboots the device itself once
    // setup completes (see provisioning.h).
    runProvisioning(configStore);
  }

  showStatusScreen("Starting...", "", /*loading=*/true);

  logHeapStats("before task creation");
  // 16384, not 8192 - hardware crashed with a "Stack protection fault" in
  // this task (SP pinned exactly at the stack's upper bound) when
  // rendering the hero temp font at 240px; LVGL's RLE decompression for
  // large compressed glyphs needs more of this task's stack than a
  // smaller/uncompressed font does. Matches netTask's existing size.
  xTaskCreatePinnedToCore(uiTaskFn, "uiTask", 16384, nullptr, 1, nullptr, 0);
  // 24576, not 16384 - kWeatherJsonArenaBytes (weather_parse.h) grew to
  // 12288 for the 16-hour forecast fetch (hourly_detail_ui.cpp), a
  // StackJsonDocument<N> that lives as a real stack-frame array inside
  // fetchHourlyForecast(), called from this task. That alone would be
  // 75% of the previous 16384-byte stack, leaving too little headroom
  // for the rest of the call chain (HTTPClient/WiFiClientSecure buffers,
  // String temporaries, the filter JsonDocument) - same risk pattern
  // that caused uiTask's own stack-overflow crash above, addressed
  // proactively here instead of waiting for the same failure on this
  // task.
  xTaskCreatePinnedToCore(netTaskFn, "netTask", 24576, nullptr, 1, nullptr, 1);

  // Nothing left for the default Arduino task to do — uiTask/netTask above
  // own everything from here. Deletes itself rather than falling through
  // to an empty loop().
  vTaskDelete(nullptr);
}

void loop() {}
