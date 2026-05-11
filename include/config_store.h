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
  // Style
  bool      clock_24h          = false;
  int       pulse_speed        = 1;     // 0=slow(0.5Hz), 1=normal(1Hz), 2=fast(2Hz)
  int       pulse_min          = 50;    // minimum brightness % during alert pulse
  int       wave_speed         = 1;     // 0=slow, 1=normal, 2=fast
  int       status_bar_style   = 0;     // 0=wave, 1=solid, 2=off
  bool      show_sparkline     = true;
  bool      sparkline_auto     = false; // false=fixed 40-400, true=auto-range
  bool      show_age           = true;
  bool      auto_rotate        = true;
  bool      show_status_label  = true;
  // Colors (packed 0x00RRGGBB)
  uint32_t  color_low          = 0xFF2200;  // low/high alert
  uint32_t  color_warn         = 0xFFCC00;  // warning threshold
  uint32_t  color_ok           = 0x00CC44;  // in-range
  uint32_t  color_wave         = 0x0035D2;  // wave base color
};

class ConfigStore {
 public:
  // Returns false if no WiFi SSID has been saved yet (first-boot state).
  bool load(DashConfig &out);
  void save(const DashConfig &config);
  void clear();  // factory reset
};
