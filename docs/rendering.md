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
