// Firmware for the Tab5 weather dashboard.
//
// Boot flow: bring up display + touch (docs/rendering.md) -> if not yet
// provisioned, run first-run setup (AP + web page, docs/mockups/
// provisioning.html) and reboot -> split into two FreeRTOS tasks pinned to
// the P4's two cores (docs/firmware-architecture.md's "Concurrency"
// section): uiTask (core 0) owns LVGL/touch and never blocks on network
// I/O; netTask (core 1) joins the saved Wi-Fi, geocodes the saved location
// once if needed (docs/google-weather-api.md), and fetches current
// conditions (src/weather.cpp) — free to block, since it's off the render
// path. The two hand off boot-status text via SharedState.
// [Real dashboard layout/hourly+daily strips not built yet — this is just
// current conditions on the same status-screen chrome, to prove the
// weather client end-to-end.]
//
// This file is orchestration only — display/LVGL plumbing lives in
// display.{h,cpp}, boot-time screen rendering in boot_ui.{h,cpp}. See
// docs/firmware-architecture.md for the reasoning behind that split.

#include <M5Unified.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <time.h>

#include "boot_ui.h"
#include "config_store.h"
#include "display.h"
#include "geocode.h"
#include "logging.h"
#include "provisioning.h"
#include "shared_state.h"
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
  if (configStore.hasLocation()) {
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
  String error;
  bool retryable = true;
  bool resolved = false;
  for (int attempt = 0; attempt < 3 && !resolved && retryable; attempt++) {
    if (attempt > 0) {
      delay(500);
    }
    resolved = geocodeLocation(configStore.locationQuery(), configStore.apiKey(), lat, lon, error, retryable);
  }

  if (resolved) {
    configStore.saveLocation(lat, lon);
    return;
  }

  sharedState.publishStatus("Could not resolve that location", error, /*loading=*/false);
  delay(4000);
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

// No dashboard UI yet (see docs/rendering.md's "Open problem" section on
// hero-scale fonts) — this just proves the weather.googleapis.com client
// (src/weather.cpp) end-to-end on real hardware by fetching current
// conditions once and showing them on the same status-screen chrome used
// for every other boot state. Same single-shot-with-limited-retry shape as
// resolveLocationIfNeeded(): only retries when fetchCurrentConditions()
// itself says the failure was transient.
void fetchAndShowCurrentConditions() {
  settleWifiLink();

  sharedState.publishStatus("Getting weather", configStore.locationQuery(), /*loading=*/true);

  CurrentConditions conditions;
  String error;
  bool retryable = true;
  bool fetched = false;
  for (int attempt = 0; attempt < 3 && !fetched && retryable; attempt++) {
    if (attempt > 0) {
      delay(500);
    }
    fetched = fetchCurrentConditions(configStore.apiKey(), configStore.latitude(), configStore.longitude(),
                                      configStore.unitsImperial(), conditions, error, retryable);
  }

  if (!fetched) {
    sharedState.publishStatus("Could not get weather", error, /*loading=*/false);
    return;
  }

  const char *unitSuffix = configStore.unitsImperial() ? "F" : "C";
  char subtitle[192];
  snprintf(subtitle, sizeof(subtitle), "%s\nFeels like %d%s - humidity %d%%",
           conditions.condition.description.c_str(), static_cast<int>(conditions.feelsLikeTemperature),
           unitSuffix, conditions.relativeHumidity);

  char title[32];
  snprintf(title, sizeof(title), "%d%s", static_cast<int>(conditions.temperature), unitSuffix);
  sharedState.publishStatus(title, subtitle, /*loading=*/false);
}

// Temporary: no hourly/daily UI exists yet (no dashboard yet at all — see
// fetchAndShowCurrentConditions() above), so this just exercises
// fetchHourlyForecast()/fetchDailyForecast() once at boot and logs what
// came back, to confirm the same getJson() chunked-response fix that fixed
// current conditions also covers these two endpoints on real hardware.
// Remove once the real dashboard consumes these instead of Serial.
void fetchAndLogForecasts() {
  settleWifiLink();

  HourlyForecastPoint hourly[kMaxHourlyPoints];
  size_t hourlyCount = 0;
  String hourlyError;
  bool hourlyRetryable = true;
  if (fetchHourlyForecast(configStore.apiKey(), configStore.latitude(), configStore.longitude(),
                           configStore.unitsImperial(), hourly, hourlyCount, hourlyError, hourlyRetryable)) {
    for (size_t i = 0; i < hourlyCount; i++) {
      LOG_D("net", "hourly[%u]: %s %d %s\n", static_cast<unsigned>(i), hourly[i].displayDateTime.c_str(),
            static_cast<int>(hourly[i].temperature), hourly[i].condition.type.c_str());
    }
  } else {
    LOG_E("net", "hourly forecast failed: %s\n", hourlyError.c_str());
  }

  settleWifiLink();

  DailyForecastPoint daily[kMaxDailyPoints];
  size_t dailyCount = 0;
  String dailyError;
  bool dailyRetryable = true;
  if (fetchDailyForecast(configStore.apiKey(), configStore.latitude(), configStore.longitude(),
                          configStore.unitsImperial(), daily, dailyCount, dailyError, dailyRetryable)) {
    for (size_t i = 0; i < dailyCount; i++) {
      LOG_D("net", "daily[%u]: %s %d/%d %s\n", static_cast<unsigned>(i), daily[i].displayDate.c_str(),
            static_cast<int>(daily[i].maxTemperature), static_cast<int>(daily[i].minTemperature),
            daily[i].daytimeCondition.type.c_str());
    }
  } else {
    LOG_E("net", "daily forecast failed: %s\n", dailyError.c_str());
  }
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

  fetchAndShowCurrentConditions();
  fetchAndLogForecasts();

  // One-shot for now — no dashboard exists yet to periodically refresh
  // (see docs/firmware-architecture.md's concurrency section: a refresh
  // loop belongs with the dashboard work, not bolted on here first).
  // Nothing left for this task to do; matches today's behavior, where the
  // old single-task loop() never refetched either.
  vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------
// uiTask (core 0): LVGL/touch only. Never calls into WiFi/HTTP — the only
// thing it does with netTask's output is render whatever SharedState last
// published.
// ---------------------------------------------------------------------

void uiTaskFn(void *) {
  logHeapStats("uiTaskFn start");
  for (;;) {
    M5.update();
    pumpLvgl();

    SharedState::Status status;
    if (sharedState.tryConsumeStatus(status)) {
      showStatusScreen(status.title.c_str(), status.subtitle.c_str(), status.loading);
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
    runProvisioning(configStore);
    ESP.restart();
  }

  showStatusScreen("Starting...", "", /*loading=*/true);

  logHeapStats("before task creation");
  xTaskCreatePinnedToCore(uiTaskFn, "uiTask", 8192, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(netTaskFn, "netTask", 16384, nullptr, 1, nullptr, 1);

  // Nothing left for the default Arduino task to do — uiTask/netTask above
  // own everything from here. Deletes itself rather than falling through
  // to an empty loop().
  vTaskDelete(nullptr);
}

void loop() {}
