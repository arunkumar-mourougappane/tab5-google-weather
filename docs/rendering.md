# Rendering: LVGL + M5GFX

Goal stated up front: "fast, good GUI response." At 1280×720 that rules out
naive full-frame software redraws every tick, so the approach below is chosen
specifically to keep touch/redraw latency low, not just "get something on screen."

## Stack

**LVGL v9** for widgets/layout, driven through **M5GFX** (via M5Unified) for the
actual panel/touch I/O, rather than talking to the MIPI-DSI panel directly.
M5GFX already has Tab5 panel support (both driver-IC revisions, see
[hardware.md](hardware.md)) and DMA-capable pixel push, so LVGL just needs a
flush callback and a touch-read callback wired to it — a well-worn pattern on
M5Stack hardware (a public LVGL-on-M5Stack demo reports 60+ fps this way).

## Flush path

- `lv_display_set_flush_cb()` → `M5.Display.startWrite()` →
  `pushImageDMA(x, y, w, h, data)` → `endWrite()` → `lv_display_flush_ready()`.
  DMA push means the CPU isn't blocked copying pixels out over the MIPI-DSI
  link while LVGL could otherwise be preparing the next frame. (An earlier
  draft used the generic SPI-panel pattern —
  `setAddrWindow()`+`pushPixelsDMA(data, count)` — written from general M5GFX
  familiarity rather than a confirmed Tab5 example; it compiled fine but
  produced visibly corrupted output on real hardware. `pushImageDMA(x, y, w,
  h, data)` is the form actually confirmed working, in community references
  and on our own device.)
- **Partial, PSRAM-backed draw buffers**, not one full-frame buffer: two
  buffers sized for a horizontal slice (screen-width × 40px) allocated with
  `heap_caps_malloc(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA)` — confirmed
  against ESP-IDF's own docs as the correct/supported way to get a
  DMA-capable PSRAM buffer on the P4, alignment handled by the allocator.
  LVGL redraws only the dirty rectangles (the temperature digits changing, a
  touch highlight) each tick instead of the whole panel — this is the actual
  "fast GUI response" lever, more than any single hardware feature.
