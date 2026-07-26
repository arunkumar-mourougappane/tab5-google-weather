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

  // Called once, from the provisioning web server, with everything the
  // setup page collected. Persists immediately.
  void saveProvisioning(const String &ssid, const String &password, const String &locationQuery,
                         const String &apiKey, bool unitsImperial);

  // Called once, after the first successful post-provisioning geocode.
  void saveLocation(float lat, float lon);

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
};
