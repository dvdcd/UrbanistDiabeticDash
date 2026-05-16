#pragma once
#include <ESPAsyncWebServer.h>
#include <functional>
#include "config_store.h"

class DashWebServer {
 public:
  explicit DashWebServer(ConfigStore &store);

  void begin();

  // Set a callback that fires after a successful POST /config or POST /reset.
  // The callback should set a flag; call ESP.restart() from loop(), not here.
  void set_restart_callback(std::function<void()> cb) { restart_cb_ = cb; }

  // Call from loop() — executes a queued cloud OTA update if one is pending.
  void run_ota_if_pending();

  // Call once after WiFi connects — checks manifest and flashes if a newer
  // version is available.  Blocks for the duration of the download (~10–30 s).
  // Returns true if a flash occurred (caller should restart).
  bool run_auto_update_check();

  // Update the runtime status returned by GET /status.
  // Call from loop() after each CGM fetch — not from an async handler.
  void push_status(bool valid, const String &error, int value_mgdl = 0);

 private:
  AsyncWebServer          server_{80};
  ConfigStore            &store_;
  std::function<void()>   restart_cb_;

  // Runtime status for GET /status (updated by push_status()).
  bool          status_valid_   = false;
  String        status_error_   = "No data yet";
  int           status_mgdl_    = 0;
  unsigned long status_push_ms_ = 0;

  bool          ota_update_pending_ = false;
};
