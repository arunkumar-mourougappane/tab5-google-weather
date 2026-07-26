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
  // resolved lat/lon — force a re-geocode on next boot.
  hasLocation_ = false;
  prefs_.putBool("has_loc", false);
}

void ConfigStore::saveLocation(float lat, float lon) {
  latitude_ = lat;
  longitude_ = lon;
  hasLocation_ = true;

  prefs_.putFloat("lat", latitude_);
  prefs_.putFloat("lon", longitude_);
  prefs_.putBool("has_loc", true);
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
}
