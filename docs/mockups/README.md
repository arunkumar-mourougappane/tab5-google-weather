# UI mockups

Static, interactive HTML mockups of the Tab5 weather kiosk UI — open
[index.html](index.html) in a browser for the gallery, or any screen directly.
Not firmware; these are the design reference the LVGL implementation follows
(see [../rendering.md](../rendering.md)).

| Screen | File | Covers |
|---|---|---|
| 1. Dashboard | `dashboard.html` | Default always-on screen |
| 2. Hourly detail | `hourly-detail.html` | 16-hour chart, drilled into from the dashboard |
| 3. 7-day forecast | `daily-forecast.html` | Full week, day/night split per row |
| 4. Boot / sync / offline | `status.html` | Connecting, first fetch, offline fallback to stale data |
| 5. Severe alert | `alert.html` | Alert banner over the dimmed dashboard |
| 6. First-run provisioning | `provisioning.html` | The device's own AP + setup screen, and the phone-facing setup page it serves |

All screens share `assets/style.css` and the two embedded typefaces in
`assets/fonts/` (Archivo for display type, IBM Plex Mono for all numeric/data
readouts) — the "instrument panel" design system: brass/teal/ember accents on
a warm parchment (day) or graphite (night) ground. Each screen has a Day/Night
toggle that previews the kiosk's own auto-dim behavior, independent of your
browser's OS theme.

Real content throughout (Bellevue, WA sample data shaped like actual
`weather.googleapis.com` responses — see [../google-weather-api.md](../google-weather-api.md))
rather than placeholder lorem ipsum.
