#include "WindSourceDummy.h"

#include <math.h>

void WindSourceDummy::begin() {
  randomSeed(esp_random());
}

void WindSourceDummy::tick(uint32_t nowMs) {
  (void)nowMs;
}

float WindSourceDummy::readMps(float dtSeconds, uint32_t nowMs) {
  (void)dtSeconds;

  const float t = nowMs / 1000.0f;
  const float slowWave = 4.0f + 2.2f * sinf((2.0f * PI * t) / 340.0f);
  const float gustWave = 1.3f * sinf((2.0f * PI * t) / 55.0f + phase_);
  const float noise = ((float)random(-20, 20)) / 100.0f;

  phase_ += 0.03f;
  const float mps = slowWave + gustWave + noise;
  return mps < 0.0f ? 0.0f : mps;
}

const char* WindSourceDummy::name() const {
  return "dummy";
}
