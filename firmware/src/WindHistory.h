#pragma once

#include <Arduino.h>

struct WindSample {
  uint32_t tsSeconds;
  float mps;
};

class WindHistory {
 public:
  static constexpr size_t kCapacity = 7 * 24 * 60 * 2;  // 30-second samples for 7 days.

  void push(uint32_t tsSeconds, float mps);

  size_t size() const;
  bool latest(WindSample& out) const;

  size_t copyLastSeconds(uint32_t windowSeconds, WindSample* out, size_t maxOut) const;
  size_t copyAll(WindSample* out, size_t maxOut) const;

 private:
  WindSample samples_[kCapacity] = {};
  size_t count_ = 0;
  size_t head_ = 0;
};
