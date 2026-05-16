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
  if (cfg.palette_noct) {
    // Bioluminescent palette: ocean-derived hues replacing traffic-light colors.
    if (mgdl <= cfg.glucose_low  || mgdl >= cfg.glucose_high)          return dma_->color565(255,  48,  48); // coral
    if (mgdl <= cfg.glucose_warn_low || mgdl >= cfg.glucose_warn_high)  return dma_->color565(255, 170,   0); // amber-gold
    return dma_->color565(0, 229, 204); // bioluminescent aqua
  }
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
// Positions are fixed (deterministic hash with a constant slot); each star
// has its own sine phase and speed so they twinkle independently but never
// move. Draw BEFORE all content — content overwrites stars that land under
// it, leaving them visible only in empty space.
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

  if (cfg.stars_const) {
    struct Anchor { int x, y, slot; };
    static constexpr Anchor ANCHORS[] = {
      {15,5,201},{21,6,202},{27,5,203},           // belt
      {90,3,204},{87,8,205},{93,8,206},{90,13,207}, // cross
      {108,4,208},                                  // lone bright
    };
    for (const auto &s : ANCHORS) {
      float freq  = 0.0010f + px_hash(s.x, s.y, s.slot)    * 0.003f;
      float phase = px_hash(s.x, s.y, s.slot + 50) * 6.2832f;
      float bright = 0.5f + 0.5f * (sinf(t * freq + phase) * 0.5f + 0.5f);
      uint8_t v = (uint8_t)(bright * 255);
      dma_->drawPixel(s.x, s.y, dma_->color565(v, v, v));
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

// ── Instrument frame around glucose number ────────────────────────────────────
void DisplayRenderer::draw_glucose_frame_(int mgdl, const DashConfig &cfg) {
  if (!cfg.glucose_frame) return;
  uint16_t c = value_color_(mgdl, cfg);
  uint8_t r = (uint8_t)(((c >> 11) & 0x1F) << 3);
  uint8_t g = (uint8_t)(((c >>  5) & 0x3F) << 2);
  uint8_t b = (uint8_t)(( c        & 0x1F) << 3);
  uint16_t dim = dma_->color565(r * 28 / 100, g * 28 / 100, b * 28 / 100);
  dma_->drawFastHLine(1,  2, 54, dim);
  dma_->drawFastHLine(1, 19, 54, dim);
  for (int dy = 3; dy <= 4;  dy++) { dma_->drawPixel(1, dy, dim); dma_->drawPixel(54, dy, dim); }
  for (int dy = 17; dy <= 18; dy++) { dma_->drawPixel(1, dy, dim); dma_->drawPixel(54, dy, dim); }
}

void DisplayRenderer::draw_glucose_(int value_mgdl, uint16_t color) {
  dma_->setTextSize(2);
  dma_->setTextColor(color);
  dma_->setCursor(1, 3);
  dma_->print(value_mgdl);
}

// ── Trend arrows ──────────────────────────────────────────────────────────────

void DisplayRenderer::draw_line_thick_(int x1, int y1, int x2, int y2, uint16_t color) {
  dma_->drawLine(x1, y1, x2, y2, color);
  int adx = abs(x2 - x1), ady = abs(y2 - y1);
  if (ady > adx) dma_->drawLine(x1+1, y1, x2+1, y2, color);
  else           dma_->drawLine(x1, y1+1, x2, y2+1, color);
}

void DisplayRenderer::draw_trend_arrow_bearing_(int trend_code, uint16_t color) {
  constexpr int CX = 59, CY = 11;
  // Filled diamond tip: Manhattan distance ≤ 2
  auto tip = [&](int px, int py) {
    for (int dy = -2; dy <= 2; dy++)
      for (int dx = -2; dx <= 2; dx++)
        if (abs(dx) + abs(dy) <= 2) dma_->drawPixel(px+dx, py+dy, color);
  };
  if (trend_code == 1) {
    draw_line_thick_(CX-3, CY+5, CX-3, CY-2, color); tip(CX-3, CY-4);
    draw_line_thick_(CX+2, CY+5, CX+2, CY-2, color); tip(CX+2, CY-4);
    return;
  }
  if (trend_code == 7) {
    draw_line_thick_(CX-3, CY-5, CX-3, CY+2, color); tip(CX-3, CY+4);
    draw_line_thick_(CX+2, CY-5, CX+2, CY+2, color); tip(CX+2, CY+4);
    return;
  }
  int x1, y1, x2, y2;
  switch (trend_code) {
    case 2:  x1=CX;   y1=CY+5; x2=CX;   y2=CY-3; break; // SingleUp
    case 6:  x1=CX;   y1=CY-5; x2=CX;   y2=CY+3; break; // SingleDown
    case 3:  x1=CX-4; y1=CY+4; x2=CX+3; y2=CY-3; break; // 45Up
    case 5:  x1=CX-4; y1=CY-4; x2=CX+3; y2=CY+3; break; // 45Down
    default: x1=CX-5; y1=CY;   x2=CX+3; y2=CY;   break; // Flat
  }
  draw_line_thick_(x1, y1, x2, y2, color);
  tip(x2, y2);
}

void DisplayRenderer::draw_trend_arrow_(int trend_code, uint16_t color,
                                        const DashConfig &cfg) {
  if (cfg.arrows_bearing) { draw_trend_arrow_bearing_(trend_code, color); return; }

  constexpr int CX = 59, CY = 11;
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

  // Sonar persistence: ghost previous frame at 25% brightness
  if (cfg.sparkline_sonar && !sparkline_prev_.empty()) {
    for (const auto &p : sparkline_prev_) {
      uint8_t r = (uint8_t)(((p.color >> 11) & 0x1F) << 3) / 4;
      uint8_t g = (uint8_t)(((p.color >>  5) & 0x3F) << 2) / 4;
      uint8_t b = (uint8_t)(( p.color        & 0x1F) << 3) / 4;
      dma_->drawPixel(p.x, p.y, dma_->color565(r, g, b));
    }
  }

  // Abyss fill: dim gradient from reading color (top) to wave color (bottom)
  if (cfg.sparkline_wake && count > 0) {
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

  // Draw line chart — wake trail 1px below before each segment
  std::vector<SparkPoint> new_prev;
  if (cfg.sparkline_sonar) new_prev.reserve(count);

  for (size_t i = 1; i < count; i++) {
    int x1 = px_for(i - 1), y1 = y_for(spark[start_i + i - 1]);
    int x2 = px_for(i),     y2 = y_for(spark[start_i + i]);
    uint16_t c = value_color_(spark[start_i + i], cfg);
    if (cfg.sparkline_wake) {
      uint8_t r = (uint8_t)(((c >> 11) & 0x1F) << 3) / 4;
      uint8_t g = (uint8_t)(((c >>  5) & 0x3F) << 2) / 4;
      uint8_t b = (uint8_t)(( c        & 0x1F) << 3) / 4;
      dma_->drawLine(x1, y1+1, x2, y2+1, dma_->color565(r, g, b));
    }
    dma_->drawLine(x1, y1, x2, y2, c);
    if (cfg.sparkline_sonar) new_prev.push_back({(int16_t)x1, (int16_t)y1, c});
  }
  if (count > 0) {
    size_t last = count - 1;
    int lx = px_for(last), ly = y_for(spark[start_i + last]);
    uint16_t lc = value_color_(spark[start_i + last], cfg);
    dma_->drawPixel(lx, ly, lc);
    if (cfg.sparkline_sonar) new_prev.push_back({(int16_t)lx, (int16_t)ly, lc});
  }

  sparkline_prev_ = cfg.sparkline_sonar ? std::move(new_prev) : std::vector<SparkPoint>{};
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

  // Tide: amplitude/baseline shift with glucose zone
  float amplitude = 1.5f, baseline = 2.5f;
  if (cfg.wave_tide) {
    bool is_alert = mgdl <= cfg.glucose_low  || mgdl >= cfg.glucose_high;
    bool is_warn  = mgdl <= cfg.glucose_warn_low || mgdl >= cfg.glucose_warn_high;
    if (is_alert)      { amplitude = 3.8f; baseline = 3.2f; }
    else if (is_warn)  { amplitude = 2.5f; baseline = 2.8f; }
  }

  // Pre-compute surface per column (reused by reflection and particle passes)
  float surfaces[128];
  for (int x = 0; x < 128; x++) {
    float wave = sinf(x * 0.130f - ft * 3.00f)
               + sinf(x * 0.071f + ft * 2.10f) * 0.55f
               + sinf(x * 0.211f - ft * 3.30f) * 0.35f
               + sinf(x * 0.047f + ft * 1.40f) * 0.25f;
    surfaces[x] = baseline + amplitude * (wave / 2.15f);
  }

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

      // Abyssal depth: lerp hue toward midnight indigo at bottom rows
      if (cfg.wave_enhanced) {
        float depth = (float)row / (WAVE_H - 1);
        r_out = (uint8_t)(r_out * (1.0f - depth * 0.75f) + 15.0f * depth);
        g_out = (uint8_t)(g_out * (1.0f - depth * 0.85f));
        float b_abyss = b_out * (1.0f - depth * 0.2f) + 45.0f * depth;
        b_out = (b_abyss > 255.0f) ? 255 : (uint8_t)b_abyss;
      }

      dma_->drawPixel(x, WAVE_Y + row, dma_->color565(r_out, g_out, b_out));
    }
  }

  if (cfg.wave_enhanced) {
    static constexpr int STAR_SLOT = 42;

    // Starlight reflections at wave surface
    float reflPhase = t * 0.0007f;
    for (int x = 0; x < 128; x++) {
      if (px_hash(x, 77, STAR_SLOT) < 0.022f) {
        float v = (0.10f + 0.08f * px_hash(x, 78, STAR_SLOT)) * 230.0f;
        uint8_t iv = (uint8_t)v;
        int wobble = (int)(sinf(reflPhase + x * 0.5f));
        int rx = constrain(x + wobble, 0, 127);
        int reflRow = constrain((int)surfaces[x] + 1, 0, WAVE_H - 1);
        dma_->drawPixel(rx, WAVE_Y + reflRow,
                        dma_->color565(iv, iv, (uint8_t)fminf(255.0f, v * 1.15f)));
      }
    }

    // Plankton particles rising above wave crest
    unsigned long slot = t / 900UL;
    float phase = (float)(t % 900UL) / 900.0f;
    for (int x = 0; x < 128; x++) {
      if (px_hash(x, 99, (int)slot) > 0.94f) {
        float alpha = fmaxf(0.0f, 1.0f - phase * 1.5f);
        if (alpha > 0.0f) {
          uint8_t v = (uint8_t)(alpha * 200.0f);
          int drift    = (int)(phase * 3.0f);
          int crestRow = WAVE_Y + constrain((int)surfaces[x] - 1, 0, WAVE_H - 1);
          int py = crestRow - drift;
          if (py >= 0) dma_->drawPixel(x, py, dma_->color565(v, (uint8_t)(v * 0.92f), v));
        }
      }
    }
  }
}

// 3×5 chronometer pixel font. Column bitmaps: bit0=top row, bit4=bottom row.
struct ChronoGlyph { uint8_t ncols, cols[4]; };
static const ChronoGlyph CHRONO_FONT[] = {
  {3,{31,17,31}}, // '0'
  {3,{18,31,16}}, // '1'
  {3,{29,21,23}}, // '2'
  {3,{21,21,31}}, // '3'
  {3,{ 7, 4,31}}, // '4'
  {3,{23,21,29}}, // '5'
  {3,{31,21,29}}, // '6'
  {3,{ 1, 1,31}}, // '7'
  {3,{31,21,31}}, // '8'
  {3,{23,21,31}}, // '9'
  {1,{10,0,0,0}}, // ':'  (dots at rows 1 and 3)
  {2,{ 0, 0}},    // ' '
  {3,{31, 5,31}}, // 'A'
  {3,{31, 5, 7}}, // 'P'
  {4,{31, 3, 3,31}}, // 'M'
};

static const char *CHRONO_CHARS = "0123456789: APMM"; // index map

void DisplayRenderer::draw_chrono_(const char *text, int cx, int cy, uint16_t color) {
  auto glyph_for = [](char ch) -> const ChronoGlyph * {
    const char *p = strchr("0123456789: APM", ch);
    return p ? &CHRONO_FONT[p - "0123456789: APM"] : &CHRONO_FONT[11]; // fallback space
  };
  int x = cx;
  for (const char *p = text; *p; p++) {
    const ChronoGlyph &g = *glyph_for(*p);
    for (int col = 0; col < g.ncols; col++) {
      for (int row = 0; row < 5; row++) {
        if (g.cols[col] & (1 << row))
          dma_->drawPixel(x + col, cy + row, color);
      }
    }
    x += g.ncols + 1;
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

  if (cfg.clock_chrono) {
    draw_chrono_(buf, 2, 25, dma_->color565(80, 200, 215));
  } else {
    dma_->setTextSize(1);
    dma_->setCursor(2, 24);
    dma_->setTextColor(dma_->color565(220, 220, 220));
    dma_->print(buf);
  }
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
    uint16_t alert_c = cfg.palette_noct
      ? dma_->color565(255, 48, 48)
      : dma_->color565((cfg.color_low >> 16) & 0xFF,
                       (cfg.color_low >>  8) & 0xFF,
                        cfg.color_low        & 0xFF);
    draw_status_bar_(alert_c, 40 /* treat as low */, cfg);
    draw_bottom_msg_(data, cfg);
    dma_->flipDMABuffer();
    return;
  }

  int      mgdl  = data.current.value_mgdl;
  uint16_t color = value_color_(mgdl, cfg);
  bool     alert = (mgdl <= cfg.glucose_low || mgdl >= cfg.glucose_high);

  draw_glucose_frame_(mgdl, cfg);

  if (alert) {
    float freq  = (cfg.pulse_speed == 0) ? 0.003142f
                : (cfg.pulse_speed == 2) ? 0.012566f
                :                          0.006283f;
    float min_f = cfg.pulse_min / 100.0f;
    float pulse = (1.0f + min_f) * 0.5f + (1.0f - min_f) * 0.5f * sinf(now_ms * freq);
    uint8_t r = (uint8_t)(((color >> 11) & 0x1F) * 8 * pulse);
    uint8_t g = (uint8_t)(((color >>  5) & 0x3F) * 4 * pulse);
    uint8_t b = (uint8_t)(( color        & 0x1F) * 8 * pulse);
    draw_glucose_(mgdl, dma_->color565(r, g, b));
  } else {
    draw_glucose_(mgdl, color);
  }

  draw_trend_arrow_(data.current.trend_code, color, cfg);
  draw_age_(data.current.timestamp, cfg);
  draw_status_label_(mgdl, cfg);
  draw_sparkline_(data.sparkline, cfg);
  draw_status_bar_(color, mgdl, cfg);
  draw_bottom_msg_(data, cfg);

  // Alert sweep: bioluminescent flash races L→R each pulse cycle
  if (cfg.alert_sweep && alert) {
    unsigned long period = (cfg.pulse_speed == 0) ? 2000UL
                         : (cfg.pulse_speed == 2) ?  500UL : 1000UL;
    unsigned long phase_ms = now_ms % period;
    unsigned long sweep_window = period * 38 / 100;
    if (phase_ms < sweep_window) {
      int sweepX = (int)((float)phase_ms / sweep_window * 134);
      float fade = 1.0f - (float)phase_ms / sweep_window;
      for (int y = 0; y < 32; y++) {
        auto sv = [&](float f) -> uint8_t { return (uint8_t)(fade * f); };
        if (sweepX     < 128) dma_->drawPixel(sweepX,   y, dma_->color565(0, sv(170), sv(140)));
        if (sweepX - 1 >= 0)  dma_->drawPixel(sweepX-1, y, dma_->color565(0, sv(55),  sv(45)));
        if (sweepX - 2 >= 0)  dma_->drawPixel(sweepX-2, y, dma_->color565(0, sv(18),  sv(15)));
      }
    }
  }

  dma_->flipDMABuffer();
}
