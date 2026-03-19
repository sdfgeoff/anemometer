#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>

#include "src/WebAssets.h"
#include "src/WindHistory.h"
#include "src/WindSource.h"
#include "src/WindSourceDummy.h"
#include "src/WindSourceRPR220.h"

namespace {
constexpr bool kUseDummySource = false;
constexpr uint8_t kRpr220Pin = 32;
constexpr uint8_t kRpr220IrLedEnablePin = 33;
constexpr bool kRpr220IrLedActiveHigh = true;
constexpr uint8_t kRpr220VirtualVccPin = 25;
constexpr uint8_t kRpr220VirtualGndPin = 26;
constexpr uint16_t kPulsesPerRevolution = 5;
constexpr float kMpsPerHz = 0.63f;  // Geometry-based initial estimate (r=60mm, TSR~0.6).
constexpr uint8_t kSolarVoltagePin = 39;    // VN (ADC1_CH3)
constexpr uint8_t kBatteryVoltagePin = 36;  // VP (ADC1_CH0)
constexpr float kAdcRefVolts = 3.3f;
constexpr float kAdcMaxCounts = 4095.0f;
constexpr float kDividerScale = 10.0f;  // 10:1 divider, pin voltage is source/10.

constexpr uint32_t kSampleIntervalDayMs = 5000;
constexpr uint32_t kSampleIntervalNightMs = 5UL * 60UL * 1000UL;
constexpr float kSolarNightThresholdV = 0.5f;
constexpr float kSolarDayThresholdV = 0.6f;
constexpr uint32_t kNightEnterHoldMs = 15UL * 60UL * 1000UL;
constexpr uint32_t kDayExitHoldMs = 3UL * 60UL * 1000UL;

const char* kApSsid = "anemometer";
constexpr bool kApOpenNetwork = true;
const char* kApPassword = "";
constexpr bool kKeepApAfterStaConnect = false;
constexpr uint32_t kStaConnectTimeoutMs = 20000;

const char* kPrefsNamespace = "wifi";
const char* kPrefsKeySsid = "ssid";
const char* kPrefsKeyPass = "pass";
const char* kSensorPrefsNamespace = "sensor";
const char* kSensorPrefsKeyThreshold = "th";

WebServer server(80);
Preferences prefs;

WindSourceDummy dummySource;
WindSourceRPR220 rpr220Source(kRpr220Pin, kRpr220IrLedEnablePin, kRpr220IrLedActiveHigh,
                              kPulsesPerRevolution, kMpsPerHz);
WindSource* windSource = nullptr;

WindHistory history;
uint32_t lastSampleMs = 0;
float latestBatteryV = 0.0f;
float latestSolarV = 0.0f;
bool havePowerSample = false;
bool nightMode = false;
bool wifiEnabled = true;
uint32_t firstDarkMs = 0;
uint32_t firstBrightMs = 0;

String configuredSsid;
String configuredPassword;
bool hasWifiCredentials = false;

bool staConnectInProgress = false;
bool staConnected = false;
uint32_t staConnectStartedMs = 0;
int persistedThreshold = 120;

bool usingRpr220Source();

float readScaledVoltage(uint8_t pin) {
  const int raw = analogRead(pin);
  const float pinVolts = ((float)raw / kAdcMaxCounts) * kAdcRefVolts;
  return pinVolts * kDividerScale;
}

uint32_t currentSampleIntervalMs() {
  return nightMode ? kSampleIntervalNightMs : kSampleIntervalDayMs;
}

void setupRpr220IrLedControl() {
  pinMode(kRpr220VirtualVccPin, OUTPUT);
  digitalWrite(kRpr220VirtualVccPin, HIGH);
  Serial.printf("[sensor] RPR220 virtual VCC pin %u driven HIGH\n",
                (unsigned int)kRpr220VirtualVccPin);

  pinMode(kRpr220VirtualGndPin, OUTPUT);
  digitalWrite(kRpr220VirtualGndPin, LOW);
  Serial.printf("[sensor] RPR220 virtual GND pin %u driven LOW\n",
                (unsigned int)kRpr220VirtualGndPin);
}

void logLine(const char* msg) {
  Serial.println(msg);
}

void loadWifiCredentials() {
  prefs.begin(kPrefsNamespace, true);
  configuredSsid = prefs.getString(kPrefsKeySsid, "");
  configuredPassword = prefs.getString(kPrefsKeyPass, "");
  prefs.end();

  hasWifiCredentials = configuredSsid.length() > 0;
}

void saveWifiCredentials(const String& ssid, const String& password) {
  prefs.begin(kPrefsNamespace, false);
  prefs.putString(kPrefsKeySsid, ssid);
  prefs.putString(kPrefsKeyPass, password);
  prefs.end();

  configuredSsid = ssid;
  configuredPassword = password;
  hasWifiCredentials = configuredSsid.length() > 0;
}

void clearWifiCredentials() {
  prefs.begin(kPrefsNamespace, false);
  prefs.remove(kPrefsKeySsid);
  prefs.remove(kPrefsKeyPass);
  prefs.end();

  configuredSsid = "";
  configuredPassword = "";
  hasWifiCredentials = false;
}

void loadSensorCalibration() {
  prefs.begin(kSensorPrefsNamespace, true);
  persistedThreshold = prefs.getInt(kSensorPrefsKeyThreshold, 120);
  prefs.end();
}

void saveSensorCalibrationThreshold(int threshold) {
  prefs.begin(kSensorPrefsNamespace, false);
  prefs.putInt(kSensorPrefsKeyThreshold, threshold);
  prefs.end();
  persistedThreshold = threshold;
}

void startProvisioningAp() {
  wifiEnabled = true;
  WiFi.mode(WIFI_AP_STA);
  const bool apOk = kApOpenNetwork ? WiFi.softAP(kApSsid) : WiFi.softAP(kApSsid, kApPassword);

  if (apOk) {
    const IPAddress apIp = WiFi.softAPIP();
    Serial.printf("[wifi] AP started SSID='%s' IP=%s (%s)\n", kApSsid,
                  apIp.toString().c_str(), kApOpenNetwork ? "open" : "secured");
  } else {
    logLine("[wifi] ERROR: failed to start AP");
  }
}

void beginStaConnect() {
  if (!wifiEnabled) {
    return;
  }
  if (!hasWifiCredentials) {
    logLine("[wifi] No saved STA credentials");
    return;
  }

  Serial.printf("[wifi] Connecting to STA SSID='%s'\n", configuredSsid.c_str());
  WiFi.begin(configuredSsid.c_str(), configuredPassword.c_str());
  staConnectInProgress = true;
  staConnectStartedMs = millis();
}

void handleWiFiState() {
  if (!wifiEnabled) {
    return;
  }

  const wl_status_t status = WiFi.status();

  if (status == WL_CONNECTED) {
    if (!staConnected) {
      staConnected = true;
      staConnectInProgress = false;
      const IPAddress staIp = WiFi.localIP();
      Serial.printf("[wifi] STA connected. IP=%s\n", staIp.toString().c_str());

      if (!kKeepApAfterStaConnect) {
        WiFi.softAPdisconnect(true);
        logLine("[wifi] AP disabled after STA connect");
      }
    }
    return;
  }

  if (staConnected) {
    staConnected = false;
    Serial.printf("[wifi] STA disconnected. status=%d\n", (int)status);
    if (!WiFi.softAPgetStationNum()) {
      startProvisioningAp();
    }
    if (hasWifiCredentials) {
      beginStaConnect();
    }
    return;
  }

  if (staConnectInProgress && (millis() - staConnectStartedMs >= kStaConnectTimeoutMs)) {
    staConnectInProgress = false;
    Serial.printf("[wifi] STA connect timeout after %lu ms\n", (unsigned long)kStaConnectTimeoutMs);
    WiFi.disconnect();
  }
}

void disableWifiForNight() {
  if (!wifiEnabled) {
    return;
  }

  logLine("[power] entering night mode: disabling Wi-Fi");
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  staConnectInProgress = false;
  staConnected = false;
  wifiEnabled = false;
}

void enableWifiForDay() {
  if (wifiEnabled) {
    return;
  }

  logLine("[power] leaving night mode: enabling Wi-Fi");
  startProvisioningAp();
  if (hasWifiCredentials) {
    beginStaConnect();
  }
}

void applyNightMode(bool enabled) {
  if (nightMode == enabled) {
    return;
  }

  nightMode = enabled;
  if (usingRpr220Source()) {
    rpr220Source.setLowPowerMode(nightMode);
  }

  Serial.printf("[power] mode=%s sampleInterval=%lu ms\n", nightMode ? "night" : "day",
                (unsigned long)currentSampleIntervalMs());

  if (nightMode) {
    disableWifiForNight();
  } else {
    enableWifiForDay();
  }
}

void updateDayNightState(uint32_t nowMs, float solarV) {
  if (solarV <= kSolarNightThresholdV) {
    firstBrightMs = 0;
    if (firstDarkMs == 0) {
      firstDarkMs = nowMs;
    }
    if (!nightMode && nowMs - firstDarkMs >= kNightEnterHoldMs) {
      applyNightMode(true);
    }
    return;
  }

  if (solarV >= kSolarDayThresholdV) {
    firstDarkMs = 0;
    if (firstBrightMs == 0) {
      firstBrightMs = nowMs;
    }
    if (nightMode && nowMs - firstBrightMs >= kDayExitHoldMs) {
      applyNightMode(false);
    }
    return;
  }

  firstDarkMs = 0;
  firstBrightMs = 0;
}

void streamJsonPoints(const WindSample* samples, size_t count, uint32_t requestedSeconds) {
  char line[160];
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("{");
  snprintf(line, sizeof(line), "\"seconds\":%lu,\"count\":%u,\"points\":[",
           (unsigned long)requestedSeconds, (unsigned int)count);
  server.sendContent(line);

  for (size_t i = 0; i < count; i++) {
    if (i > 0) {
      server.sendContent(",");
    }
    snprintf(line, sizeof(line), "{\"ts\":%lu,\"mps\":%.3f,\"batteryV\":%.3f,\"solarV\":%.3f}",
             (unsigned long)samples[i].tsSeconds, samples[i].mps, samples[i].batteryV,
             samples[i].solarV);
    server.sendContent(line);
  }

  server.sendContent("]}");
}

void handleCurrent() {
  WindSample latest{};
  if (!history.latest(latest)) {
    server.send(200, "application/json", "{\"ok\":true,\"hasData\":false,\"source\":\"none\"}");
    return;
  }

  char body[256];
  snprintf(body, sizeof(body),
           "{\"ok\":true,\"hasData\":true,\"source\":\"%s\",\"sampleIntervalSeconds\":%lu,"
           "\"ts\":%lu,\"mps\":%.3f,\"batteryV\":%.3f,\"solarV\":%.3f}",
           windSource->name(), (unsigned long)(currentSampleIntervalMs() / 1000),
           (unsigned long)latest.tsSeconds, latest.mps, havePowerSample ? latestBatteryV : 0.0f,
           havePowerSample ? latestSolarV : 0.0f);
  server.send(200, "application/json", body);
}

void handleHistory() {
  if (!server.hasArg("seconds")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing_seconds\"}");
    return;
  }

