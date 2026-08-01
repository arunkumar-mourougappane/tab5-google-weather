# Research

Component research gathered before writing any firmware code, so build decisions
(board config, WiFi stack, rendering approach, API client) are made against
verified facts rather than assumptions.

- [hardware.md](hardware.md) — M5Stack Tab5 board: SoC, display/touch, WiFi co-processor, memory, I/O
- [google-weather-api.md](google-weather-api.md) — endpoints, auth, response schema, quota/pricing
- [rendering.md](rendering.md) — LVGL + M5GFX integration approach for fast GUI response
- [platformio-and-ci.md](platformio-and-ci.md) — PlatformIO board config and GitHub Actions build
- [firmware-architecture.md](firmware-architecture.md) — module boundaries, stack-vs-heap JSON parsing, and a dual-core task-split proposal
- [roadmap.md](roadmap.md) — done/not-done checklist

See [mockups/](mockups/) for the UI screen concepts (open `mockups/index.html`).
