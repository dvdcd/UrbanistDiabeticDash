#pragma once
#include "cgm_source.h"
#include "config_store.h"

class NightscoutSource : public CGMSource {
 public:
  explicit NightscoutSource(const DashConfig &config);

  bool fetch(CGMData &out) override;

 private:
  int    trend_str_to_code_(const char *dir);
  String hex_sha1_(const String &input);

  String url_;              // base URL, e.g. "https://mysite.fly.dev"
  String api_secret_hash_;  // hex SHA1 of the raw secret, sent as api-secret header

  unsigned long last_fetch_ms_ = 0;
  CGMData       cached_;

  static constexpr unsigned long MIN_FETCH_MS = 270000UL;
};
