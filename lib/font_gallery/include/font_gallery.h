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

#include <lvgl.h>

#include <cstddef>

struct FontGalleryEntry {
  const lv_font_t *font;
  // Per-entry, not gallery-wide: different entries can hold entirely
  // different glyph subsets (a digit-only hero font vs. a full-Latin
  // label font), so there's no one sample string valid for all of them.
  const char *sample;
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
