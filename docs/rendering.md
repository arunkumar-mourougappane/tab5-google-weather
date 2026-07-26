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

  Not implemented — no dashboard code exists yet to need it. Worth
  revisiting once the dashboard's actual hero-number treatment is being
  built, not before.

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
