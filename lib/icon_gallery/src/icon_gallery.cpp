#include "icon_gallery.h"

namespace {

constexpr uint32_t kBackground = 0x121417;
constexpr uint32_t kInk = 0xf0e9d8;

}  // namespace

void iconGalleryBegin(const IconGalleryEntry *entries, size_t count) {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(kBackground), 0);
  lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(screen, 24, 0);
  lv_obj_set_style_pad_gap(screen, 24, 0);

  for (size_t i = 0; i < count; i++) {
    const IconGalleryEntry &entry = entries[i];

    lv_obj_t *card = lv_obj_create(screen);
    lv_obj_set_size(card, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *img = lv_image_create(card);
    lv_image_set_src(img, entry.icon);

    lv_obj_t *caption = lv_label_create(card);
    lv_obj_set_style_text_color(caption, lv_color_hex(kInk), 0);
    lv_label_set_text(caption, entry.caption);
  }
}
