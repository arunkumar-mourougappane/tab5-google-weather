#include "provisioning.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <lvgl.h>
#include <qrcode.h>

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

// Populated once, in plain STA mode, before the AP starts — see
// scanNetworksOnce() below for why. handleScan() just serves this.
String g_scanResultsJson = "[]";

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

  const int scale = 4;
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

void showWaitingScreen(const String &ssid) {
  g_waitingScreen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(g_waitingScreen, lv_color_hex(0x121417), 0);
  lv_obj_set_style_bg_opa(g_waitingScreen, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(g_waitingScreen, 0, 0);
  lv_obj_set_style_border_width(g_waitingScreen, 0, 0);

  makeLabel(g_waitingScreen, "TAB5 - WEATHER", 0xA89E8C, &lv_font_montserrat_14);
  lv_obj_align(lv_obj_get_child(g_waitingScreen, -1), LV_ALIGN_TOP_MID, 0, 20);

  lv_obj_t *title = makeLabel(g_waitingScreen, "Let's get you set up", 0xF0E9D8, &lv_font_montserrat_28);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 46);

  lv_obj_t *qr = buildQrCanvas(g_waitingScreen, ssid);
  lv_obj_align(qr, LV_ALIGN_LEFT_MID, 90, 10);

  char info[256];
  snprintf(info, sizeof(info),
           "Connect a phone or laptop to \"%s\"\n(open network, no password) or scan the code.\n\n"
           "A setup page should open automatically.\nIf not, visit 192.168.4.1",
           ssid.c_str());
  lv_obj_t *steps = makeLabel(g_waitingScreen, info, 0xF0E9D8, &lv_font_montserrat_14);
  lv_obj_set_style_text_line_space(steps, 8, 0);
  lv_obj_align(steps, LV_ALIGN_LEFT_MID, 420, 10);

  lv_obj_t *waiting = makeLabel(g_waitingScreen, "Waiting for setup...", 0xD99A4E, &lv_font_montserrat_14);
  lv_obj_align(waiting, LV_ALIGN_BOTTOM_MID, 0, -24);

  lv_screen_load(g_waitingScreen);
}

void showSuccessScreen(const String &ssid, const String &locationQuery) {
  g_successScreen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(g_successScreen, lv_color_hex(0x121417), 0);
  lv_obj_set_style_bg_opa(g_successScreen, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(g_successScreen, 0, 0);
  lv_obj_set_style_border_width(g_successScreen, 0, 0);

  lv_obj_t *title = makeLabel(g_successScreen, "Setup saved", 0xF0E9D8, &lv_font_montserrat_28);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);

  char info[192];
  snprintf(info, sizeof(info), "Joining \"%s\" and loading weather for %s...", ssid.c_str(),
           locationQuery.c_str());
  lv_obj_t *sub = makeLabel(g_successScreen, info, 0xA89E8C, &lv_font_montserrat_14);
  lv_obj_align(sub, LV_ALIGN_CENTER, 0, 20);

  lv_screen_load(g_successScreen);
}

void sendJsonError(const char *message) {
  String body = "{\"ok\":false,\"error\":\"";
  body += message;
  body += "\"}";
  server.send(400, "application/json", body);
}

void handleRoot() { server.send_P(200, "text/html", kProvisioningPageHtml); }

// Scans once, in plain STA mode, before the AP starts — not on every /scan
// request. Observed on real hardware: scanning *while* the AP is actively
// beaconing/serving HTTP is flaky (empty result one call, outright
// "failed" the next, back to back, same device) — almost certainly
// contention for the C6's radio/SDIO link between AP traffic and a
// concurrent STA scan, not a real "no networks in range." Scanning before
// the AP exists removes that contention entirely. A few retries here too,
// since even the uncontended scan wasn't perfectly reliable in testing.
void scanNetworksOnce() {
  WiFi.mode(WIFI_STA);
  // Give the radio a moment after the mode switch before the first scan —
  // confirmed on real hardware that attempt 0 came back a clean "completed,
  // 0 networks" (not a failure) immediately after WIFI_STA was set, with no
  // settling time at all beforehand.
  delay(200);

  int count = -1;
  // Retry on <= 0, not just < 0: a suspicious "completed but empty" result
  // deserves the same skepticism as an outright failure here, not just a
  // negative one — see above. Three attempts either way; a genuinely empty
  // area just costs ~600ms extra at worst.
  for (int attempt = 0; attempt < 3 && count <= 0; attempt++) {
    if (attempt > 0) delay(300);
    count = WiFi.scanNetworks();
    Serial.printf("[provisioning] scanNetworks() attempt %d -> %d\n", attempt, count);
  }

  String json = "[";
  for (int i = 0; i < count; i++) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    // ESP32-C6 (the Tab5's WiFi co-processor) is 2.4GHz-only — logging
    // channel here so a 5GHz-only target network shows up as an absence
    // here rather than a mystery, if that turns out to matter later.
    Serial.printf("[provisioning]   %2d: \"%s\" rssi=%d ch=%d\n", i, WiFi.SSID(i).c_str(),
                  WiFi.RSSI(i), WiFi.channel(i));
  }
  json += "]";
  WiFi.scanDelete();

  g_scanResultsJson = json;
}

void handleScan() { server.send(200, "application/json", g_scanResultsJson); }

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

  // Scan first, in plain STA mode, before touching AP mode at all — see
  // scanNetworksOnce() for why (concurrent AP+scan was flaky on real
  // hardware). apSsid() only needs the MAC, which is stable regardless of
  // mode, so reading it before the mode switch is fine.
  g_apSsid = apSsid();
  scanNetworksOnce();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(g_apSsid.c_str());
  delay(100);

  Serial.printf("[provisioning] AP \"%s\" up, mode=%d, softAPIP=%s\n", g_apSsid.c_str(), WiFi.getMode(),
                WiFi.softAPIP().toString().c_str());

  dnsServer.start(53, "*", WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/scan", HTTP_GET, handleScan);
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
