#include "font_gallery.h"

#include <cstdio>

namespace {

const FontGalleryEntry *g_entries = nullptr;
size_t g_count = 0;
size_t g_index = 0;

lv_obj_t *g_bigLabel = nullptr;
lv_obj_t *g_captionLabel = nullptr;

void showCurrent() {
  const FontGalleryEntry &entry = g_entries[g_index];
  lv_obj_set_style_text_font(g_bigLabel, entry.font, 0);
  lv_obj_set_style_text_color(g_bigLabel, lv_color_hex(entry.color), 0);
  lv_label_set_text(g_bigLabel, entry.sample);

  char caption[128];
  snprintf(caption, sizeof(caption), "%s  (%u/%u, tap to advance)", entry.caption,
           static_cast<unsigned>(g_index + 1), static_cast<unsigned>(g_count));
  lv_label_set_text(g_captionLabel, caption);
}

void onScreenClicked(lv_event_t *e) {
  (void)e;
  g_index = (g_index + 1) % g_count;
  showCurrent();
}

}  // namespace

void fontGalleryBegin(const FontGalleryEntry *entries, size_t count) {
  g_entries = entries;
  g_count = count;
  g_index = 0;

  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x121417), 0);
  lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(screen, onScreenClicked, LV_EVENT_CLICKED, nullptr);

  g_bigLabel = lv_label_create(screen);
  lv_obj_set_style_text_align(g_bigLabel, LV_TEXT_ALIGN_CENTER, 0);
  // Digit-only hero samples never came close to this width, but a
  // full-alphabet label sample at a large size easily could - wrap
  // instead of silently running off both edges of the panel.
  lv_obj_set_width(g_bigLabel, lv_pct(90));
  lv_label_set_long_mode(g_bigLabel, LV_LABEL_LONG_WRAP);
  lv_obj_align(g_bigLabel, LV_ALIGN_CENTER, 0, -20);

  g_captionLabel = lv_label_create(screen);
  lv_obj_set_style_text_color(g_captionLabel, lv_color_hex(0x9aa0a6), 0);
  lv_obj_align(g_captionLabel, LV_ALIGN_BOTTOM_MID, 0, -24);

  showCurrent();
}
