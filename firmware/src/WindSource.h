#pragma once

#include <Arduino.h>

class WindSource {
 public:
  virtual ~WindSource() = default;
  virtual void begin() = 0;
  virtual float readMps(float dtSeconds, uint32_t nowMs) = 0;
  virtual const char* name() const = 0;
};
