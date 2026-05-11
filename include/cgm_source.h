#pragma once
#include <Arduino.h>
#include <vector>
#include <time.h>

struct GlucoseReading {
  int    value_mgdl  = 0;
  int    trend_code  = 0;   // 0=None 1=DoubleUp 2=SingleUp 3=FortyFiveUp
                            // 4=Flat  5=FortyFiveDown 6=SingleDown 7=DoubleDown
                            // 8=NotComputable 9=RateOutOfRange
  time_t timestamp   = 0;  // Unix epoch seconds
};

struct CGMData {
  GlucoseReading   current;
  std::vector<int> sparkline;  // mg/dL values, oldest-first, up to 12 entries
  bool             valid = false;
  String           error;
};

class CGMSource {
 public:
  virtual ~CGMSource() = default;
  // Populate `out` and return true on success.
  // On failure, set out.valid=false, out.error, and return false.
  virtual bool fetch(CGMData &out) = 0;
};
