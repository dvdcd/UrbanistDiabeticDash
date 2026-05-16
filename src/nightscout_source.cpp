#include "nightscout_source.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <mbedtls/md.h>
#include <time.h>

NightscoutSource::NightscoutSource(const DashConfig &config) {
  url_ = config.nightscout_url;
  // Strip trailing slash so endpoint construction is consistent.
  while (url_.endsWith("/")) url_.remove(url_.length() - 1);

  if (!config.nightscout_secret.isEmpty()) {
    api_secret_hash_ = hex_sha1_(config.nightscout_secret);
  }
}

String NightscoutSource::hex_sha1_(const String &input) {
  uint8_t hash[20];
  mbedtls_md_context_t ctx;
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, info, 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, (const uint8_t *)input.c_str(), input.length());
  mbedtls_md_finish(&ctx, hash);
  mbedtls_md_free(&ctx);

  String hex;
  hex.reserve(40);
  for (int i = 0; i < 20; i++) {
    if (hash[i] < 0x10) hex += '0';
    hex += String(hash[i], HEX);
  }
  return hex;
}

int NightscoutSource::trend_str_to_code_(const char *dir) {
  if (strcmp(dir, "DoubleUp")          == 0) return 1;
  if (strcmp(dir, "SingleUp")          == 0) return 2;
  if (strcmp(dir, "FortyFiveUp")       == 0) return 3;
  if (strcmp(dir, "Flat")              == 0) return 4;
  if (strcmp(dir, "FortyFiveDown")     == 0) return 5;
  if (strcmp(dir, "SingleDown")        == 0) return 6;
  if (strcmp(dir, "DoubleDown")        == 0) return 7;
  if (strcmp(dir, "NOT COMPUTABLE")    == 0) return 8;
  if (strcmp(dir, "NotComputable")     == 0) return 8;
  if (strcmp(dir, "RATE OUT OF RANGE") == 0) return 9;
  if (strcmp(dir, "RateOutOfRange")    == 0) return 9;
  return 0;
}

bool NightscoutSource::fetch(CGMData &out) {
  if (last_fetch_ms_ > 0 && (millis() - last_fetch_ms_) < MIN_FETCH_MS) {
    out = cached_;
    return out.valid;
  }

  String endpoint = url_ + "/api/v1/entries/sgv.json?count=12";

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, endpoint)) {
    out.valid = false;
    out.error = "HTTP begin failed";
    return false;
  }
  if (!api_secret_hash_.isEmpty()) {
    http.addHeader("api-secret", api_secret_hash_);
  }
  http.addHeader("Accept", "application/json");
  http.setTimeout(10000);

  int code = http.GET();
  String resp = http.getString();
  http.end();

  if (code != 200) {
    Serial.printf("[Nightscout] HTTP %d: %s\n", code, resp.c_str());
    out.valid = false;
    out.error = "HTTP " + String(code);
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, resp);
  if (err) {
    out.valid = false;
    out.error = String("JSON parse error: ") + err.c_str();
    return false;
  }
  if (!doc.is<JsonArray>()) {
    out.valid = false;
    out.error = "Unexpected response (not an array)";
    return false;
  }
  if (doc.size() == 0) {
    out.valid = false;
    out.error = "No data";
    return false;
  }

  JsonArray arr = doc.as<JsonArray>();

  // Nightscout returns newest-first.
  JsonObject latest     = arr[0];
  out.current.value_mgdl = latest["sgv"] | 0;
  out.current.trend_code = trend_str_to_code_(latest["direction"] | "Flat");
  long long date_ms      = latest["date"] | (long long)0;
  out.current.timestamp  = static_cast<time_t>(date_ms / 1000LL);

  // Sparkline: reverse to oldest-first for left→right rendering.
  out.sparkline.clear();
  for (int i = static_cast<int>(arr.size()) - 1; i >= 0; i--) {
    out.sparkline.push_back(arr[i]["sgv"] | 0);
  }

  time_t now = time(nullptr);
  bool stale  = (now > 0) && ((now - out.current.timestamp) > 900);
  out.valid = true;
  out.error = stale ? "STALE" : "";

  Serial.printf("[Nightscout] glucose=%d trend=%d age=%lds\n",
      out.current.value_mgdl, out.current.trend_code,
      static_cast<long>(now - out.current.timestamp));

  cached_        = out;
  last_fetch_ms_ = millis();
  return true;
}
