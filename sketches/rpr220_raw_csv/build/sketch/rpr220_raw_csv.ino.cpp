#line 1 "/home/geoffrey/.Data/Projects/anemometer/sketches/rpr220_raw_csv/rpr220_raw_csv.ino"
#include <Arduino.h>

namespace {
constexpr uint8_t kSensorInputPin = 32;
constexpr uint8_t kIrLedControlPin = 33;
constexpr bool kIrLedActiveHigh = true;
constexpr uint8_t kVirtualVccPin = 25;
constexpr uint8_t kVirtualGndPin = 26;

constexpr uint32_t kSampleIntervalMs = 100;
constexpr uint32_t kSettleMicros = 80;

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

  setLed(true);
}

void sampleAndPrintCsv() {
  setLed(false);
  delayMicroseconds(kSettleMicros);
  const int baseline = analogRead(kSensorInputPin);

  setLed(true);
  delayMicroseconds(kSettleMicros);
  const int reflected = analogRead(kSensorInputPin);

  Serial.print(baseline);
  Serial.print(',');
  Serial.println(reflected);
}
}  // namespace

#line 48 "/home/geoffrey/.Data/Projects/anemometer/sketches/rpr220_raw_csv/rpr220_raw_csv.ino"
void setup();
#line 61 "/home/geoffrey/.Data/Projects/anemometer/sketches/rpr220_raw_csv/rpr220_raw_csv.ino"
void loop();
#line 48 "/home/geoffrey/.Data/Projects/anemometer/sketches/rpr220_raw_csv/rpr220_raw_csv.ino"
void setup() {
  Serial.begin(115200);
  delay(200);

  setupPins();

  // Optional: wider ADC range for ESP32 ADC1 channels.
  analogSetPinAttenuation(kSensorInputPin, ADC_11db);

  Serial.println("baseline,reflected");
  lastSampleMs = millis();
}

void loop() {
  const uint32_t now = millis();
  if (now - lastSampleMs >= kSampleIntervalMs) {
    lastSampleMs = now;
    sampleAndPrintCsv();
  }
}

