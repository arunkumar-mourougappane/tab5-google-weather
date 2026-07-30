#include "boot_ui.h"

#include <lvgl.h>

// The small "TAB5 - WEATHER" wordmark is a persistent brand mark shown on
// every screen here, same treatment every time (matches
// docs/mockups/status.html's wordmark, always smaller than the title) — it
// must never be reused as a screen's actual title/headline. Doing that once
// (the old "connected" placeholder) was a real bug: the same string
// visibly changed size between screens depending on which role it was
// playing, which reads as broken even though each individual screen was
// rendering correctly.
//
// Plain ASCII punctuation only — LVGL's built-in Montserrat fonts don't
// include Latin-1 Supplement glyphs like middle-dot/em-dash, so those
// rendered as tofu boxes early on.
void showStatusScreen(const char *title, const char *subtitle, bool loading) {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_clean(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x121417), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  lv_obj_t *stack = lv_obj_create(screen);
  lv_obj_set_size(stack, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(stack, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(stack, 0, 0);
  lv_obj_set_style_pad_all(stack, 0, 0);
  lv_obj_set_style_pad_row(stack, 16, 0);
  lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(stack, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *wordmark = lv_label_create(stack);
  lv_label_set_text(wordmark, "TAB5 - WEATHER");
  lv_obj_set_style_text_color(wordmark, lv_color_hex(0xA89E8C), 0);
  lv_obj_set_style_text_font(wordmark, &lv_font_montserrat_20, 0);

  if (loading) {
    lv_obj_t *spinner = lv_spinner_create(stack);
    lv_obj_set_size(spinner, 64, 64);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0xD99A4E), LV_PART_INDICATOR);
  }

  lv_obj_t *titleLabel = lv_label_create(stack);
  lv_label_set_text(titleLabel, title);
  lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xD99A4E), 0);
  lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_48, 0);

  lv_obj_t *subtitleLabel = lv_label_create(stack);
  lv_label_set_text(subtitleLabel, subtitle);
  lv_obj_set_style_text_color(subtitleLabel, lv_color_hex(0xA89E8C), 0);
  lv_obj_set_style_text_font(subtitleLabel, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_align(subtitleLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(subtitleLabel, 900);
  lv_label_set_long_mode(subtitleLabel, LV_LABEL_LONG_WRAP);

  lv_obj_center(stack);
}

const char *describeWifiStatus(wl_status_t status) {
  switch (status) {
    case WL_NO_SSID_AVAIL:
      return "Network not found - check it's in range and was entered correctly during setup.";
    case WL_CONNECT_FAILED:
      return "Could not authenticate - check the Wi-Fi password entered during setup.";
    case WL_CONNECTION_LOST:
      return "Connection lost partway through joining.";
    case WL_DISCONNECTED:
      return "Disconnected before finishing.";
    default:
      return "Timed out waiting to connect.";
  }
}
