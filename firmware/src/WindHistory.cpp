#include "WindHistory.h"

void WindHistory::push(uint32_t tsSeconds, float mps) {
  samples_[head_] = WindSample{tsSeconds, mps};
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

  size_t idx = (head_ + kCapacity - 1) % kCapacity;
  out = samples_[idx];
  return true;
}

size_t WindHistory::copyLastSeconds(uint32_t windowSeconds, WindSample* out,
                                    size_t maxOut) const {
  if (count_ == 0 || maxOut == 0) {
    return 0;
  }

  const size_t maxCopy = count_ < maxOut ? count_ : maxOut;
  const size_t latestIdx = (head_ + kCapacity - 1) % kCapacity;
  const uint32_t cutoffTs = (samples_[latestIdx].tsSeconds > windowSeconds)
                                ? (samples_[latestIdx].tsSeconds - windowSeconds)
                                : 0;

  size_t written = 0;
  const size_t oldestOffset = count_ - maxCopy;
  for (size_t i = oldestOffset; i < count_; i++) {
    const size_t idx = (head_ + kCapacity - count_ + i) % kCapacity;
    if (samples_[idx].tsSeconds >= cutoffTs) {
      out[written++] = samples_[idx];
    }
  }
  return written;
}

size_t WindHistory::copyAll(WindSample* out, size_t maxOut) const {
  if (count_ == 0 || maxOut == 0) {
    return 0;
  }

  const size_t maxCopy = count_ < maxOut ? count_ : maxOut;
  const size_t oldestOffset = count_ - maxCopy;

  for (size_t i = 0; i < maxCopy; i++) {
    const size_t idx = (head_ + kCapacity - count_ + oldestOffset + i) % kCapacity;
    out[i] = samples_[idx];
  }

  return maxCopy;
}
