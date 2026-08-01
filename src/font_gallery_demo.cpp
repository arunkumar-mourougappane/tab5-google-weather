// Standalone example, not part of the tab5 firmware - built by the
// separate `pio run -e font-gallery` environment (see platformio.ini).
//
// Thin entry point for lib/font_gallery: does this project's usual
// display/LVGL bring-up (src/display.cpp, already confirmed working on
// this panel), then hands off to the library with whatever fonts
// tools/gen_font_gallery.sh generated from tools/font_gallery_manifest.txt
// (include/font_gallery_manifest.h). No font-specific knowledge lives
// here - to test a different family/size/subset, edit the manifest and
// rerun the generator.
#include <M5Unified.h>
#include <font_gallery.h>
#include <lvgl.h>

#include "display.h"
#include "font_gallery_manifest.h"

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  initDisplay();
  fontGalleryBegin(kFontGalleryEntries, kFontGalleryEntryCount, "68°");
}

void loop() {
  M5.update();
  pumpLvgl();
  delay(2);
}
