#pragma once

#include <stddef.h>
#include <stdint.h>

struct WindSample {
  uint32_t tsSeconds;
  float mps;
  float batteryV;
  float solarV;
};

class WindHistory {
 public:
  static constexpr uint32_t kBaseSamplePeriodSeconds = 5;
  static constexpr size_t kMaxQueryPoints = 2017;

  WindHistory();

  void push(uint32_t tsSeconds, float mps, float batteryV, float solarV);
  bool latest(WindSample& out) const;

  // Returns oldest->newest points for the requested trailing window.
  size_t copyLastSeconds(uint32_t seconds, WindSample* out, size_t maxOut) const;

  size_t totalSamples() const;

 private:
  struct EncodedSample {
    uint32_t tsSeconds;
    uint16_t mpsDeci;
    uint16_t batteryCenti;
    uint16_t solarCenti;
  };

  struct PendingBucket {
    bool active;
    uint32_t bucketStart;
    float sum;
    float batterySum;
    float solarSum;
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

  static constexpr size_t kTierCount = 2;

  static constexpr size_t kTier5Capacity = 360;    // 30m @ 5s
  static constexpr size_t kTier300Capacity = 2010; // 7d-30m @ 300s

  uint16_t encodeMps(float mps) const;
  float decodeMps(uint16_t encoded) const;
  uint16_t encodeVoltage(float volts) const;
  float decodeVoltage(uint16_t encoded) const;

  void initTiers();
  void pushToTier(TierState& tier, uint32_t tsSeconds, float mps, float batteryV, float solarV);
  void commitPending(TierState& tier);
  void ringPush(TierState& tier, uint32_t tsSeconds, float mps, float batteryV, float solarV);

  const TierState& selectTierForSeconds(uint32_t seconds) const;

  EncodedSample tier5_[kTier5Capacity] = {};
  EncodedSample tier300_[kTier300Capacity] = {};

  TierState tiers_[kTierCount] = {};

  bool hasLatest_ = false;
  uint32_t latestTsSeconds_ = 0;
  uint16_t latestMpsDeci_ = 0;
  uint16_t latestBatteryCenti_ = 0;
  uint16_t latestSolarCenti_ = 0;
};
