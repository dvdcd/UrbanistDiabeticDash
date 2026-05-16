#include "dexcom_source.h"
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// TCP probe to Cloudflare DNS (1.1.1.1:80) — fast, no TLS, no DNS needed.
// Returns true if a basic internet path exists.
static bool check_internet_() {
  WiFiClient client;
  client.setTimeout(3000);
  bool ok = client.connect(IPAddress(1, 1, 1, 1), 80);
  if (ok) client.stop();
  return ok;
}

DexcomSource::DexcomSource(const DashConfig &config) {
  username_ = config.dexcom_username;
  password_ = config.dexcom_password;
  base_url_ = config.dexcom_region_us
      ? "https://share2.dexcom.com"
      : "https://shareous1.dexcom.com";
}

void DexcomSource::reset_session() {
  account_id_   = "";
  session_id_   = "";
  last_fetch_ms_ = 0;
}

// Parse Dexcom wall-time format: /Date(1715000000000-0700)/
// Only the millisecond epoch before the timezone offset is needed.
time_t DexcomSource::parse_dexcom_time_(const char *wt) {
  const char *open = strchr(wt, '(');
  if (!open) return 0;
  long long ms = strtoll(open + 1, nullptr, 10);
  return static_cast<time_t>(ms / 1000LL);
}

int DexcomSource::trend_str_to_code_(const char *trend) {
  if (strcmp(trend, "DoubleUp")       == 0) return 1;
  if (strcmp(trend, "SingleUp")       == 0) return 2;
  if (strcmp(trend, "FortyFiveUp")    == 0) return 3;
  if (strcmp(trend, "Flat")           == 0) return 4;
  if (strcmp(trend, "FortyFiveDown")  == 0) return 5;
  if (strcmp(trend, "SingleDown")     == 0) return 6;
  if (strcmp(trend, "DoubleDown")     == 0) return 7;
  if (strcmp(trend, "NotComputable")  == 0) return 8;
  if (strcmp(trend, "RateOutOfRange") == 0) return 9;
  return 0;
}

// Returns the response body on HTTP 200, or "ERR:<code>:<body>" on failure.
String DexcomSource::http_post_(const String &url, const String &body) {
  WiFiClientSecure client;
  client.setInsecure();  // Dexcom's cert chain is fine; we skip pinning here.

  HTTPClient http;
  if (!http.begin(client, url)) {
    return "ERR:0:begin failed";
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept",       "application/json");
  http.setTimeout(10000);

  int code = http.POST(body);
  String resp = http.getString();
  http.end();

  if (code != 200) {
    return "ERR:" + String(code) + ":" + resp;
  }
  return resp;
}

bool DexcomSource::authenticate_(CGMData &out) {
  JsonDocument req;
  req["accountName"]   = username_;
  req["password"]      = password_;
  req["applicationId"] = APP_ID;
  String body;
  serializeJson(req, body);

  String url = base_url_ +
      "/ShareWebServices/Services/General/AuthenticatePublisherAccount";
  String resp = http_post_(url, body);

  if (resp.startsWith("ERR:")) {
    Serial.println("[Dexcom] authenticate failed: " + resp);
    out.valid = false;
    // Negative HTTP code = connection-level failure (timeout, refused, etc.).
    // Probe 1.1.1.1 to distinguish "no internet" from a Dexcom-specific error.
    long code = strtol(resp.c_str() + 4, nullptr, 10);
    out.error = (code <= 0 && !check_internet_()) ? "No internet" : "Auth: " + resp;
    return false;
  }
  resp.replace("\"", "");
  resp.trim();
  if (resp.length() < 10) {
    Serial.println("[Dexcom] unexpected account_id: " + resp);
    out.valid = false;
    out.error = "Auth: bad account_id: " + resp;
    return false;
  }
  account_id_ = resp;
  return true;
}

bool DexcomSource::login_(CGMData &out) {
  JsonDocument req;
  req["accountId"]     = account_id_;
  req["password"]      = password_;
  req["applicationId"] = APP_ID;
  String body;
  serializeJson(req, body);

  String url = base_url_ +
      "/ShareWebServices/Services/General/LoginPublisherAccountById";
  String resp = http_post_(url, body);

  if (resp.startsWith("ERR:")) {
    Serial.println("[Dexcom] login failed: " + resp);
    out.valid = false;
    out.error = "Login: " + resp;
    return false;
  }
  resp.replace("\"", "");
  resp.trim();
  if (resp.length() < 10) {
    Serial.println("[Dexcom] unexpected session_id: " + resp);
    out.valid = false;
    out.error = "Login: bad session_id: " + resp;
    return false;
  }
  session_id_ = resp;
  return true;
}

bool DexcomSource::read_glucose_(CGMData &out) {
  String url = base_url_ +
      "/ShareWebServices/Services/Publisher/ReadPublisherLatestGlucoseValues"
      "?sessionId=" + session_id_ + "&minutes=60&maxCount=12";
  String resp = http_post_(url, "");

  if (resp.startsWith("ERR:")) {
    // HTTP 500 with SessionNotValid means the session expired.
    if (resp.indexOf("SessionNotValid") >= 0 || resp.indexOf(":500:") >= 0) {
      session_id_ = "";  // trigger re-auth on next call
    }
    out.error = resp;
    out.valid = false;
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

  // Dexcom returns newest reading first.
  JsonObject latest   = arr[0];
  out.current.value_mgdl = latest["Value"] | 0;
  out.current.trend_code = trend_str_to_code_(latest["Trend"] | "Flat");
  out.current.timestamp  = parse_dexcom_time_(latest["WT"] | "");

  // Sparkline: reverse array so it's oldest-first for left→right rendering.
  out.sparkline.clear();
  for (int i = static_cast<int>(arr.size()) - 1; i >= 0; i--) {
    out.sparkline.push_back(arr[i]["Value"] | 0);
  }

  time_t now = time(nullptr);
  bool stale  = (now > 0) && ((now - out.current.timestamp) > 900);
  out.valid = true;
  out.error = stale ? "STALE" : "";

  Serial.printf("[Dexcom] glucose=%d trend=%d age=%lds\n",
      out.current.value_mgdl, out.current.trend_code,
      static_cast<long>(now - out.current.timestamp));
  return true;
}

bool DexcomSource::fetch(CGMData &out) {
  // Rate-limit: serve cached data if we fetched recently.
  if (last_fetch_ms_ > 0 && (millis() - last_fetch_ms_) < MIN_FETCH_MS) {
    out = cached_;
    return out.valid;
  }

  // Authenticate if we don't have credentials yet.
  // authenticate_() and login_() set out.error with the actual HTTP response.
  if (account_id_.isEmpty() && !authenticate_(out)) return false;
  if (session_id_.isEmpty()  && !login_(out))       return false;

  // Fetch readings; retry once if the session expired mid-flight.
  bool ok = read_glucose_(out);
  if (!ok && session_id_.isEmpty()) {
    Serial.println("[Dexcom] session expired, re-authenticating");
    if (!authenticate_(out) || !login_(out)) return false;
    ok = read_glucose_(out);
  }

  if (ok) {
    cached_        = out;
    last_fetch_ms_ = millis();
  }
  return ok;
}