  const uint32_t requestedSeconds = (uint32_t)server.arg("seconds").toInt();
  if (requestedSeconds == 0) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_seconds\"}");
    return;
  }

  static WindSample scratch[WindHistory::kMaxQueryPoints];
  const size_t count = history.copyLastSeconds(requestedSeconds, scratch, WindHistory::kMaxQueryPoints);
  streamJsonPoints(scratch, count, requestedSeconds);
}

bool usingRpr220Source() {
  return windSource == static_cast<WindSource*>(&rpr220Source);
}

void handleSensorStatus() {
  if (!usingRpr220Source()) {
    server.send(200, "application/json", "{\"ok\":true,\"mode\":\"dummy\"}");
    return;
  }

  Rpr220Snapshot snapshot{};
  rpr220Source.snapshot(snapshot);

  char body[384];
  snprintf(body, sizeof(body),
           "{\"ok\":true,\"mode\":\"rpr220\",\"threshold\":%d,\"baseline\":%d,\"reflected\":%d,"
           "\"signal\":%d,\"aboveThreshold\":%s,\"calibrating\":%s,\"calibrationMin\":%d,"
           "\"calibrationMax\":%d}",
           snapshot.threshold, snapshot.baseline, snapshot.reflected, snapshot.signal,
           snapshot.aboveThreshold ? "true" : "false", snapshot.calibrating ? "true" : "false",
           snapshot.calibrationMin, snapshot.calibrationMax);
  server.send(200, "application/json", body);
}

