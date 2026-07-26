# Google Weather API

Part of Google Maps Platform (launched 2025). REST, API-key auth, JSON responses.
Base host: `weather.googleapis.com`, all requests are `GET .../v1/<resource>:lookup`.

## Auth

Query param `key=YOUR_API_KEY`. Standard Google Cloud API key from a project with
billing enabled and the Weather API turned on.

**Device-specific concern:** Google Cloud API key restrictions are designed for
servers (IP allowlist) or browsers (HTTP referrer allowlist) — neither fits a
device on a residential/DHCP IP with no referrer header. Practical options, worst
to best:
1. Store the key unrestricted. Works, but a pulled-apart device, a flash dump,
   or a packet capture leaks a live-billing key.
2. Restrict the key to the Weather API only (no referrer/IP restriction) — caps
   the blast radius to weather quota abuse, not other Google APIs.
3. Proxy through a small server you control that holds the real key — device
   never sees it. Out of scope for v1 but the right answer if this ships beyond
   personal use.

We'll go with **(2)** for v1 and document (3) as a follow-up in the README.

The key itself isn't compiled into firmware — it's entered once through the
on-device provisioning flow (device opens its own AP + setup page on first
boot; mockup: [mockups/provisioning.html](mockups/provisioning.html),
implemented in `src/provisioning.cpp`) and stored in flash (NVS, via
`ConfigStore` in `src/config_store.cpp`), the same as the WiFi credentials and
location below. That supersedes an earlier plan to hardcode everything in a
`secrets.h` at build time — compile-time secrets don't let a non-developer
set this device up, and provisioning firmware is barely more work than a
captive-portal WiFi setup alone, which the device needs regardless.

## Endpoints used

### Current conditions
`GET /v1/currentConditions:lookup?key=...&location.latitude=...&location.longitude=...&unitsSystem=IMPERIAL`

Key response fields: `currentTime`, `timeZone`, `isDaytime`, `weatherCondition`
(type + description + icon URI), `temperature`, `feelsLikeTemperature`, `dewPoint`,
`heatIndex`, `windChill`, `relativeHumidity`, `uvIndex`, `precipitation`
(probability % + type + qpf amount), `thunderstormProbability`, `airPressure`,
`wind` (direction deg/cardinal, speed, gust), `visibility`, `cloudCover`.

### Hourly forecast
`GET /v1/forecast/hours:lookup?key=...&location.latitude=...&location.longitude=...&hours=8&pageSize=24`

Up to 240 hours available; we only need the next ~8 for the hourly strip, so
`hours=8` keeps the response (and parse time) small. Returns a `forecastHours[]`
array — same per-hour field shape as current conditions, plus `interval`
(start/end) and `displayDateTime`. Paginated via `pageToken`/`nextPageToken`,
irrelevant at `hours=8` (fits in one page).

### Daily forecast
`GET /v1/forecast/days:lookup?key=...&location.latitude=...&location.longitude=...&days=7&pageSize=7`

Up to 10 days available; we show 7. Returns `forecastDays[]`, each with
`maxTemperature`/`minTemperature`, `feelsLikeMax/MinTemperature`, a
`daytimeForecast`/`nighttimeForecast` split (weather condition, humidity, UV,
precip probability/qpf, wind, cloud cover independently for day vs. night), plus
`sunEvents` (sunrise/sunset) and `moonEvents`.

## Units

No separate unit conversion needed — `unitsSystem=IMPERIAL` (or omit for metric)
returns temperature in °F, wind in mph, etc. directly, so the firmware just
formats the numbers it's given.

## Location

The Weather API takes raw lat/lon only — no geocoding built in, and the Tab5
has no GPS. The provisioning page (mockup:
[mockups/provisioning.html](mockups/provisioning.html), implementation:
`src/provisioning.cpp` + `include/provisioning_page.h`) collects a "City or
ZIP" string instead of asking for coordinates, since that's what a person
actually knows.

That string still has to become a lat/lon somehow. The device is in AP mode
(not yet on the internet) while the setup form is being filled out, so
geocoding can't happen until *after* it joins the home WiFi with the
credentials just given to it. Concretely: save WiFi + city text + API key →
join WiFi (`connectWifiOrRetryBoot()` in `src/main.cpp`) → **geocode the city
text once** (`geocodeLocation()` in `src/geocode.cpp`, calling Google's
[Geocoding API](https://developers.google.com/maps/documentation/geocoding) —
a separate Maps Platform API from Weather, needs its own enablement, shares
free tier terms) → store the resulting lat/lon in NVS via `ConfigStore`
(`src/config_store.cpp`) → start polling `weather.googleapis.com` with that.
Not re-geocoded on every refresh, only once (`ConfigStore::hasLocation()`
gates it). A location change means re-running setup — the right tradeoff for
something wall-mounted, not portable.

## Quota / pricing

- Free tier: 10,000 calls/SKU/month (current conditions, hourly, and daily each
  count as separate SKUs).
- $0.15 per 1,000 calls beyond that.
- Default project quota: 6,000 queries/minute — irrelevant at our poll rate.

At a refresh interval of, say, every 10 minutes, that's 3 endpoints × 144
calls/day × 30 = ~13,000 calls/month total across all three SKUs — comfortably
inside the free tier per-SKU, but worth picking a refresh interval deliberately
rather than polling aggressively "for freshness." A 10–15 minute interval is
already far more frequent than the underlying forecast data changes.

## Sources

- [Weather API overview](https://developers.google.com/maps/documentation/weather/overview)
- [Get current conditions](https://developers.google.com/maps/documentation/weather/current-conditions)
- [Get hourly forecast](https://developers.google.com/maps/documentation/weather/hourly-forecast)
- [Get daily forecast](https://developers.google.com/maps/documentation/weather/daily-forecast)
- [Weather API usage and billing](https://developers.google.com/maps/documentation/weather/usage-and-billing)
