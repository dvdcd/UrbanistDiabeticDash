#pragma once
#include <Arduino.h>

struct DashConfig {
  String wifi_ssid;
  String wifi_password;
  String dexcom_username;
  String dexcom_password;
  bool   dexcom_region_us  = true;
  int    glucose_low       = 70;
  int    glucose_high      = 180;
  int    glucose_warn_low  = 80;
  int    glucose_warn_high = 160;
  bool   unit_mgdl         = true;
};

class ConfigStore {
 public:
  // Returns false if no WiFi SSID has been saved yet (first-boot state).
  bool load(DashConfig &out);
  void save(const DashConfig &config);
  void clear();  // factory reset
};