void handleSensorCalibrateStart() {
  if (!usingRpr220Source()) {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"calibration_requires_rpr220_mode\"}");
    return;
  }

  uint32_t durationMs = 10000;
  if (server.hasArg("seconds")) {
    const uint32_t requestedSeconds = (uint32_t)server.arg("seconds").toInt();
    if (requestedSeconds > 0) {
      durationMs = requestedSeconds * 1000;
    }
  }

  rpr220Source.startCalibration(millis(), durationMs);
  char body[128];
  snprintf(body, sizeof(body), "{\"ok\":true,\"message\":\"calibration_started\",\"durationMs\":%lu}",
           (unsigned long)durationMs);
  server.send(200, "application/json", body);
}

void handleHealth() {
  char body[320];
  const bool apActive = WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA;
  snprintf(body, sizeof(body),
           "{\"ok\":true,\"source\":\"%s\",\"storedSamples\":%u,\"uptimeSeconds\":%lu,"
           "\"apActive\":%s,\"staConnected\":%s}",
           windSource->name(), (unsigned int)history.totalSamples(),
           (unsigned long)(millis() / 1000), apActive ? "true" : "false",
           staConnected ? "true" : "false");
  server.send(200, "application/json", body);
}

void handleWifiStatus() {
  const wl_status_t status = WiFi.status();
  const bool apActive = WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA;

  const String apIp = apActive ? WiFi.softAPIP().toString() : "";
  const String staIp = status == WL_CONNECTED ? WiFi.localIP().toString() : "";

  char body[512];
  snprintf(body, sizeof(body),
           "{\"ok\":true,\"ap\":{\"active\":%s,\"ssid\":\"%s\",\"ip\":\"%s\"},"
           "\"sta\":{\"status\":%d,\"connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\"},"
           "\"credentialsSaved\":%s}",
           apActive ? "true" : "false", kApSsid, apIp.c_str(), (int)status,
           status == WL_CONNECTED ? "true" : "false", configuredSsid.c_str(), staIp.c_str(),
           hasWifiCredentials ? "true" : "false");
  server.send(200, "application/json", body);
}

