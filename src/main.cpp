#include <Arduino.h>
#include <WiFi.h>
#include "config_store.h"
#include "dexcom_source.h"
#include "nightscout_source.h"
#include "display_renderer.h"
#include "web_server.h"

// ── State machine ──────────────────────────────────────────────────────────────
enum AppState {
  STATE_AP_CONFIG,       // No WiFi creds — running AP + config web server.
  STATE_NEEDS_DEXCOM,    // WiFi ok, but no Dexcom creds yet.
  STATE_DASHBOARD,       // Fully configured — polling + rendering.
};

// ── Globals ───────────────────────────────────────────────────────────────────
static ConfigStore    config_store;
static DashConfig     config;
static DisplayRenderer renderer;
static AppState       state;

static CGMSource     *source     = nullptr;
static DashWebServer *web_server = nullptr;
static CGMData        cgm_data;

static bool           should_restart = false;
static unsigned long  last_fetch_ms  = 0;
static unsigned long  last_draw_ms   = 0;

static constexpr unsigned long POLL_INTERVAL_MS = 60000UL;  // 1 minute
static constexpr unsigned long DRAW_INTERVAL_MS = 500UL;    // 2× per second (blink)
static constexpr unsigned long WIFI_TIMEOUT_MS  = 15000UL;

// ── Helpers ───────────────────────────────────────────────────────────────────

static void start_web_server() {
  web_server = new DashWebServer(config_store);
  web_server->set_restart_callback([]() { should_restart = true; });
  web_server->begin();
}

static void start_ap_mode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("DiabeticDash-Setup");
  Serial.println("[Main] AP mode: DiabeticDash-Setup / 192.168.4.1");
  renderer.show_message("Setup mode", "192.168.4.1");
  start_web_server();
  state = STATE_AP_CONFIG;
}

static bool connect_wifi() {
  renderer.show_message("Connecting WiFi", config.wifi_ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifi_ssid.c_str(), config.wifi_password.c_str());

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("[Main] boot");

  renderer.begin();
  renderer.show_message("Starting...");

  bool has_wifi = config_store.load(config);

  if (!has_wifi || config.wifi_ssid.isEmpty()) {
    start_ap_mode();
    return;
  }

  if (!connect_wifi()) {
    Serial.println("[Main] WiFi failed, falling back to AP mode");
    renderer.show_message("WiFi failed", "Visit 192.168.4.1");
    delay(2000);
    start_ap_mode();
    return;
  }

  Serial.printf("[Main] WiFi connected: %s\n", WiFi.localIP().toString().c_str());

  // Sync time — required for stale-reading detection and clock display.
  configTzTime(config.timezone.c_str(), "pool.ntp.org", "time.nist.gov");
  Serial.printf("[Main] NTP sync requested (tz=%s)\n", config.timezone.c_str());

  start_web_server();

  bool needs_cgm_config = (config.cgm_source == CGM_NIGHTSCOUT)
      ? config.nightscout_url.isEmpty()
      : config.dexcom_username.isEmpty();

  if (needs_cgm_config) {
    char ip[20];
    WiFi.localIP().toString().toCharArray(ip, sizeof(ip));
    renderer.show_message("Configure at:", ip);
    state = STATE_NEEDS_DEXCOM;
    return;
  }

  source = (config.cgm_source == CGM_NIGHTSCOUT)
      ? static_cast<CGMSource *>(new NightscoutSource(config))
      : static_cast<CGMSource *>(new DexcomSource(config));
  state  = STATE_DASHBOARD;

  // Fetch immediately on boot rather than waiting POLL_INTERVAL_MS.
  source->fetch(cgm_data);
  renderer.draw(cgm_data, config);
  last_fetch_ms = millis();
  last_draw_ms  = millis();
}

// ── Loop ──────────────────────────────────────────────────────────────────────

void loop() {
  // Deferred restart (set by web server callback so we don't restart inside
  // an async HTTP handler).
  if (should_restart) {
    delay(1000);
    ESP.restart();
  }

  if (state == STATE_AP_CONFIG || state == STATE_NEEDS_DEXCOM) {
    // Nothing to do in loop for these states — AsyncWebServer runs in the
    // background.  Yield so the WiFi stack and async tasks stay healthy.
    delay(50);
    return;
  }

  unsigned long now = millis();

  // Redraw every DRAW_INTERVAL_MS so the 1 Hz blink animation works.
  if (now - last_draw_ms >= DRAW_INTERVAL_MS) {
    renderer.draw(cgm_data, config);
    last_draw_ms = now;
  }

  // Poll CGM source every POLL_INTERVAL_MS.
  if (now - last_fetch_ms >= POLL_INTERVAL_MS) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[Main] WiFi disconnected, attempting reconnect");
      renderer.show_message("No WiFi", "Reconnecting...");
      if (!connect_wifi()) {
        cgm_data.valid = false;
        cgm_data.error = "NO WIFI";
      }
    }

    if (WiFi.status() == WL_CONNECTED) {
      source->fetch(cgm_data);
    }

    last_fetch_ms = now;
  }
}
