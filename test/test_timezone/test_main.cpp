// Regression test for the exact bug found on hardware 2026-08-01: the
// Time Zone API's response for Dunlap, IL parsed fine in isolation, but
// overflowed a 768-byte arena after only 16 bytes used - a single
// allocation request during deserializeJson() needed more room than
// that, unrelated to how small the actual field data was (see
// timezone_parse.h's kTimezoneJsonArenaBytes comment). This body is
// copy-pasted verbatim from that hardware log, not reconstructed from
// the API docs - it's the real thing that broke.
#include <unity.h>

#include "json_arena_allocator.h"
#include "timezone_parse.h"

namespace {

// clang-format off
constexpr char kRealDunlapIlResponse[] = R"json(
{
   "dstOffset" : 3600,
   "rawOffset" : -21600,
   "status" : "OK",
   "timeZoneId" : "America/Chicago",
   "timeZoneName" : "Central Daylight Time"
}
)json";
// clang-format on

void test_real_response_parses_without_overflow() {
  JsonDocument filter;
  buildTimezoneFilter(filter);

  StackJsonDocument<kTimezoneJsonArenaBytes> stackDoc;
  JsonDocument &doc = stackDoc.doc();
  const DeserializationError err =
      deserializeJson(doc, kRealDunlapIlResponse, DeserializationOption::Filter(filter));

  TEST_ASSERT_FALSE_MESSAGE(static_cast<bool>(err), "deserializeJson() failed");
  TEST_ASSERT_FALSE_MESSAGE(stackDoc.overflowed(), "arena overflowed - see kTimezoneJsonArenaBytes");
}

void test_real_response_fields_match_expected() {
  JsonDocument filter;
  buildTimezoneFilter(filter);

  StackJsonDocument<kTimezoneJsonArenaBytes> stackDoc;
  JsonDocument &doc = stackDoc.doc();
  deserializeJson(doc, kRealDunlapIlResponse, DeserializationOption::Filter(filter));

  TEST_ASSERT_EQUAL_STRING("OK", doc["status"] | "");
  TEST_ASSERT_EQUAL_INT32(-21600, doc["rawOffset"] | 0);
  TEST_ASSERT_EQUAL_INT32(3600, doc["dstOffset"] | 0);
  TEST_ASSERT_EQUAL_STRING("America/Chicago", doc["timeZoneId"] | "");
}

// The regression's whole point: confirms the *previous* size (768) really
// would still fail today, so this test would have caught it before ever
// reaching hardware. If this test itself ever starts failing (i.e. 768
// suddenly becomes sufficient), that's a sign ArduinoJson's internal
// chunk-growth behavior changed, not that this test is wrong.
void test_undersized_arena_reproduces_original_failure() {
  JsonDocument filter;
  buildTimezoneFilter(filter);

  StackJsonDocument<768> undersizedDoc;
  JsonDocument &doc = undersizedDoc.doc();
  deserializeJson(doc, kRealDunlapIlResponse, DeserializationOption::Filter(filter));

  TEST_ASSERT_TRUE_MESSAGE(undersizedDoc.overflowed(),
                            "768 bytes no longer overflows - the ArduinoJson version's chunk-growth "
                            "size must have changed; re-check kTimezoneJsonArenaBytes's sizing note");
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
  RUN_TEST(test_real_response_parses_without_overflow);
  RUN_TEST(test_real_response_fields_match_expected);
  RUN_TEST(test_undersized_arena_reproduces_original_failure);
  UNITY_END();
}

void loop() {}
#else
int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_real_response_parses_without_overflow);
  RUN_TEST(test_real_response_fields_match_expected);
  RUN_TEST(test_undersized_arena_reproduces_original_failure);
  return UNITY_END();
}
#endif
