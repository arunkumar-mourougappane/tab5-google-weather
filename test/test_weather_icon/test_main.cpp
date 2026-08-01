#include <unity.h>

#include "weather_icon.h"

namespace {

void test_daytime_clear_is_sun() {
  TEST_ASSERT_TRUE(bucketForCondition("CLEAR", /*isDaytime=*/true) == WeatherIconBucket::Sun);
}

void test_daytime_mostly_clear_is_its_own_bucket() {
  TEST_ASSERT_TRUE(bucketForCondition("MOSTLY_CLEAR", /*isDaytime=*/true) == WeatherIconBucket::MostlyClear);
}

void test_daytime_partly_cloudy_is_its_own_bucket() {
  TEST_ASSERT_TRUE(bucketForCondition("PARTLY_CLOUDY", /*isDaytime=*/true) == WeatherIconBucket::PartlyCloudy);
}

void test_nighttime_clear_family_is_moon() {
  TEST_ASSERT_TRUE(bucketForCondition("CLEAR", /*isDaytime=*/false) == WeatherIconBucket::Moon);
  TEST_ASSERT_TRUE(bucketForCondition("MOSTLY_CLEAR", /*isDaytime=*/false) == WeatherIconBucket::Moon);
  TEST_ASSERT_TRUE(bucketForCondition("PARTLY_CLOUDY", /*isDaytime=*/false) == WeatherIconBucket::Moon);
}

void test_nighttime_overcast_is_night_cloudy() {
  // Previously the one condition pair that showed the exact same icon
  // regardless of time of day - see night_cloudy.svg's header comment.
  TEST_ASSERT_TRUE(bucketForCondition("MOSTLY_CLOUDY", /*isDaytime=*/false) == WeatherIconBucket::NightCloudy);
  TEST_ASSERT_TRUE(bucketForCondition("CLOUDY", /*isDaytime=*/false) == WeatherIconBucket::NightCloudy);
}

void test_daytime_overcast_stays_cloud() {
  TEST_ASSERT_TRUE(bucketForCondition("MOSTLY_CLOUDY", /*isDaytime=*/true) == WeatherIconBucket::Cloud);
  TEST_ASSERT_TRUE(bucketForCondition("CLOUDY", /*isDaytime=*/true) == WeatherIconBucket::Cloud);
}

void test_nighttime_rain_stays_rain_not_moon() {
  // docs/mockups/daily-forecast.html's own sample data: a rainy night row
  // still uses the rain icon, not moon - moon only substitutes for
  // clear-family/partly-cloudy specifically.
  TEST_ASSERT_TRUE(bucketForCondition("RAIN_SHOWERS", /*isDaytime=*/false) == WeatherIconBucket::Rain);
}

void test_windy_types() {
  TEST_ASSERT_TRUE(bucketForCondition("WINDY", true) == WeatherIconBucket::Windy);
  TEST_ASSERT_TRUE(bucketForCondition("WIND_AND_RAIN", true) == WeatherIconBucket::Windy);
}

void test_light_rain_types_not_confused_with_light_snow() {
  TEST_ASSERT_TRUE(bucketForCondition("LIGHT_RAIN", true) == WeatherIconBucket::LightRain);
  TEST_ASSERT_TRUE(bucketForCondition("LIGHT_RAIN_SHOWERS", true) == WeatherIconBucket::LightRain);
  TEST_ASSERT_TRUE(bucketForCondition("LIGHT_TO_MODERATE_RAIN", true) == WeatherIconBucket::LightRain);
  TEST_ASSERT_TRUE(bucketForCondition("CHANCE_OF_SHOWERS", true) == WeatherIconBucket::LightRain);
  // "LIGHT" alone must not steal these away from Snow - Snow is checked
  // first in bucketForCondition().
  TEST_ASSERT_TRUE(bucketForCondition("LIGHT_SNOW", true) == WeatherIconBucket::Snow);
  TEST_ASSERT_TRUE(bucketForCondition("LIGHT_SNOW_SHOWERS", true) == WeatherIconBucket::Snow);
}

void test_heavy_rain_types_not_confused_with_heavy_snow_or_thunder() {
  TEST_ASSERT_TRUE(bucketForCondition("HEAVY_RAIN", true) == WeatherIconBucket::HeavyRain);
  TEST_ASSERT_TRUE(bucketForCondition("HEAVY_RAIN_SHOWERS", true) == WeatherIconBucket::HeavyRain);
  TEST_ASSERT_TRUE(bucketForCondition("MODERATE_TO_HEAVY_RAIN", true) == WeatherIconBucket::HeavyRain);
  TEST_ASSERT_TRUE(bucketForCondition("RAIN_PERIODICALLY_HEAVY", true) == WeatherIconBucket::HeavyRain);
  // "HEAVY" alone must not steal these away from Snow/Thunderstorm - both
  // are checked before the heavy-rain check in bucketForCondition().
  TEST_ASSERT_TRUE(bucketForCondition("HEAVY_SNOW", true) == WeatherIconBucket::Snow);
  TEST_ASSERT_TRUE(bucketForCondition("HEAVY_THUNDERSTORM", true) == WeatherIconBucket::Thunderstorm);
}

void test_rain_family_buckets_to_rain() {
  TEST_ASSERT_TRUE(bucketForCondition("RAIN", true) == WeatherIconBucket::Rain);
  TEST_ASSERT_TRUE(bucketForCondition("SCATTERED_SHOWERS", true) == WeatherIconBucket::Rain);
}

void test_snow_family_buckets_to_snow() {
  TEST_ASSERT_TRUE(bucketForCondition("SNOW", true) == WeatherIconBucket::Snow);
  TEST_ASSERT_TRUE(bucketForCondition("BLOWING_SNOW", true) == WeatherIconBucket::Snow);
}

void test_rain_and_snow_is_sleet() {
  TEST_ASSERT_TRUE(bucketForCondition("RAIN_AND_SNOW", true) == WeatherIconBucket::Sleet);
}

void test_hail_family_buckets_to_hail() {
  TEST_ASSERT_TRUE(bucketForCondition("HAIL", true) == WeatherIconBucket::Hail);
  TEST_ASSERT_TRUE(bucketForCondition("HAIL_SHOWERS", true) == WeatherIconBucket::Hail);
}

void test_thunderstorm_family_buckets_to_thunderstorm() {
  TEST_ASSERT_TRUE(bucketForCondition("THUNDERSTORM", true) == WeatherIconBucket::Thunderstorm);
  TEST_ASSERT_TRUE(bucketForCondition("THUNDERSHOWER", true) == WeatherIconBucket::Thunderstorm);
  // Contains both THUNDER and RAIN/LIGHT - thunderstorm must win.
  TEST_ASSERT_TRUE(bucketForCondition("LIGHT_THUNDERSTORM_RAIN", true) == WeatherIconBucket::Thunderstorm);
}

void test_unrecognized_type_falls_back_to_cloud() {
  TEST_ASSERT_TRUE(bucketForCondition("TYPE_UNSPECIFIED", true) == WeatherIconBucket::Cloud);
  TEST_ASSERT_TRUE(bucketForCondition("", true) == WeatherIconBucket::Cloud);
}

void test_null_type_falls_back_to_cloud() {
  TEST_ASSERT_TRUE(bucketForCondition(nullptr, true) == WeatherIconBucket::Cloud);
}

}  // namespace

