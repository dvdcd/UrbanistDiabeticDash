#include "display_renderer.h"
#include <Arduino.h>
#include <time.h>
#include <math.h>

static inline float px_hash(int x, int row, int t_slot);

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
//      Trend arrow    x=33..51  y=7..15  (CX=42, CY=11)
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

// Scatter ~20 twinkling star pixels across the dark content background.
void DisplayRenderer::draw_sparkles_(const DashConfig &cfg) {
  if (!cfg.show_stars) return;

  unsigned long t = millis();
  static constexpr int STAR_SLOT = 42;

  uint8_t wr = (cfg.color_wave >> 16) & 0xFF;
  uint8_t wg = (cfg.color_wave >>  8) & 0xFF;
  uint8_t wb =  cfg.color_wave        & 0xFF;

  for (int y = 1; y < 22; y++) {
    for (int x = 0; x < 128; x++) {
      if (px_hash(x, y, STAR_SLOT) < 0.008f) {
        float freq  = 0.0020f + px_hash(x, y, STAR_SLOT + 700) * 0.006f;
        float phase = px_hash(x, y, STAR_SLOT + 300) * 6.2832f;
        float bright = 0.25f + 0.75f * (sinf(t * freq + phase) * 0.5f + 0.5f);
        uint8_t v = (uint8_t)(bright * 230);
        if (cfg.stars_tint && y >= 17) {
          float tint = (y - 17) / 4.0f * 0.35f;
          uint8_t r = (uint8_t)(v * (1.0f - tint) + wr * tint);
          uint8_t g = (uint8_t)(v * (1.0f - tint) + wg * tint);
          uint8_t b = (uint8_t)(v * (1.0f - tint) + wb * tint);
          dma_->drawPixel(x, y, dma_->color565(r, g, b));
        } else {
          dma_->drawPixel(x, y, dma_->color565(v, v, v));
        }
      }
    }
  }
}

// ── Aurora background ─────────────────────────────────────────────────────────
void DisplayRenderer::draw_aurora_(unsigned long t, const DashConfig &cfg) {
  if (!cfg.aurora) return;
  float ft = t * 0.001f;
  float bands[128];
  for (int x = 0; x < 128; x++)
    bands[x] = (sinf(x * 0.05f + ft * 0.07f) + sinf(x * 0.09f - ft * 0.05f + 1.8f)) * 0.5f + 0.5f;
  for (int y = 0; y < 22; y++) {
    float rowFade = y < 11 ? y / 11.0f : (21 - y) / 10.0f;
    for (int x = 0; x < 128; x++) {
      float intensity = fmaxf(0.0f, bands[x] * rowFade - 0.28f);
      if (intensity > 0.0f) {
        uint8_t v = (uint8_t)(intensity * 22.0f);
        dma_->drawPixel(x, y, dma_->color565(v / 5, v, (uint8_t)fminf(255.0f, v * 1.5f)));
      }
    }
  }
}

void DisplayRenderer::draw_glucose_(int value_mgdl, uint16_t color) {
  dma_->setTextSize(2);
  dma_->setTextColor(color);
  dma_->setCursor(1, 3);
  dma_->print(value_mgdl);
}

// ── Trend arrows ──────────────────────────────────────────────────────────────