void handleWifiConfigPost() {
  if (!server.hasArg("ssid")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing_ssid\"}");
    return;
  }

  const String ssid = server.arg("ssid");
  const String password = server.hasArg("password") ? server.arg("password") : "";

  if (ssid.length() == 0) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"empty_ssid\"}");
    return;
  }

  saveWifiCredentials(ssid, password);
  WiFi.disconnect(true);
  if (WiFi.getMode() != WIFI_AP_STA) {
    startProvisioningAp();
  }
  beginStaConnect();

  server.send(200, "application/json", "{\"ok\":true,\"message\":\"saved_and_connecting\"}");
}

void handleWifiClearPost() {
  clearWifiCredentials();
  WiFi.disconnect(true);
  startProvisioningAp();
  server.send(200, "application/json", "{\"ok\":true,\"message\":\"credentials_cleared\"}");
}

bool serveStaticPath(const String& path) {
  const EmbeddedAsset* asset = findEmbeddedAsset(path);
  if (!asset) {
    return false;
  }

  if (asset->gzip) {
    server.sendHeader("Content-Encoding", "gzip");
  }
  server.send_P(200, asset->mimeType, (PGM_P)asset->data, asset->length);
  return true;
}

void handleNotFound() {
  const String uri = server.uri();
  if (serveStaticPath(uri)) {
    return;
  }

  if (uri.startsWith("/api/")) {
    server.send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
    return;
  }

  if (serveStaticPath("/index.html")) {
    return;
  }

  server.send(404, "text/plain", "Not Found");
}

