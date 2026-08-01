// Persisted setup state, backed by NVS (via the Preferences library).
// Everything the provisioning web page collects, plus the lat/lon resolved
// from the location text on first successful Wi-Fi join. See
// docs/google-weather-api.md ("Location") for why geocoding happens here,
// once, rather than at provisioning time.
#pragma once

#include <Arduino.h>
#include <Preferences.h>

class ConfigStore {
 public:
  void begin();

  bool isProvisioned() const;

  String wifiSsid() const { return wifiSsid_; }
  String wifiPassword() const { return wifiPassword_; }
  String locationQuery() const { return locationQuery_; }
  String apiKey() const { return apiKey_; }
  bool unitsImperial() const { return unitsImperial_; }

  bool hasLocation() const { return hasLocation_; }
  float latitude() const { return latitude_; }
  float longitude() const { return longitude_; }

  // Resolved city/state from geocoding (see geocode.h) - e.g. "Bellevue"/
  // "WA" even if the user typed a ZIP code as locationQuery(). Either or
  // both may be "" if Google's response didn't include that component, or
  // (on an already-provisioned device from before this existed) if this
  // location was resolved before city/state were saved at all - callers
  // should fall back to locationQuery() when empty.
  String city() const { return city_; }
  String state() const { return state_; }

  // Resolved from the Time Zone API (see timezone_lookup.h), seconds -
  // meant to be passed straight to Arduino's configTime(rawOffsetSec,
  // dstOffsetSec, ntpServer). hasTimezone() gates whether these are
  // meaningful yet, same pattern as hasLocation()/latitude()/longitude().
  bool hasTimezone() const { return hasTimezone_; }
  int32_t timezoneRawOffsetSec() const { return timezoneRawOffsetSec_; }
  int32_t timezoneDstOffsetSec() const { return timezoneDstOffsetSec_; }

  // Called once, from the provisioning web server, with everything the
  // setup page collected. Persists immediately.
  void saveProvisioning(const String &ssid, const String &password, const String &locationQuery,
                         const String &apiKey, bool unitsImperial);

  // Called once, after the first successful post-provisioning geocode.
  // city/state may be "" - see city()/state() above.
  void saveLocation(float lat, float lon, const String &city, const String &state);

  // Called once, after the first successful post-geocode timezone
  // resolution.
  void saveTimezone(int32_t rawOffsetSec, int32_t dstOffsetSec);

  // Wipes everything and drops back into provisioning mode on next boot.
  void reset();

 private:
  mutable Preferences prefs_;

  String wifiSsid_;
  String wifiPassword_;
  String locationQuery_;
  String apiKey_;
  bool unitsImperial_ = true;

  bool hasLocation_ = false;
  float latitude_ = 0.0f;
  float longitude_ = 0.0f;
  String city_;
  String state_;

  bool hasTimezone_ = false;
  int32_t timezoneRawOffsetSec_ = 0;
  int32_t timezoneDstOffsetSec_ = 0;
};
