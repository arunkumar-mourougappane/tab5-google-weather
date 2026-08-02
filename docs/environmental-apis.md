# Air Quality API & Pollen API

Research for the planned Current-conditions-detail screen (mockup:
[mockups/current-detail.html](mockups/current-detail.html)), which needs two
data points Google's Weather API doesn't provide: air quality (AQI +
pollutants) and pollen counts. Both are separate Google Maps Platform APIs —
their own enablement, their own key restriction, their own quota, same as
Geocoding/Time Zone already are relative to Weather (see
[google-weather-api.md](google-weather-api.md#location)). Not yet
implemented in firmware — this is the research pass before that, same
sequencing this project used for Weather itself.

Pressure and visibility, by contrast, are **already available** — the
Weather API's `currentConditions:lookup` returns both (`airPressure`,
`visibility`), just not currently read by `weather.cpp`'s filter (see
[google-weather-api.md](google-weather-api.md#current-conditions)'s field
list). Adding those to the detail screen is a `weather_parse.h` filter
change, not a new API integration.

## Air Quality API

`POST https://airquality.googleapis.com/v1/currentConditions:lookup?key=...`

Unlike Weather/Geocoding/Pollen (all `GET` with query params), this is a
**POST with a JSON body** — a real implementation difference, not just a
docs quirk, that `src/weather.cpp`'s existing `getJson()` helper (built
around `GET`) won't cover as-is if/when this becomes a real client.

Request body:
```json
{
  "location": {"latitude": 47.61, "longitude": -122.20},
  "universalAqi": true,
  "extraComputations": ["HEALTH_RECOMMENDATIONS", "POLLUTANT_CONCENTRATION"],
  "languageCode": "en"
}
```
`extraComputations` gates most of the response's own richness — omit it and
Air Quality API. Fields actually worth reading for this screen:
- `indexes[].aqi` (0–500) + `indexes[].category` (e.g. "Moderate air
  quality") + `indexes[].color` (RGB, matches the category — the API
  computes this, no local color-mapping table needed) — the `"uaqi"`
  (Universal AQI) entry is the one to display; `extraComputations:
  ["LOCAL_AQI"]` would add a second, region-specific index (e.g. US EPA)
  most users won't recognize, not planned to be shown.
- `indexes[].dominantPollutant` (e.g. `"pm25"`) — worth a one-line "Main:
  PM2.5" readout, matching the mockup's own treatment.
- `pollutants[]` (only present with `extraComputations:
  ["POLLUTANT_CONCENTRATION"]`) — `code`/`displayName`/`concentration.value`
  + `.units`. Not planned to be shown individually on a kiosk-sized screen
  (7 pollutant readouts is Environment-agency-dashboard territory, not
  glanceable); the single AQI number + category + dominant pollutant is
  the whole point of an index in the first place.
- `healthRecommendations` — long-form text per population group (general/
  elderly/children/...), clearly meant for an app with room to scroll, not
  a fixed-size kiosk panel. Not planned to be shown.

### Pricing

Free tier: 10,000 calls/month. Beyond that, ~$5/1,000 calls. Same "one
SKU per distinct endpoint" model as Weather — a fourth SKU on this
project's refresh loop.

## Pollen API

`GET https://pollen.googleapis.com/v1/forecast:lookup?key=...&location.latitude=...&location.longitude=...&days=1`

`GET` with query params, matching this project's existing `getJson()`
pattern exactly (unlike Air Quality above) — the more natural of the two to
add first if only one ships initially.

`days` maxes out at **5** (not 7 — this API's own forecast horizon is
shorter than Weather's daily endpoint). `plantsDescription` (bool, default
`true`) controls whether the response includes per-plant detail
(`plantInfo[]`: family, season, cross-reactions, reference photos) — worth
setting to `false` for this project, since none of that is shown on a
kiosk panel and it's pure response-size/parse-time cost otherwise (same
"filter to what's actually read" discipline `weather_parse.h` already
applies everywhere else).

Response shape (`dailyInfo[0]` — only today matters for this screen):
- `pollenTypeInfo[]` — up to 3 entries, one each for `"GRASS"`, `"TREE"`,
  `"WEED"` (a location without a given type in season may omit that
  entry entirely, not return it as zero — worth defaulting to "not in
  season" display, not `0`, when an entry is missing).
- Each entry's `indexInfo`: `value` (0–5, the **Universal Pollen Index**),
  `category` (e.g. "Low", "Moderate"), `color` (RGB, same "API computes
  it" convenience Air Quality's index has), `indexDescription`
  (human-readable sentence). **`indexInfo` itself is omitted** (not
  zeroed) when a type is out of season and its count is negligible — a
  real "field absent vs. field zero" distinction worth handling
  explicitly if this becomes a real parser, same class of gotcha this
  project already tracks for other optional fields.
- `healthRecommendations` (per pollen type, string array) — same "too
  long-form for a kiosk tile" call as Air Quality's; not planned to be
  shown.

UPI scale (0–5, not the AQI's 0–500):

| Value | Category |
|---|---|
| 0 | None |
| 1 | Very Low |
| 2 | Low |
| 3 | Moderate |
| 4 | High |
| 5 | Very High |

### Pricing

Free tier: 5,000 calls/month — half Weather/Air-Quality's 10,000, and
Pollen is billed as a "Pro" SKU (~$10/1,000 beyond free tier, vs. Air
Quality's ~$5/1,000). At this project's existing ~10-minute refresh
cadence, a naive "poll every endpoint every cycle" approach would burn
through Pollen's free tier fastest of the four APIs in play — pollen
counts don't change meaningfully hour-to-hour, so this is the strongest
candidate for a **much longer refresh interval** than the other three
endpoints (e.g. once every few hours, not every 10 minutes) once actually
implemented, not a decision to make silently at implementation time.

## Open questions for the implementation pass (not yet answered)

- Air Quality's `POST`-with-body shape needs either a small addition to
  `getJson()`'s signature or a parallel `postJson()` — not designed here,
  flagging it since it's the one real code-shape difference from every
  API this project has integrated so far.
- Whether both APIs get their own refresh cadence (per the Pollen
  reasoning above) or just piggyback on the existing dashboard-snapshot
  refresh loop at a coarser multiple (e.g. "fetch every Nth dashboard
  refresh") — the latter avoids a second concurrent task/timer, matching
  this project's existing single-`netTask` architecture
  ([firmware-architecture.md](firmware-architecture.md)), but hasn't been
  scoped out.
- Both need their own `ConfigStore`-gated API-key-restriction step in
  provisioning docs/UI, same as Weather's own restricted-key guidance
  ([google-weather-api.md](google-weather-api.md#auth)).

## Sources

- [Air Quality API — Current conditions](https://developers.google.com/maps/documentation/air-quality/current-conditions)
- [Air Quality API — usage and billing](https://developers.google.com/maps/documentation/air-quality/usage-and-billing)
- [Pollen API — Forecast](https://developers.google.com/maps/documentation/pollen/forecast)
- [Pollen API — usage and billing](https://developers.google.com/maps/documentation/pollen/usage-and-billing)
- [Weather API — Get current conditions](https://developers.google.com/maps/documentation/weather/current-conditions) (pressure/visibility field shapes)
