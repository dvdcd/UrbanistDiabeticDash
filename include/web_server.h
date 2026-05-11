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

 private:
  AsyncWebServer          server_{80};
  ConfigStore            &store_;
  std::function<void()>   restart_cb_;
};
