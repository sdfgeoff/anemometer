#pragma once

#include "WindSource.h"

class WindSourceDummy : public WindSource {
 public:
  void begin() override;
  void tick(uint32_t nowMs) override;
  float readMps(float dtSeconds, uint32_t nowMs) override;
  const char* name() const override;

 private:
  float phase_ = 0.0f;
};
