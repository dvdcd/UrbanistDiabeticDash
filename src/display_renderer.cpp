#include "display_renderer.h"
#include <Arduino.h>
#include <time.h>

// ── Pin mapping: Matrix Portal S3 + Waveshare 64×32 (G/B channels swapped) ──
static const HUB75_I2S_CFG::i2s_pins MATRIX_PINS = {
  /*.r1 =*/ 42,
  /*.g1 =*/ 40,  // swapped with B1 for Waveshare panels
  /*.b1 =*/ 41,
  /*.r2 =*/ 38,
  /*.g2 =*/ 37,  // swapped with B2 for Waveshare panels
  /*.b2 =*/ 39,
  /*.a  =*/ 45,
  /*.b  =*/ 36,
  /*.c  =*/ 48,
  /*.d  =*/ 35,
  /*.e  =*/ 21,
  /*.lat=*/ 47,
  /*.oe =*/ 14,
  /*.clk=*/ 2
};

// ── Display geometry ──────────────────────────────────────────────────────────
//  Two 64×32 panels chained side-by-side = 128×32 total.
//
//  Layout (y=0 is top):
//    y 0..21   content area
//      Glucose value  x=1..54   y=3..18  (GFX size-2 font: 12×16 px/char)
//      Trend arrow    x=55..69  y=7..15  (ASCII size-1 chars)
//      Age            x=71..95  y=3..10  (GFX size-1 font: 6×8 px/char)
//      Status label   x=71..95  y=13..20
//      Sparkline      x=88..127 y=2..21  (40×20 px bar chart)
//    y 22..23  solid status-color strip
//    y 24..31  bottom message (1 line, size-1 font)

void DisplayRenderer::begin() {
  HUB75_I2S_CFG cfg(64, 32, 2, MATRIX_PINS);
  cfg.double_buff = true;
  dma_ = new MatrixPanel_I2S_DMA(cfg);
  dma_->begin();
  dma_->setBrightness8(200);
  fill_black();
}

void DisplayRenderer::fill_black() {
  dma_->fillScreen(dma_->color565(0, 0, 0));
  dma_->flipDMABuffer();
}

void DisplayRenderer::show_message(const char *line1, const char *line2) {
  dma_->fillScreen(dma_->color565(0, 0, 0));
  dma_->setTextSize(1);
  dma_->setTextColor(dma_->color565(255, 255, 255));
  dma_->setCursor(2, 5);
  dma_->print(line1);
  if (line2) {
    dma_->setCursor(2, 16);
    dma_->print(line2);
  }
  dma_->flipDMABuffer();
}

// ── Color helpers ─────────────────────────────────────────────────────────────

uint16_t DisplayRenderer::value_color_(int mgdl, const DashConfig &cfg) const {
  if (mgdl <= cfg.glucose_low || mgdl >= cfg.glucose_high)
    return dma_->color565(255, 34,  0);   // red
  if (mgdl <= cfg.glucose_warn_low || mgdl >= cfg.glucose_warn_high)
    return dma_->color565(255, 204, 0);   // yellow
  return dma_->color565(0, 204, 68);      // green
}

// ── Draw sub-methods ──────────────────────────────────────────────────────────

void DisplayRenderer::draw_glucose_(int value_mgdl, uint16_t color) {
  dma_->setTextSize(2);
  dma_->setTextColor(color);
  dma_->setCursor(1, 3);
  dma_->print(value_mgdl);
}

// Trend arrows rendered as ASCII characters at size 1 — avoids custom bitmaps.
// Two characters wide for DoubleUp/DoubleDown so they stay visible at this size.
void DisplayRenderer::draw_trend_arrow_(int trend_code, uint16_t color) {
  dma_->setTextSize(1);
  dma_->setTextColor(color);
  dma_->setCursor(55, 7);

  switch (trend_code) {
    case 1: dma_->print("^^"); break;   // DoubleUp
    case 2: dma_->print(" ^"); break;   // SingleUp
    case 3: dma_->print(" /"); break;   // FortyFiveUp
    case 4: dma_->print(" -"); break;   // Flat
    case 5: dma_->print(" \\"); break;  // FortyFiveDown
    case 6: dma_->print(" v"); break;   // SingleDown
    case 7: dma_->print("vv"); break;   // DoubleDown
    default: dma_->print(" ?"); break;
  }
}

void DisplayRenderer::draw_age_(time_t timestamp) {
  dma_->setTextSize(1);
  dma_->setTextColor(dma_->color565(160, 160, 160));
  dma_->setCursor(71, 3);

  time_t now = time(nullptr);
  if (now <= 0 || timestamp <= 0) {
    dma_->print("--m");
    return;
  }
  long age_s = static_cast<long>(now - timestamp);
  if (age_s < 60) {
    dma_->print("<1m");
  } else {
    long mins = age_s / 60;
    if (mins < 100) {
      dma_->print(mins);
      dma_->print("m");
    } else {
      dma_->print("99m+");
    }
  }
}

