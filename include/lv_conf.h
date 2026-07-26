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

/* Panel is 1280x720; used by lv_conf_internal's default DPI-scaled sizing. */
#define LV_DPI_DEF 160

#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

#endif /* LV_CONF_H */
