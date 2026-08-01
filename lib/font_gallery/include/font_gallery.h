// Reusable on-device font viewer: given a list of fonts, shows one at a
// time full-screen with a caption; tapping anywhere advances to the
// next. No knowledge of any specific font baked in here - the caller
// supplies the list (see tools/gen_font_gallery.sh, which generates one
// from tools/font_gallery_manifest.txt for the demo app in
// src/font_gallery_demo.cpp).
//
// Built to answer a typography question by eye on real hardware instead
// of by math (see docs/rendering.md's "Open problem" note on hero-digit
// legibility), but deliberately generic - reusable for the next one too.
#pragma once

// This library's generated font sources (tools/gen_font_gallery.sh, via
// lv_font_conv) need one build-flag guarantee from whatever env links
// them in - already set for [env:font-gallery] in platformio.ini, but
// asserted here too so reusing this library elsewhere fails loudly at
// compile time instead of reproducing a bug already hit once on this
// project (generated font .c files silently `#include "lvgl/lvgl.h"`
// instead of "lvgl.h" without it, which doesn't resolve under this
// project's lvgl library layout - a fatal compile error, not a subtle
// one, but better caught here with a clear message):
//
//   -DLV_LVGL_H_INCLUDE_SIMPLE
//
// Separately, -DLV_FONT_FMT_TXT_LARGE=1 is needed only if a generated
// font's glyphs are large enough to overflow the compact
// glyph-descriptor format (confirmed needed at 256px+ for Archivo
// digits). Not asserted here since it's self-diagnosing - LVGL's own
// build fails with an explicit "Enable LV_FONT_FMT_TXT_LARGE" error
// when it's missing and actually needed, and forcing it on for every
// consumer of this library (even ones with no large fonts) would cost
// every built-in LVGL font a wider glyph-descriptor layout for no
// reason.
#ifndef LV_LVGL_H_INCLUDE_SIMPLE
#error \
    "font_gallery.h requires -DLV_LVGL_H_INCLUDE_SIMPLE in this env's build_flags (see this header's comment, or platformio.ini's [env:font-gallery])."
#endif

#include <lvgl.h>

#include <cstddef>
#include <cstdint>

struct FontGalleryEntry {
  const lv_font_t *font;
  // Per-entry, not gallery-wide: different entries can hold entirely
  // different glyph subsets (a digit-only hero font vs. a full-Latin
  // label font), so there's no one sample string valid for all of them.
  const char *sample;
  // 0xRRGGBB, passed to lv_color_hex(). Also per-entry rather than
  // gallery-wide - lets a manifest test a font in the actual mockup
  // palette color it'd be used in (docs/mockups/assets/style.css's
  // --brass/--teal/--ember/etc.), not just white-on-dark.
  uint32_t color;
  const char *caption;
};

// Builds the gallery UI on the currently active LVGL screen (background,
// sample-text label, caption label, tap-to-advance) and shows the first
// entry. Call once, after your own display/LVGL init (this library only
// owns the screen's contents, not the display driver or LVGL's tick/timer
// pump - the caller keeps driving those as it already does).
//
// `entries`/`count` must outlive the gallery (a raw pointer is kept, no
// copy) - a `static constexpr` array, as the generator emits, satisfies
// this trivially.
void fontGalleryBegin(const FontGalleryEntry *entries, size_t count);
