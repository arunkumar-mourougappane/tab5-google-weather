# Roadmap

Status checklist, moved out of [README.md](../README.md) so the README
stays a pitch/quickstart and this stays the living list of what's done and
what's left. Update this file as work lands or new gaps are found — it's
the one place "what's next" should live, not scattered across doc TODOs.

## Done (v0.1.0 — Phase 1: Dashboard)

- [x] Hardware, API, rendering, and build-tooling research ([../docs/](.))
- [x] UI mockups for all six screens ([mockups/](mockups/))
- [x] PlatformIO project scaffold ([../platformio.ini](../platformio.ini))
- [x] GitHub Actions CI — firmware build, native parser tests, on-device test compile check ([../.github/workflows/build.yml](../.github/workflows/build.yml))
- [x] Display + touch (LVGL over M5GFX) — confirmed on a real Tab5 ([rendering.md](rendering.md) for the rotation/color-format gotchas)
- [x] First-run provisioning (AP + setup web page) — confirmed end-to-end on real hardware, including WiFi network scanning (see [hardware.md](hardware.md) for the platform-version fix that took)
- [x] Google Weather API client (current conditions + hourly + daily) — [../src/weather.cpp](../src/weather.cpp), confirmed on hardware
- [x] Location resolution — Geocoding API turns the setup-time "City, ST"/ZIP into lat/lon + a display-friendly city/state ([google-weather-api.md](google-weather-api.md))
- [x] Timezone resolution — Time Zone API resolves the dashboard clock's UTC offset, applied at display time only
- [x] Dashboard screen wired to live data, self-refreshing every 10 minutes — confirmed on hardware ([../src/dashboard_ui.cpp](../src/dashboard_ui.cpp))
- [x] Parser test suite (`test/`) for every API response shape, runnable natively or on-device ([platformio-and-ci.md](platformio-and-ci.md))

See [../RELEASE_NOTES.md](../RELEASE_NOTES.md) for the full v0.1.0 writeup.

## Done (since v0.1.0, not yet in a tagged release)

- [x] Weather condition icons — 15-icon set (sun/mostly_clear/partly_cloudy/cloud/night_cloudy/windy/light_rain/rain/heavy_rain/snow/sleet/hail/thunderstorm/fog/moon; only sun/cloud/rain/moon come from the mockups, the other 11 are original designs added in two accuracy passes) on the Dashboard's hero glyph and each 7-day row; see [rendering.md](rendering.md#icons) for the generation pipeline and `include/weather_icon.h` for the bucketing logic. Static only — animated icons deliberately deferred, see the same doc section. Not yet split further: heavy snow/blizzard still renders as plain Snow (see rendering.md#icons's note).
- [x] Tap-through Hourly-detail screen — the Dashboard's first real navigation target: tapping "HOURLY" pushes to a 16-hour temperature line + precipitation-chance bars (two overlaid `lv_chart`s, independent Y axes) + a 2×8 grid of hour cards (real condition icons, day/night-aware), "← Dashboard" returns, both buttons animate on press; see [../src/hourly_detail_ui.cpp](../src/hourly_detail_ui.cpp). `kMaxHourlyPoints` grew 8→16 to feed it (the Dashboard's own compact strip stays capped at 8 via a separate `kMaxDashboardHourCells`, decoupled on purpose). No gradient area fill under the temperature line yet — scoped out, see that file's own header comment.

## Not yet done

Screens / UI:

- [ ] Gradient area fill under the Hourly-detail chart's temperature line — the mockup renders this as an SVG `linearGradient` under the polyline; `lv_chart` has no built-in line-series fill, so it needs a custom `LV_EVENT_DRAW_MAIN_END` draw handler to paint it manually (`src/hourly_detail_ui.cpp`). Pure visual flourish, no information the line/points don't already carry — scoped out for a later pass.
- [ ] Full 7-Day-forecast screen (day/night split per row, sunrise/sunset/moon phase, per the mockup)
- [ ] Severe alert screen — depends on whether Google's Weather API actually surfaces alerts for a given location; not yet checked
- [ ] Animated weather condition icons — architecture supports it (see [rendering.md](rendering.md#icons)), but needs hand-authored animation frames that don't exist yet
- [ ] Richer offline/stale-data UX on the boot/status screen (mockup: [mockups/status.html](mockups/status.html)) — today a failed refresh just silently retries next cycle; the mockup's fuller "last synced Xm ago, retrying" treatment isn't built

Data / correctness:

- [ ] Periodic timezone re-check — the resolved UTC offset is a snapshot from provisioning time, not a rule set; a device with long enough uptime to cross a DST transition without rebooting keeps showing the pre-transition offset until the next re-provision (see [google-weather-api.md](google-weather-api.md#timezone), `include/timezone_lookup.h`)

Platform:

- [ ] Re-enable `WiFi.scanNetworks()` in provisioning — deterministically broken on the currently-pinned `pioarduino` platform version (chosen instead for its esp-hosted SDIO/DMA-memory fix); manual SSID entry is the only path today. Revisit if a future platform tag ever bundles both fixes (see [platformio-and-ci.md](platformio-and-ci.md))
- [ ] PPA/DMA2D hardware-accelerated fills/blends — deliberately not wired up (LVGL's own docs call it experimental with "no significant gains" in partial-redraw mode, which is what this UI uses); only worth revisiting if profiling ever shows CPU-bound fill/blend work (see [rendering.md](rendering.md#why-not-the-ppa-yet))
