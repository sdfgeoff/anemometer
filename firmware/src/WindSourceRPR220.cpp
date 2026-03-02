#include "WindSourceRPR220.h"

volatile uint32_t WindSourceRPR220::pulseCount_ = 0;
portMUX_TYPE WindSourceRPR220::pulseMux_ = portMUX_INITIALIZER_UNLOCKED;

WindSourceRPR220::WindSourceRPR220(uint8_t signalPin, uint16_t pulsesPerRevolution,
                                   float mpsPerHz)
    : signalPin_(signalPin),
      pulsesPerRevolution_(pulsesPerRevolution),
      mpsPerHz_(mpsPerHz) {}

void WindSourceRPR220::begin() {
  pinMode(signalPin_, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(signalPin_), onPulseISR, FALLING);
}

float WindSourceRPR220::readMps(float dtSeconds, uint32_t nowMs) {
  (void)nowMs;
  if (dtSeconds <= 0.0f || pulsesPerRevolution_ == 0) {
    return 0.0f;
  }

  uint32_t pulses = 0;
  portENTER_CRITICAL(&pulseMux_);
  pulses = pulseCount_;
  pulseCount_ = 0;
  portEXIT_CRITICAL(&pulseMux_);

  const float hz = ((float)pulses) / ((float)pulsesPerRevolution_ * dtSeconds);
  const float mps = hz * mpsPerHz_;
  return mps < 0.0f ? 0.0f : mps;
}

const char* WindSourceRPR220::name() const {
  return "rpr220";
}

void IRAM_ATTR WindSourceRPR220::onPulseISR() {
  portENTER_CRITICAL_ISR(&pulseMux_);
  pulseCount_++;
  portEXIT_CRITICAL_ISR(&pulseMux_);
}
