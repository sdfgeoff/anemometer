#include <Arduino.h>

namespace {
constexpr uint8_t kSensorInputPin = 32;
constexpr uint8_t kIrLedControlPin = 33;
constexpr bool kIrLedActiveHigh = true;
constexpr uint8_t kVirtualVccPin = 25;
constexpr uint8_t kVirtualGndPin = 26;
constexpr uint8_t kBatteryVoltagePin = 36;  // VP (ADC1_CH0)
constexpr uint8_t kSolarVoltagePin = 39;    // VN (ADC1_CH3)

constexpr uint32_t kSampleIntervalMs = 10;
constexpr uint32_t kSettleMicros = 80;
constexpr float kAdcRefVolts = 3.3f;
constexpr float kAdcMaxCounts = 4095.0f;
constexpr float kDividerScale = 10.0f;  // 10:1 divider

uint32_t lastSampleMs = 0;

void setLed(bool on) {
  const bool high = kIrLedActiveHigh ? on : !on;
  digitalWrite(kIrLedControlPin, high ? HIGH : LOW);
}

void setupPins() {
  pinMode(kVirtualVccPin, OUTPUT);
  digitalWrite(kVirtualVccPin, HIGH);

  pinMode(kVirtualGndPin, OUTPUT);
  digitalWrite(kVirtualGndPin, LOW);

  pinMode(kIrLedControlPin, OUTPUT);
  pinMode(kSensorInputPin, INPUT);
  pinMode(kBatteryVoltagePin, INPUT);
  pinMode(kSolarVoltagePin, INPUT);

  setLed(true);
}

static int prev_reading = 0;

float readScaledVoltage(uint8_t pin) {
  const int raw = analogRead(pin);
  const float pinVolts = ((float)raw / kAdcMaxCounts) * kAdcRefVolts;
  return pinVolts * kDividerScale;
}

void sampleAndPrintCsv() {
  setLed(false);
  delayMicroseconds(kSettleMicros);
  const int baseline = analogRead(kSensorInputPin);

  setLed(true);
  delayMicroseconds(kSettleMicros);
  const int reflected = analogRead(kSensorInputPin);

  const int reading = reflected - baseline;
  const float batteryV = readScaledVoltage(kBatteryVoltagePin);
  const float solarV = readScaledVoltage(kSolarVoltagePin);

  if (reading < 160 && prev_reading > 160) {
    Serial.println("------------TICK------------");
  }
  prev_reading = reading;

  Serial.print(baseline);
  Serial.print(',');
  Serial.print(reflected);
  Serial.print(',');
  Serial.print(reading);
  Serial.print(',');
  Serial.print(batteryV, 3);
  Serial.print(',');
  Serial.print(solarV, 3);
  Serial.println(',');


  
  
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(10);

  setupPins();

  // Optional: wider ADC range for ESP32 ADC1 channels.
  analogSetPinAttenuation(kSensorInputPin, ADC_11db);

  Serial.println("baseline,reflected,reading,batteryV,solarV");
  lastSampleMs = millis();
}

void loop() {
  const uint32_t now = millis();
  if (now - lastSampleMs >= kSampleIntervalMs) {
    lastSampleMs = now;
    sampleAndPrintCsv();
  }
}
