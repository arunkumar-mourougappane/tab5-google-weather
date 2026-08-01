# v0.1.0 — Phase 1: Dashboard

First release. A M5Stack Tab5 boots straight into a self-updating weather
dashboard after a one-time setup over its own Wi-Fi access point — no
laptop or serial cable needed once it's flashed.

## Highlights

- **Dashboard screen, live on hardware.** Current conditions, an 8-hour
  strip chart, and a 7-day outlook, matching
  [docs/mockups/dashboard.html](docs/mockups/dashboard.html). Refreshes
  itself every 10 minutes; a failed refresh just retries next cycle and
  leaves the last good data on screen rather than showing an error.
- **First-run provisioning**, no compile-time secrets. The device opens
  its own AP + setup page on first boot, collects Wi-Fi credentials, a
  city/ZIP, and a Google Cloud API key, then reboots into normal
  operation. Nothing is hardcoded into firmware.
- **Location and timezone resolved automatically.** A person types "City,
  ST" or a ZIP; the Geocoding API turns that into lat/lon plus a
  display-friendly city/state, and the Time Zone API resolves the
  dashboard clock's UTC offset — no on-device IANA/POSIX timezone table.
- **Instrument-panel UI at native resolution.** LVGL over M5GFX, custom
  IBM Plex Mono / Archivo bitmap fonts generated per element
  (`tools/gen_dashboard_fonts.sh`), sized against the panel's real
  1280×720 — not scaled from the mockup or from a generic DPI guess.
- **Test suite for every API response shape.** `test/test_geocode`,
  `test/test_timezone`, `test/test_weather` exercise the exact JSON
  filters and arena sizes production code uses, runnable natively
  (`pio test -e native`) or on real hardware (`pio test -e tab5-test`).
  Wired into CI alongside the firmware build.

## What's included

- [x] Display + touch (LVGL/M5GFX), confirmed on real hardware
- [x] First-run Wi-Fi/location/API-key provisioning
- [x] Google Weather API client — current conditions, hourly, daily
- [x] Geocoding-based location resolution (city/state, not just lat/lon)
- [x] Time Zone API-based dashboard clock
- [x] Dashboard screen wired to live data, self-refreshing
- [x] Parser test suite + CI (build, native tests, on-device test compile check)

See [README.md](README.md#status) for the full, currently-maintained
checklist.

## Known limitations

- **Dashboard doesn't navigate anywhere yet.** The mockups' tap-through
  Hourly-detail and full 7-Day-forecast screens aren't built — Dashboard
  is a dead end for now.
- **No Severe alert screen.**
- **No weather condition icons** — conditions render as text, with a
  placeholder glyph standing in for a future icon set (see
  [docs/rendering.md](docs/rendering.md)).
- **Timezone offset is a snapshot, not a rule set.** A device with long
  enough uptime to cross a DST transition without rebooting keeps
  showing the pre-transition offset until the next re-provision (see
  [docs/google-weather-api.md](docs/google-weather-api.md)).
- **`WiFi.scanNetworks()` is unavailable** on the currently-pinned
  platform version — provisioning uses manual SSID entry only, by design
  (see [docs/platformio-and-ci.md](docs/platformio-and-ci.md)).

## Hardware

[M5Stack Tab5](https://shop.m5stack.com/products/m5stack-tab5-iot-development-kit-esp32-p4)
only — ESP32-P4, ESP32-C6 Wi-Fi co-processor, 32MB PSRAM, 16MB flash, 5"
1280×720 touchscreen. Board-specific pin/driver details in
[docs/hardware.md](docs/hardware.md); nothing here is written to be
portable to other ESP32 boards.

## Getting started

```sh
python3.12 -m venv .venv && source .venv/bin/activate
pip install --upgrade platformio pyyaml

pio run -e tab5 -t upload    # flash over USB
pio device monitor           # watch it boot
```

On first boot the device opens a Wi-Fi access point of its own — connect
to it from a phone or laptop, and the setup page walks through Wi-Fi,
location, and a
[Google Cloud API key](docs/google-weather-api.md#auth) with the Weather,
Geocoding, and Time Zone APIs enabled. Full detail in
[README.md](README.md).

## Under the hood, worth knowing about

A few hardware-confirmed findings from getting here, each documented in
full where linked:

- The pinned `pioarduino` platform version
  (`docs/platformio-and-ci.md`) is a deliberate three-way tradeoff
  between a WiFi-scan bug, a MIPI-DSI backlight flicker regression, and a
  severe esp-hosted SDIO/DMA-memory crash — not an arbitrary pin.
- ArduinoJson's internal memory-pool growth needed several rounds of
  arena-size correction across three different build targets (32-bit
  ESP32 hardware, 64-bit native on macOS/arm64, 64-bit native on Linux/
  x86_64 in CI) — each genuinely needs a different minimum, not just
  "native vs. hardware" as originally assumed
  (`docs/firmware-architecture.md`).
- A hardware `Stack protection fault` traced to LVGL's RLE glyph
  decompression needing more of `uiTask`'s stack than its original 8KB
  allocation once the hero temperature font grew — fixed by doubling the
  stack, not shrinking the font (`docs/rendering.md`).

## Full changelog

[`e101b49...c366171`](https://github.com/arunkumar-mourougappane/tab5-google-weather/compare/e101b49...c366171)
