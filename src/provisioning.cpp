#include "provisioning.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <lvgl.h>
#include <qrcode.h>

#include "logging.h"
#include "provisioning_page.h"

namespace {

// RGB565 packer — used instead of lv_color_t/lv_color_to_u16 helpers so the
// QR bitmap can be written straight into the canvas buffer without
// depending on which pixel-set function a given LVGL 9.x minor version
// exposes (see docs/rendering.md for the flush-API lesson that made us
// wary of guessing LVGL/M5GFX call signatures without a hardware check).
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t kColorPaper = rgb565(0xF2, 0xED, 0xE1);
constexpr uint16_t kColorInk = rgb565(0x1C, 0x20, 0x24);
constexpr uint16_t kColorSub = rgb565(0x52, 0x58, 0x5E);
constexpr uint16_t kColorBrass = rgb565(0xA8, 0x68, 0x1C);

DNSServer dnsServer;
WebServer server(80);
ConfigStore *g_config = nullptr;
volatile bool g_done = false;
String g_apSsid;

lv_obj_t *g_waitingScreen = nullptr;
lv_obj_t *g_successScreen = nullptr;

String apSsid() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[24];
  snprintf(buf, sizeof(buf), "Tab5-Weather-%02X%02X", mac[4], mac[5]);
  return String(buf);
}

// No URL-encoding helper needed here (unlike geocode.cpp): the setup page
// posts via the browser's own URLSearchParams over
// application/x-www-form-urlencoded, so WebServer's server.arg() decodes
// incoming fields for us — this file never builds a query string itself.

lv_obj_t *makeLabel(lv_obj_t *parent, const char *text, uint32_t colorArgb, const lv_font_t *font) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_hex(colorArgb), 0);
  if (font != nullptr) {
    lv_obj_set_style_text_font(label, font, 0);
  }
  return label;
}

// Draws a real, scannable QR code (WIFI: join URI for this device's own AP)
// directly into a manually-filled RGB565 buffer — see the rgb565() comment
// above for why this bypasses the lv_canvas_set_* pixel API.
// qrcode_getBufferSize() (declared in qrcode.h) is a real linked function,
// not a macro — can't size a compile-time array with it even though the
// version number itself is constexpr. This mirrors its formula (module
// count = 4*version+17, buffer = ceil(modules^2 / 8)) so the array bound
// stays a genuine compile-time constant.
constexpr int qrModuleCount(int version) { return 4 * version + 17; }
constexpr size_t qrBufferSize(int version) {
  return (static_cast<size_t>(qrModuleCount(version)) * qrModuleCount(version) + 7) / 8;
}

lv_obj_t *buildQrCanvas(lv_obj_t *parent, const String &ssid) {
  const String joinUri = "WIFI:T:nopass;S:" + ssid + ";;";

  // 37x37 modules — comfortable margin for a ~35 char join URI at ECC_LOW.
  constexpr uint8_t kQrVersion = 5;
  QRCode qrcode;
  static uint8_t qrData[qrBufferSize(kQrVersion)];
  qrcode_initText(&qrcode, qrData, kQrVersion, ECC_LOW, joinUri.c_str());

  // scale=4 (16px/module physical) made for a QR only ~14mm across on this
  // ~294 PPI panel — too small to scan reliably at arm's length. 6 gets it
  // to ~21mm.
  const int scale = 6;
  const int quiet = 2;  // modules of quiet-zone border each side, required for reliable scanning
  const int side = (qrcode.size + quiet * 2) * scale;

  auto *buf = static_cast<uint16_t *>(heap_caps_malloc(
      static_cast<size_t>(side) * side * sizeof(uint16_t), MALLOC_CAP_SPIRAM));

  for (int i = 0; i < side * side; i++) {
    buf[i] = kColorPaper;
  }
  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      if (!qrcode_getModule(&qrcode, x, y)) continue;
      const int px0 = (x + quiet) * scale;
      const int py0 = (y + quiet) * scale;
      for (int dy = 0; dy < scale; dy++) {
        uint16_t *row = buf + (py0 + dy) * side + px0;
        for (int dx = 0; dx < scale; dx++) {
          row[dx] = kColorInk;
        }
      }
    }
  }

  lv_obj_t *canvas = lv_canvas_create(parent);
  lv_canvas_set_buffer(canvas, buf, side, side, LV_COLOR_FORMAT_RGB565);
  return canvas;
}

// Cycles the "Waiting for setup" label's trailing dots, matching the
// mockup's pulsing-dots footer (docs/mockups/provisioning.html
// .setup-footer .dots) instead of a static ellipsis.
lv_obj_t *g_waitingDotsLabel = nullptr;
lv_timer_t *g_waitingDotsTimer = nullptr;

