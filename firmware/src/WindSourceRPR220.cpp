#include "WindSourceRPR220.h"

#include <Arduino.h>
#include <string.h>

WindSourceRPR220::WindSourceRPR220(uint8_t signalPin, uint8_t ledPin, bool ledActiveHigh,
                                   uint16_t pulsesPerRevolution, float mpsPerHz)
    : signalPin_(signalPin),
      ledPin_(ledPin),
      ledActiveHigh_(ledActiveHigh),
      pulsesPerRevolution_(pulsesPerRevolution),
      mpsPerHz_(mpsPerHz) {}

void WindSourceRPR220::begin() {
  pinMode(signalPin_, INPUT);
  pinMode(ledPin_, OUTPUT);
  setLed(true);
  measureSignal();
  aboveThreshold_ = signal_ >= threshold_;
}

void WindSourceRPR220::setLed(bool on) {
  const bool high = ledActiveHigh_ ? on : !on;
  digitalWrite(ledPin_, high ? HIGH : LOW);
}

void WindSourceRPR220::measureSignal() {
  setLed(false);
  delayMicroseconds(80);
  baseline_ = analogRead(signalPin_);

  setLed(true);
  delayMicroseconds(80);
  reflected_ = analogRead(signalPin_);

  signal_ = baseline_ - reflected_;
}

void WindSourceRPR220::tick(uint32_t nowMs) {
  const uint32_t nowUs = micros();
  if (nowUs - lastPollMicros_ < pollIntervalMicros_) {
    return;
  }
  lastPollMicros_ = nowUs;

  measureSignal();

  if (calibrating_) {
    if (signal_ < calibrationMin_) {
      calibrationMin_ = signal_;
    }
    if (signal_ > calibrationMax_) {
      calibrationMax_ = signal_;
    }

    int clamped = signal_;
    if (clamped < kSignalMin_) {
      clamped = kSignalMin_;
    } else if (clamped > kSignalMax_) {
      clamped = kSignalMax_;
    }
    const size_t bin = (size_t)(clamped - kSignalMin_);
    if (calibrationHistogram_[bin] < 65535) {
      calibrationHistogram_[bin]++;
    }
    calibrationSampleCount_++;

    if (nowMs >= calibrationEndMs_) {
      calibrating_ = false;
      calibrationResultReady_ = true;

      calibrationResult_.minSignal = calibrationMin_;
      calibrationResult_.maxSignal = calibrationMax_;
      calibrationResult_.valid = false;
      calibrationResult_.threshold = threshold_;

      if (calibrationSampleCount_ >= 20) {
        const uint32_t p10Target = (calibrationSampleCount_ * 10 + 99) / 100;
        const uint32_t p90Target = (calibrationSampleCount_ * 90 + 99) / 100;

        uint32_t cumulative = 0;
        int p10 = threshold_;
        int p90 = threshold_;
        bool haveP10 = false;

        for (size_t i = 0; i < kSignalBins_; i++) {
          cumulative += calibrationHistogram_[i];
          if (!haveP10 && cumulative >= p10Target) {
            p10 = (int)i + kSignalMin_;
            haveP10 = true;
          }
          if (cumulative >= p90Target) {
            p90 = (int)i + kSignalMin_;
            break;
          }
        }

        if (p90 > p10) {
          calibrationResult_.valid = true;
          calibrationResult_.threshold = (p10 + p90) / 2;
          threshold_ = calibrationResult_.threshold;
        }
      }
    }
    return;
  }

  const int upper = threshold_ + (hysteresis_ / 2);
  const int lower = threshold_ - (hysteresis_ / 2);

  const bool nowAbove = aboveThreshold_ ? (signal_ > lower) : (signal_ >= upper);

  if (!aboveThreshold_ && nowAbove) {
    pulseCount_++;
    pulseEvents_++;
  }
  aboveThreshold_ = nowAbove;
}

float WindSourceRPR220::readMps(float dtSeconds, uint32_t nowMs) {
  (void)nowMs;
  if (dtSeconds <= 0.0f || pulsesPerRevolution_ == 0) {
    return 0.0f;
  }

  const uint32_t pulses = pulseCount_;
  pulseCount_ = 0;

  const float hz = ((float)pulses) / ((float)pulsesPerRevolution_ * dtSeconds);
  const float mps = hz * mpsPerHz_;
  return mps < 0.0f ? 0.0f : mps;
}

const char* WindSourceRPR220::name() const {
  return "rpr220";
}

void WindSourceRPR220::setThreshold(int threshold) {
  threshold_ = threshold;
}

int WindSourceRPR220::threshold() const {
  return threshold_;
}

void WindSourceRPR220::startCalibration(uint32_t nowMs, uint32_t durationMs) {
  calibrating_ = true;
  calibrationEndMs_ = nowMs + durationMs;
  calibrationMin_ = 32767;
  calibrationMax_ = -32768;
  calibrationResultReady_ = false;
  calibrationSampleCount_ = 0;
  memset(calibrationHistogram_, 0, sizeof(calibrationHistogram_));
}

void WindSourceRPR220::cancelCalibration() {
  calibrating_ = false;
}

bool WindSourceRPR220::consumeCalibrationResult(Rpr220CalibrationResult& out) {
  if (!calibrationResultReady_) {
    return false;
  }

  out = calibrationResult_;
  calibrationResultReady_ = false;
  return true;
}

uint32_t WindSourceRPR220::consumePulseEvents() {
  const uint32_t events = pulseEvents_;
  pulseEvents_ = 0;
  return events;
}

void WindSourceRPR220::snapshot(Rpr220Snapshot& out) const {
  out.baseline = baseline_;
  out.reflected = reflected_;
  out.signal = signal_;
  out.threshold = threshold_;
  out.aboveThreshold = aboveThreshold_;
  out.calibrating = calibrating_;
  out.calibrationMin = calibrationMin_;
  out.calibrationMax = calibrationMax_;
}
