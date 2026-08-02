# Research

Component research gathered before writing any firmware code, so build decisions
(board config, WiFi stack, rendering approach, API client) are made against
verified facts rather than assumptions.

- [hardware.md](hardware.md) — M5Stack Tab5 board: SoC, display/touch, WiFi co-processor, memory, I/O
- [google-weather-api.md](google-weather-api.md) — endpoints, auth, response schema, quota/pricing
- [environmental-apis.md](environmental-apis.md) — Air Quality API + Pollen API, researched for the Current-conditions-detail screen (mockups/current-detail.html), not yet implemented in firmware
- [feels-like-formulas.md](feels-like-formulas.md) — NWS Heat Index + Wind Chill formulas behind the Current-conditions-detail screen's dynamic "feels like" card
- [power-management.md](power-management.md) — backlight control, brownout/power-loss behavior, ESP32-P4 sleep modes; research started, nothing implemented yet
- [rendering.md](rendering.md) — LVGL + M5GFX integration approach for fast GUI response
- [platformio-and-ci.md](platformio-and-ci.md) — PlatformIO board config and GitHub Actions build
- [firmware-architecture.md](firmware-architecture.md) — module boundaries, stack-vs-heap JSON parsing, and a dual-core task-split proposal
- [roadmap.md](roadmap.md) — done/not-done checklist

See [mockups/](mockups/) for the UI screen concepts (open `mockups/index.html`).
