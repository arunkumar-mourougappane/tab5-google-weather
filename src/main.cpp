// Firmware for the Tab5 weather dashboard.
//
// Boot flow: bring up display + touch (docs/rendering.md) -> if not yet
// provisioned, run first-run setup (AP + web page, docs/mockups/
// provisioning.html) and reboot -> join the saved Wi-Fi -> geocode the
// saved location once if we haven't already (docs/google-weather-api.md)
// -> fetch current conditions (src/weather.cpp) and show them.
// [Real dashboard layout/hourly+daily strips not built yet — this is just
// current conditions on the same status-screen chrome, to prove the
// weather client end-to-end.]
//
// This file is orchestration only — display/LVGL plumbing lives in
// display.{h,cpp}, boot-time screen rendering in boot_ui.{h,cpp}. See
// docs/firmware-architecture.md for the reasoning behind that split.

#include <M5Unified.h>
#include <WiFi.h>

#include "boot_ui.h"
#include "config_store.h"
#include "display.h"
#include "geocode.h"
#include "provisioning.h"
#include "weather.h"

namespace {

ConfigStore configStore;

void connectWifiOrRetryBoot() {
  showStatusScreen("Connecting to Wi-Fi", ("Joining \"" + configStore.wifiSsid() + "\"...").c_str(),
                    /*loading=*/true);

  WiFi.mode(WIFI_STA);
  WiFi.begin(configStore.wifiSsid().c_str(), configStore.wifiPassword().c_str());

  constexpr uint32_t kWifiTimeoutMs = 20000;
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiTimeoutMs) {
    pumpLvgl();
    delay(50);
  }

  const wl_status_t status = WiFi.status();
  if (status != WL_CONNECTED) {
    Serial.printf("[main] WiFi.status() = %d\n", status);
    showStatusScreen("Could not join Wi-Fi", describeWifiStatus(status), /*loading=*/false);
    const uint32_t retryStart = millis();
    while (millis() - retryStart < 4000) {
      pumpLvgl();
      delay(20);
    }
    ESP.restart();
  }
}

void resolveLocationIfNeeded() {
  if (configStore.hasLocation()) {
    return;
  }

  showStatusScreen("Finding your location", configStore.locationQuery().c_str(), /*loading=*/true);

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
      const uint32_t retryStart = millis();
      while (millis() - retryStart < 500) {
        pumpLvgl();
        delay(20);
      }
    }
    resolved = geocodeLocation(configStore.locationQuery(), configStore.apiKey(), lat, lon, error, retryable);
  }

  if (resolved) {
    configStore.saveLocation(lat, lon);
    return;
  }

  showStatusScreen("Could not resolve that location", error.c_str(), /*loading=*/false);
  const uint32_t start = millis();
  while (millis() - start < 4000) {
    pumpLvgl();
    delay(20);
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
// than just wait longer.
constexpr uint32_t kWifiSettleMs = 2000;

void settleWifiLink() {
  // Total free heap alone doesn't explain the esp-aes DMA-alignment-buffer
  // allocation failure seen once on real hardware (free heap was >60KB both
  // times) — logging the largest contiguous internal/DMA-capable block too,
  // since that's what a tiny scratch-buffer allocation actually needs and
  // fragmentation could starve it even with plenty of free heap overall.
  Serial.printf("[main] free heap: %u (largest internal block: %u, largest DMA block: %u)\n",
                ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
  const uint32_t start = millis();
  while (millis() - start < kWifiSettleMs) {
    pumpLvgl();
    delay(20);
  }
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

  showStatusScreen("Getting weather", configStore.locationQuery().c_str(), /*loading=*/true);

  CurrentConditions conditions;
  String error;
  bool retryable = true;
  bool fetched = false;
  for (int attempt = 0; attempt < 3 && !fetched && retryable; attempt++) {
    if (attempt > 0) {
      const uint32_t retryStart = millis();
      while (millis() - retryStart < 500) {
        pumpLvgl();
        delay(20);
      }
    }
    fetched = fetchCurrentConditions(configStore.apiKey(), configStore.latitude(), configStore.longitude(),
                                      configStore.unitsImperial(), conditions, error, retryable);
  }

  if (!fetched) {
    showStatusScreen("Could not get weather", error.c_str(), /*loading=*/false);
    return;
  }

  const char *unitSuffix = configStore.unitsImperial() ? "F" : "C";
  char subtitle[192];
  snprintf(subtitle, sizeof(subtitle), "%s\nFeels like %d%s - humidity %d%%",
           conditions.condition.description.c_str(), static_cast<int>(conditions.feelsLikeTemperature),
           unitSuffix, conditions.relativeHumidity);

  char title[32];
  snprintf(title, sizeof(title), "%d%s", static_cast<int>(conditions.temperature), unitSuffix);
  showStatusScreen(title, subtitle, /*loading=*/false);
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
      Serial.printf("[main] hourly[%u]: %s %d %s\n", static_cast<unsigned>(i),
                    hourly[i].displayDateTime.c_str(), static_cast<int>(hourly[i].temperature),
                    hourly[i].condition.type.c_str());
    }
  } else {
    Serial.printf("[main] hourly forecast failed: %s\n", hourlyError.c_str());
  }

  settleWifiLink();

  DailyForecastPoint daily[kMaxDailyPoints];
  size_t dailyCount = 0;
  String dailyError;
  bool dailyRetryable = true;
  if (fetchDailyForecast(configStore.apiKey(), configStore.latitude(), configStore.longitude(),
                          configStore.unitsImperial(), daily, dailyCount, dailyError, dailyRetryable)) {
    for (size_t i = 0; i < dailyCount; i++) {
      Serial.printf("[main] daily[%u]: %s %d/%d %s\n", static_cast<unsigned>(i), daily[i].displayDate.c_str(),
                    static_cast<int>(daily[i].maxTemperature), static_cast<int>(daily[i].minTemperature),
                    daily[i].daytimeCondition.type.c_str());
    }
  } else {
    Serial.printf("[main] daily forecast failed: %s\n", dailyError.c_str());
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);

  auto cfg = M5.config();
  M5.begin(cfg);

  initDisplay();

  configStore.begin();

  if (!configStore.isProvisioned()) {
    runProvisioning(configStore);
    ESP.restart();
  }

  connectWifiOrRetryBoot();
  resolveLocationIfNeeded();
  fetchAndShowCurrentConditions();
  fetchAndLogForecasts();
}

void loop() {
  M5.update();
  pumpLvgl();
  delay(5);
}