// pio test -e native builds a plain executable (int main()); pio test
// -e tab5-test builds for the Arduino framework, which supplies its own
// main.cpp expecting setup()/loop() instead - same tests either way,
// just a different entry point per target.
#ifdef ARDUINO
#include <Arduino.h>

void setup() {
  delay(2000);  // let the board's USB-serial connection settle before
                // Unity starts writing output, or the first few lines
                // are lost - PlatformIO's own recommended pattern.
  UNITY_BEGIN();
  RUN_TEST(test_daytime_clear_is_sun);
  RUN_TEST(test_daytime_mostly_clear_is_its_own_bucket);
  RUN_TEST(test_daytime_partly_cloudy_is_its_own_bucket);
  RUN_TEST(test_nighttime_clear_family_is_moon);
  RUN_TEST(test_nighttime_overcast_is_night_cloudy);
  RUN_TEST(test_daytime_overcast_stays_cloud);
  RUN_TEST(test_nighttime_rain_stays_rain_not_moon);
  RUN_TEST(test_windy_types);
  RUN_TEST(test_light_rain_types_not_confused_with_light_snow);
  RUN_TEST(test_heavy_rain_types_not_confused_with_heavy_snow_or_thunder);
  RUN_TEST(test_rain_family_buckets_to_rain);
  RUN_TEST(test_snow_family_buckets_to_snow);
  RUN_TEST(test_rain_and_snow_is_sleet);
  RUN_TEST(test_hail_family_buckets_to_hail);
  RUN_TEST(test_thunderstorm_family_buckets_to_thunderstorm);
  RUN_TEST(test_unrecognized_type_falls_back_to_cloud);
  RUN_TEST(test_null_type_falls_back_to_cloud);
  UNITY_END();
}

void loop() {}
#else
int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_daytime_clear_is_sun);
  RUN_TEST(test_daytime_mostly_clear_is_its_own_bucket);
  RUN_TEST(test_daytime_partly_cloudy_is_its_own_bucket);
  RUN_TEST(test_nighttime_clear_family_is_moon);
  RUN_TEST(test_nighttime_overcast_is_night_cloudy);
  RUN_TEST(test_daytime_overcast_stays_cloud);
  RUN_TEST(test_nighttime_rain_stays_rain_not_moon);
  RUN_TEST(test_windy_types);
  RUN_TEST(test_light_rain_types_not_confused_with_light_snow);
  RUN_TEST(test_heavy_rain_types_not_confused_with_heavy_snow_or_thunder);
  RUN_TEST(test_rain_family_buckets_to_rain);
  RUN_TEST(test_snow_family_buckets_to_snow);
  RUN_TEST(test_rain_and_snow_is_sleet);
  RUN_TEST(test_hail_family_buckets_to_hail);
  RUN_TEST(test_thunderstorm_family_buckets_to_thunderstorm);
  RUN_TEST(test_unrecognized_type_falls_back_to_cloud);
  RUN_TEST(test_null_type_falls_back_to_cloud);
  return UNITY_END();
}
#endif
