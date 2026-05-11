#pragma once
#include <Arduino.h>

enum CgmSource { CGM_DEXCOM = 0, CGM_NIGHTSCOUT = 1 };

struct DashConfig {
  String    wifi_ssid;
  String    wifi_password;
  CgmSource cgm_source         = CGM_DEXCOM;
  // Dexcom Share
  String    dexcom_username;
  String    dexcom_password;
  bool      dexcom_region_us   = true;
  // Nightscout
  String    nightscout_url;    // e.g. "https://mysite.fly.dev"
  String    nightscout_secret; // raw API secret (stored in NVS, never returned by GET /config)
  // Time
  String    timezone           = "UTC0";  // POSIX TZ string
  uint8_t   brightness         = 200;
  // Display
  int       glucose_low        = 70;
  int       glucose_high       = 180;
  int       glucose_warn_low   = 80;
  int       glucose_warn_high  = 160;
  bool      unit_mgdl          = true;
};

class ConfigStore {
 public:
  // Returns false if no WiFi SSID has been saved yet (first-boot state).
  bool load(DashConfig &out);
  void save(const DashConfig &config);
  void clear();  // factory reset
};