void waitingDotsTimerCb(lv_timer_t *timer) {
  (void)timer;
  static int step = 0;
  step = (step + 1) % 4;
  const char *dots[] = {"", ".", "..", "..."};
  if (g_waitingDotsLabel != nullptr) {
    String text = "Waiting for setup";
    text += dots[step];
    lv_label_set_text(g_waitingDotsLabel, text.c_str());
  }
}

// Bordered card showing the AP's own name/security, matching the mockup's
// .net-card (docs/mockups/provisioning.html) rather than folding this into
// plain paragraph text.
lv_obj_t *buildNetCard(lv_obj_t *parent, const String &ssid) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, 460, 92);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x1B1E22), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(0x2A2E33), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_radius(card, 10, 0);
  lv_obj_set_style_pad_all(card, 16, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *nameKey = makeLabel(card, "NETWORK NAME", 0xA89E8C, &lv_font_montserrat_20);
  lv_obj_align(nameKey, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_t *nameVal = makeLabel(card, ssid.c_str(), 0xD99A4E, &lv_font_montserrat_28);
  lv_obj_align(nameVal, LV_ALIGN_TOP_LEFT, 0, 26);

  lv_obj_t *secKey = makeLabel(card, "SECURITY", 0xA89E8C, &lv_font_montserrat_20);
  lv_obj_align(secKey, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_t *secVal = makeLabel(card, "Open, no password", 0xF0E9D8, &lv_font_montserrat_20);
  lv_obj_align(secVal, LV_ALIGN_TOP_RIGHT, 0, 26);

  return card;
}

// Numbered 1/2/3 step list, matching the mockup's .step-list rather than
// one run-on paragraph.
lv_obj_t *buildStepList(lv_obj_t *parent, const String &ssid) {
  lv_obj_t *list = lv_obj_create(parent);
  lv_obj_set_size(list, 480, 190);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, 14, 0);
  lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);

  char step1[128];
  snprintf(step1, sizeof(step1), "1. Connect to \"%s\" (or scan the code)", ssid.c_str());
  makeLabel(list, step1, 0xF0E9D8, &lv_font_montserrat_20);
  makeLabel(list, "2. A setup page should open automatically -\n    if not, visit 192.168.4.1", 0xF0E9D8,
            &lv_font_montserrat_20);
  makeLabel(list, "3. Enter your Wi-Fi, location, and API key", 0xF0E9D8, &lv_font_montserrat_20);

  return list;
}

