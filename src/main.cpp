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

#include "boot_ui.h"
#include "config_store.h"
#include "display.h"
#include "geocode.h"
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
  Serial.printf("[heap] %s: free=%u largest_internal=%u largest_dma=%u\n", label, ESP.getFreeHeap(),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
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
    Serial.printf("[net] WiFi.status() = %d\n", status);
    sharedState.publishStatus("Could not join Wi-Fi", describeWifiStatus(status), /*loading=*/false);
    delay(4000);
    ESP.restart();
  }
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
  Serial.printf("[net] free heap: %u (largest internal block: %u, largest DMA block: %u)\n", ESP.getFreeHeap(),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
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
      Serial.printf("[net] hourly[%u]: %s %d %s\n", static_cast<unsigned>(i), hourly[i].displayDateTime.c_str(),
                    static_cast<int>(hourly[i].temperature), hourly[i].condition.type.c_str());
    }
  } else {
    Serial.printf("[net] hourly forecast failed: %s\n", hourlyError.c_str());
  }

  settleWifiLink();

  DailyForecastPoint daily[kMaxDailyPoints];
  size_t dailyCount = 0;
  String dailyError;
  bool dailyRetryable = true;
  if (fetchDailyForecast(configStore.apiKey(), configStore.latitude(), configStore.longitude(),
                          configStore.unitsImperial(), daily, dailyCount, dailyError, dailyRetryable)) {
    for (size_t i = 0; i < dailyCount; i++) {
      Serial.printf("[net] daily[%u]: %s %d/%d %s\n", static_cast<unsigned>(i), daily[i].displayDate.c_str(),
                    static_cast<int>(daily[i].maxTemperature), static_cast<int>(daily[i].minTemperature),
                    daily[i].daytimeCondition.type.c_str());
    }
  } else {
    Serial.printf("[net] daily forecast failed: %s\n", dailyError.c_str());
  }
}

void netTaskFn(void *) {
  logHeapStats("netTaskFn start");
  connectWifiOrRetryBoot();
  resolveLocationIfNeeded();
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
