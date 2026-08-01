/* LVGL config for the Tab5 build. Deliberately minimal: only the settings
 * that matter for this project are overridden — everything else falls back
 * to LVGL's own defaults (see lv_conf_internal.h). Pixel buffers are
 * allocated from PSRAM by the app itself (src/display.cpp), not by LVGL's
 * internal allocator, so LV_MEM_SIZE only needs to cover objects/styles. */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

#define LV_MEM_SIZE (128U * 1024U)

/* We drive lv_tick_inc() ourselves from millis(), see src/main.cpp. */
#define LV_TICK_CUSTOM 0

#define LV_USE_LOG 0

/* Default build only enables montserrat_14; the mockups' type scale needs a
 * few more sizes (see docs/mockups/assets/style.css for the reference scale). */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1

/* Panel is 5", 1280x720 -> ~294 PPI (verified: sqrt(1280^2+720^2)/5 in,
 * width_px/width_in). That's dense — a "14px" label is only ~1.2mm tall on
 * the glass, tiny even up close, let alone at any distance. 48 is the
 * largest size LVGL ships a built-in bitmap font for; a genuinely large
 * "read from across the room" hero number (matching the ~108px look in
 * docs/mockups/dashboard.html) will need a custom-generated font later —
 * not attempted here, this pass just moves every current screen off
 * default-scale sizes that were picked without accounting for this panel's
 * real density. */
#define LV_FONT_DEFAULT &lv_font_montserrat_20

/* Panel is 1280x720; used by lv_conf_internal's default DPI-scaled sizing. */
#define LV_DPI_DEF 294

#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

/* Dashboard's hero temperature font (src/dashboard_fonts/) is large enough
 * (312px) that uncompressed glyph bitmaps cost real flash - RLE compression
 * (LVGL's own built-in scheme, not a new dependency) cuts that down. Only
 * worth paying its ~30% render-time cost on large, rarely-redrawn glyphs
 * (this one redraws once per refresh cycle, not per frame) - see
 * docs/rendering.md's font notes. Every other generated font stays
 * uncompressed; this flag only enables the *decode* path, so it doesn't
 * change how any existing uncompressed font (built-in or generated)
 * behaves. */
#define LV_USE_FONT_COMPRESSED 1

#endif /* LV_CONF_H */
