#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>

#include "src/WebAssets.h"
#include "src/WindHistory.h"
#include "src/WindSource.h"
#include "src/WindSourceDummy.h"
#include "src/WindSourceRPR220.h"

namespace {
constexpr bool kUseDummySource = true;
constexpr uint8_t kRpr220Pin = 18;
constexpr uint16_t kPulsesPerRevolution = 12;
constexpr float kMpsPerHz = 1.0f;  // Replace with your calibration factor.

constexpr uint32_t kSampleIntervalMs = 30000;
constexpr uint32_t kWindow24hSeconds = 24 * 60 * 60;

const char* kApSsid = "anemometer";
constexpr bool kApOpenNetwork = true;
const char* kApPassword = "";
constexpr bool kKeepApAfterStaConnect = false;
constexpr uint32_t kStaConnectTimeoutMs = 20000;

const char* kPrefsNamespace = "wifi";
const char* kPrefsKeySsid = "ssid";
const char* kPrefsKeyPass = "pass";

WebServer server(80);
Preferences prefs;

WindSourceDummy dummySource;
WindSourceRPR220 rpr220Source(kRpr220Pin, kPulsesPerRevolution, kMpsPerHz);
WindSource* windSource = nullptr;

WindHistory history;
uint32_t lastSampleMs = 0;

String configuredSsid;
String configuredPassword;
bool hasWifiCredentials = false;

bool staConnectInProgress = false;
bool staConnected = false;
uint32_t staConnectStartedMs = 0;

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

void startProvisioningAp() {
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

void streamHistoryJson(const char* rangeLabel, uint32_t cutoffTs, bool applyCutoff) {
  char line[96];
  const size_t total = history.size();
  size_t count = 0;
  WindSample sample{};

  for (size_t i = 0; i < total; i++) {
    if (!history.getFromOldest(i, sample)) {
      break;
    }
    if (!applyCutoff || sample.tsSeconds >= cutoffTs) {
      count++;
    }
  }

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("{");
  snprintf(line, sizeof(line), "\"range\":\"%s\",\"count\":%u,\"points\":[", rangeLabel,
           (unsigned int)count);
  server.sendContent(line);

  size_t emitted = 0;
  for (size_t i = 0; i < total; i++) {
    if (!history.getFromOldest(i, sample)) {
      break;
    }
    if (applyCutoff && sample.tsSeconds < cutoffTs) {
      continue;
    }

    if (emitted > 0) {
      server.sendContent(",");
    }
    snprintf(line, sizeof(line), "{\"ts\":%lu,\"mps\":%.3f}", (unsigned long)sample.tsSeconds,
             sample.mps);
    server.sendContent(line);
    emitted++;
  }

  server.sendContent("]}");
}

void handleCurrent() {
  WindSample latest{};
  if (!history.latest(latest)) {
    server.send(200, "application/json", "{\"ok\":true,\"hasData\":false,\"source\":\"none\"}");
    return;
  }

  char body[192];
  snprintf(body, sizeof(body),
           "{\"ok\":true,\"hasData\":true,\"source\":\"%s\",\"sampleIntervalSeconds\":30,"
           "\"ts\":%lu,\"mps\":%.3f}",
           windSource->name(), (unsigned long)latest.tsSeconds, latest.mps);
  server.send(200, "application/json", body);
}

void handleHistory() {
  String range = "24h";
  if (server.hasArg("range")) {
    range = server.arg("range");
  }

  if (range == "week") {
    streamHistoryJson("week", 0, false);
    return;
  }

  WindSample latest{};
  if (!history.latest(latest)) {
    streamHistoryJson("24h", 0, false);
    return;
  }

  const uint32_t cutoffTs = latest.tsSeconds > kWindow24hSeconds ? latest.tsSeconds - kWindow24hSeconds : 0;
  streamHistoryJson("24h", cutoffTs, true);
}

void handleHealth() {
  char body[320];
  const bool apActive = WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA;
  snprintf(body, sizeof(body),
           "{\"ok\":true,\"source\":\"%s\",\"storedSamples\":%u,\"uptimeSeconds\":%lu,"
           "\"apActive\":%s,\"staConnected\":%s}",
           windSource->name(), (unsigned int)history.size(), (unsigned long)(millis() / 1000),
           apActive ? "true" : "false", staConnected ? "true" : "false");
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

  server.on("/api/wifi/status", HTTP_GET, handleWifiStatus);
  server.on("/api/wifi/config", HTTP_POST, handleWifiConfigPost);
  server.on("/api/wifi/clear", HTTP_POST, handleWifiClearPost);

  server.on("/", HTTP_GET, []() { serveStaticPath("/index.html"); });
  server.onNotFound(handleNotFound);
}

void sampleIfDue() {
  const uint32_t nowMs = millis();
  if (nowMs - lastSampleMs < kSampleIntervalMs) {
    return;
  }

  float dtSeconds = (nowMs - lastSampleMs) / 1000.0f;
  if (lastSampleMs == 0) {
    dtSeconds = kSampleIntervalMs / 1000.0f;
  }

  lastSampleMs = nowMs;
  const float mps = windSource->readMps(dtSeconds, nowMs);
  history.push(nowMs / 1000, mps);
  Serial.printf("[sample] ts=%lu mps=%.2f\n", (unsigned long)(nowMs / 1000), mps);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  logLine("[boot] anemometer starting");

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
  handleWiFiState();
  sampleIfDue();
  server.handleClient();
}