void DisplayRenderer::draw_status_label_(int mgdl, const DashConfig &cfg) {
  dma_->setTextSize(1);
  dma_->setCursor(71, 13);

  if (mgdl <= cfg.glucose_low) {
    dma_->setTextColor(dma_->color565(255, 34, 0));
    dma_->print("LOW");
  } else if (mgdl >= cfg.glucose_high) {
    dma_->setTextColor(dma_->color565(255, 34, 0));
    dma_->print("HIGH");
  } else if (mgdl <= cfg.glucose_warn_low) {
    dma_->setTextColor(dma_->color565(255, 204, 0));
    dma_->print("LOW?");
  } else if (mgdl >= cfg.glucose_warn_high) {
    dma_->setTextColor(dma_->color565(255, 204, 0));
    dma_->print("HIGH?");
  }
  // In-range: leave blank.
}

void DisplayRenderer::draw_sparkline_(const std::vector<int> &spark,
                                      const DashConfig &cfg) {
  if (spark.empty()) return;

  constexpr int X0     = 88;   // left edge of sparkline zone
  constexpr int Y0     = 2;    // top of zone
  constexpr int HEIGHT = 20;   // total height in pixels
  constexpr int WIDTH  = 40;   // total width in pixels
  constexpr int BAR_W  = 3;    // each bar is 3 px wide

  uint16_t gray = dma_->color565(40, 40, 40);

  // Faint threshold guidelines.
  auto y_for = [&](int mgdl) -> int {
    int clamped = constrain(mgdl, 40, 400);
    return Y0 + HEIGHT - 1 - map(clamped, 40, 400, 0, HEIGHT - 1);
  };
  int y_warn_lo = y_for(cfg.glucose_warn_low);
  int y_warn_hi = y_for(cfg.glucose_warn_high);
  for (int x = X0; x < X0 + WIDTH; x++) {
    dma_->drawPixel(x, y_warn_lo, gray);
    dma_->drawPixel(x, y_warn_hi, gray);
  }

  // Bars — up to 12 readings fit in 40 px at 3 px/bar (with 4 px slack).
  size_t count   = min(spark.size(), static_cast<size_t>(WIDTH / BAR_W));
  size_t start_i = spark.size() > count ? spark.size() - count : 0;

  for (size_t i = 0; i < count; i++) {
    int mgdl    = spark[start_i + i];
    int bar_h   = max(1, map(constrain(mgdl, 40, 400), 40, 400, 1, HEIGHT));
    int bx      = X0 + static_cast<int>(i) * BAR_W;
    int by      = Y0 + HEIGHT - bar_h;
    uint16_t c  = value_color_(mgdl, cfg);
    dma_->fillRect(bx, by, BAR_W - 1, bar_h, c);
  }
}

void DisplayRenderer::draw_status_bar_(uint16_t color) {
  dma_->fillRect(0, 22, 128, 2, color);
}

void DisplayRenderer::draw_bottom_msg_(const CGMData &data) {
  if (data.valid && data.error.isEmpty()) return;

  dma_->setTextSize(1);
  dma_->setCursor(2, 24);

  if (!data.valid) {
    dma_->setTextColor(dma_->color565(255, 34, 0));
    dma_->print(data.error.isEmpty() ? "NO DATA" : data.error.substring(0, 20));
  } else if (data.error == "STALE") {
    dma_->setTextColor(dma_->color565(140, 140, 140));
    dma_->print("STALE");
  }
}

// ── Main draw entry point ─────────────────────────────────────────────────────

void DisplayRenderer::draw(const CGMData &data, const DashConfig &cfg) {
  dma_->fillScreen(dma_->color565(0, 0, 0));

  if (!data.valid) {
    // Show error state — red status bar and message only.
    draw_status_bar_(dma_->color565(255, 34, 0));
    draw_bottom_msg_(data);
    dma_->flipDMABuffer();
    return;
  }

  int      mgdl  = data.current.value_mgdl;
  uint16_t color = value_color_(mgdl, cfg);

  // Alert blink: 1 Hz, implemented by blanking the glucose value every other
  // 500 ms draw call when the value is outside the hard thresholds.
  bool alert = (mgdl <= cfg.glucose_low || mgdl >= cfg.glucose_high);
  bool blink_off = alert && ((millis() / 500) % 2 == 0);

  if (!blink_off) {
    draw_glucose_(mgdl, color);
  }

  draw_trend_arrow_(data.current.trend_code, color);
  draw_age_(data.current.timestamp);
  draw_status_label_(mgdl, cfg);
  draw_sparkline_(data.sparkline, cfg);
  draw_status_bar_(color);
  draw_bottom_msg_(data);

  dma_->flipDMABuffer();
}
