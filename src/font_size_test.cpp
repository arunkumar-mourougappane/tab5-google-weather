// Standalone example, not part of the tab5 firmware - built by the
// separate `pio run -e font-test` environment (see platformio.ini).
//
// Answers the open question in docs/rendering.md: what does the hero
// temperature digit actually need to be, in px, to read comfortably on
// this ~294 PPI panel "from across the room"? LVGL's built-in fonts only
// go up to montserrat_48; this cycles through custom Archivo fonts
// generated at 48/64/80/96/108/128px (tools/gen_test_fonts.sh) so the
// sizes can be judged by eye on real hardware instead of by math alone.
//
// Tap anywhere on screen to advance to the next size.
#include <M5Unified.h>
#include <lvgl.h>

#include "display.h"
#include "fonts_test.h"

namespace {

struct SizeEntry {
  const lv_font_t *font;
  const char *label;
};

// clang-format off
const SizeEntry kSizes[] = {
    {&font_archivo_48,  "Archivo 48px  (current LVGL built-in ceiling)"},
    {&font_archivo_64,  "Archivo 64px"},
    {&font_archivo_80,  "Archivo 80px"},
    {&font_archivo_96,  "Archivo 96px"},
    {&font_archivo_108, "Archivo 108px (dashboard.html hero size)"},
    {&font_archivo_128, "Archivo 128px"},
};
// clang-format on
constexpr size_t kNumSizes = sizeof(kSizes) / sizeof(kSizes[0]);

size_t g_index = 0;
lv_obj_t *g_bigLabel = nullptr;
lv_obj_t *g_captionLabel = nullptr;

void showCurrent() {
  const SizeEntry &entry = kSizes[g_index];
  lv_obj_set_style_text_font(g_bigLabel, entry.font, 0);
  lv_label_set_text(g_bigLabel, "68°");

  char caption[96];
  snprintf(caption, sizeof(caption), "%s  (%u/%u, tap to advance)", entry.label,
           static_cast<unsigned>(g_index + 1), static_cast<unsigned>(kNumSizes));
  lv_label_set_text(g_captionLabel, caption);
}

void onScreenClicked(lv_event_t *e) {
  (void)e;
  g_index = (g_index + 1) % kNumSizes;
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
