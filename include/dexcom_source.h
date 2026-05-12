#pragma once
#include "cgm_source.h"
#include "config_store.h"

class DexcomSource : public CGMSource {
 public:
  explicit DexcomSource(const DashConfig &config);

  bool fetch(CGMData &out) override;

  // Force a full re-authentication on the next fetch (e.g. after config change).
  void reset_session();

 private:
  bool    authenticate_(CGMData &out);
  bool    login_(CGMData &out);
  bool    read_glucose_(CGMData &out);
  String  http_post_(const String &url, const String &body);
  int     trend_str_to_code_(const char *trend);
  time_t  parse_dexcom_time_(const char *wt);

  String username_;
  String password_;
  String base_url_;
  String account_id_;
  String session_id_;

  unsigned long last_fetch_ms_ = 0;
  CGMData       cached_;

  // Dexcom Share public application ID (same for all third-party apps).
  static constexpr const char *APP_ID =
      "d89443d2-327c-4a6f-89e5-496bbb0317db";

  // Don't hit the API faster than every 4.5 minutes — sensor only updates
  // every 5 minutes, so more frequent calls waste battery and risk rate limits.
  static constexpr unsigned long MIN_FETCH_MS = 270000UL;
};
