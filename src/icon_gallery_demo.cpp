// Standalone example, not part of the tab5 firmware - built by the
// separate `pio run -e icon-gallery` environment (see platformio.ini).
//
// Thin entry point for lib/icon_gallery: does this project's usual
// display/LVGL bring-up (src/display.cpp, already confirmed working on
// this panel), then hands off to the library with every icon
// tools/gen_dashboard_icons.sh generated (include/dashboard_icons.h) -
// these are the actual production assets dashboard_ui.cpp uses, not a
// separate test-only set, so there's no manifest step here the way
// font_gallery has (that one compares many candidate sizes never meant
// to ship; this one just needs to show the 30 icons that already do).
//
// Compare each rendered icon against its source mockup
// (docs/mockups/dashboard.html / daily-forecast.html) before trusting it
// on the real Dashboard.
#include <M5Unified.h>
#include <icon_gallery.h>
#include <lvgl.h>

#include "dashboard_icons.h"
#include "display.h"

namespace {

constexpr IconGalleryEntry kEntries[] = {
    {&sun_hero, "sun_hero"},
    {&sun_dayrow, "sun_dayrow"},
    {&mostly_clear_hero, "mostly_clear_hero"},
    {&mostly_clear_dayrow, "mostly_clear_dayrow"},
    {&partly_cloudy_hero, "partly_cloudy_hero"},
    {&partly_cloudy_dayrow, "partly_cloudy_dayrow"},
    {&cloud_hero, "cloud_hero"},
    {&cloud_dayrow, "cloud_dayrow"},
    {&night_cloudy_hero, "night_cloudy_hero"},
    {&night_cloudy_dayrow, "night_cloudy_dayrow"},
    {&windy_hero, "windy_hero"},
    {&windy_dayrow, "windy_dayrow"},
    {&light_rain_hero, "light_rain_hero"},
    {&light_rain_dayrow, "light_rain_dayrow"},
    {&rain_hero, "rain_hero"},
    {&rain_dayrow, "rain_dayrow"},
    {&heavy_rain_hero, "heavy_rain_hero"},
    {&heavy_rain_dayrow, "heavy_rain_dayrow"},
    {&snow_hero, "snow_hero"},
    {&snow_dayrow, "snow_dayrow"},
    {&sleet_hero, "sleet_hero"},
    {&sleet_dayrow, "sleet_dayrow"},
    {&hail_hero, "hail_hero"},
    {&hail_dayrow, "hail_dayrow"},
    {&thunderstorm_hero, "thunderstorm_hero"},
    {&thunderstorm_dayrow, "thunderstorm_dayrow"},
    {&fog_hero, "fog_hero"},
    {&fog_dayrow, "fog_dayrow"},
    {&moon_hero, "moon_hero"},
    {&moon_dayrow, "moon_dayrow"},
};

}  // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  initDisplay();
  iconGalleryBegin(kEntries, sizeof(kEntries) / sizeof(kEntries[0]));
}

void loop() {
  M5.update();
  pumpLvgl();
  delay(2);
}
