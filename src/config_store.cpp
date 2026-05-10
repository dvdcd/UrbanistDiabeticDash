#include "config_store.h"
#include <Preferences.h>

static constexpr const char *NVS_NS = "diabdash";

bool ConfigStore::load(DashConfig &out) {
  Preferences prefs;
  prefs.begin(NVS_NS, /*readOnly=*/true);
  out.wifi_ssid         = prefs.getString("wifi_ssid",   "");
  out.wifi_password     = prefs.getString("wifi_pass",   "");
  out.dexcom_username   = prefs.getString("dex_user",    "");
  out.dexcom_password   = prefs.getString("dex_pass",    "");
  out.dexcom_region_us  = prefs.getBool(  "dex_us",      true);
  out.glucose_low       = prefs.getInt(   "g_low",       70);
  out.glucose_high      = prefs.getInt(   "g_high",      180);
  out.glucose_warn_low  = prefs.getInt(   "g_warnlo",    80);
  out.glucose_warn_high = prefs.getInt(   "g_warnhi",    160);
  out.unit_mgdl         = prefs.getBool(  "unit_mgdl",   true);
  prefs.end();
  return out.wifi_ssid.length() > 0;
}

void ConfigStore::save(const DashConfig &c) {
  Preferences prefs;
  prefs.begin(NVS_NS, /*readOnly=*/false);
  prefs.putString("wifi_ssid",  c.wifi_ssid);
  prefs.putString("wifi_pass",  c.wifi_password);
  prefs.putString("dex_user",   c.dexcom_username);
  prefs.putString("dex_pass",   c.dexcom_password);
  prefs.putBool(  "dex_us",     c.dexcom_region_us);
  prefs.putInt(   "g_low",      c.glucose_low);
  prefs.putInt(   "g_high",     c.glucose_high);
  prefs.putInt(   "g_warnlo",   c.glucose_warn_low);
  prefs.putInt(   "g_warnhi",   c.glucose_warn_high);
  prefs.putBool(  "unit_mgdl",  c.unit_mgdl);
  prefs.end();
}

void ConfigStore::clear() {
  Preferences prefs;
  prefs.begin(NVS_NS, /*readOnly=*/false);
  prefs.clear();
  prefs.end();
}
