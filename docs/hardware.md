# M5Stack Tab5 hardware

## SoC

- **Espressif ESP32-P4** — dual-core RISC-V, 400 MHz. No built-in radio (no WiFi/BT).
- **32 MB PSRAM, 16 MB flash.**
- Has a **PPA (Pixel Processing Accelerator)** + DMA2D peripheral for hardware
  fill/blend/rotate/scale — see [rendering.md](rendering.md) for why we're not
  depending on it for v1.

## WiFi: the ESP32-C6 co-processor

The P4 has no radio, so the Tab5 carries a second chip, an **ESP32-C6**, wired to
the P4 over **SDIO**, running Espressif's `esp-hosted` firmware. The C6 acts as a
dumb WiFi/BT adapter; the P4 drives it through `esp_wifi_remote`, so from
application code (Arduino `WiFi.h`) it behaves like a normal on-chip WiFi stack —
`WiFi.begin(ssid, pass)` works unmodified. `pioarduino` (the PlatformIO fork we use,
see [platformio-and-ci.md](platformio-and-ci.md)) bundles the pieces needed for this
to work out of the box for the `esp32-p4-evboard` board target.

Two things worth knowing before debugging WiFi issues on this hardware:

- The C6 runs its own **`esp_hosted` firmware image**, flashed to a dedicated
  partition, versioned independently of the app. Known-bad `esp_hosted` versions
  (e.g. v2.3.0 on some other P4 boards) have caused WiFi to drop after ~4 minutes;
  if we see periodic disconnects, that's the first thing to check, not our own code.
- SDIO is a single shared channel (40 MHz + HT20 tops out around ~36 Mbps
  real-world) multiplexed between WiFi data and RPC control traffic. Plenty for
  a weather API poll every few minutes; not a channel to worry about saturating.

## Display

- **5" IPS TFT, 1280×720 (720p), MIPI-DSI.**
- Touch: capacitive multi-touch, currently GT911 on most shipped units, though
  M5Stack has been migrating panel revisions (ILI9881C+GT911 → integrated
  ST7123 → ST7121 display/touch driver ICs) across production runs since late
  2025. Application-level behavior (resolution, multi-touch) is unaffected;
  it's handled inside M5GFX/M5Unified, so keeping those libraries reasonably
  current is what actually matters, not chasing the panel revision ourselves.
- A known ESP-IDF 5.5.2 regression causes MIPI-DSI backlight flicker on Tab5 —
  reason we pin the `pioarduino` platform version rather than tracking latest
  (see [platformio-and-ci.md](platformio-and-ci.md)).

## Other onboard peripherals (not used by this project, noted for completeness)

- SC2356 2 MP camera (MIPI-CSI)
- ES8388 audio codec + 1 W speaker, ES7210 dual-mic array with AEC
- Removable 2000 mAh battery
- Gyro, RS485, Thread/Zigbee radio (via the C6's 802.15.4), microSD, 3.5 mm jack

## Sources

- [M5Stack Tab5 docs](https://docs.m5stack.com/en/core/Tab5)
- [M5Stack Tab5 product page](https://shop.m5stack.com/products/m5stack-tab5-iot-development-kit-esp32-p4)
- [esp-hosted-mcu: ESP32-P4 function EV board](https://github.com/espressif/esp-hosted-mcu/blob/main/docs/esp32_p4_function_ev_board.md)
- [dmatking/esp32-p4-wifi-starter](https://github.com/dmatking/esp32-p4-wifi-starter) — P4+C6 architecture writeup
- [lboshuizen/crowpanel-p4-c6-sdio-ota](https://github.com/lboshuizen/crowpanel-p4-c6-sdio-ota) — esp_hosted version/disconnect-bug notes
- [ESP-IDF PPA peripheral guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/ppa.html)
