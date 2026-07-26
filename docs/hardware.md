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
application code (Arduino `WiFi.h`) it behaves like a normal on-chip WiFi stack
*once the link is up*.

**Corrected after testing on real hardware** — this section previously claimed
`WiFi.begin(ssid, pass)` "works unmodified" out of the box on the
`esp32-p4-evboard` board target. It doesn't. That board profile is generic (the
Espressif P4 EV-board reference design, not Tab5-specific) and doesn't know
which GPIOs Tab5 actually wires to the C6's SDIO bus. Without telling it,
every `WiFi.mode()`/`begin()`/`softAP()` call fails outright — observed as a
repeating `sdmmc_init_ocr: send_op_cond (1) returned 0x107` /
`ensure_slave_bus_ready failed` / `ESP-Hosted link not yet up` cascade in the
serial log, ending in `esp_wifi_init: ESP_FAIL`. (That `sdmmc`/`sdio_wrapper`
naming is just the driver stack `esp-hosted` reuses from SD-card support —
nothing to do with an actual SD card or the Tab5's card slot.) Fix, per
M5Stack's own Tab5 WiFi docs, called once before any `WiFi.*` use in either
boot path (`configureWifiPins()` in `src/main.cpp`):

```cpp
WiFi.setPins(/*clk=*/GPIO_NUM_12, /*cmd=*/GPIO_NUM_13, /*d0=*/GPIO_NUM_11,
             /*d1=*/GPIO_NUM_10, /*d2=*/GPIO_NUM_9, /*d3=*/GPIO_NUM_8,
             /*rst=*/GPIO_NUM_15);
```

Compiles clean against this project's `WiFi@3.3.5`; not yet confirmed this
alone is sufficient on our unit (community reports on the same board profile
are mixed — some needed this and nothing else, some report it still not
working and had to rebuild `arduino-esp32` via Espressif's online Library
Builder with Tab5-specific SDIO config baked in). If `setPins()` doesn't
resolve it, that heavier rebuild — or restoring the C6's factory `esp-hosted`
firmware via M5Stack's
[recovery tool](https://docs.m5stack.com/en/guide/restore_factory/m5tab5_c6_wifi)
in case it's corrupted/mismatched rather than a host-side pin issue — are the
next things to try, in that order.

Two other things worth knowing before debugging WiFi issues on this hardware:

- The C6 runs its own **`esp_hosted` firmware image**, flashed to a dedicated
  partition, versioned independently of the app. Known-bad `esp_hosted` versions
  (e.g. v2.3.0 on some other P4 boards) have caused WiFi to drop after ~4 minutes;
  if we see periodic disconnects, that's the first thing to check, not our own code.
- SDIO is a single shared channel (40 MHz + HT20 tops out around ~36 Mbps
  real-world) multiplexed between WiFi data and RPC control traffic. Plenty for
  a weather API poll every few minutes; not a channel to worry about saturating.
- **Concurrent AP+STA scan is flaky.** Provisioning's network picker
  (`scanNetworksOnce()` in `src/provisioning.cpp`) originally called
  `WiFi.scanNetworks()` on demand, with the softAP already up and actively
  serving the setup page. On real hardware this was unreliable — the exact
  same device returned an empty result once, then `WIFI_SCAN_FAILED` twice in
  a row on the next two attempts, no pattern to it. Read as contention for
  the C6's radio/SDIO link between AP beaconing/HTTP traffic and a concurrent
  STA scan, not a real "zero networks in range" (that shared-channel
  multiplexing above is presumably part of it too, though we didn't isolate
  which contended resource specifically). Worked around, not root-caused:
  scan once, in plain STA mode, *before* `WiFi.softAP()` is ever called, and
  serve that cached result for the rest of the provisioning session rather
  than rescanning per request. The setup page also grew a manual "type the
  SSID" fallback field for the same reason — a picker that depends on a scan
  we've already seen fail intermittently shouldn't be the only way in.

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
- [m5-docs: Tab5 WiFi (Arduino)](https://docs.m5stack.com/en/arduino/m5tab5/wifi) — the `setPins()` call and GPIO numbers
- [M5Stack community: Using WiFi on Tab5 with the Arduino IDE](https://community.m5stack.com/topic/7576/using-wifi-on-tab5-with-the-arduino-ide) — mixed reports on whether `setPins()` alone is sufficient
- [m5-docs: Tab5 ESP32-C6 WiFi module factory firmware restore](https://docs.m5stack.com/en/guide/restore_factory/m5tab5_c6_wifi) — fallback if the C6's `esp-hosted` firmware itself is the problem
