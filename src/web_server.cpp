#include "web_server.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <stdarg.h>
#include "version.h"

// ── OTA log ring buffer + safety flag ────────────────────────────────────────
static String s_ota_log;
static constexpr size_t OTA_LOG_MAX = 4096;
// Set true when we detect a bad upload; blocks Update.end() so a corrupt
// binary is never committed as the next boot target.
static bool s_ota_bad = false;

static const char *OTA_MANIFEST_URL =
    "https://raw.githubusercontent.com/dvdcd/UrbanistDiabeticDash"
    "/main/installer/manifest-update.json";
static const char *OTA_FIRMWARE_URL =
    "https://raw.githubusercontent.com/dvdcd/UrbanistDiabeticDash"
    "/main/installer/firmware.bin";

static void ota_log(const char *fmt, ...) {
  char tmp[256];
  va_list ap; va_start(ap, fmt); vsnprintf(tmp, sizeof(tmp), fmt, ap); va_end(ap);
  Serial.print(tmp);
  s_ota_log += tmp;
  if (s_ota_log.length() > OTA_LOG_MAX)
    s_ota_log = s_ota_log.substring(s_ota_log.length() - OTA_LOG_MAX);
}

DashWebServer::DashWebServer(ConfigStore &store) : store_(store) {}

void DashWebServer::begin() {
  if (!LittleFS.begin(/*formatOnFail=*/true)) {
    Serial.println("[WebServer] LittleFS mount failed");
  }

  // ── GET /status — runtime debug info (CGM error, WiFi, uptime) ────────────
  server_.on("/status", HTTP_GET, [this](AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["ok"]          = status_valid_;
    doc["error"]       = status_error_;
    doc["value_mgdl"]  = status_mgdl_;
    doc["last_fetch_s"] = status_push_ms_ > 0
        ? (long)((millis() - status_push_ms_) / 1000UL) : -1L;
    doc["wifi_ip"]     = WiFi.localIP().toString();
    doc["wifi_rssi"]   = WiFi.RSSI();
    doc["uptime_s"]    = (long)(millis() / 1000UL);
    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  // ── GET /logs — OTA log buffer for the update page ────────────────────────
  server_.on("/logs", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "text/plain", s_ota_log);
  });

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
    doc["brightness"]         = cfg.brightness;
    // Style
    doc["clock_24h"]          = cfg.clock_24h;
    doc["pulse_speed"]        = cfg.pulse_speed;
    doc["pulse_min"]          = cfg.pulse_min;
    doc["wave_speed"]         = cfg.wave_speed;
    doc["status_bar_style"]   = cfg.status_bar_style;
    doc["show_sparkline"]     = cfg.show_sparkline;
    doc["sparkline_auto"]     = cfg.sparkline_auto;
    doc["show_age"]           = cfg.show_age;
    doc["auto_rotate"]        = cfg.auto_rotate;
    doc["show_status_label"]  = cfg.show_status_label;
    doc["show_stars"]         = cfg.show_stars;
    // Noctiluca theme
    doc["wave_enhanced"]      = cfg.wave_enhanced;
    doc["wave_tide"]          = cfg.wave_tide;
    doc["stars_const"]        = cfg.stars_const;
    doc["stars_tint"]         = cfg.stars_tint;
    doc["sparkline_wake"]     = cfg.sparkline_wake;
    doc["sparkline_sonar"]    = cfg.sparkline_sonar;
    doc["palette_noct"]       = cfg.palette_noct;
    doc["arrows_bearing"]     = cfg.arrows_bearing;
    doc["aurora"]             = cfg.aurora;
    doc["alert_sweep"]        = cfg.alert_sweep;
    doc["glucose_frame"]      = cfg.glucose_frame;
    doc["clock_chrono"]       = cfg.clock_chrono;
    // Colors as #rrggbb hex strings (safe for HTML color inputs).
    char cbuf[8];
    snprintf(cbuf, sizeof(cbuf), "#%06lx", (unsigned long)cfg.color_low);
    doc["color_low"]  = String(cbuf);
    snprintf(cbuf, sizeof(cbuf), "#%06lx", (unsigned long)cfg.color_warn);
    doc["color_warn"] = String(cbuf);
    snprintf(cbuf, sizeof(cbuf), "#%06lx", (unsigned long)cfg.color_ok);
    doc["color_ok"]   = String(cbuf);
    snprintf(cbuf, sizeof(cbuf), "#%06lx", (unsigned long)cfg.color_wave);
    doc["color_wave"] = String(cbuf);

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
      if (doc["brightness"].is<int>())
        cfg.brightness          = (uint8_t)constrain(doc["brightness"].as<int>(), 10, 255);
      // Style
      if (doc["clock_24h"].is<bool>())
        cfg.clock_24h           = doc["clock_24h"].as<bool>();
      if (doc["pulse_speed"].is<int>())
        cfg.pulse_speed         = doc["pulse_speed"].as<int>();
      if (doc["pulse_min"].is<int>())
        cfg.pulse_min           = doc["pulse_min"].as<int>();
      if (doc["wave_speed"].is<int>())
        cfg.wave_speed          = doc["wave_speed"].as<int>();
      if (doc["status_bar_style"].is<int>())
        cfg.status_bar_style    = doc["status_bar_style"].as<int>();
      if (doc["show_sparkline"].is<bool>())
        cfg.show_sparkline      = doc["show_sparkline"].as<bool>();
      if (doc["sparkline_auto"].is<bool>())
        cfg.sparkline_auto      = doc["sparkline_auto"].as<bool>();
      if (doc["show_age"].is<bool>())
        cfg.show_age            = doc["show_age"].as<bool>();
      if (doc["auto_rotate"].is<bool>())
        cfg.auto_rotate         = doc["auto_rotate"].as<bool>();
      if (doc["show_status_label"].is<bool>())
        cfg.show_status_label   = doc["show_status_label"].as<bool>();
      if (doc["show_stars"].is<bool>())
        cfg.show_stars          = doc["show_stars"].as<bool>();
      // Noctiluca theme
      if (doc["wave_enhanced"].is<bool>())   cfg.wave_enhanced   = doc["wave_enhanced"].as<bool>();
      if (doc["wave_tide"].is<bool>())       cfg.wave_tide       = doc["wave_tide"].as<bool>();
      if (doc["stars_const"].is<bool>())     cfg.stars_const     = doc["stars_const"].as<bool>();
      if (doc["stars_tint"].is<bool>())      cfg.stars_tint      = doc["stars_tint"].as<bool>();
      if (doc["sparkline_wake"].is<bool>())  cfg.sparkline_wake  = doc["sparkline_wake"].as<bool>();
      if (doc["sparkline_sonar"].is<bool>()) cfg.sparkline_sonar = doc["sparkline_sonar"].as<bool>();
      if (doc["palette_noct"].is<bool>())    cfg.palette_noct    = doc["palette_noct"].as<bool>();
      if (doc["arrows_bearing"].is<bool>())  cfg.arrows_bearing  = doc["arrows_bearing"].as<bool>();
      if (doc["aurora"].is<bool>())          cfg.aurora          = doc["aurora"].as<bool>();
      if (doc["alert_sweep"].is<bool>())     cfg.alert_sweep     = doc["alert_sweep"].as<bool>();
      if (doc["glucose_frame"].is<bool>())   cfg.glucose_frame   = doc["glucose_frame"].as<bool>();
      if (doc["clock_chrono"].is<bool>())    cfg.clock_chrono    = doc["clock_chrono"].as<bool>();
      // Colors — client sends "#rrggbb"; convert to packed uint32_t.
      auto parse_hex_color = [](const String &s) -> uint32_t {
        String h = s.startsWith("#") ? s.substring(1) : s;
        return (uint32_t)strtoul(h.c_str(), nullptr, 16);
      };
      if (doc["color_low"].is<String>())
        cfg.color_low           = parse_hex_color(doc["color_low"].as<String>());
      if (doc["color_warn"].is<String>())
        cfg.color_warn          = parse_hex_color(doc["color_warn"].as<String>());
      if (doc["color_ok"].is<String>())
        cfg.color_ok            = parse_hex_color(doc["color_ok"].as<String>());
      if (doc["color_wave"].is<String>())
        cfg.color_wave          = parse_hex_color(doc["color_wave"].as<String>());

      store_.save(cfg);
      req->send(200, "application/json", "{\"ok\":true}");

      if (restart_cb_) restart_cb_();
    }
  );

  // ── GET /ota/version — returns embedded firmware version instantly ──────────
  server_.on("/ota/version", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "application/json",
              "{\"version\":\"" FIRMWARE_VERSION "\"}");
  });

  // ── GET /ota/check — fetches manifest from GitHub, compares versions ────────
  // NOTE: HTTPClient is synchronous; this blocks the async task for ~1–5 s
  // while fetching over HTTPS. The main loop() is unaffected. Other web
  // requests queue during this window, which is fine for a single-user UI.
  server_.on("/ota/check", HTTP_GET, [](AsyncWebServerRequest *req) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, OTA_MANIFEST_URL)) {
      req->send(503, "application/json", "{\"error\":\"http.begin failed\"}");
      return;
    }
    http.setTimeout(8000);
    int code = http.GET();
    if (code != 200) {
      String msg = "{\"error\":\"fetch failed HTTP " + String(code) + "\"}";
      http.end();
      req->send(502, "application/json", msg);
      return;
    }
    String body = http.getString();
    http.end();

    JsonDocument manifest;
    if (deserializeJson(manifest, body)) {
      req->send(502, "application/json", "{\"error\":\"parse failed\"}");
      return;
    }
    const char *latest  = manifest["version"] | "";
    const char *current = FIRMWARE_VERSION;
    JsonDocument resp;
    resp["current"]          = current;
    resp["latest"]           = latest;
    resp["update_available"] = (strlen(latest) > 0 && strcmp(current, latest) != 0);
    String json; serializeJson(resp, json);
    req->send(200, "application/json", json);
  });

  // ── POST /ota/update — queues a cloud OTA download; runs from loop() ────────
  server_.on("/ota/update", HTTP_POST, [this](AsyncWebServerRequest *req) {
    if (ota_update_pending_) {
      req->send(409, "application/json", "{\"error\":\"already in progress\"}");
      return;
    }
    s_ota_log = "";
    ota_log("[OTA-cloud] Update queued\n");
    ota_update_pending_ = true;
    req->send(200, "application/json", "{\"ok\":true}");
  });

  // ── GET /update — firmware upload page ────────────────────────────────────
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
      "#bar-wrap{background:#222;border-radius:4px;height:6px;margin:8px 0;display:none}"
      "#bar{background:#2a7;height:6px;border-radius:4px;width:0%;transition:width .2s}"
      ".err{color:#f66}.ok{color:#6f6}"
      "#log-wrap{display:none;margin-top:14px}"
      "#log-wrap p{margin:0 0 4px;font-size:.8em;color:#666}"
      "#log{background:#0a0a0a;color:#5d5;font-family:monospace;font-size:11px;"
           "padding:8px;border-radius:4px;height:160px;overflow-y:auto;"
           "white-space:pre-wrap;word-break:break-all;border:1px solid #222}"
      "a{color:#888}"
      "</style></head><body>"
      "<h1>Firmware Update</h1>"
      "<p>Select firmware.bin — credentials and settings are preserved.</p>"
      "<input type='file' id='f' accept='.bin'>"
      "<button id='btn' onclick='upload()'>Upload &amp; Restart</button>"
      "<div id='bar-wrap'><div id='bar'></div></div>"
      "<div id='status'></div>"
      "<div id='log-wrap'><p>Device log</p><div id='log'></div></div>"
      "<p style='margin-top:20px'><a href='/'>&#8592; Back to config</a></p>"
      "<script>"
      "function upload(){"
        "const file=document.getElementById('f').files[0];"
        "if(!file){alert('Select a .bin file first');return;}"
        "const btn=document.getElementById('btn');"
        "const st=document.getElementById('status');"
        "const wrap=document.getElementById('bar-wrap');"
        "const bar=document.getElementById('bar');"
        "btn.disabled=true;"
        "wrap.style.display='block';"
        "st.className='';"
        "st.textContent='Starting upload...';"
        "const logEl=document.getElementById('log');"
        "const logWrap=document.getElementById('log-wrap');"
        "logWrap.style.display='block';"
        "logEl.textContent='';"
        "let logTimer=setInterval(function(){"
          "fetch('/logs').then(function(r){return r.text();}).then(function(t){"
            "logEl.textContent=t;logEl.scrollTop=logEl.scrollHeight;"
          "});"
        "},1000);"
        "const fd=new FormData();"
        "fd.append('firmware',file,'firmware.bin');"
        "const xhr=new XMLHttpRequest();"
        "xhr.timeout=120000;"
        "xhr.upload.onprogress=function(e){"
          "if(e.lengthComputable){"
            "const pct=Math.round(e.loaded/e.total*100);"
            "bar.style.width=pct+'%';"
            "st.textContent='Uploading '+pct+'%"
              " ('+Math.round(e.loaded/1024)+'/'+Math.round(e.total/1024)+' KB)';"
          "}"
        "};"
        "xhr.onload=function(){"
          "clearInterval(logTimer);"
          "wrap.style.display='none';"
          "if(xhr.status===200){"
            "st.className='ok';"
            "st.textContent='Done! Device is restarting — reconnect in a few seconds.';"
          "}else{"
            "st.className='err';"
            "st.textContent='Failed ('+xhr.status+'): '+xhr.responseText;"
            "btn.disabled=false;"
          "}"
        "};"
        "xhr.onerror=function(){"
          "clearInterval(logTimer);"
          "wrap.style.display='none';"
          "st.className='err';"
          "st.textContent='Network error — device may have restarted. Try reconnecting.';"
          "btn.disabled=false;"
        "};"
        "xhr.ontimeout=function(){"
          "clearInterval(logTimer);"
          "wrap.style.display='none';"
          "st.className='err';"
          "st.textContent='Timed out — device may be restarting. Try reconnecting.';"
          "btn.disabled=false;"
        "};"
        "xhr.open('POST','/update');"
        "xhr.send(fd);"
      "}"
      "</script>"
      "</body></html>"
    );
  });

  // ── POST /update — multipart/form-data upload, written to OTA slot ─────────
  server_.on(
    "/update", HTTP_POST,
    // onRequest — fires after all upload chunks are processed.
    [this](AsyncWebServerRequest *req) {
      if (s_ota_bad) {
        req->send(400, "text/plain", s_ota_log.c_str());
        return;
      }
      bool ok = !Update.hasError();
      ota_log("[OTA] result: %s\n", ok ? "OK" : Update.errorString());
      req->send(ok ? 200 : 500, "text/plain", ok ? "OK" : Update.errorString());
      if (ok && restart_cb_) restart_cb_();
    },
    // onUpload — called for each chunk of the multipart body.
    [](AsyncWebServerRequest *req, const String &filename,
       size_t index, uint8_t *data, size_t len, bool final) {
      if (index == 0) {
        s_ota_log = "";
        s_ota_bad = false;
        ota_log("[OTA] start '%s'  heap=%u\n",
                filename.c_str(), (unsigned)ESP.getFreeHeap());

        // ESP32 firmware binaries always begin with magic byte 0xE9.
        // Reject anything else (e.g. littlefs.bin, wrong file) immediately
        // so we never commit a non-firmware binary as the boot target.
        if (len == 0 || data[0] != 0xE9) {
          s_ota_bad = true;
          ota_log("[OTA] REJECTED: not an ESP32 firmware "
                  "(first byte=0x%02X, expected 0xE9)\n",
                  len > 0 ? data[0] : 0);
          return;
        }

        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
          s_ota_bad = true;
          ota_log("[OTA] begin FAILED: %s\n", Update.errorString());
          return;
        }
        ota_log("[OTA] begin OK\n");
      }

      if (s_ota_bad) return;  // swallow remaining chunks after a detected error

      if (!Update.hasError()) {
        size_t written = Update.write(data, len);
        if (written != len) {
          s_ota_bad = true;
          ota_log("[OTA] write FAILED at %u: wrote %u/%u  err=%s\n",
                  (unsigned)index, (unsigned)written, (unsigned)len,
                  Update.errorString());
          Update.abort();
          return;
        }
        if ((index >> 16) != ((index + len) >> 16))
          ota_log("[OTA] %u KB written\n", (unsigned)((index + len) >> 10));
      }

      if (final) {
        size_t total = index + len;
        if (s_ota_bad || Update.hasError()) {
          Update.abort();
          ota_log("[OTA] ABORTED after errors — partition NOT marked bootable\n");
        } else if (total < 131072) {
          // A valid ESP32-S3 app is always larger than 128 KB.
          // Abort if suspiciously small to avoid bricking on partial uploads.
          Update.abort();
          s_ota_bad = true;
          ota_log("[OTA] ABORTED: binary only %u bytes — looks truncated\n",
                  (unsigned)total);
        } else if (Update.end(true)) {
          ota_log("[OTA] end OK — %u KB total\n", (unsigned)(total >> 10));
        } else {
          ota_log("[OTA] end FAILED: %s\n", Update.errorString());
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

void DashWebServer::push_status(bool valid, const String &error, int value_mgdl) {
  status_valid_   = valid;
  status_error_   = error;
  status_mgdl_    = value_mgdl;
  status_push_ms_ = millis();
}

void DashWebServer::run_ota_if_pending() {
  if (!ota_update_pending_) return;
  ota_update_pending_ = false;

  ota_log("[OTA-cloud] Downloading from GitHub...\n");
  WiFiClientSecure client;
  client.setInsecure();

  httpUpdate.rebootOnUpdate(false);
  httpUpdate.onProgress([](int cur, int total) {
    static int last_kb = 0;
    int kb = cur >> 10;
    if (kb - last_kb >= 64 || cur == total) {
      ota_log("[OTA-cloud] %d / %d KB\n", kb, total >> 10);
      last_kb = kb;
    }
  });

  t_httpUpdate_return result = httpUpdate.update(client, OTA_FIRMWARE_URL);
  switch (result) {
    case HTTP_UPDATE_OK:
      ota_log("[OTA-cloud] Flash OK — restarting\n");
      if (restart_cb_) restart_cb_();
      break;
    case HTTP_UPDATE_NO_UPDATES:
      ota_log("[OTA-cloud] Server says no update available\n");
      break;
    case HTTP_UPDATE_FAILED:
      ota_log("[OTA-cloud] FAILED: %s\n",
              httpUpdate.getLastErrorString().c_str());
      break;
  }
}
