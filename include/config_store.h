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
  bool      show_stars         = true;   // twinkle stars in background
  // Colors (packed 0x00RRGGBB)
  uint32_t  color_low          = 0xFF2200;  // low/high alert
  uint32_t  color_warn         = 0xFFCC00;  // warning threshold
  uint32_t  color_ok           = 0x00CC44;  // in-range
  uint32_t  color_wave         = 0x0035D2;  // wave base color
  // Noctiluca theme (all optional, off by default)
  bool wave_enhanced    = false;  // abyssal depth gradient + starlight reflection + plankton particles
  bool wave_tide        = false;  // wave amplitude/baseline encodes glucose zone (calm → rough)
  bool stars_const      = false;  // hand-placed constellation anchor stars
  bool stars_tint       = false;  // horizon glow: lower stars blended toward wave color
  bool sparkline_wake   = false;  // wake trail below line + abyss gradient fill
  bool sparkline_sonar  = false;  // phosphor persistence — previous frame ghosted at 25%
  bool palette_noct     = false;  // bioluminescent palette: aqua / amber-gold / coral
  bool arrows_bearing   = false;  // thick shafts + filled diamond heads
  bool aurora           = false;  // faint teal/violet aurora bands in content background
  bool alert_sweep      = false;  // bioluminescent flash sweeps L→R on each alert pulse
  bool glucose_frame    = false;  // thin instrument frame above/below glucose number
  bool clock_chrono     = false;  // 3×5 chronometer pixel font for clock
};

class ConfigStore {
 public:
  // Returns false if no WiFi SSID has been saved yet (first-boot state).
  bool load(DashConfig &out);
  void save(const DashConfig &config);
  void clear();  // factory reset
};
