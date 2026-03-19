#include "WindHistory.h"

#include <math.h>

WindHistory::WindHistory() {
  initTiers();
}

uint16_t WindHistory::encodeMps(float mps) const {
  if (mps < 0.0f) {
    mps = 0.0f;
  }

  const float scaled = mps * 10.0f;
  if (scaled >= 65535.0f) {
    return 65535;
  }
  return (uint16_t)lroundf(scaled);
}

float WindHistory::decodeMps(uint16_t encoded) const {
  return ((float)encoded) / 10.0f;
}

uint16_t WindHistory::encodeVoltage(float volts) const {
  if (volts < 0.0f) {
    volts = 0.0f;
  }

  const float scaled = volts * 100.0f;
  if (scaled >= 65535.0f) {
    return 65535;
  }
  return (uint16_t)lroundf(scaled);
}

float WindHistory::decodeVoltage(uint16_t encoded) const {
  return ((float)encoded) / 100.0f;
}

void WindHistory::initTiers() {
  tiers_[0] = TierState{tier5_, kTier5Capacity, 5, 30 * 60, 0, 0,
                        PendingBucket{false, 0, 0.0f, 0.0f, 0.0f, 0}};
  tiers_[1] = TierState{tier300_, kTier300Capacity, 300, 7 * 24 * 60 * 60, 0, 0,
                        PendingBucket{false, 0, 0.0f, 0.0f, 0.0f, 0}};
}

void WindHistory::ringPush(TierState& tier, uint32_t tsSeconds, float mps, float batteryV,
                           float solarV) {
  tier.samples[tier.head] =
      EncodedSample{tsSeconds, encodeMps(mps), encodeVoltage(batteryV), encodeVoltage(solarV)};
  tier.head = (tier.head + 1) % tier.capacity;
  if (tier.count < tier.capacity) {
    tier.count++;
  }
}

void WindHistory::commitPending(TierState& tier) {
  if (!tier.pending.active || tier.pending.count == 0) {
    return;
  }

  const float avg = tier.pending.sum / (float)tier.pending.count;
  const float batteryAvg = tier.pending.batterySum / (float)tier.pending.count;
  const float solarAvg = tier.pending.solarSum / (float)tier.pending.count;
  const uint32_t ts = tier.pending.bucketStart + tier.intervalSeconds;
  ringPush(tier, ts, avg, batteryAvg, solarAvg);
}

void WindHistory::pushToTier(TierState& tier, uint32_t tsSeconds, float mps, float batteryV,
                             float solarV) {
  const uint32_t bucketStart = (tsSeconds / tier.intervalSeconds) * tier.intervalSeconds;

  if (!tier.pending.active) {
    tier.pending = PendingBucket{true, bucketStart, mps, batteryV, solarV, 1};
    return;
  }

  if (bucketStart != tier.pending.bucketStart) {
    commitPending(tier);
    tier.pending = PendingBucket{true, bucketStart, mps, batteryV, solarV, 1};
    return;
  }

  tier.pending.sum += mps;
  tier.pending.batterySum += batteryV;
  tier.pending.solarSum += solarV;
  tier.pending.count++;
}

void WindHistory::push(uint32_t tsSeconds, float mps, float batteryV, float solarV) {
  hasLatest_ = true;
  latestTsSeconds_ = tsSeconds;
  latestMpsDeci_ = encodeMps(mps);
  latestBatteryCenti_ = encodeVoltage(batteryV);
  latestSolarCenti_ = encodeVoltage(solarV);

  for (size_t i = 0; i < kTierCount; i++) {
    pushToTier(tiers_[i], tsSeconds, mps, batteryV, solarV);
  }
}

bool WindHistory::latest(WindSample& out) const {
  if (!hasLatest_) {
    return false;
  }

  out.tsSeconds = latestTsSeconds_;
  out.mps = decodeMps(latestMpsDeci_);
  out.batteryV = decodeVoltage(latestBatteryCenti_);
  out.solarV = decodeVoltage(latestSolarCenti_);
  return true;
}

const WindHistory::TierState& WindHistory::selectTierForSeconds(uint32_t seconds) const {
  if (seconds <= 30 * 60) {
    return tiers_[0];
  }
  return tiers_[1];
}

size_t WindHistory::copyLastSeconds(uint32_t seconds, WindSample* out, size_t maxOut) const {
  if (!hasLatest_ || maxOut == 0 || seconds == 0) {
    return 0;
  }

  const TierState& tier = selectTierForSeconds(seconds);
  const uint32_t cutoffTs = latestTsSeconds_ > seconds ? latestTsSeconds_ - seconds : 0;

  size_t written = 0;
  const size_t oldestBase = (tier.head + tier.capacity - tier.count) % tier.capacity;

  for (size_t i = 0; i < tier.count && written < maxOut; i++) {
    const size_t idx = (oldestBase + i) % tier.capacity;
    const EncodedSample& s = tier.samples[idx];
    if (s.tsSeconds < cutoffTs) {
      continue;
    }

    out[written++] = WindSample{s.tsSeconds, decodeMps(s.mpsDeci), decodeVoltage(s.batteryCenti),
                                decodeVoltage(s.solarCenti)};
  }

  // Include in-flight bucket value so freshest points are visible.
  if (written < maxOut && tier.pending.active && tier.pending.count > 0) {
    const uint32_t pendingTs = tier.pending.bucketStart + tier.intervalSeconds;
    if (pendingTs >= cutoffTs) {
      out[written++] = WindSample{
          pendingTs,
          tier.pending.sum / (float)tier.pending.count,
          tier.pending.batterySum / (float)tier.pending.count,
          tier.pending.solarSum / (float)tier.pending.count,
      };
    }
  }

  return written;
}

size_t WindHistory::totalSamples() const {
  size_t total = 0;
  for (size_t i = 0; i < kTierCount; i++) {
    total += tiers_[i].count;
    if (tiers_[i].pending.active && tiers_[i].pending.count > 0) {
      total += 1;
    }
  }
  return total;
}
