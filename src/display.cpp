#include "display.h"

#include <M5Unified.h>
#include <lvgl.h>

namespace {

// The Tab5 panel is native 720x1280 (portrait); M5GFX must be told to
// rotate into landscape before we ask it for width()/height(). Screen
// dimensions are read back from M5GFX rather than hardcoded, so LVGL and
// the panel can never disagree about geometry — a mismatch there is what
// caused the earlier corrupted/clipped-right render.
int32_t screenW = 0;
int32_t screenH = 0;

// Partial draw buffers: a horizontal slice each, not a full frame. LVGL
// only redraws the dirty rectangle each tick, so this is what actually
// keeps touch/redraw latency low — see docs/rendering.md.
constexpr size_t kBufLines = 40;
size_t bufPixels = 0;

lv_display_t *display = nullptr;
lv_color_t *drawBuf1 = nullptr;
lv_color_t *drawBuf2 = nullptr;
uint32_t lastTickMs = 0;

void lvglFlushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *pxMap) {
  const int32_t w = area->x2 - area->x1 + 1;
  const int32_t h = area->y2 - area->y1 + 1;

  M5.Display.startWrite();
  M5.Display.pushImageDMA(area->x1, area->y1, w, h, reinterpret_cast<uint16_t *>(pxMap));
  M5.Display.endWrite();

  lv_display_flush_ready(disp);
}

void lvglTouchReadCb(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  if (M5.Touch.getCount() > 0) {
    const auto detail = M5.Touch.getDetail(0);
    data->point.x = detail.x;
    data->point.y = detail.y;
    data->state = detail.isPressed() ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

}  // namespace

void initDisplay() {
  // Landscape, 1280 wide. If this comes up mirrored or upside down on real
  // hardware, the fix is this rotation value (try 3 next), not anything
  // downstream — everything else derives its geometry from M5GFX below.
  M5.Display.setRotation(1);

  screenW = M5.Display.width();
  screenH = M5.Display.height();
  bufPixels = static_cast<size_t>(screenW) * kBufLines;

  drawBuf1 = static_cast<lv_color_t *>(
      heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA));
  drawBuf2 = static_cast<lv_color_t *>(
      heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA));

  lv_init();

  display = lv_display_create(screenW, screenH);
  lv_display_set_flush_cb(display, lvglFlushCb);
  lv_display_set_buffers(display, drawBuf1, drawBuf2, bufPixels * sizeof(lv_color_t),
                          LV_DISPLAY_RENDER_MODE_PARTIAL);
  // M5GFX/the panel wants the opposite RGB565 byte order from LVGL's native
  // format — confirmed empirically (near-black background was rendering as
  // purple, exactly the RGB565 byte-swapped color). This asks LVGL to render
  // directly in the byte order the panel wants, instead of an extra
  // per-pixel swap pass in the flush callback.
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565_SWAPPED);

  lv_indev_t *touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, lvglTouchReadCb);

  lastTickMs = millis();
}

void pumpLvgl() {
  const uint32_t now = millis();
  lv_tick_inc(now - lastTickMs);
  lastTickMs = now;
  lv_timer_handler();
}
