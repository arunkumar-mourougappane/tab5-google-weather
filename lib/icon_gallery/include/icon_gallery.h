// Reusable on-device icon viewer: given a list of generated
// lv_image_dsc_t icons, lays them all out at once (image + caption per
// entry) so they can be checked by eye against their source mockups on
// real hardware, before trusting them in the real Dashboard UI. No
// knowledge of any specific icon baked in here - the caller supplies the
// list (see tools/gen_dashboard_icons.sh's output, wired up by
// src/icon_gallery_demo.cpp).
//
// Sibling to lib/font_gallery (same "verify a generated asset by eye on
// hardware before shipping it" purpose, same thin-library +
// generic-list-of-entries shape) - laid out all-at-once rather than
// tap-to-advance since a handful of small icons fit on screen together,
// unlike font_gallery's one-large-sample-at-a-time approach.
#pragma once

#include <lvgl.h>

#include <cstddef>

struct IconGalleryEntry {
  const lv_image_dsc_t *icon;
  const char *caption;
};

// Builds the gallery UI on the currently active LVGL screen. Call once,
// after your own display/LVGL init (this library only owns the screen's
// contents, not the display driver or LVGL's tick/timer pump - the
// caller keeps driving those as it already does).
//
// `entries`/`count` must outlive the gallery (a raw pointer is kept, no
// copy) - a `static constexpr` array satisfies this trivially.
void iconGalleryBegin(const IconGalleryEntry *entries, size_t count);
