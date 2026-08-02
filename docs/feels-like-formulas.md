# Heat Index & Wind Chill Formulas

Research behind the "feels like" card in the Current-conditions-detail
mockup (`mockups/current-detail.html`'s `feelsLikeAdjustment()`), which
picks Heat Index or Wind Chill dynamically from temperature/humidity/wind
rather than showing a fixed one. Written up here so the mockup's own inline
comments don't have to carry the full derivation, and so a future firmware
implementation has a real source to implement against instead of
re-deriving from memory.

Both are official NWS (National Weather Service) formulas — not invented
for this project — chosen because Google's Weather API already returns
`heatIndex`/`windChill` as precomputed fields on `currentConditions:lookup`
(see [google-weather-api.md](google-weather-api.md#current-conditions)),
so a real firmware implementation likely won't even need to compute these
itself. They're documented here anyway because the mockup needed *some*
formula to demonstrate the dynamic branch logic with, and matching NWS's
own math means the mockup's sample numbers are actually correct, not just
plausible-looking.

## Heat Index

Two formulas, not one — NWS only applies the complex one when it matters:

**Simple formula** (valid at any temperature — this is the one
`current-detail.html` actually uses):
```
HI = 0.5 * (T + 61.0 + ((T - 68.0) * 1.2) + (RH * 0.094))
```
where `T` is air temperature in °F, `RH` is relative humidity in percent.

**Full Rothfusz regression** (NWS only applies this when the simple
formula's own result averages ≥80°F — i.e. only in genuinely hot
conditions):
```
HI = -42.379 + 2.04901523*T + 10.14333127*RH - 0.22475541*T*RH
     - 0.00683783*T² - 0.05481717*RH² + 0.00122874*T²*RH
     + 0.00085282*T*RH² - 0.00000199*T²*RH²
```
Valid range: T ≥ 80°F, RH ≥ 40%. Several correction terms apply outside
that (very low humidity + high heat, or very high humidity + moderate
heat) that this project hasn't needed and doesn't implement — see the
NWS source for the full correction table if a future pass needs them.

**Why the mockup uses the simple formula, not the full regression:** the
full regression is a polynomial fit only valid in its stated 80–112°F
range — evaluating it outside that range produces nonsensical results
(it's a curve fit, not a physical model). The simple formula, by
contrast, is well-behaved at any temperature and is what NWS itself
falls back to below 80°F. Since this project's dynamic branch needs to
produce a sane number across the *whole* comfortable-to-hot range (not
just extreme heat), the simple formula is the correct choice — using the
full regression outside its valid range and calling it "good enough"
would've been the actual invented-data violation.

## Wind Chill

```
WC = 35.74 + 0.6215*T - 35.75*V^0.16 + 0.4275*T*V^0.16
```
where `T` is air temperature in °F, `V` is wind speed in mph.

**Only defined when T ≤ 50°F and V ≥ 3 mph** — NWS doesn't define wind
chill outside that range (calm air or mild temperatures don't have a
meaningful "wind chill" effect to describe). `feelsLikeAdjustment()`
enforces both conditions before using this formula, falling back to the
Heat Index formula otherwise — which is also why a 45°F/calm sample
still shows Heat Index rather than an undefined Wind Chill.

The formula itself derives from 2001 human-trial data measuring facial
skin cooling rates at various temperature/wind combinations. It assumes
a 3 mph walking pace, 5-foot height, nighttime, and no solar radiation —
bright sun can raise the *actual* felt temperature 10–18°F above the
formula's output, a factor this project doesn't attempt to correct for
(matches NWS's own chart, which doesn't correct for it either).

## Implementation note: the ring-fill scale

`current-detail.html`'s card visualizes the result on a 270° ring gauge.
Heat Index and Wind Chill need *different* fill ranges — Heat Index's
whole point is the 50–110°F territory, while Wind Chill legitimately runs
well below 0°F (that's the range it exists to describe). A single shared
scale silently clipped every real Wind Chill reading toward 0% fill until
this was caught by testing the Wind Chill branch with an actual cold
sample rather than only the always-warm sample data the rest of the
screen uses. Current ranges: Heat Index 30–100°F, Wind Chill -20–50°F.

## Sources

- [The Heat Index Equation — NOAA/NWS Weather Prediction Center](https://www.wpc.ncep.noaa.gov/html/heatindex_equationbody.html)
- [SR 90-23 Technical Attachment — Rothfusz's original 1990 NWS regression](https://www.weather.gov/media/ffc/ta_htindx.PDF)
- [Wind Chill Calculation — NOAA/NWS Weather Prediction Center](https://www.wpc.ncep.noaa.gov/html/windchill.shtml)
- [Understanding Wind Chill — NWS](https://www.weather.gov/safety/cold-wind-chill-chart)
