#pragma once

#include <Arduino.h>

struct WindSample {
  uint32_t tsSeconds;
  float mps;
};

class WindHistory {
 public:
  static constexpr size_t kCapacity = 7 * 24 * 60 * 2;  // 30-second samples for 7 days.
  static constexpr uint32_t kSamplePeriodSeconds = 30;

  void push(uint32_t tsSeconds, float mps);

  size_t size() const;
  bool latest(WindSample& out) const;
  bool getFromOldest(size_t offset, WindSample& out) const;

 private:
  static uint16_t encodeMps(float mps);
  static float decodeMps(uint16_t encoded);

  uint16_t samples_[kCapacity] = {};
  uint32_t latestTsSeconds_ = 0;
  size_t count_ = 0;
  size_t head_ = 0;
};
