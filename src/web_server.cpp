#include "web_server.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Update.h>

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
    doc["cgm_source"]         = static_cast<int>(cfg.cgm_source);
    doc["dexcom_username"]    = cfg.dexcom_username;
    // dexcom_password intentionally omitted.
    doc["dexcom_region_us"]   = cfg.dexcom_region_us;
    doc["nightscout_url"]     = cfg.nightscout_url;
    // nightscout_secret intentionally omitted.
    doc["glucose_low"]        = cfg.glucose_low;
    doc["glucose_high"]       = cfg.glucose_high;
    doc["glucose_warn_low"]   = cfg.glucose_warn_low;
    doc["glucose_warn_high"]  = cfg.glucose_warn_high;
    doc["unit_mgdl"]          = cfg.unit_mgdl;
    doc["timezone"]           = cfg.timezone;

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
        cfg.wifi_ssid           = doc["wifi_ssid"].as<String>();
      if (doc["wifi_password"].is<String>() &&
          doc["wifi_password"].as<String>().length() > 0)
        cfg.wifi_password       = doc["wifi_password"].as<String>();
      if (doc["cgm_source"].is<int>())
        cfg.cgm_source          = static_cast<CgmSource>(doc["cgm_source"].as<int>());
      if (doc["dexcom_username"].is<String>())
        cfg.dexcom_username     = doc["dexcom_username"].as<String>();
      if (doc["dexcom_password"].is<String>() &&
          doc["dexcom_password"].as<String>().length() > 0)
        cfg.dexcom_password     = doc["dexcom_password"].as<String>();
      if (doc["dexcom_region_us"].is<bool>())
        cfg.dexcom_region_us    = doc["dexcom_region_us"].as<bool>();
      if (doc["nightscout_url"].is<String>())
        cfg.nightscout_url      = doc["nightscout_url"].as<String>();
      if (doc["nightscout_secret"].is<String>() &&
          doc["nightscout_secret"].as<String>().length() > 0)
        cfg.nightscout_secret   = doc["nightscout_secret"].as<String>();
      if (doc["glucose_low"].is<int>())
        cfg.glucose_low         = doc["glucose_low"].as<int>();
      if (doc["glucose_high"].is<int>())
        cfg.glucose_high        = doc["glucose_high"].as<int>();
      if (doc["glucose_warn_low"].is<int>())
        cfg.glucose_warn_low    = doc["glucose_warn_low"].as<int>();
      if (doc["glucose_warn_high"].is<int>())
        cfg.glucose_warn_high   = doc["glucose_warn_high"].as<int>();
      if (doc["unit_mgdl"].is<bool>())
        cfg.unit_mgdl           = doc["unit_mgdl"].as<bool>();
      if (doc["timezone"].is<String>() &&
          doc["timezone"].as<String>().length() > 0)
        cfg.timezone            = doc["timezone"].as<String>();

      store_.save(cfg);
      req->send(200, "application/json", "{\"ok\":true}");

      if (restart_cb_) restart_cb_();
    }
  );

  // ── GET /update — firmware upload page ────────────────────────────────────
  // Uses JS fetch with application/octet-stream body — avoids multipart
  // parsing issues with AsyncWebServer on large binaries.
  server_.on("/update", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "text/html",
      "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Firmware Update</title>"
      "<style>"
      "body{font-family:sans-serif;background:#111;color:#eee;max-width:420px;margin:40px auto;padding:20px}"
      "h1{margin:0 0 8px}p{color:#aaa;margin:0 0 16px;font-size:.9em}"
      "input,button{width:100%;padding:10px;margin:6px 0;box-sizing:border-box;border-radius:4px;border:none}"
      "input{background:#222;color:#eee}"
      "button{background:#2a7;color:#fff;cursor:pointer;font-size:1em}"
      "button:disabled{background:#1a4a2a;color:#555;cursor:default}"
      "#status{margin-top:12px;font-size:.9em;min-height:1.2em}"
      ".err{color:#f66}.ok{color:#6f6}"
      "a{color:#888}"
      "</style></head><body>"
      "<h1>Firmware Update</h1>"
      "<p>Select firmware.bin — credentials and settings are preserved.</p>"
      "<input type='file' id='f' accept='.bin'>"
      "<button id='btn' onclick='upload()'>Upload &amp; Restart</button>"
      "<div id='status'></div>"
      "<p style='margin-top:20px'><a href='/'>&#8592; Back to config</a></p>"
      "<script>"
      "async function upload(){"
        "const file=document.getElementById('f').files[0];"
        "if(!file){alert('Select a .bin file first');return;}"
        "const btn=document.getElementById('btn');"
        "const st=document.getElementById('status');"
        "btn.disabled=true;"
        "st.textContent='Uploading '+file.name+' ('+Math.round(file.size/1024)+'KB)...';"
        "try{"
          "const r=await fetch('/update',{method:'POST',"
            "headers:{'Content-Type':'application/octet-stream'},"
            "body:file});"
          "if(r.ok){"
            "st.className='ok';"
            "st.textContent='Done! Device is restarting — reconnect in a few seconds.';"
          "}else{"
            "st.className='err';"
            "st.textContent='Failed: '+await r.text();"
            "btn.disabled=false;"
          "}"
        "}catch(e){"
          "st.className='err';"
          "st.textContent='Error: '+e;"
          "btn.disabled=false;"
        "}"
      "}"
      "</script>"
      "</body></html>"
    );
  });

  // ── POST /update — raw octet-stream body, written directly to OTA slot ────
  server_.on(
    "/update", HTTP_POST,
    [this](AsyncWebServerRequest *req) {
      bool ok = !Update.hasError();
      req->send(ok ? 200 : 500, "text/plain", ok ? "OK" : "FAILED");
      if (ok && restart_cb_) restart_cb_();
    },
    nullptr,  // no multipart upload handler
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len,
       size_t index, size_t total) {
      if (index == 0) {
        Serial.printf("[OTA] Start: %u bytes\n", total);
        if (!Update.begin(total > 0 ? total : UPDATE_SIZE_UNKNOWN, U_FLASH)) {
          Update.printError(Serial);
        }
      }
      if (!Update.hasError() && Update.write(data, len) != len) {
        Update.printError(Serial);
      }
      if (index + len >= total) {
        if (Update.end(true)) {
          Serial.printf("[OTA] Done: %u bytes\n", index + len);
        } else {
          Update.printError(Serial);
        }
      }
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
