// Standalone example, not part of the tab5 firmware - built by the
// separate `pio run -e font-test` environment (see platformio.ini).
//
// Generic font-size gallery: cycles through whatever fonts are listed in
// tools/font_test_manifest.txt (regenerated into
// include/fonts_test_manifest.h by tools/gen_test_fonts.sh), showing
// sample text and a caption for each. This file has no knowledge of any
// specific font/size - to test a different family, size ladder, or
// glyph subset, edit the manifest and rerun the generator, nothing here
// changes.
//
// Originally built to answer one open question from docs/rendering.md:
// what does the hero temperature digit actually need to be, in px, to
// read comfortably on this ~294 PPI panel "from across the room"? Kept
// general so it's reusable for the next typography question too.
//
// Tap anywhere on screen to advance to the next entry.
#include <M5Unified.h>
#include <lvgl.h>

#include "display.h"
#include "fonts_test.h"
#include "fonts_test_manifest.h"

namespace {

// Sample text is fixed rather than per-entry: this gallery is currently
// only exercised with the hero-digit manifest (docs+° subset), so one
// glyph set covers every entry. If a future manifest entry needs
// different sample text (e.g. testing a full-Latin label font), add a
// `sample` column to the manifest/generator rather than hardcoding a
// second string here.
constexpr const char *kSampleText = "68°";

size_t g_index = 0;
lv_obj_t *g_bigLabel = nullptr;
lv_obj_t *g_captionLabel = nullptr;

void showCurrent() {
  const FontTestEntry &entry = kFontTestEntries[g_index];
  lv_obj_set_style_text_font(g_bigLabel, entry.font, 0);
  lv_label_set_text(g_bigLabel, kSampleText);

  char caption[128];
  snprintf(caption, sizeof(caption), "%s  (%u/%u, tap to advance)", entry.caption,
           static_cast<unsigned>(g_index + 1), static_cast<unsigned>(kFontTestEntryCount));
  lv_label_set_text(g_captionLabel, caption);
}

void onScreenClicked(lv_event_t *e) {
  (void)e;
  g_index = (g_index + 1) % kFontTestEntryCount;
  showCurrent();
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  initDisplay();

  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x121417), 0);
  lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(screen, onScreenClicked, LV_EVENT_CLICKED, nullptr);

  g_bigLabel = lv_label_create(screen);
  lv_obj_set_style_text_color(g_bigLabel, lv_color_hex(0xf5f0e8), 0);
  lv_obj_align(g_bigLabel, LV_ALIGN_CENTER, 0, -20);

  g_captionLabel = lv_label_create(screen);
  lv_obj_set_style_text_color(g_captionLabel, lv_color_hex(0x9aa0a6), 0);
  lv_obj_align(g_captionLabel, LV_ALIGN_BOTTOM_MID, 0, -24);

  showCurrent();
}

void loop() {
  M5.update();
  pumpLvgl();
  delay(2);
}
