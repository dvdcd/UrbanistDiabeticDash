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
  bool      show_clock         = true;    // show clock / stale in bottom bar
  int       pulse_speed        = 1;     // 0=0.25Hz, 1=0.5Hz, 2=1Hz
  int       pulse_min          = 50;    // minimum brightness % during alert pulse
  int       wave_speed         = 0;     // 0=slow, 1=normal, 2=fast
  int       status_bar_style   = 2;     // 0=wave, 1=solid, 2=off
  bool      show_sparkline     = true;
  bool      sparkline_auto     = false; // false=fixed 40-400, true=auto-range
  bool      sparkline_shadow   = false; // drop shadow: y+1 line at 25% brightness
  bool      sparkline_fill     = false; // area gradient fill below sparkline
  bool      show_age           = true;
  bool      auto_rotate        = true;
  bool      show_status_label  = true;
  bool      show_stars         = false;  // twinkle stars in background
  // Colors (packed 0x00RRGGBB)
  uint32_t  color_low          = 0xFF2200;  // low/high alert
  uint32_t  color_warn         = 0xFFCC00;  // warning threshold
  uint32_t  color_ok           = 0x00CC44;  // in-range
  uint32_t  color_wave         = 0x0035D2;  // wave base color
  uint32_t  color_wave2        = 0x0a0020;  // second wave color (wave_enhanced lerp target)
  uint32_t  color_boat_hull    = 0xC87828;  // boat raft/hull color
  uint32_t  color_boat_sail    = 0xAA8C3C;  // boat sail color
  // Noctiluca theme (all optional, off by default)
  bool wave_enhanced    = false;  // second wave color gradient toward color_wave2
  bool wave_tide        = false;  // wave amplitude/baseline encodes glucose zone
  int  tide_strength    = 75;     // 0=no tide effect, 100=full effect
  bool stars_tint       = false;  // horizon glow: lower stars blended toward wave color
  bool boat_ride        = false;  // small boat sprite bobbing on wave surface
  bool aurora           = false;  // faint teal/violet aurora bands in content background
  // OTA
  bool auto_update      = false;  // check GitHub on boot and flash if newer
};

class ConfigStore {
 public:
  // Returns false if no WiFi SSID has been saved yet (first-boot state).
  bool load(DashConfig &out);
  void save(const DashConfig &config);
  void clear();  // factory reset
};
