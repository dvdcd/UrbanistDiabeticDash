#pragma once
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <vector>
#include "cgm_source.h"
#include "config_store.h"

class DisplayRenderer {
 public:
  void begin();

  void draw(const CGMData &data, const DashConfig &config);
  void show_message(const char *line1, const char *line2 = nullptr);
  void fill_black();

  void set_flipped(bool f) { flipped_ = f; }
  void set_brightness(uint8_t b) { if (dma_) dma_->setBrightness8(b); }

  MatrixPanel_I2S_DMA *panel() { return dma_; }

 private:
  void draw_aurora_(unsigned long t, const DashConfig &cfg);
  void draw_sparkles_(const DashConfig &cfg);
  void draw_glucose_frame_(int mgdl, const DashConfig &cfg);
  void draw_glucose_(int value_mgdl, uint16_t color);
  void draw_trend_arrow_(int trend_code, uint16_t color, const DashConfig &cfg);
  void draw_trend_arrow_bearing_(int trend_code, uint16_t color);
  void draw_line_thick_(int x1, int y1, int x2, int y2, uint16_t color);
  void draw_age_(time_t timestamp, const DashConfig &cfg);
  void draw_status_label_(int value_mgdl, const DashConfig &cfg);
  void draw_sparkline_(const std::vector<int> &spark, const DashConfig &cfg);
  void draw_status_bar_(uint16_t color, int mgdl, const DashConfig &cfg);
  void draw_bottom_msg_(const CGMData &data, const DashConfig &cfg);
  void draw_chrono_(const char *text, int cx, int cy, uint16_t color);

  uint16_t value_color_(int mgdl, const DashConfig &cfg) const;

  struct SparkPoint { int16_t x, y; uint16_t color; };
  std::vector<SparkPoint> sparkline_prev_;

  MatrixPanel_I2S_DMA *dma_     = nullptr;
  bool                 flipped_ = false;
};
