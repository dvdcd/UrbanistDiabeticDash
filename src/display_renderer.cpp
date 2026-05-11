#include "display_renderer.h"
#include <Arduino.h>
#include <time.h>
#include <math.h>

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
//    y 22..31  animated wave strip (10 rows)
//      Clock text     x=2..~80  y=24..31  (overlaid on wave, transparent bg)

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
  dma_->setRotation(flipped_ ? 2 : 0);
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
  auto c565 = [this](uint32_t rgb) -> uint16_t {
    return dma_->color565((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
  };
  if (mgdl <= cfg.glucose_low || mgdl >= cfg.glucose_high)
    return c565(cfg.color_low);
  if (mgdl <= cfg.glucose_warn_low || mgdl >= cfg.glucose_warn_high)
    return c565(cfg.color_warn);
  return c565(cfg.color_ok);
}

// ── Draw sub-methods ──────────────────────────────────────────────────────────

void DisplayRenderer::draw_glucose_(int value_mgdl, uint16_t color) {
  dma_->setTextSize(2);
  dma_->setTextColor(color);
  dma_->setCursor(1, 3);
  dma_->print(value_mgdl);
}

// Trend arrow drawn as pixel lines — cleaner than ASCII at this scale.
// All arrows fit in a 9×11 px box centered at (x0, y0).
void DisplayRenderer::draw_trend_arrow_(int trend_code, uint16_t color) {
  constexpr int CX = 59;   // center x of arrow box
  constexpr int CY = 11;   // center y of arrow box

  // shaft endpoints and arrowhead direction encoded per trend
  // dx,dy = shaft vector (from tail to tip); head = 3-px arrowhead arms
  struct { int x1,y1,x2,y2; } shaft;
  int hx1=0,hy1=0,hx2=0,hy2=0;  // arrowhead arm endpoints relative to tip

  switch (trend_code) {
    case 1:  // DoubleUp — two vertical lines
      dma_->drawLine(CX-2, CY+4, CX-2, CY-4, color);
      dma_->drawLine(CX-2, CY-4, CX-4, CY-2, color);
      dma_->drawLine(CX-2, CY-4, CX,   CY-2, color);
      dma_->drawLine(CX+2, CY+4, CX+2, CY-4, color);
      dma_->drawLine(CX+2, CY-4, CX,   CY-2, color);
      dma_->drawLine(CX+2, CY-4, CX+4, CY-2, color);
      return;
    case 7:  // DoubleDown — two vertical lines
      dma_->drawLine(CX-2, CY-4, CX-2, CY+4, color);
      dma_->drawLine(CX-2, CY+4, CX-4, CY+2, color);
      dma_->drawLine(CX-2, CY+4, CX,   CY+2, color);
      dma_->drawLine(CX+2, CY-4, CX+2, CY+4, color);
      dma_->drawLine(CX+2, CY+4, CX,   CY+2, color);
      dma_->drawLine(CX+2, CY+4, CX+4, CY+2, color);
      return;
    case 2:  // SingleUp
      shaft = {CX, CY+4, CX, CY-4};
      hx1=CX-3; hy1=CY-1; hx2=CX+3; hy2=CY-1;
      break;
    case 6:  // SingleDown
      shaft = {CX, CY-4, CX, CY+4};
      hx1=CX-3; hy1=CY+1; hx2=CX+3; hy2=CY+1;
      break;
    case 3:  // FortyFiveUp
      shaft = {CX-3, CY+3, CX+3, CY-3};
      hx1=CX; hy1=CY-3; hx2=CX+3; hy2=CY;
      break;
    case 5:  // FortyFiveDown
      shaft = {CX-3, CY-3, CX+3, CY+3};
      hx1=CX, hy1=CY+3; hx2=CX+3; hy2=CY;
      break;
    default:  // Flat / unknown
      shaft = {CX-4, CY, CX+4, CY};
      hx1=CX+1; hy1=CY-3; hx2=CX+1; hy2=CY+3;
      break;
  }
  dma_->drawLine(shaft.x1, shaft.y1, shaft.x2, shaft.y2, color);
  dma_->drawLine(shaft.x2, shaft.y2, hx1, hy1, color);
  dma_->drawLine(shaft.x2, shaft.y2, hx2, hy2, color);
}

void DisplayRenderer::draw_age_(time_t timestamp, const DashConfig &cfg) {
  if (!cfg.show_age) return;
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
  if (!cfg.show_status_label) return;
  dma_->setTextSize(1);
  dma_->setCursor(71, 13);

  if (mgdl <= cfg.glucose_low) {
    dma_->setTextColor(dma_->color565(255, 34, 0));
    dma_->print("LO");
  } else if (mgdl >= cfg.glucose_high) {
    dma_->setTextColor(dma_->color565(255, 34, 0));
    dma_->print("HI");
  } else if (mgdl <= cfg.glucose_warn_low) {
    dma_->setTextColor(dma_->color565(255, 204, 0));
    dma_->print("LO?");
  } else if (mgdl >= cfg.glucose_warn_high) {
    dma_->setTextColor(dma_->color565(255, 204, 0));
    dma_->print("HI?");
  }
  // In-range: leave blank.
}

void DisplayRenderer::draw_sparkline_(const std::vector<int> &spark,
                                      const DashConfig &cfg) {
  if (!cfg.show_sparkline || spark.empty()) return;

  constexpr int X0     = 88;
  constexpr int Y0     = 2;
  constexpr int HEIGHT = 20;
  constexpr int WIDTH  = 40;
  constexpr int BAR_W  = 3;

  size_t count   = min(spark.size(), static_cast<size_t>(WIDTH / BAR_W));
  size_t start_i = spark.size() > count ? spark.size() - count : 0;

  // Auto-range: fit y-axis to visible readings with some padding.
  int spark_lo = 40, spark_hi = 400;
  if (cfg.sparkline_auto && count > 0) {
    int mn = spark[start_i], mx = spark[start_i];
    for (size_t i = start_i + 1; i < start_i + count; i++) {
      if (spark[i] < mn) mn = spark[i];
      if (spark[i] > mx) mx = spark[i];
    }
    int rng = max(20, mx - mn);
    int pad = max(5, rng / 5);
    spark_lo = max(40,  mn - pad);
    spark_hi = min(400, mx + pad);
  }

  uint16_t gray = dma_->color565(40, 40, 40);
  auto y_for = [&](int mgdl) -> int {
    int clamped = constrain(mgdl, spark_lo, spark_hi);
    return Y0 + HEIGHT - 1 - map(clamped, spark_lo, spark_hi, 0, HEIGHT - 1);
  };
  for (int x = X0; x < X0 + WIDTH; x++) {
    dma_->drawPixel(x, y_for(cfg.glucose_warn_low),  gray);
    dma_->drawPixel(x, y_for(cfg.glucose_warn_high), gray);
  }

  for (size_t i = 0; i < count; i++) {
    int mgdl  = spark[start_i + i];
    int bar_h = max(1, (int)map(constrain(mgdl, spark_lo, spark_hi),
                                spark_lo, spark_hi, 1, HEIGHT));
    int bx    = X0 + static_cast<int>(i) * BAR_W;
    int by    = Y0 + HEIGHT - bar_h;
    dma_->fillRect(bx, by, BAR_W - 1, bar_h, value_color_(mgdl, cfg));
  }
}

// Fast integer hash for per-pixel noise — no float rand() needed.
static inline float px_hash(int x, int row, int t_slot) {
  uint32_t n = (uint32_t)(x * 1619 + row * 31337 + t_slot * 3571);
  n ^= (n << 13); n ^= (n >> 17); n ^= (n << 5);
  return (n & 0xFFFF) / 65535.0f;   // 0..1
}

// Ten-row ocean wave — asymmetric brightness model: dark sky above crest,
// bright surface, gradually darkening water depth. Wave surface position
// oscillates per column using four incommensurate sine waves + noise.
void DisplayRenderer::draw_status_bar_(uint16_t solid_color, const DashConfig &cfg) {
  constexpr int WAVE_Y = 22;
  constexpr int WAVE_H = 10;

  if (cfg.status_bar_style == 2) return;  // off

  if (cfg.status_bar_style == 1) {
    for (int row = 0; row < WAVE_H; row++)
      dma_->drawFastHLine(0, WAVE_Y + row, 128, solid_color);
    return;
  }

  unsigned long t  = millis();
  int           t4 = (int)(t >> 6);
  float speed = (cfg.wave_speed == 0) ? 0.5f : (cfg.wave_speed == 2) ? 2.0f : 1.0f;

  uint8_t wr = (cfg.color_wave >> 16) & 0xFF;
  uint8_t wg = (cfg.color_wave >> 8)  & 0xFF;
  uint8_t wb =  cfg.color_wave        & 0xFF;

  for (int x = 0; x < 128; x++) {
    float ft = t * 0.001f * speed;
    float wave = sinf(x * 0.130f - ft * 3.00f)
               + sinf(x * 0.071f + ft * 2.10f) * 0.55f
               + sinf(x * 0.211f - ft * 3.30f) * 0.35f
               + sinf(x * 0.047f + ft * 1.40f) * 0.25f;
    wave /= 2.15f;   // ~−1..1
    float surface = 2.5f + 1.5f * wave;  // oscillates ~1.0..4.0 from top of strip

    for (int row = 0; row < WAVE_H; row++) {
      float d = (float)row - surface;  // d<0 = sky (above), d>=0 = water (below)

      float bright;
      if (d < 0.0f) {
        // Sky: exponential glow near surface, dark above.
        bright = 0.03f + 0.25f * expf(d * 2.5f);
      } else {
        // Water: bright at crest, darkens with depth.
        bright = 0.12f + 0.58f * expf(-d * 0.55f);
      }

      // Noise flutter, strongest near the surface.
      float nd = fabsf(d);
      if (nd < 2.5f) bright += (px_hash(x, row, t4) - 0.5f) * 0.12f * (1.0f - nd * 0.4f);
      if (bright < 0.0f) bright = 0.0f;
      if (bright > 1.0f) bright = 1.0f;

      uint8_t r_out = (uint8_t)(wr * bright);
      uint8_t g_out = (uint8_t)(wg * bright);
      // Constant blue floor keeps deep water from going pure black.
      float b_raw = wb * bright + 18.0f * (1.0f - bright);
      uint8_t b_out = (b_raw > 255.0f) ? 255 : (uint8_t)b_raw;

      dma_->drawPixel(x, WAVE_Y + row, dma_->color565(r_out, g_out, b_out));
    }
  }
}

void DisplayRenderer::draw_bottom_msg_(const CGMData &data, const DashConfig &cfg) {
  dma_->setTextSize(1);
  dma_->setCursor(2, 24);

  if (!data.valid) {
    dma_->setTextColor(dma_->color565(255, 80, 60));
    dma_->print(data.error.isEmpty() ? "NO DATA" : data.error.substring(0, 20));
    return;
  }
  if (data.error == "STALE") {
    dma_->setTextColor(dma_->color565(200, 200, 200));
    dma_->print("STALE");
    return;
  }

  time_t now = time(nullptr);
  if (now <= 0) return;
  struct tm t;
  localtime_r(&now, &t);
  char buf[9];
  if (cfg.clock_24h) {
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  } else {
    int hour = t.tm_hour % 12;
    if (hour == 0) hour = 12;
    snprintf(buf, sizeof(buf), "%d:%02d %s", hour, t.tm_min,
             t.tm_hour < 12 ? "AM" : "PM");
  }
  // Bright white so the clock reads clearly over the wave background.
  dma_->setTextColor(dma_->color565(220, 220, 220));
  dma_->print(buf);
}

// ── Main draw entry point ─────────────────────────────────────────────────────

void DisplayRenderer::draw(const CGMData &data, const DashConfig &cfg) {
  dma_->setRotation(flipped_ ? 2 : 0);
  dma_->fillScreen(dma_->color565(0, 0, 0));

  if (!data.valid) {
    uint16_t alert_c = dma_->color565((cfg.color_low >> 16) & 0xFF,
                                      (cfg.color_low >>  8) & 0xFF,
                                       cfg.color_low        & 0xFF);
    draw_status_bar_(alert_c, cfg);
    draw_bottom_msg_(data, cfg);
    dma_->flipDMABuffer();
    return;
  }

  int      mgdl  = data.current.value_mgdl;
  uint16_t color = value_color_(mgdl, cfg);

  bool alert = (mgdl <= cfg.glucose_low || mgdl >= cfg.glucose_high);
  if (alert) {
    // Configurable pulse: speed (0=0.5Hz, 1=1Hz, 2=2Hz) and minimum brightness.
    float freq  = (cfg.pulse_speed == 0) ? 0.003142f
                : (cfg.pulse_speed == 2) ? 0.012566f
                :                          0.006283f;
    float min_f = cfg.pulse_min / 100.0f;
    float mid   = (1.0f + min_f) * 0.5f;
    float amp   = (1.0f - min_f) * 0.5f;
    float pulse = mid + amp * sinf(millis() * freq);
    uint8_t r = (uint8_t)(((color >> 11) & 0x1F) * 8 * pulse);
    uint8_t g = (uint8_t)(((color >>  5) & 0x3F) * 4 * pulse);
    uint8_t b = (uint8_t)(( color        & 0x1F) * 8 * pulse);
    draw_glucose_(mgdl, dma_->color565(r, g, b));
  } else {
    draw_glucose_(mgdl, color);
  }

  draw_trend_arrow_(data.current.trend_code, color);
  draw_age_(data.current.timestamp, cfg);
  draw_status_label_(mgdl, cfg);
  draw_sparkline_(data.sparkline, cfg);
  draw_status_bar_(color, cfg);
  draw_bottom_msg_(data, cfg);

  dma_->flipDMABuffer();
}