- Touch: `lv_indev` read callback backed by `M5.Touch.getDetail()`
  (M5Unified's wrapper over the panel's touch controller — see
  [hardware.md](hardware.md) for why we don't hand-roll GT911/ST7123 I2C).

## Confirmed the hard way, on real hardware

Two bugs only showed up once actually flashed to a device — worth recording
so nobody re-derives these from scratch:

- **RGB565 byte order.** LVGL's native RGB565 output and what the panel
  wants are byte-swapped relative to each other. Symptom: an intended
  near-black background (`0x121417`) rendered as a visibly purple fill —
  which checks out exactly: byte-swapping `0x121417`'s RGB565 encoding
  (`0x10a2`) gives `0xa210`, which decodes to RGB (164, 64, 131), a medium
  purple. Fix: `lv_display_set_color_format(display,
  LV_COLOR_FORMAT_RGB565_SWAPPED)` on the LVGL display — this asks LVGL to
  render directly in the byte order the panel wants, rather than adding a
  per-flush `lv_draw_sw_rgb565_swap()` pass (which would cost CPU time on
  every flush, working against the whole point of this doc).
- **Panel is native 720×1280 (portrait), not 1280×720.** Without an explicit
  `M5.Display.setRotation(1)`, the panel stayed in its native orientation
  while our code declared 1280×720 to LVGL — LVGL centered text assuming a
  1280-wide screen, which landed the text near the *right edge* of the
  actually-720-wide panel (clipped, exactly as seen on device), and row
  writes wider than the real framebuffer wrapped/aliased into corrupted
  bands. Fix: rotate explicitly, then **read `screenW`/`screenH` back from
  `M5.Display.width()/height()`** rather than hardcoding — LVGL and the panel
  then can't disagree about geometry by construction, regardless of which
  rotation value ends up correct on a given unit.

Also: LVGL's built-in `lv_font_montserrat_*` fonts only ship Basic Latin
(ASCII) plus LVGL's own icon glyphs — Latin-1 Supplement punctuation like
`·` (U+00B7) or `—` (U+2014) rendered as tofu boxes in early placeholder
text. Fine to use in `docs/` and mockups (plain HTML/CSS, real fonts); not
in on-device LVGL label text unless a custom font is generated via LVGL's
font converter to include that range. Placeholder/scaffold text uses plain
ASCII punctuation instead.

## Typography and this panel's real density

**Verified, not eyeballed:** this panel is ~294 PPI — `sqrt(1280² + 720²) /
5in` (5" diagonal, confirmed spec) works out to a 4.36"×2.45" panel at that
resolution, i.e. denser than a typical monitor (usually 90–110 PPI) and
closer to a phone. A "14px" label is only ~1.2mm tall on the glass; every
font size chosen for the current status/provisioning screens was picked by
eye against low-DPI references (mockup screenshots, habit) without
accounting for that, and read as illegibly small on the real device.
Current fix: moved every screen up a tier (`LV_FONT_DEFAULT` now
`montserrat_20`, titles at `montserrat_48`) — covered in the git history,
not repeated here.

**Open problem this doesn't solve:** `montserrat_48` — the largest size
LVGL ships a built-in bitmap font for — is still only ~4.15mm tall. Fine
for setup-time screens read up close; nowhere near enough for a genuinely
large "read from across the room" number like the dashboard mockup's
108px hero temperature digit (docs/mockups/dashboard.html). Two paths
looked at for that, before any dashboard code exists to need it yet:

- **FreeType (live TTF rendering), rejected.** Confirmed unsupported on
  ESP32-S3; no confirmed working support found for P4 either — search
  turned up general PSRAM/cache tuning guidance, nothing FreeType-specific
  for this chip, which reads as "untested/unlikely" rather than "yes."
  Even where it does run, live TTF rasterization is measurably slower
  per-glyph than a pre-rendered bitmap (mitigated only by a glyph cache,
  which adds complexity for exactly the wrong case here — a kiosk's
  character set barely changes at runtime, so there's little upside to
  paying rendering cost per glyph instead of once at build time). Working
  against the stated "fast GUI response" goal for a case that doesn't
  need runtime flexibility.
- **Pre-generate the specific bitmap fonts we need, via LVGL's own
  [font converter](https://lvgl.io/tools/fontconverter) (or the offline
  `lv_font_conv` CLI for a scriptable pipeline) — the likely path.** Same
  rendering model already proven working on this device (no new runtime
  dependency), and can be generated from the *same* TTFs already in the
  repo for the HTML mockups (Archivo for headings/hero numbers, IBM Plex
  Mono for tabular data) instead of falling back to LVGL's default
  Montserrat — keeps the device visually consistent with the mockups
  rather than introducing a second, different typographic identity.

  Subsetting matters a lot at this scale: a full-Latin 96px+ font is
  expensive to store, but a hero temperature readout only ever needs
  digits + a handful of symbols (`0-9 ° - . :`), so narrow per-purpose
  fonts (e.g. "Archivo Bold 108, digits+°" for the big number vs. a
  fuller "Archivo SemiBold 32, full Latin" for city names) keep flash
  sane instead of one large kitchen-sink font per size. Per LVGL's own
  docs: 4bpp anti-aliasing for quality, and reach for the compression
  option only on the largest, least-frequently-redrawn fonts specifically
  (compression is ~30% slower to render, a cost worth paying only where a
  font is both large and rare) — not something to enable project-wide by
  default.

**Implemented.** `tools/gen_dashboard_fonts.sh` + `tools/dashboard_fonts_manifest.txt`
drive `lv_font_conv` (via `npx`) the same way `tools/gen_font_gallery.sh`
does for the dev-only font gallery, but generates production assets
directly — `include/dashboard_fonts.h` (plain `extern const lv_font_t`
declarations) + `src/dashboard_fonts/*.c` — consumed straight from
`dashboard_ui.cpp`, no `lib/font_gallery` machinery involved. One
generated font per exact (family, weight, size) pair the Dashboard screen
actually uses, each with its own narrow symbol subset (e.g. the hero
temperature only needs `0123456789°`), not a consolidated type scale —
pixel-perfect to the mockup over minimizing font count, confirmed as the
right tradeoff once flash usage came in at 28% with 21 such fonts.

Two things this section predicted correctly, worth confirming landed as
expected:

- **Compression, on exactly the fonts predicted.** `LV_USE_FONT_COMPRESSED`
  (`include/lv_conf.h`) is on project-wide, but only the hero temperature
  (240px) and its `°F`/`°C` suffix (128px) are generated with it — every
  other font stays uncompressed, matching this doc's "large and
  rare-redraw" criterion exactly (the hero digit only repaints once per
  refresh cycle, everything else repaints more often or is small enough
  that the ~30% render-time cost isn't worth paying).
- **A real stack cost from decompression, not just render time.** Growing
  the hero font to 240px caused a hardware `Stack protection fault` in
  `uiTask` (see [firmware-architecture.md](firmware-architecture.md)'s
  concurrency section) — LVGL's RLE decompression of a glyph that large
  needs meaningfully more of the calling task's stack than this doc's
  "~30% slower to render" framing accounts for. Fixed by doubling
  `uiTask`'s stack (8KB → 16KB), not by shrinking the font. Worth knowing
  before pushing any font larger still: the render-time cost isn't the
  only budget compression trades against.

`LV_FONT_FMT_TXT_LARGE=1` (`platformio.ini`) was also needed alongside
compression — a separate LVGL setting for glyphs whose metrics exceed the
compact glyph-descriptor format, empirically found (via the font gallery)
to bite somewhere around 256px+.

## Icons

Same "precompile everything, no runtime decode" philosophy as the fonts
above, applied to the Dashboard's weather condition glyphs (the "now"
section's hero glyph, and a small icon per 7-day outlook row).

**Icon vocabulary started at exactly the 4 concepts the mockups define**
— sun (clear), cloud (partly cloudy / the generic fallback), rain, and
moon (the night-time substitute for clear/partly-cloudy specifically) —
since that was the *entire* set any mockup with an icon actually defines
(`dashboard.html`, `daily-forecast.html`, `hourly-detail.html`'s inline
SVGs). It's since grown to **15**, in two rounds, both original designs
with no mockup reference (see each `.svg`'s own header comment in
`docs/mockups/assets/icons/` for the reasoning behind its look, e.g. why
thunderstorm's bolt reuses the ember severity-color accent rather than
introducing a new one):

- Round one added `windy`/`light_rain`/`snow`/`hail`/`thunderstorm`/`fog`
  — covering condition *families* the original 4 had no icon for at all.
- Round two added `mostly_clear`/`partly_cloudy`/`night_cloudy`/
  `heavy_rain`/`sleet` for finer accuracy within families that already
  had *an* icon but were losing real distinctions: `PARTLY_CLOUDY` and
  full `CLOUDY` overcast used to render identically (both `cloud`);
  `MOSTLY_CLOUDY`/`CLOUDY` at night showed the exact same icon as during
  the day (no night-cloud variant existed, unlike clear/partly-cloudy's
  existing moon substitution); `HEAVY_RAIN` looked identical to plain
  `RAIN`; `RAIN_AND_SNOW` (a wintry mix) rendered as plain `Snow`.

`include/weather_icon.h`'s `bucketForCondition()` maps all 39
`weatherCondition.type` values (plus `TYPE_UNSPECIFIED`) onto these 15 —
see [google-weather-api.md](google-weather-api.md#current-conditions) for
the full type→bucket table. `fog` is a special case: generated and
available, but Google's condition-type enum has no dedicated fog/mist
value, so nothing currently routes to it — sitting ready for a future
heuristic (e.g. a low-visibility threshold) rather than reachable today.
`mostly_clear`/`partly_cloudy`/`night_cloudy` also introduced a new
technique the original 4 don't use: a *solid* background-filled cloud
(not just an outline) positioned to actually occlude part of a sun or
moon behind it, rather than sitting beside a symbol like the rain/snow/
hail/thunderstorm icons do.

**Pipeline**: the original 4 icons' vector art was extracted verbatim
from the mockups' inline `<svg>` paths; every icon added since is an
original design built the same way (plain line-art SVGs, same
viewBox/stroke-weight/dark-theme-palette conventions, often reusing an
existing icon's exact path as a starting point — e.g. `heavy_rain`/
`sleet`/`thunderstorm` all reuse `rain.svg`'s cloud shape verbatim). All
of it lives as standalone source files (`docs/mockups/assets/icons/*.svg`,
dark-theme colors resolved to concrete hex since these render standalone,
outside any stylesheet), then `tools/gen_dashboard_icons.sh` rasterizes
each to PNG via `rsvg-convert` at the two sizes the Dashboard actually
uses (130px hero, 26px day-row) and converts that to an LVGL
`lv_image_dsc_t` C source via `tools/png_to_lvgl_image.py`.

That last step is a small hand-rolled converter, not LVGL's own
`lv_img_conv` npm tool (the naive choice, since `gen_dashboard_fonts.sh`
already reaches for `lv_font_conv` the same way) — confirmed by reading
`lv_img_conv`'s source that its last published version (0.4.0) still
emits LVGL v8's old packed-uint32 image header (`cf | w<<10 | h<<21`),
predating v9's real `lv_image_header_t` struct
(`magic`/`cf`/`flags`/`w`/`h`/`stride` as actual fields, see
`.pio/libdeps/tab5/lvgl/src/draw/lv_image_dsc.h`) this project's LVGL
9.5.0 expects. Its C output doesn't compile as a valid v9
`lv_image_dsc_t` initializer, so `png_to_lvgl_image.py` emits that struct
directly instead — `LV_COLOR_FORMAT_ARGB8888` (uncompressed, 4
bytes/pixel), chosen over `RGB565A8`/RLE compression because the icon set
is small (30 images at 15 icons × 2 sizes, ~1.05MB total against the
6.25MB OTA budget, 44.2% flash used with everything else already in the
firmware) and ARGB8888 is the format least likely to have a subtle
byte-packing bug in a from-scratch converter — not a place to relearn the
RGB565 byte-swap/packed-header lessons documented elsewhere on this page.

**Static today, animation-ready.** `lv_animimg` (LVGL's frame-cycling
widget) is enabled by default and needs no new dependency, but no
animated source art exists anywhere in this project — building it is new
art-direction work, not just a pipeline, so v1 ships static icons only.
Each generated icon is already exactly the shape `lv_animimg` consumes
(one `lv_image_dsc_t`) — a static icon is trivially "an animation with 1
frame." Today's widgets are plain `lv_image_create()`/`lv_image_set_src()`;
adding real animation later means generating N frames per icon (the
manifest gains a frame-count/frame-source column) and swapping just that
widget's creation call to `lv_animimg_create()` — the bucketing logic and
manifest/generator shape don't change.

**Verification**: `lib/icon_gallery` (sibling to `lib/font_gallery`, same
"check a generated asset by eye on hardware before trusting it" purpose)
shows every generated icon at both sizes, captioned, side by side — run
with `pio run -e icon-gallery -t upload` and compare against
`dashboard.html`/`daily-forecast.html` open in a browser.

## Why not the PPA (yet)

The ESP32-P4's PPA/DMA2D hardware accelerator can offload fills, blends,
rotate/scale — but per LVGL's own docs it's still **experimental**, gives
"no significant gains" on blending (DMA2D bandwidth-bound), and specifically
**does not help in partial-redraw mode**, which is exactly the mode we're
using for responsiveness. It pays off mainly with full-frame double buffering
and image transforms (rotate/scale), neither of which this UI needs. Documented
here as a known lever, not wired up in v1 — revisit only if profiling shows
CPU-bound fill/blend work, not preemptively.

## Memory budget

LVGL's internal object heap can stay modest (~128 KB) since the actual pixel
buffers live in PSRAM outside LVGL's allocator — objects/styles are cheap, pixel
data is not.

## Sources

- [LVGL running on M5Stack at 60+ FPS (forum)](https://forum.lvgl.io/t/lvgl-running-on-a-m5stack-device-on-60-fps/23435)
- [LVGL PPA (Espressif) integration docs](https://lvgl.io/docs/open/integration/chip_vendors/espressif/hardware_accelerator_ppa)
- [LVGL DMA2D (Espressif) integration docs](https://lvgl.io/docs/open/integration/chip_vendors/espressif/hardware_accelerator_dma2d)
- [ESP-IDF PPA peripheral guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/ppa.html)
- [LVGL Font Converter (online tool)](https://lvgl.io/tools/fontconverter)
- [lv_font_conv (offline/scriptable converter)](https://github.com/lvgl/lv_font_conv)
- [LVGL Built-In Fonts docs](https://docs.lvgl.io/master/main-modules/fonts/built_in_fonts.html) — bpp/compression tradeoffs
- [LVGL FreeType Font Engine docs](https://docs.lvgl.io/master/libs/font_support/freetype.html)
- [Rendering TTF fonts on ESP32 with LVGL + Tiny TTF (Medium)](https://medium.com/@pvginkel/rendering-ttf-fonts-on-esp32-devices-using-lvgl-and-tiny-ttf-0278374a06df) — the S3 FreeType-unsupported note, live-render performance discussion
