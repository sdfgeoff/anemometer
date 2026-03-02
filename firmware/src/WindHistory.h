#pragma once

#include <stddef.h>
#include <stdint.h>

struct WindSample {
  uint32_t tsSeconds;
  float mps;
};

class WindHistory {
 public:
  static constexpr uint32_t kBaseSamplePeriodSeconds = 5;
  static constexpr size_t kMaxQueryPoints = 2017;

  WindHistory();

  void push(uint32_t tsSeconds, float mps);
  bool latest(WindSample& out) const;

  // Returns oldest->newest points for the requested trailing window.
  size_t copyLastSeconds(uint32_t seconds, WindSample* out, size_t maxOut) const;

  size_t totalSamples() const;

 private:
  struct EncodedSample {
    uint32_t tsSeconds;
    uint16_t mpsDeci;
  };

  struct PendingBucket {
    bool active;
    uint32_t bucketStart;
    float sum;
    uint16_t count;
  };

  struct TierState {
    EncodedSample* samples;
    size_t capacity;
    uint16_t intervalSeconds;
    uint32_t windowSeconds;

    size_t count;
    size_t head;
    PendingBucket pending;
  };

  static constexpr size_t kTierCount = 5;

  static constexpr size_t kTier5Capacity = 180;    // 15m @ 5s
  static constexpr size_t kTier10Capacity = 180;   // 30m @ 10s
  static constexpr size_t kTier30Capacity = 120;   // 1h @ 30s
  static constexpr size_t kTier60Capacity = 1440;  // 24h @ 60s
  static constexpr size_t kTier300Capacity = 2016; // 7d @ 300s

  uint16_t encodeMps(float mps) const;
  float decodeMps(uint16_t encoded) const;

  void initTiers();
  void pushToTier(TierState& tier, uint32_t tsSeconds, float mps);
  void commitPending(TierState& tier);
  void ringPush(TierState& tier, uint32_t tsSeconds, float mps);

  const TierState& selectTierForSeconds(uint32_t seconds) const;

  EncodedSample tier5_[kTier5Capacity] = {};
  EncodedSample tier10_[kTier10Capacity] = {};
  EncodedSample tier30_[kTier30Capacity] = {};
  EncodedSample tier60_[kTier60Capacity] = {};
  EncodedSample tier300_[kTier300Capacity] = {};

  TierState tiers_[kTierCount] = {};

  bool hasLatest_ = false;
  uint32_t latestTsSeconds_ = 0;
  uint16_t latestMpsDeci_ = 0;
};
