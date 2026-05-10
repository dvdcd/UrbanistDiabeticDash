#include "web_server.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

DashWebServer::DashWebServer(ConfigStore &store) : store_(store) {}

void DashWebServer::begin() {
  if (!LittleFS.begin(/*formatOnFail=*/true)) {
    Serial.println("[WebServer] LittleFS mount failed");
  }

  // ── Static files ────────────────────────────────────────────────────────────
  server_.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(LittleFS, "/index.html", "text/html");
  });
  server_.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(LittleFS, "/style.css", "text/css");
  });

  // ── GET /config — return non-sensitive current settings ────────────────────
  server_.on("/config", HTTP_GET, [this](AsyncWebServerRequest *req) {
    DashConfig cfg;
    store_.load(cfg);

    JsonDocument doc;
    doc["wifi_ssid"]          = cfg.wifi_ssid;
    // wifi_password intentionally omitted (write-only from the UI).
    doc["dexcom_username"]    = cfg.dexcom_username;
    // dexcom_password intentionally omitted.
    doc["dexcom_region_us"]   = cfg.dexcom_region_us;
    doc["glucose_low"]        = cfg.glucose_low;
    doc["glucose_high"]       = cfg.glucose_high;
    doc["glucose_warn_low"]   = cfg.glucose_warn_low;
    doc["glucose_warn_high"]  = cfg.glucose_warn_high;
    doc["unit_mgdl"]          = cfg.unit_mgdl;

    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  // ── POST /config — save settings and restart ───────────────────────────────
  server_.on(
    "/config", HTTP_POST,
    [](AsyncWebServerRequest *req) {},  // onRequest (unused — body comes below)
    nullptr,                            // onUpload
    [this](AsyncWebServerRequest *req, uint8_t *data, size_t len,
           size_t /*index*/, size_t /*total*/) {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, data, len);
      if (err) {
        req->send(400, "application/json", "{\"error\":\"invalid json\"}");
        return;
      }

      // Load existing config so that omitted fields keep their current values.
      DashConfig cfg;
      store_.load(cfg);

      if (doc["wifi_ssid"].is<String>())
        cfg.wifi_ssid         = doc["wifi_ssid"].as<String>();
      if (doc["wifi_password"].is<String>() &&
          doc["wifi_password"].as<String>().length() > 0)
        cfg.wifi_password     = doc["wifi_password"].as<String>();
      if (doc["dexcom_username"].is<String>())
        cfg.dexcom_username   = doc["dexcom_username"].as<String>();
      if (doc["dexcom_password"].is<String>() &&
          doc["dexcom_password"].as<String>().length() > 0)
        cfg.dexcom_password   = doc["dexcom_password"].as<String>();
      if (doc["dexcom_region_us"].is<bool>())
        cfg.dexcom_region_us  = doc["dexcom_region_us"].as<bool>();
      if (doc["glucose_low"].is<int>())
        cfg.glucose_low       = doc["glucose_low"].as<int>();
      if (doc["glucose_high"].is<int>())
        cfg.glucose_high      = doc["glucose_high"].as<int>();
      if (doc["glucose_warn_low"].is<int>())
        cfg.glucose_warn_low  = doc["glucose_warn_low"].as<int>();
      if (doc["glucose_warn_high"].is<int>())
        cfg.glucose_warn_high = doc["glucose_warn_high"].as<int>();
      if (doc["unit_mgdl"].is<bool>())
        cfg.unit_mgdl         = doc["unit_mgdl"].as<bool>();

      store_.save(cfg);
      req->send(200, "application/json", "{\"ok\":true}");

      if (restart_cb_) restart_cb_();
    }
  );

  // ── POST /reset — factory reset ────────────────────────────────────────────
  server_.on("/reset", HTTP_POST, [this](AsyncWebServerRequest *req) {
    store_.clear();
    req->send(200, "application/json", "{\"ok\":true}");
    if (restart_cb_) restart_cb_();
  });

  server_.begin();
  Serial.println("[WebServer] started on port 80");
}
