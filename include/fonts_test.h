// Stable interface for the on-device font-size gallery
// (src/font_size_test.cpp, [env:font-test] in platformio.ini). The actual
// font list lives in tools/font_test_manifest.txt and is regenerated into
// include/fonts_test_manifest.h by tools/gen_test_fonts.sh - this file is
// hand-maintained and only defines the shape both sides agree on, so
// adding/removing fonts never touches this file or the viewer code.
#pragma once

#include <lvgl.h>

struct FontTestEntry {
  const lv_font_t *font;
  const char *caption;
};
