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

#include <M5Unified.h>
#include <WiFi.h>
#include <lvgl.h>

#include "config_store.h"
#include "geocode.h"
#include "provisioning.h"
#include "weather.h"

namespace {

// The Tab5 panel is native 720x1280 (portrait); M5GFX must be told to
// rotate into landscape before we ask it for width()/height(). Screen
// dimensions are read back from M5GFX rather than hardcoded, so LVGL and
// the panel can never disagree about geometry — a mismatch there is what
// caused the earlier corrupted/clipped-right render.
int32_t screenW = 0;
int32_t screenH = 0;

// Partial draw buffers: a horizontal slice each, not a full frame. LVGL
// only redraws the dirty rectangle each tick, so this is what actually
// keeps touch/redraw latency low — see docs/rendering.md.
constexpr size_t kBufLines = 40;
size_t bufPixels = 0;

lv_display_t *display = nullptr;
lv_color_t *drawBuf1 = nullptr;
lv_color_t *drawBuf2 = nullptr;
uint32_t lastTickMs = 0;

void lvglFlushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *pxMap) {
  const int32_t w = area->x2 - area->x1 + 1;
  const int32_t h = area->y2 - area->y1 + 1;

  M5.Display.startWrite();
  M5.Display.pushImageDMA(area->x1, area->y1, w, h, reinterpret_cast<uint16_t *>(pxMap));
  M5.Display.endWrite();

  lv_display_flush_ready(disp);
}

void lvglTouchReadCb(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  if (M5.Touch.getCount() > 0) {
    const auto detail = M5.Touch.getDetail(0);
    data->point.x = detail.x;
    data->point.y = detail.y;
    data->state = detail.isPressed() ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void initDisplay() {
  // Landscape, 1280 wide. If this comes up mirrored or upside down on real
  // hardware, the fix is this rotation value (try 3 next), not anything
  // downstream — everything else derives its geometry from M5GFX below.
  M5.Display.setRotation(1);

  screenW = M5.Display.width();
  screenH = M5.Display.height();
  bufPixels = static_cast<size_t>(screenW) * kBufLines;

  drawBuf1 = static_cast<lv_color_t *>(
      heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA));
  drawBuf2 = static_cast<lv_color_t *>(
      heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA));

  lv_init();

  display = lv_display_create(screenW, screenH);
  lv_display_set_flush_cb(display, lvglFlushCb);
  lv_display_set_buffers(display, drawBuf1, drawBuf2, bufPixels * sizeof(lv_color_t),
                          LV_DISPLAY_RENDER_MODE_PARTIAL);
  // M5GFX/the panel wants the opposite RGB565 byte order from LVGL's native
  // format — confirmed empirically (near-black background was rendering as
  // purple, exactly the RGB565 byte-swapped color). This asks LVGL to render
  // directly in the byte order the panel wants, instead of an extra
  // per-pixel swap pass in the flush callback.
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565_SWAPPED);

  lv_indev_t *touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, lvglTouchReadCb);
}

// Reused across every boot-time status screen (connecting, geocoding,
// errors, and the final placeholder) so we're not creating a new lv_obj
// screen per state. Plain ASCII punctuation only — LVGL's built-in
// Montserrat fonts don't include Latin-1 Supplement glyphs like
// middle-dot/em-dash, so those rendered as tofu boxes early on.
//
// The small "TAB5 - WEATHER" wordmark is a persistent brand mark shown on
// every screen here, same treatment every time (matches
// docs/mockups/status.html's wordmark, always smaller than the title) — it
// must never be reused as a screen's actual title/headline. Doing that once
// (the old "connected" placeholder) was a real bug: the same string
// visibly changed size between screens depending on which role it was
// playing, which reads as broken even though each individual screen was
// rendering correctly.
//
// `loading` controls only the spinner, matching the mockup's boot/sync
// (spinner) vs. offline (static icon, not built here) distinction.
void showStatusScreen(const char *title, const char *subtitle, bool loading) {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_clean(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x121417), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  lv_obj_t *stack = lv_obj_create(screen);
  lv_obj_set_size(stack, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(stack, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(stack, 0, 0);
  lv_obj_set_style_pad_all(stack, 0, 0);
  lv_obj_set_style_pad_row(stack, 16, 0);
  lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(stack, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *wordmark = lv_label_create(stack);
  lv_label_set_text(wordmark, "TAB5 - WEATHER");
  lv_obj_set_style_text_color(wordmark, lv_color_hex(0xA89E8C), 0);
  lv_obj_set_style_text_font(wordmark, &lv_font_montserrat_20, 0);

  if (loading) {
    lv_obj_t *spinner = lv_spinner_create(stack);
    lv_obj_set_size(spinner, 64, 64);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0xD99A4E), LV_PART_INDICATOR);
  }

  lv_obj_t *titleLabel = lv_label_create(stack);
  lv_label_set_text(titleLabel, title);
  lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xD99A4E), 0);
  lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_48, 0);

  lv_obj_t *subtitleLabel = lv_label_create(stack);
  lv_label_set_text(subtitleLabel, subtitle);
  lv_obj_set_style_text_color(subtitleLabel, lv_color_hex(0xA89E8C), 0);
  lv_obj_set_style_text_font(subtitleLabel, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_align(subtitleLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(subtitleLabel, 900);
  lv_label_set_long_mode(subtitleLabel, LV_LABEL_LONG_WRAP);

  lv_obj_center(stack);
}

void pumpLvgl() {
  const uint32_t now = millis();
  lv_tick_inc(now - lastTickMs);
  lastTickMs = now;
  lv_timer_handler();
}

ConfigStore configStore;

// Maps WiFi.status() to a reason a non-developer can act on, rather than
// one generic "could not join" message for every case — a wrong password
// and an out-of-range network need different fixes, both different from a
// plain timeout.
const char *describeWifiStatus(wl_status_t status) {
  switch (status) {
    case WL_NO_SSID_AVAIL:
      return "Network not found - check it's in range and was entered correctly during setup.";
    case WL_CONNECT_FAILED:
      return "Could not authenticate - check the Wi-Fi password entered during setup.";
    case WL_CONNECTION_LOST:
      return "Connection lost partway through joining.";
    case WL_DISCONNECTED:
      return "Disconnected before finishing.";
    default:
      return "Timed out waiting to connect.";
  }
}

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
  lastTickMs = millis();

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