void DisplayRenderer::draw_trend_arrow_(int trend_code, uint16_t color) {
  constexpr int CX = 42, CY = 11;
  struct { int x1,y1,x2,y2; } shaft;
  int hx1=0,hy1=0,hx2=0,hy2=0;

  switch (trend_code) {
    case 1:
      dma_->drawLine(CX-2, CY+4, CX-2, CY-4, color);
      dma_->drawLine(CX-2, CY-4, CX-4, CY-2, color);
      dma_->drawLine(CX-2, CY-4, CX,   CY-2, color);
      dma_->drawLine(CX+2, CY+4, CX+2, CY-4, color);
      dma_->drawLine(CX+2, CY-4, CX,   CY-2, color);
      dma_->drawLine(CX+2, CY-4, CX+4, CY-2, color);
      return;
    case 7:
      dma_->drawLine(CX-2, CY-4, CX-2, CY+4, color);
      dma_->drawLine(CX-2, CY+4, CX-4, CY+2, color);
      dma_->drawLine(CX-2, CY+4, CX,   CY+2, color);
      dma_->drawLine(CX+2, CY-4, CX+2, CY+4, color);
      dma_->drawLine(CX+2, CY+4, CX,   CY+2, color);
      dma_->drawLine(CX+2, CY+4, CX+4, CY+2, color);
      return;
    case 2:  shaft={CX,CY+4,CX,CY-4};     hx1=CX-3;hy1=CY-1;hx2=CX+3;hy2=CY-1; break;
    case 6:  shaft={CX,CY-4,CX,CY+4};     hx1=CX-3;hy1=CY+1;hx2=CX+3;hy2=CY+1; break;
    case 3:  shaft={CX-3,CY+3,CX+3,CY-3}; hx1=CX;hy1=CY-3;hx2=CX+3;hy2=CY;     break;
    case 5:  shaft={CX-3,CY-3,CX+3,CY+3}; hx1=CX;hy1=CY+3;hx2=CX+3;hy2=CY;     break;
    default: shaft={CX-4,CY,CX+4,CY};     hx1=CX+1;hy1=CY-3;hx2=CX+1;hy2=CY+3; break;
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
  dma_->setTextColor(value_color_(mgdl, cfg));

  if      (mgdl <= cfg.glucose_low)       dma_->print("LO");
  else if (mgdl >= cfg.glucose_high)      dma_->print("HI");
  else if (mgdl <= cfg.glucose_warn_low)  dma_->print("LO?");
  else if (mgdl >= cfg.glucose_warn_high) dma_->print("HI?");
}

// Draw one horizontal row with edge pixels at 40% brightness.
void DisplayRenderer::draw_shaded_(int xL, int xR, int y,
                                   uint8_t r, uint8_t g, uint8_t b) {
  for (int x = xL; x <= xR; x++) {
    float dim = (x == xL || x == xR) ? 0.4f : 1.0f;
    dma_->drawPixel(x, y, dma_->color565(
      (uint8_t)(r * dim), (uint8_t)(g * dim), (uint8_t)(b * dim)));
  }
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
  auto px_for = [&](size_t i) -> int {
    return X0 + static_cast<int>(i) * BAR_W + 1;
  };

  for (int x = X0; x < X0 + WIDTH; x++) {
    dma_->drawPixel(x, y_for(cfg.glucose_warn_low),  gray);
    dma_->drawPixel(x, y_for(cfg.glucose_warn_high), gray);
  }

  // Area fill: gradient from reading color (top) toward wave color (bottom)
  if (cfg.sparkline_fill && count > 0) {
    uint8_t fw = (cfg.color_wave >> 16) & 0xFF;
    uint8_t gw = (cfg.color_wave >>  8) & 0xFF;
    uint8_t bw =  cfg.color_wave        & 0xFF;
    for (size_t i = 0; i < count; i++) {
      uint16_t lc = value_color_(spark[start_i + i], cfg);
      uint8_t lr = (uint8_t)(((lc >> 11) & 0x1F) << 3);
      uint8_t lg = (uint8_t)(((lc >>  5) & 0x3F) << 2);
      uint8_t lb = (uint8_t)(( lc        & 0x1F) << 3);
      int y   = y_for(spark[start_i + i]);
      int bot = Y0 + HEIGHT - 1;
      int maxDy = bot - y;
      if (maxDy <= 0) continue;
      for (int dy = 1; dy <= maxDy; dy++) {
        float tv    = (float)dy / maxDy;
        float alpha = (1.0f - tv) * 0.14f;
        uint8_t r = (uint8_t)((lr * (1.0f - tv) + fw * tv) * alpha);
        uint8_t g = (uint8_t)((lg * (1.0f - tv) + gw * tv) * alpha);
        uint8_t b = (uint8_t)((lb * (1.0f - tv) + bw * tv) * alpha);
        dma_->drawPixel(px_for(i), y + dy, dma_->color565(r, g, b));
      }
    }
  }

  // Draw line chart — optional drop shadow (y+1 at 25% brightness)
  for (size_t i = 1; i < count; i++) {
    int x1 = px_for(i - 1), y1 = y_for(spark[start_i + i - 1]);
    int x2 = px_for(i),     y2 = y_for(spark[start_i + i]);
    uint16_t c = value_color_(spark[start_i + i], cfg);
    if (cfg.sparkline_shadow) {
      uint8_t r = (uint8_t)(((c >> 11) & 0x1F) << 3) / 4;
      uint8_t g = (uint8_t)(((c >>  5) & 0x3F) << 2) / 4;
      uint8_t b = (uint8_t)(( c        & 0x1F) << 3) / 4;
      dma_->drawLine(x1, y1+1, x2, y2+1, dma_->color565(r, g, b));
    }
    dma_->drawLine(x1, y1, x2, y2, c);
  }
  if (count > 0) {
    size_t last = count - 1;
    dma_->drawPixel(px_for(last), y_for(spark[start_i + last]),
                    value_color_(spark[start_i + last], cfg));
  }
}

// Fast integer hash for per-pixel noise — no float rand() needed.
static inline float px_hash(int x, int row, int t_slot) {
  uint32_t n = (uint32_t)(x * 1619 + row * 31337 + t_slot * 3571);
  n ^= (n << 13); n ^= (n >> 17); n ^= (n << 5);
  return (n & 0xFFFF) / 65535.0f;
}

// Ten-row ocean wave — asymmetric brightness model: dark sky above crest,
// bright surface, gradually darkening water depth.
void DisplayRenderer::draw_status_bar_(uint16_t solid_color, int mgdl,
                                       const DashConfig &cfg) {
  constexpr int WAVE_Y = 22;
  constexpr int WAVE_H = 10;

  if (cfg.status_bar_style == 2) return;

  if (cfg.status_bar_style == 1) {
    for (int row = 0; row < WAVE_H; row++)
      dma_->drawFastHLine(0, WAVE_Y + row, 128, solid_color);
    return;
  }

  unsigned long t  = millis();
  int           t4 = (int)(t >> 6);
  float speed = (cfg.wave_speed == 0) ? 0.5f : (cfg.wave_speed == 2) ? 2.0f : 1.0f;
  float ft = t * 0.001f * speed;

  uint8_t wr = (cfg.color_wave >> 16) & 0xFF;
  uint8_t wg = (cfg.color_wave >>  8) & 0xFF;
  uint8_t wb =  cfg.color_wave        & 0xFF;

  // Tide: amplitude/baseline shift with glucose zone, scaled by tide_strength
  float amplitude = 1.5f, baseline = 2.5f;
  if (cfg.wave_tide) {
    float s = cfg.tide_strength / 100.0f;
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    bool is_alert = mgdl <= cfg.glucose_low  || mgdl >= cfg.glucose_high;
    bool is_warn  = mgdl <= cfg.glucose_warn_low || mgdl >= cfg.glucose_warn_high;
    if (is_alert) {
      amplitude = lerp(1.5f, 3.8f, s);
      baseline  = lerp(2.5f, 3.2f, s);
    } else if (is_warn) {
      amplitude = lerp(1.5f, 2.5f, s);
      baseline  = lerp(2.5f, 2.8f, s);
    }
  }

  // Pre-compute surface per column
  float surfaces[128];
  for (int x = 0; x < 128; x++) {
    float wave = sinf(x * 0.130f - ft * 3.00f)
               + sinf(x * 0.071f + ft * 2.10f) * 0.55f
               + sinf(x * 0.211f - ft * 3.30f) * 0.35f
               + sinf(x * 0.047f + ft * 1.40f) * 0.25f;
    surfaces[x] = baseline + amplitude * (wave / 2.15f);
  }

  // Second wave color components (lerp target for wave_enhanced)
  uint8_t d2r = (cfg.color_wave2 >> 16) & 0xFF;
  uint8_t d2g = (cfg.color_wave2 >>  8) & 0xFF;
  uint8_t d2b =  cfg.color_wave2        & 0xFF;

  // Main wave draw
  for (int x = 0; x < 128; x++) {
    float surface = surfaces[x];
    for (int row = 0; row < WAVE_H; row++) {
      float d = (float)row - surface;
      float bright = (d < 0.0f)
        ? 0.03f + 0.25f * expf(d * 2.5f)
        : 0.12f + 0.58f * expf(-d * 0.55f);
      float nd = fabsf(d);
      if (nd < 2.5f) bright += (px_hash(x, row, t4) - 0.5f) * 0.12f * (1.0f - nd * 0.4f);
      bright = fmaxf(0.0f, fminf(1.0f, bright));

      uint8_t r_out = (uint8_t)(wr * bright);
      uint8_t g_out = (uint8_t)(wg * bright);
      float b_raw   = wb * bright + 18.0f * (1.0f - bright);
      uint8_t b_out = (b_raw > 255.0f) ? 255 : (uint8_t)b_raw;

      // Second wave color: lerp hue toward color_wave2 at bottom rows
      if (cfg.wave_enhanced) {
        float depth = (float)row / (WAVE_H - 1);
        r_out = (uint8_t)(r_out * (1.0f - depth * 0.75f) + d2r * depth);
        g_out = (uint8_t)(g_out * (1.0f - depth * 0.85f) + d2g * depth);
        float b_deep = b_out * (1.0f - depth * 0.2f) + d2b * depth;
        b_out = (b_deep > 255.0f) ? 255 : (uint8_t)b_deep;
      }

      dma_->drawPixel(x, WAVE_Y + row, dma_->color565(r_out, g_out, b_out));
    }
  }

  // Boat or duck sprite bobbing on wave surface
  if (cfg.boat_ride) {
    const int bx = 62;
    const int by = WAVE_Y + constrain((int)surfaces[bx], 0, WAVE_H - 1);
    uint8_t hr = (cfg.color_boat_hull >> 16) & 0xFF;
    uint8_t hg = (cfg.color_boat_hull >>  8) & 0xFF;
    uint8_t hb =  cfg.color_boat_hull        & 0xFF;
    uint8_t sr = (cfg.color_boat_sail >> 16) & 0xFF;
    uint8_t sg = (cfg.color_boat_sail >>  8) & 0xFF;
    uint8_t sb =  cfg.color_boat_sail        & 0xFF;
    if (cfg.boat_duck) {
      // Duck: round body + head shifted right + beak pixel
      // hr/hg/hb = body color (yellow),  sr/sg/sb = beak color (orange)
      draw_shaded_(bx-2, bx+2, by,   hr, hg, hb);  // body base: 5px
      draw_shaded_(bx-3, bx+3, by-1, hr, hg, hb);  // body widest: 7px
      draw_shaded_(bx-2, bx+2, by-2, hr, hg, hb);  // body upper: 5px
      draw_shaded_(bx+1, bx+3, by-3, hr, hg, hb);  // neck/head base: 3px
      draw_shaded_(bx,   bx+3, by-4, hr, hg, hb);  // head: 4px
      dma_->drawPixel(bx+4, by-3, dma_->color565(sr, sg, sb));       // beak
      dma_->drawPixel(bx+2, by-4, dma_->color565(hr/8, hg/8, hb/8)); // eye
    } else {
      draw_shaded_(bx-3, bx+3, by,   hr, hg, hb);  // raft: 7px
      draw_shaded_(bx-2, bx+2, by-2, sr, sg, sb);  // sail base: 5px
      draw_shaded_(bx,   bx+2, by-3, sr, sg, sb);  // sail mid: 3px right-aligned
      dma_->drawPixel(bx+2, by-4, dma_->color565(sr/2, sg/2, sb/2)); // sail tip
    }
  }
}

void DisplayRenderer::draw_bottom_msg_(const CGMData &data, const DashConfig &cfg) {
  if (!data.valid) {
    dma_->setTextSize(1);
    dma_->setCursor(2, 24);
    dma_->setTextColor(dma_->color565(255, 80, 60));
    dma_->print(data.error.isEmpty() ? "NO DATA" : data.error.substring(0, 20));
    return;
  }
  if (data.error == "STALE") {
    dma_->setTextSize(1);
    dma_->setCursor(2, 24);
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
  dma_->setTextSize(1);
  dma_->setCursor(2, 24);
  dma_->setTextColor(dma_->color565(220, 220, 220));
  dma_->print(buf);
}

// ── Main draw entry point ─────────────────────────────────────────────────────

void DisplayRenderer::draw(const CGMData &data, const DashConfig &cfg) {
  dma_->setRotation(flipped_ ? 2 : 0);
  dma_->fillScreen(dma_->color565(0, 0, 0));

  unsigned long now_ms = millis();

  // Aurora first — faintest layer, everything else renders on top
  draw_aurora_(now_ms, cfg);
  draw_sparkles_(cfg);

  if (!data.valid) {
    uint16_t alert_c = dma_->color565(
      (cfg.color_low >> 16) & 0xFF,
      (cfg.color_low >>  8) & 0xFF,
       cfg.color_low        & 0xFF);
    draw_status_bar_(alert_c, 40, cfg);
    if (cfg.show_clock) draw_bottom_msg_(data, cfg);
    dma_->flipDMABuffer();
    return;
  }

  int      mgdl  = data.current.value_mgdl;
  uint16_t color = value_color_(mgdl, cfg);
  bool     alert = (mgdl <= cfg.glucose_low || mgdl >= cfg.glucose_high);

  // Compute pulse color once — used by both arrow and glucose text
  uint16_t pulse_color = color;
  if (alert) {
    float freq  = (cfg.pulse_speed == 0) ? 0.001571f   // 0.25 Hz
                : (cfg.pulse_speed == 2) ? 0.006283f   // 1.0 Hz
                :                          0.003142f;  // 0.5 Hz
    float min_f = cfg.pulse_min / 100.0f;
    float pulse = (1.0f + min_f) * 0.5f + (1.0f - min_f) * 0.5f * sinf(now_ms * freq);
    uint8_t r = (uint8_t)(((color >> 11) & 0x1F) * 8 * pulse);
    uint8_t g = (uint8_t)(((color >>  5) & 0x3F) * 4 * pulse);
    uint8_t b = (uint8_t)(( color        & 0x1F) * 8 * pulse);
    pulse_color = dma_->color565(r, g, b);
  }

  draw_trend_arrow_(data.current.trend_code, pulse_color);
  draw_glucose_(mgdl, pulse_color);
  draw_age_(data.current.timestamp, cfg);
  draw_status_label_(mgdl, cfg);
  draw_sparkline_(data.sparkline, cfg);
  draw_status_bar_(color, mgdl, cfg);
  if (cfg.show_clock) draw_bottom_msg_(data, cfg);

  dma_->flipDMABuffer();
}
