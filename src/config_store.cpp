#include "config_store.h"

namespace {
constexpr const char *kNamespace = "tab5wx";
}

void ConfigStore::begin() {
  prefs_.begin(kNamespace, /*readOnly=*/false);

  wifiSsid_ = prefs_.getString("wifi_ssid", "");
  wifiPassword_ = prefs_.getString("wifi_pass", "");
  locationQuery_ = prefs_.getString("loc_query", "");
  apiKey_ = prefs_.getString("api_key", "");
  unitsImperial_ = prefs_.getBool("units_imp", true);

  hasLocation_ = prefs_.getBool("has_loc", false);
  latitude_ = prefs_.getFloat("lat", 0.0f);
  longitude_ = prefs_.getFloat("lon", 0.0f);
  city_ = prefs_.getString("city", "");
  state_ = prefs_.getString("state", "");

  hasTimezone_ = prefs_.getBool("has_tz", false);
  timezoneRawOffsetSec_ = prefs_.getInt("tz_raw", 0);
  timezoneDstOffsetSec_ = prefs_.getInt("tz_dst", 0);
}

bool ConfigStore::isProvisioned() const {
  return wifiSsid_.length() > 0 && apiKey_.length() > 0 && locationQuery_.length() > 0;
}

void ConfigStore::saveProvisioning(const String &ssid, const String &password,
                                    const String &locationQuery, const String &apiKey,
                                    bool unitsImperial) {
  wifiSsid_ = ssid;
  wifiPassword_ = password;
  locationQuery_ = locationQuery;
  apiKey_ = apiKey;
  unitsImperial_ = unitsImperial;

  prefs_.putString("wifi_ssid", wifiSsid_);
  prefs_.putString("wifi_pass", wifiPassword_);
  prefs_.putString("loc_query", locationQuery_);
  prefs_.putString("api_key", apiKey_);
  prefs_.putBool("units_imp", unitsImperial_);

  // A location string change (re-running setup) invalidates any previously
  // resolved lat/lon/city/state/timezone — force a re-geocode (and
  // re-resolve of timezone, which depends on lat/lon) on next boot rather
  // than showing a stale place name or offset until that happens.
  hasLocation_ = false;
  city_ = "";
  state_ = "";
  hasTimezone_ = false;
  timezoneRawOffsetSec_ = 0;
  timezoneDstOffsetSec_ = 0;
  prefs_.putBool("has_loc", false);
  prefs_.putBool("has_tz", false);
}

void ConfigStore::saveLocation(float lat, float lon, const String &city, const String &state) {
  latitude_ = lat;
  longitude_ = lon;
  city_ = city;
  state_ = state;
  hasLocation_ = true;

  prefs_.putFloat("lat", latitude_);
  prefs_.putFloat("lon", longitude_);
  prefs_.putString("city", city_);
  prefs_.putString("state", state_);
  prefs_.putBool("has_loc", true);
}

void ConfigStore::saveTimezone(int32_t rawOffsetSec, int32_t dstOffsetSec) {
  timezoneRawOffsetSec_ = rawOffsetSec;
  timezoneDstOffsetSec_ = dstOffsetSec;
  hasTimezone_ = true;

  prefs_.putInt("tz_raw", timezoneRawOffsetSec_);
  prefs_.putInt("tz_dst", timezoneDstOffsetSec_);
  prefs_.putBool("has_tz", true);
}

void ConfigStore::reset() {
  prefs_.clear();
  wifiSsid_ = "";
  wifiPassword_ = "";
  locationQuery_ = "";
  apiKey_ = "";
  unitsImperial_ = true;
  hasLocation_ = false;
  latitude_ = 0.0f;
  longitude_ = 0.0f;
  city_ = "";
  state_ = "";
  hasTimezone_ = false;
  timezoneRawOffsetSec_ = 0;
  timezoneDstOffsetSec_ = 0;
}
