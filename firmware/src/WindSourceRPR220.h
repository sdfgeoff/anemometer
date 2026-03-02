#pragma once

#include "WindSource.h"

class WindSourceRPR220 : public WindSource {
 public:
  WindSourceRPR220(uint8_t signalPin, uint16_t pulsesPerRevolution, float mpsPerHz);

  void begin() override;
  float readMps(float dtSeconds, uint32_t nowMs) override;
  const char* name() const override;

 private:
  static void IRAM_ATTR onPulseISR();

  uint8_t signalPin_;
  uint16_t pulsesPerRevolution_;
  float mpsPerHz_;

  static volatile uint32_t pulseCount_;
  static portMUX_TYPE pulseMux_;
};
