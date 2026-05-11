#include "config_store.h"
#include <Preferences.h>

static constexpr const char *NVS_NS = "diabdash";

bool ConfigStore::load(DashConfig &out) {
  Preferences prefs;
  prefs.begin(NVS_NS, /*readOnly=*/true);
  out.wifi_ssid          = prefs.getString("wifi_ssid",  "");
  out.wifi_password      = prefs.getString("wifi_pass",  "");
  out.cgm_source         = static_cast<CgmSource>(prefs.getInt("cgm_src", CGM_DEXCOM));
  out.dexcom_username    = prefs.getString("dex_user",   "");
  out.dexcom_password    = prefs.getString("dex_pass",   "");
  out.dexcom_region_us   = prefs.getBool(  "dex_us",     true);
  out.nightscout_url     = prefs.getString("ns_url",     "");
  out.nightscout_secret  = prefs.getString("ns_secret",  "");
  out.glucose_low        = prefs.getInt(   "g_low",      70);
  out.glucose_high       = prefs.getInt(   "g_high",     180);
  out.glucose_warn_low   = prefs.getInt(   "g_warnlo",   80);
  out.glucose_warn_high  = prefs.getInt(   "g_warnhi",   160);
  out.unit_mgdl          = prefs.getBool(  "unit_mgdl",  true);
  out.timezone           = prefs.getString("timezone",   "UTC0");
  out.brightness         = prefs.getUChar( "brightness",  200);
  out.clock_24h         = prefs.getBool(  "clk_24h",    false);
  out.pulse_speed       = prefs.getInt(   "pulse_spd",  1);
  out.pulse_min         = prefs.getInt(   "pulse_min",  50);
  out.wave_speed        = prefs.getInt(   "wave_spd",   1);
  out.status_bar_style  = prefs.getInt(   "bar_style",  0);
  out.show_sparkline    = prefs.getBool(  "show_spark", true);
  out.sparkline_auto    = prefs.getBool(  "spark_auto", false);
  out.show_age          = prefs.getBool(  "show_age",   true);
  out.auto_rotate       = prefs.getBool(  "auto_rot",   true);
  out.show_status_label = prefs.getBool(  "show_lbl",   true);
  out.color_low         = prefs.getUInt(  "clr_low",    0xFF2200);
  out.color_warn        = prefs.getUInt(  "clr_warn",   0xFFCC00);
  out.color_ok          = prefs.getUInt(  "clr_ok",     0x00CC44);
  out.color_wave        = prefs.getUInt(  "clr_wave",   0x0035D2);
  prefs.end();
  return out.wifi_ssid.length() > 0;
}

void ConfigStore::save(const DashConfig &c) {
  Preferences prefs;
  prefs.begin(NVS_NS, /*readOnly=*/false);
  prefs.putString("wifi_ssid",  c.wifi_ssid);
  prefs.putString("wifi_pass",  c.wifi_password);
  prefs.putInt(   "cgm_src",    static_cast<int>(c.cgm_source));
  prefs.putString("dex_user",   c.dexcom_username);
  prefs.putString("dex_pass",   c.dexcom_password);
  prefs.putBool(  "dex_us",     c.dexcom_region_us);
  prefs.putString("ns_url",     c.nightscout_url);
  prefs.putString("ns_secret",  c.nightscout_secret);
  prefs.putInt(   "g_low",      c.glucose_low);
  prefs.putInt(   "g_high",     c.glucose_high);
  prefs.putInt(   "g_warnlo",   c.glucose_warn_low);
  prefs.putInt(   "g_warnhi",   c.glucose_warn_high);
  prefs.putBool(  "unit_mgdl",  c.unit_mgdl);
  prefs.putString("timezone",   c.timezone);
  prefs.putUChar( "brightness",   c.brightness);
  prefs.putBool(  "clk_24h",     c.clock_24h);
  prefs.putInt(   "pulse_spd",   c.pulse_speed);
  prefs.putInt(   "pulse_min",   c.pulse_min);
  prefs.putInt(   "wave_spd",    c.wave_speed);
  prefs.putInt(   "bar_style",   c.status_bar_style);
  prefs.putBool(  "show_spark",  c.show_sparkline);
  prefs.putBool(  "spark_auto",  c.sparkline_auto);
  prefs.putBool(  "show_age",    c.show_age);
  prefs.putBool(  "auto_rot",    c.auto_rotate);
  prefs.putBool(  "show_lbl",    c.show_status_label);
  prefs.putUInt(  "clr_low",     c.color_low);
  prefs.putUInt(  "clr_warn",    c.color_warn);
  prefs.putUInt(  "clr_ok",      c.color_ok);
  prefs.putUInt(  "clr_wave",    c.color_wave);
  prefs.end();
}

void ConfigStore::clear() {
  Preferences prefs;
  prefs.begin(NVS_NS, /*readOnly=*/false);
  prefs.clear();
  prefs.end();
}
