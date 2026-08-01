# Tab5 Google Weather

[![Build](https://github.com/arunkumar-mourougappane/tab5-google-weather/actions/workflows/build.yml/badge.svg)](https://github.com/arunkumar-mourougappane/tab5-google-weather/actions/workflows/build.yml)
[![Tag](https://img.shields.io/github/v/tag/arunkumar-mourougappane/tab5-google-weather?label=tag&sort=semver)](https://github.com/arunkumar-mourougappane/tab5-google-weather/tags)
[![Platform](https://img.shields.io/badge/platform-PlatformIO-orange?logo=platformio&logoColor=white)](https://platformio.org)
[![Board](https://img.shields.io/badge/board-M5Stack%20Tab5%20%C2%B7%20ESP32--P4-3DA639?logo=espressif&logoColor=white)](https://docs.m5stack.com/en/core/Tab5)

A weather dashboard for the [M5Stack Tab5](https://docs.m5stack.com/en/core/Tab5) (ESP32-P4, 5" 1280×720 touchscreen), built on PlatformIO and powered by the [Google Weather API](https://developers.google.com/maps/documentation/weather/overview). Instrument-panel UI, LVGL for fast touch response, first-run Wi-Fi/location/API-key setup over the device's own access point — no laptop or serial cable required after initial flashing.

**Mockups (live):** [arunkumar-mourougappane.github.io/tab5-google-weather](https://arunkumar-mourougappane.github.io/tab5-google-weather/)

## Status

Phase 1 dashboard is live on real hardware — the always-on Dashboard screen
(current conditions, 8-hour strip, 7-day outlook) renders real data and
refreshes itself every 10 minutes. Location and timezone are resolved
automatically from whatever "City, ST" or ZIP a person enters during
first-run setup. The three detail/alert screens from the mockups (tap-through
Hourly, full 7-Day, Severe alert) aren't wired up yet — Dashboard doesn't
navigate anywhere yet.

Shipped as [v0.1.0](https://github.com/arunkumar-mourougappane/tab5-google-weather/releases/tag/v0.1.0)
([RELEASE_NOTES.md](RELEASE_NOTES.md)). Full done/not-done checklist: [docs/roadmap.md](docs/roadmap.md).

## Building

Requires **Python 3.10–3.13** (the pinned `pioarduino` platform rejects newer Pythons — see [docs/platformio-and-ci.md](docs/platformio-and-ci.md) if `python3` on your system is newer).

```sh
python3.12 -m venv .venv && source .venv/bin/activate
pip install --upgrade platformio pyyaml

pio run -e tab5              # build
pio run -e tab5 -t upload    # flash over USB
pio device monitor           # serial console
```

### Testing

The API response parsers (`geocode_parse.h`/`weather_parse.h`/`timezone_parse.h`)
have a Unity test suite under `test/`, runnable two ways:

```sh
pio test -e native      # fast, no board needed - filter/field-mapping sanity check
pio test -e tab5-test   # authoritative - builds and runs on a real Tab5 over USB
```

See [docs/platformio-and-ci.md](docs/platformio-and-ci.md) for why both exist
(native's 64-bit host needs different JSON arena sizes than the real 32-bit
target, so a native pass alone doesn't prove the on-device size is right).

## Screens

|                            |                                                                                                          |
|----------------------------|----------------------------------------------------------------------------------------------------------|
| **Dashboard**               | Current conditions, 8-hour strip, 7-day outlook — the always-on default                                  |
| **Hourly detail**          | 16-hour temperature/precipitation chart                                                                  |
| **7-day forecast**         | Full week, day/night split per row, sunrise/sunset/moon phase                                            |
| **Boot / sync / offline**  | Wi-Fi connect, first fetch, and a graceful fallback to stale data when the network or API is unreachable |
| **Severe alert**           | Alert banner over the dimmed dashboard, not a full-screen takeover                                       |
| **First-run provisioning** | The device's own access point + setup screen, and the phone-facing setup page it serves                  |

Open [docs/mockups/index.html](docs/mockups/index.html) locally, or browse the [live gallery](https://arunkumar-mourougappane.github.io/tab5-google-weather/) — every screen is interactive, including a day/night preview toggle.

## Hardware

[M5Stack Tab5](https://shop.m5stack.com/products/m5stack-tab5-iot-development-kit-esp32-p4): ESP32-P4 (no onboard radio — Wi-Fi comes from an ESP32-C6 co-processor over SDIO), 32 MB PSRAM, 16 MB flash, 5" 1280×720 IPS touchscreen. Details in [docs/hardware.md](docs/hardware.md).

## Documentation

- [docs/hardware.md](docs/hardware.md) — SoC, Wi-Fi co-processor, display/touch, known gotchas
- [docs/google-weather-api.md](docs/google-weather-api.md) — endpoints, auth, response schema, quota/pricing, location/provisioning flow
- [docs/rendering.md](docs/rendering.md) — LVGL + M5GFX approach for fast GUI response
- [docs/platformio-and-ci.md](docs/platformio-and-ci.md) — board config and GitHub Actions build
- [docs/firmware-architecture.md](docs/firmware-architecture.md) — module boundaries, stack-vs-heap JSON parsing, and the implemented dual-core (`uiTask`/`netTask`) task split
- [docs/roadmap.md](docs/roadmap.md) — done/not-done checklist

## License

[MIT](LICENSE)
