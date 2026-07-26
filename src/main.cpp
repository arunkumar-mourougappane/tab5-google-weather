// Firmware for the Tab5 weather dashboard.
//
// Boot flow: bring up display + touch (docs/rendering.md) -> if not yet
// provisioned, run first-run setup (AP + web page, docs/mockups/
// provisioning.html) and reboot -> join the saved Wi-Fi -> geocode the
// saved location once if we haven't already (docs/google-weather-api.md)
// -> [dashboard/weather client not built yet — placeholder "connected"
// screen for now].

#include <M5Unified.h>
#include <WiFi.h>
#include <lvgl.h>

#include "config_store.h"
#include "geocode.h"
#include "provisioning.h"

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
void showStatusScreen(const char *title, const char *subtitle) {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_clean(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x121417), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  lv_obj_t *titleLabel = lv_label_create(screen);
  lv_label_set_text(titleLabel, title);
  lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xD99A4E), 0);
  lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_20, 0);
  lv_obj_align(titleLabel, LV_ALIGN_CENTER, 0, -16);

  lv_obj_t *subtitleLabel = lv_label_create(screen);
  lv_label_set_text(subtitleLabel, subtitle);
  lv_obj_set_style_text_color(subtitleLabel, lv_color_hex(0xA89E8C), 0);
  lv_obj_set_style_text_align(subtitleLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(subtitleLabel, lv_pct(70));
  lv_obj_set_style_text_align(subtitleLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(subtitleLabel, LV_ALIGN_CENTER, 0, 24);
}

void pumpLvgl() {
  const uint32_t now = millis();
  lv_tick_inc(now - lastTickMs);
  lastTickMs = now;
  lv_timer_handler();
}

ConfigStore configStore;

void connectWifiOrRetryBoot() {
  showStatusScreen("Connecting to Wi-Fi",
                    ("Joining \"" + configStore.wifiSsid() + "\"...").c_str());

  WiFi.mode(WIFI_STA);
  WiFi.begin(configStore.wifiSsid().c_str(), configStore.wifiPassword().c_str());

  constexpr uint32_t kWifiTimeoutMs = 20000;
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiTimeoutMs) {
    pumpLvgl();
    delay(50);
  }

  if (WiFi.status() != WL_CONNECTED) {
    showStatusScreen("Could not join Wi-Fi", "Restarting to try again...");
    const uint32_t retryStart = millis();
    while (millis() - retryStart < 3000) {
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

  showStatusScreen("Finding your location", configStore.locationQuery().c_str());

  float lat = 0.0f;
  float lon = 0.0f;
  if (geocodeLocation(configStore.locationQuery(), configStore.apiKey(), lat, lon)) {
    configStore.saveLocation(lat, lon);
    return;
  }

  showStatusScreen("Could not resolve that location",
                    "Double check the city/ZIP entered during setup, then power-cycle to retry.");
  const uint32_t start = millis();
  while (millis() - start < 4000) {
    pumpLvgl();
    delay(20);
  }
}

void showConnectedPlaceholder() {
  char subtitle[192];
  snprintf(subtitle, sizeof(subtitle), "Connected. Weather for %s (%.3f, %.3f).\nDashboard not built yet.",
           configStore.locationQuery().c_str(), configStore.latitude(), configStore.longitude());
  showStatusScreen("TAB5 - WEATHER", subtitle);
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
  showConnectedPlaceholder();
}

void loop() {
  M5.update();
  pumpLvgl();
  delay(5);
}
