#include "dashboard_icon_lookup.h"

#include "dashboard_icons.h"

// Order must match WeatherIconBucket's declaration order exactly
// (weather_icon.h) - index-based lookup, not a switch, to stay a
// one-line addition whenever a new bucket is added.
const lv_image_dsc_t *iconForBucket(WeatherIconBucket bucket, IconSize size) {
  static const lv_image_dsc_t *const kHero[] = {
      &sun_hero,       &mostly_clear_hero, &partly_cloudy_hero, &cloud_hero,       &night_cloudy_hero,
      &windy_hero,     &light_rain_hero,   &rain_hero,          &heavy_rain_hero,  &snow_hero,
      &sleet_hero,     &hail_hero,         &thunderstorm_hero,  &fog_hero,         &moon_hero,
  };
  static const lv_image_dsc_t *const kDayRow[] = {
      &sun_dayrow,       &mostly_clear_dayrow, &partly_cloudy_dayrow, &cloud_dayrow,      &night_cloudy_dayrow,
      &windy_dayrow,     &light_rain_dayrow,   &rain_dayrow,          &heavy_rain_dayrow, &snow_dayrow,
      &sleet_dayrow,     &hail_dayrow,         &thunderstorm_dayrow,  &fog_dayrow,        &moon_dayrow,
  };
  static const lv_image_dsc_t *const kHourCard[] = {
      &sun_hcard,       &mostly_clear_hcard, &partly_cloudy_hcard, &cloud_hcard,      &night_cloudy_hcard,
      &windy_hcard,     &light_rain_hcard,   &rain_hcard,          &heavy_rain_hcard, &snow_hcard,
      &sleet_hcard,     &hail_hcard,         &thunderstorm_hcard,  &fog_hcard,        &moon_hcard,
  };
  static const lv_image_dsc_t *const kSegment[] = {
      &sun_segment,       &mostly_clear_segment, &partly_cloudy_segment, &cloud_segment,      &night_cloudy_segment,
      &windy_segment,     &light_rain_segment,   &rain_segment,          &heavy_rain_segment, &snow_segment,
      &sleet_segment,     &hail_segment,         &thunderstorm_segment,  &fog_segment,        &moon_segment,
  };
  const size_t index = static_cast<size_t>(bucket);
  switch (size) {
    case IconSize::Hero:
      return kHero[index];
    case IconSize::DayRow:
      return kDayRow[index];
    case IconSize::HourCard:
      return kHourCard[index];
    case IconSize::Segment:
      return kSegment[index];
  }
  return kDayRow[index];
}
