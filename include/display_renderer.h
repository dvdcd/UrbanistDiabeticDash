#pragma once
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "cgm_source.h"
#include "config_store.h"

class DisplayRenderer {
 public:
  void begin();

  // Called every 500 ms from loop() to handle blink animation.
  void draw(const CGMData &data, const DashConfig &config);

  // Show a simple two-line message (used during boot/setup states).
  void show_message(const char *line1, const char *line2 = nullptr);

  void fill_black();

  MatrixPanel_I2S_DMA *panel() { return dma_; }

 private:
  void draw_glucose_(int value_mgdl, uint16_t color);
  void draw_trend_arrow_(int trend_code, uint16_t color);
  void draw_age_(time_t timestamp);
  void draw_status_label_(int value_mgdl, const DashConfig &cfg);
  void draw_sparkline_(const std::vector<int> &spark, const DashConfig &cfg);
  void draw_status_bar_(uint16_t color);
  void draw_bottom_msg_(const CGMData &data);

  uint16_t value_color_(int mgdl, const DashConfig &cfg) const;

  MatrixPanel_I2S_DMA *dma_ = nullptr;
};