void setupRoutes() {
  server.enableCORS(true);
  const char* headers[] = {"Accept-Encoding"};
  server.collectHeaders(headers, 1);

  server.on("/api/health", HTTP_GET, handleHealth);
  server.on("/api/current", HTTP_GET, handleCurrent);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/sensor/status", HTTP_GET, handleSensorStatus);
  server.on("/api/sensor/calibrate/start", HTTP_POST, handleSensorCalibrateStart);

  server.on("/api/wifi/status", HTTP_GET, handleWifiStatus);
  server.on("/api/wifi/config", HTTP_POST, handleWifiConfigPost);
  server.on("/api/wifi/clear", HTTP_POST, handleWifiClearPost);

  server.on("/", HTTP_GET, []() { serveStaticPath("/index.html"); });
  server.onNotFound(handleNotFound);
}

void sampleIfDue() {
  const uint32_t nowMs = millis();
  const uint32_t sampleIntervalMs = currentSampleIntervalMs();
  if (nowMs - lastSampleMs < sampleIntervalMs) {
    return;
  }

  float dtSeconds = (nowMs - lastSampleMs) / 1000.0f;
  if (lastSampleMs == 0) {
    dtSeconds = sampleIntervalMs / 1000.0f;
  }

  lastSampleMs = nowMs;
  const float mps = windSource->readMps(dtSeconds, nowMs);
  const float batteryV = readScaledVoltage(kBatteryVoltagePin);
  const float solarV = readScaledVoltage(kSolarVoltagePin);
  latestBatteryV = batteryV;
  latestSolarV = solarV;
  havePowerSample = true;
  updateDayNightState(nowMs, solarV);
  history.push(nowMs / 1000, mps, batteryV, solarV);
  Serial.printf("[sample] ts=%lu mps=%.2f batt=%.2fV solar=%.2fV\n",
                (unsigned long)(nowMs / 1000), mps, batteryV, solarV);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  logLine("[boot] anemometer starting");
  Serial.printf("[boot] build %s %s\n", __DATE__, __TIME__);
  Serial.printf("[power] battery pin=%u solar pin=%u divider=%.1f:1\n",
                (unsigned int)kBatteryVoltagePin, (unsigned int)kSolarVoltagePin, kDividerScale);
  Serial.printf("[power] solar thresholds: night<=%.2fV for %lu ms, day>=%.2fV for %lu ms\n",
                kSolarNightThresholdV, (unsigned long)kNightEnterHoldMs, kSolarDayThresholdV,
                (unsigned long)kDayExitHoldMs);

  pinMode(kBatteryVoltagePin, INPUT);
  pinMode(kSolarVoltagePin, INPUT);

  setupRpr220IrLedControl();

  loadSensorCalibration();
  rpr220Source.setThreshold(persistedThreshold);
  Serial.printf("[sensor] loaded threshold=%d\n", persistedThreshold);

  windSource = kUseDummySource ? static_cast<WindSource*>(&dummySource)
                               : static_cast<WindSource*>(&rpr220Source);
  windSource->begin();
  Serial.printf("[boot] wind source: %s\n", windSource->name());

  loadWifiCredentials();
  if (hasWifiCredentials) {
    Serial.printf("[wifi] Found saved credentials for SSID='%s'\n", configuredSsid.c_str());
  } else {
    logLine("[wifi] No stored credentials");
  }

  startProvisioningAp();
  if (hasWifiCredentials) {
    beginStaConnect();
  }

  setupRoutes();
  server.begin();
  logLine("[http] server started on port 80");

  lastSampleMs = millis();
}

void loop() {
  const uint32_t nowMs = millis();
  windSource->tick(nowMs);

  if (usingRpr220Source()) {
    const uint32_t events = rpr220Source.consumePulseEvents();
    for (uint32_t i = 0; i < events; i++) {
      Serial.println("[pulse] detected");
    }

    Rpr220CalibrationResult result{};
    if (rpr220Source.consumeCalibrationResult(result)) {
      if (result.valid) {
        saveSensorCalibrationThreshold(result.threshold);
        Serial.printf("[sensor] calibration complete min=%d max=%d threshold=%d (saved)\n",
                      result.minSignal, result.maxSignal, result.threshold);
      } else {
        Serial.println("[sensor] calibration complete but invalid range");
      }
    }
  }

  handleWiFiState();
  sampleIfDue();
  server.handleClient();
}
