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

- `lv_display_set_flush_cb()` → `M5.Display.startWrite()` → `setAddrWindow()` →
  `pushImageDMA()` → `endWrite()` → `lv_display_flush_ready()`. DMA push means
  the CPU isn't blocked copying pixels out over the MIPI-DSI link while LVGL
  could otherwise be preparing the next frame.
- **Partial, PSRAM-backed draw buffers**, not one full 1280×720 buffer: two
  buffers sized for a horizontal slice (e.g. 1280×40px) allocated with
  `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`. LVGL redraws only the dirty
  rectangles (the temperature digits changing, a touch highlight) each tick
  instead of the whole panel — this is the actual "fast GUI response" lever,
  more than any single hardware feature.
- Touch: `lv_indev` read callback backed by `M5.Touch.getDetail()`
  (M5Unified's wrapper over the panel's touch controller — see
  [hardware.md](hardware.md) for why we don't hand-roll GT911/ST7123 I2C).

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
