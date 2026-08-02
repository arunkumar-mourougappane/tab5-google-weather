// Maps a WeatherIconBucket (weather_icon.h) to the actual generated
// lv_image_dsc_t (dashboard_icons.h) at the size a given widget needs.
// Split out of dashboard_ui.cpp so hourly_detail_ui.cpp can reuse the
// same lookup rather than duplicating it - weather_icon.h itself stays
// LVGL/Arduino-free (see its own comment, testable under
// `pio test -e native`), so this small LVGL-dependent piece lives
// separately rather than folding into it.
#pragma once

#include <lvgl.h>

#include "weather_icon.h"

enum class IconSize { Hero, DayRow, HourCard, Segment };

const lv_image_dsc_t *iconForBucket(WeatherIconBucket bucket, IconSize size);