void showWaitingScreen(const String &ssid) {
  g_waitingScreen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(g_waitingScreen, lv_color_hex(0x121417), 0);
  lv_obj_set_style_bg_opa(g_waitingScreen, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(g_waitingScreen, 0, 0);
  lv_obj_set_style_border_width(g_waitingScreen, 0, 0);

  makeLabel(g_waitingScreen, "TAB5 - WEATHER", 0xA89E8C, &lv_font_montserrat_20);
  lv_obj_align(lv_obj_get_child(g_waitingScreen, -1), LV_ALIGN_TOP_MID, 0, 20);

  lv_obj_t *title = makeLabel(g_waitingScreen, "Let's get you set up", 0xF0E9D8, &lv_font_montserrat_48);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 54);

  lv_obj_t *explainer =
      makeLabel(g_waitingScreen,
                "This display can't take Wi-Fi passwords or an API key on its own -\n"
                "connect to the network below from your phone or laptop to finish setup.",
                0xA89E8C, &lv_font_montserrat_20);
  lv_obj_set_style_text_align(explainer, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(explainer, LV_ALIGN_TOP_MID, 0, 122);

  lv_obj_t *qr = buildQrCanvas(g_waitingScreen, ssid);
  lv_obj_align(qr, LV_ALIGN_LEFT_MID, 90, 50);
  lv_obj_t *qrLabel = makeLabel(g_waitingScreen, "SCAN TO JOIN", 0xA89E8C, &lv_font_montserrat_20);
  lv_obj_align_to(qrLabel, qr, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);

  lv_obj_t *netCard = buildNetCard(g_waitingScreen, ssid);
  lv_obj_align(netCard, LV_ALIGN_TOP_LEFT, 460, 190);

  lv_obj_t *steps = buildStepList(g_waitingScreen, ssid);
  lv_obj_align_to(steps, netCard, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 24);

  g_waitingDotsLabel = makeLabel(g_waitingScreen, "Waiting for setup", 0xD99A4E, &lv_font_montserrat_20);
  lv_obj_align(g_waitingDotsLabel, LV_ALIGN_BOTTOM_MID, 0, -26);
  if (g_waitingDotsTimer != nullptr) {
    lv_timer_del(g_waitingDotsTimer);
  }
  g_waitingDotsTimer = lv_timer_create(waitingDotsTimerCb, 400, nullptr);

  lv_screen_load(g_waitingScreen);
}

void showSuccessScreen(const String &ssid, const String &locationQuery) {
  if (g_waitingDotsTimer != nullptr) {
    lv_timer_del(g_waitingDotsTimer);
    g_waitingDotsTimer = nullptr;
  }

  g_successScreen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(g_successScreen, lv_color_hex(0x121417), 0);
  lv_obj_set_style_bg_opa(g_successScreen, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(g_successScreen, 0, 0);
  lv_obj_set_style_border_width(g_successScreen, 0, 0);

  // check-ring: teal-bordered circle around a checkmark, matching the
  // mockup's .check-ring rather than text alone.
  lv_obj_t *ring = lv_obj_create(g_successScreen);
  lv_obj_set_size(ring, 84, 84);
  lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(ring, lv_color_hex(0x6FB3AC), 0);
  lv_obj_set_style_border_width(ring, 3, 0);
  lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(ring, LV_ALIGN_CENTER, 0, -100);
  lv_obj_t *check = makeLabel(ring, LV_SYMBOL_OK, 0x6FB3AC, &lv_font_montserrat_48);
  lv_obj_center(check);

  lv_obj_t *title = makeLabel(g_successScreen, "Setup saved", 0xF0E9D8, &lv_font_montserrat_48);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);

  char info[192];
  snprintf(info, sizeof(info), "Joining \"%s\" and loading weather for %s...", ssid.c_str(),
           locationQuery.c_str());
  lv_obj_t *sub = makeLabel(g_successScreen, info, 0xA89E8C, &lv_font_montserrat_20);
  lv_obj_align(sub, LV_ALIGN_CENTER, 0, 32);

  lv_screen_load(g_successScreen);
}

void sendJsonError(const char *message) {
  String body = "{\"ok\":false,\"error\":\"";
  body += message;
  body += "\"}";
  server.send(400, "application/json", body);
}

void handleRoot() { server.send_P(200, "text/html", kProvisioningPageHtml); }

void handleSave() {
  const String ssid = server.arg("ssid");
  const String password = server.arg("password");
  const String location = server.arg("location");
  const String apikey = server.arg("apikey");
  const String units = server.arg("units");

  if (ssid.length() == 0 || location.length() == 0 || apikey.length() == 0) {
    sendJsonError("Wi-Fi network, location, and API key are all required.");
    return;
  }

  g_config->saveProvisioning(ssid, password, location, apikey, units != "metric");
  server.send(200, "application/json", "{\"ok\":true}");

  showSuccessScreen(ssid, location);
  g_done = true;
}

void handleNotFound() {
  // Captive portal catch-all: redirect every unrecognized path (the OS
  // probe URLs — /generate_204, /hotspot-detect.html, /ncsi.txt, etc. —
  // all land here since none of them are registered explicitly) to the
  // setup page, which is what makes phones offer to "join" it automatically.
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

}  // namespace

void runProvisioning(ConfigStore &config) {
  g_config = &config;
  g_done = false;

  // AP only, no STA — this project no longer attempts WiFi.scanNetworks()
  // at all (see docs/hardware.md: broken outright on the ESP-IDF version
  // this project is pinned to, and no available version combines a
  // working scan with the fix for the separate SDIO/esp-aes DMA crash).
  // Manual SSID entry in the setup page is the only path now, not a
  // fallback for a rarer case, so there's no reason to bring STA up here
  // at all — also sidesteps the spurious "NO_AP_FOUND" reconnect-retry
  // noise WIFI_AP_STA caused by re-enabling the STA radio with stale
  // auto-reconnect credentials from an earlier session.
  g_apSsid = apSsid();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(g_apSsid.c_str());
  delay(100);

  LOG_I("provisioning", "AP \"%s\" up, mode=%d, softAPIP=%s\n", g_apSsid.c_str(), WiFi.getMode(),
                WiFi.softAPIP().toString().c_str());

  dnsServer.start(53, "*", WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleNotFound);
  server.begin();

  showWaitingScreen(g_apSsid);

  uint32_t lastTick = millis();
  while (!g_done) {
    dnsServer.processNextRequest();
    server.handleClient();

    const uint32_t now = millis();
    lv_tick_inc(now - lastTick);
    lastTick = now;
    lv_timer_handler();

    delay(2);
  }

  // Let the success screen sit for a couple of seconds before the caller
  // reboots into station mode, so it's not just a flash on screen.
  const uint32_t successStart = millis();
  while (millis() - successStart < 2000) {
    const uint32_t now = millis();
    lv_tick_inc(now - lastTick);
    lastTick = now;
    lv_timer_handler();
    delay(5);
  }

  server.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
}
