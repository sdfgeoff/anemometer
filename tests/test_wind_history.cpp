#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "../firmware/src/WindHistory.h"

namespace {

void fail(const char* msg) {
  std::cerr << "FAIL: " << msg << "\n";
  std::exit(1);
}

void assertTrue(bool cond, const char* msg) {
  if (!cond) {
    fail(msg);
  }
}

void assertNear(float actual, float expected, float epsilon, const char* msg) {
  if (std::fabs(actual - expected) > epsilon) {
    std::cerr << "FAIL: " << msg << " expected=" << expected << " actual=" << actual << "\n";
    std::exit(1);
  }
}

void assertMonotonicTs(const std::vector<WindSample>& samples, uint32_t expectedStep, const char* msg) {
  if (samples.size() < 2) {
    return;
  }

  for (size_t i = 1; i < samples.size(); i++) {
    const uint32_t delta = samples[i].tsSeconds - samples[i - 1].tsSeconds;
    if (delta != expectedStep) {
      std::cerr << "FAIL: " << msg << " expected step=" << expectedStep << " got=" << delta
                << " at index " << i << "\n";
      std::exit(1);
    }
  }
}

std::vector<WindSample> query(WindHistory& h, uint32_t seconds) {
  WindSample scratch[WindHistory::kMaxQueryPoints];
  const size_t count = h.copyLastSeconds(seconds, scratch, WindHistory::kMaxQueryPoints);
  return std::vector<WindSample>(scratch, scratch + count);
}

void feed(WindHistory& h, uint32_t durationSeconds) {
  for (uint32_t ts = 0; ts <= durationSeconds; ts += WindHistory::kBaseSamplePeriodSeconds) {
    h.push(ts, (float)ts / 100.0f);
  }
}

}  // namespace

int main() {
  {
    WindHistory h;
    feed(h, 2 * 60 * 60);

    auto samples15m = query(h, 15 * 60);
    auto samples30m = query(h, 30 * 60);
    auto samples1h = query(h, 60 * 60);

    assertTrue(!samples15m.empty(), "15m query should return samples");
    assertTrue(!samples30m.empty(), "30m query should return samples");
    assertTrue(!samples1h.empty(), "1h query should return samples");

    assertMonotonicTs(samples15m, 5, "15m should use 5s tier");
    assertMonotonicTs(samples30m, 10, "30m should use 10s tier");
    assertMonotonicTs(samples1h, 30, "1h should use 30s tier");
  }

  {
    WindHistory h;
    feed(h, 30 * 60 * 60);  // 30h

    auto samples4h = query(h, 4 * 60 * 60);
    auto samples24h = query(h, 24 * 60 * 60);

    assertTrue(!samples4h.empty(), "4h query should return samples");
    assertTrue(!samples24h.empty(), "24h query should return samples");

    assertMonotonicTs(samples4h, 60, "4h should use 60s tier");
    assertMonotonicTs(samples24h, 60, "24h should use 60s tier");
  }

  {
    WindHistory h;
    feed(h, 8 * 24 * 60 * 60);  // 8 days

    auto samplesWeek = query(h, 7 * 24 * 60 * 60);
    assertTrue(!samplesWeek.empty(), "week query should return samples");
    assertMonotonicTs(samplesWeek, 300, "week should use 300s tier");

    // 7 days at 5-minute buckets should fit around 2016 points (+pending not expected at this boundary)
    assertTrue(samplesWeek.size() <= WindHistory::kMaxQueryPoints, "week query should fit buffer");
  }

  {
    // Verify bucket averaging behavior on 10s tier.
    WindHistory h;
    h.push(0, 10.0f);
    h.push(5, 20.0f);
    h.push(10, 30.0f);
    h.push(15, 40.0f);

    auto samples = query(h, 30 * 60);
    assertTrue(!samples.empty(), "averaging query should return samples");

    // First completed 10s bucket [0,10) average is 15.0.
    assertNear(samples[0].mps, 15.0f, 0.11f, "10s bucket average should be correct");
  }

  std::cout << "All WindHistory tests passed\n";
  return 0;
}
