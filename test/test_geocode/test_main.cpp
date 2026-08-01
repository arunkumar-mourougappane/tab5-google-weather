// Unlike test_timezone/'s fixture (copy-pasted from a real hardware
// log), this is a *representative* Geocoding API response - built from
// Google's documented response schema, not captured on real hardware.
// Matches geocode.cpp's own comment about what a real ZIP-code query
// actually returns (address_components, bounds+viewport+location
// geometry, postcode_localities, place_id, formatted_address - most of
// which this project never reads) rather than a minimal/idealized body,
// so the arena-sizing check means something. If a real captured body
// ever gets logged, swap it in here the way test_timezone/ already does.
#include <unity.h>

#include "geocode_parse.h"
#include "json_arena_allocator.h"

namespace {

// clang-format off
constexpr char kRepresentativeZipResponse[] = R"json(
{
  "results": [
    {
      "address_components": [
        {"long_name": "61525", "short_name": "61525", "types": ["postal_code"]},
        {"long_name": "Dunlap", "short_name": "Dunlap", "types": ["locality", "political"]},
        {"long_name": "Rosefield Township", "short_name": "Rosefield Township", "types": ["administrative_area_level_3", "political"]},
        {"long_name": "Peoria County", "short_name": "Peoria County", "types": ["administrative_area_level_2", "political"]},
        {"long_name": "Illinois", "short_name": "IL", "types": ["administrative_area_level_1", "political"]},
        {"long_name": "United States", "short_name": "US", "types": ["country", "political"]}
      ],
      "formatted_address": "Dunlap, IL 61525, USA",
      "geometry": {
        "bounds": {"northeast": {"lat": 40.8912345, "lng": -89.5123456}, "southwest": {"lat": 40.7912345, "lng": -89.6523456}},
        "location": {"lat": 40.8342211, "lng": -89.5865332},
        "location_type": "APPROXIMATE",
        "viewport": {"northeast": {"lat": 40.8912345, "lng": -89.5123456}, "southwest": {"lat": 40.7912345, "lng": -89.6523456}}
      },
      "place_id": "ChIJrepresentative00000000000000",
      "postcode_localities": ["Dunlap", "Rosefield"],
      "types": ["postal_code"]
    }
  ],
  "status": "OK"
}
)json";
// clang-format on

void test_representative_response_parses_without_overflow() {
  JsonDocument filter;
  buildGeocodeFilter(filter);

  StackJsonDocument<kGeocodeJsonArenaBytes> stackDoc;
  JsonDocument &doc = stackDoc.doc();
  const DeserializationError err =
      deserializeJson(doc, kRepresentativeZipResponse, DeserializationOption::Filter(filter));

  TEST_ASSERT_FALSE_MESSAGE(static_cast<bool>(err), "deserializeJson() failed");
  TEST_ASSERT_FALSE_MESSAGE(stackDoc.overflowed(), "arena overflowed - see kGeocodeJsonArenaBytes");
}

void test_lat_lng_extracted_correctly() {
  JsonDocument filter;
  buildGeocodeFilter(filter);

  StackJsonDocument<kGeocodeJsonArenaBytes> stackDoc;
  JsonDocument &doc = stackDoc.doc();
  deserializeJson(doc, kRepresentativeZipResponse, DeserializationOption::Filter(filter));

  const float lat = doc["results"][0]["geometry"]["location"]["lat"] | 0.0f;
  const float lng = doc["results"][0]["geometry"]["location"]["lng"] | 0.0f;
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 40.8342211f, lat);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, -89.5865332f, lng);
}

void test_city_and_state_found_among_six_components() {
  JsonDocument filter;
  buildGeocodeFilter(filter);

  StackJsonDocument<kGeocodeJsonArenaBytes> stackDoc;
  JsonDocument &doc = stackDoc.doc();
  deserializeJson(doc, kRepresentativeZipResponse, DeserializationOption::Filter(filter));

  JsonArrayConst components = doc["results"][0]["address_components"].as<JsonArrayConst>();
  const char *city = findAddressComponent(components, "locality", /*useShortName=*/false);
  const char *state = findAddressComponent(components, "administrative_area_level_1", /*useShortName=*/true);

  TEST_ASSERT_EQUAL_STRING("Dunlap", city);
  TEST_ASSERT_EQUAL_STRING("IL", state);
}

// address_components entries appear in no fixed order - some real
// responses put administrative_area_level_1 before locality. Confirms
// findAddressComponent() doesn't assume a position, only searches by
// `types`.
void test_component_lookup_returns_empty_when_type_missing() {
  JsonDocument filter;
  buildGeocodeFilter(filter);

  StackJsonDocument<kGeocodeJsonArenaBytes> stackDoc;
  JsonDocument &doc = stackDoc.doc();
  deserializeJson(doc, kRepresentativeZipResponse, DeserializationOption::Filter(filter));

  JsonArrayConst components = doc["results"][0]["address_components"].as<JsonArrayConst>();
  const char *neighborhood = findAddressComponent(components, "neighborhood", /*useShortName=*/false);

  TEST_ASSERT_EQUAL_STRING("", neighborhood);
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
  RUN_TEST(test_representative_response_parses_without_overflow);
  RUN_TEST(test_lat_lng_extracted_correctly);
  RUN_TEST(test_city_and_state_found_among_six_components);
  RUN_TEST(test_component_lookup_returns_empty_when_type_missing);
  UNITY_END();
}

void loop() {}
#else
int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_representative_response_parses_without_overflow);
  RUN_TEST(test_lat_lng_extracted_correctly);
  RUN_TEST(test_city_and_state_found_among_six_components);
  RUN_TEST(test_component_lookup_returns_empty_when_type_missing);
  return UNITY_END();
}
#endif
