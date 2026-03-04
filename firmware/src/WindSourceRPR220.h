#pragma once

#include "WindSource.h"

struct Rpr220Snapshot {
  int baseline;
  int reflected;
  int signal;
  int threshold;
  bool aboveThreshold;
  bool calibrating;
  int calibrationMin;
  int calibrationMax;
};

struct Rpr220CalibrationResult {
  bool valid;
  int minSignal;
  int maxSignal;
  int threshold;
};

class WindSourceRPR220 : public WindSource {
 public:
  WindSourceRPR220(uint8_t signalPin, uint8_t ledPin, bool ledActiveHigh,
                   uint16_t pulsesPerRevolution, float mpsPerHz);

  void begin() override;
  void tick(uint32_t nowMs) override;
  float readMps(float dtSeconds, uint32_t nowMs) override;
  const char* name() const override;

  void setThreshold(int threshold);
  int threshold() const;

  void startCalibration(uint32_t nowMs, uint32_t durationMs);
  void cancelCalibration();
  bool consumeCalibrationResult(Rpr220CalibrationResult& out);

  void snapshot(Rpr220Snapshot& out) const;

 private:
  void setLed(bool on);
  void measureSignal();

  uint8_t signalPin_;
  uint8_t ledPin_;
  bool ledActiveHigh_;
  uint16_t pulsesPerRevolution_;
  float mpsPerHz_;

  uint32_t pulseCount_ = 0;
  bool aboveThreshold_ = false;

  int threshold_ = 120;
  int hysteresis_ = 20;

  int baseline_ = 0;
  int reflected_ = 0;
  int signal_ = 0;

  uint32_t lastPollMicros_ = 0;
  uint32_t pollIntervalMicros_ = 2000;

  bool calibrating_ = false;
  uint32_t calibrationEndMs_ = 0;
  int calibrationMin_ = 32767;
  int calibrationMax_ = -32768;

  bool calibrationResultReady_ = false;
  Rpr220CalibrationResult calibrationResult_ = {};
};
