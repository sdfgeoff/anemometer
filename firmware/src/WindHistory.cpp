#include "WindHistory.h"

#include <math.h>

uint16_t WindHistory::encodeMps(float mps) {
  if (mps < 0.0f) {
    mps = 0.0f;
  }

  const float scaled = mps * 10.0f;  // 0.1 m/s resolution.
  if (scaled >= 65535.0f) {
    return 65535;
  }
  return (uint16_t)lroundf(scaled);
}

float WindHistory::decodeMps(uint16_t encoded) {
  return ((float)encoded) / 10.0f;
}

void WindHistory::push(uint32_t tsSeconds, float mps) {
  samples_[head_] = encodeMps(mps);
  latestTsSeconds_ = tsSeconds;

  head_ = (head_ + 1) % kCapacity;
  if (count_ < kCapacity) {
    count_++;
  }
}

size_t WindHistory::size() const {
  return count_;
}

bool WindHistory::latest(WindSample& out) const {
  if (count_ == 0) {
    return false;
  }

  const size_t idx = (head_ + kCapacity - 1) % kCapacity;
  out.tsSeconds = latestTsSeconds_;
  out.mps = decodeMps(samples_[idx]);
  return true;
}

bool WindHistory::getFromOldest(size_t offset, WindSample& out) const {
  if (offset >= count_) {
    return false;
  }

  const size_t idx = (head_ + kCapacity - count_ + offset) % kCapacity;
  out.mps = decodeMps(samples_[idx]);

  const uint32_t oldestTs = latestTsSeconds_ - (uint32_t)((count_ - 1) * kSamplePeriodSeconds);
  out.tsSeconds = oldestTs + (uint32_t)(offset * kSamplePeriodSeconds);
  return true;
}
