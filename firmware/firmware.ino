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

constexpr bool kUseApMode = true;
const char* kApSsid = "anemometer";
const char* kApPassword = "windmeter123";

const char* kStaSsid = "YOUR_WIFI_SSID";
const char* kStaPassword = "YOUR_WIFI_PASSWORD";

WebServer server(80);
WindSourceDummy dummySource;
WindSourceRPR220 rpr220Source(kRpr220Pin, kPulsesPerRevolution, kMpsPerHz);
WindSource* windSource = nullptr;

WindHistory history;
uint32_t lastSampleMs = 0;

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
    server.send(200, "application/json",
                "{\"ok\":true,\"hasData\":false,\"source\":\"none\"}");
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

  const uint32_t cutoffTs =
      latest.tsSeconds > kWindow24hSeconds ? latest.tsSeconds - kWindow24hSeconds : 0;
  streamHistoryJson("24h", cutoffTs, true);
}

void handleHealth() {
  char body[256];
  snprintf(body, sizeof(body),
           "{\"ok\":true,\"source\":\"%s\",\"storedSamples\":%u,\"uptimeSeconds\":%lu}",
           windSource->name(), (unsigned int)history.size(), (unsigned long)(millis() / 1000));
  server.send(200, "application/json", body);
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

  server.on("/", HTTP_GET, []() { serveStaticPath("/index.html"); });
  server.onNotFound(handleNotFound);
}

void connectWiFi() {
  if (kUseApMode) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(kApSsid, kApPassword);
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(kStaSsid, kStaPassword);
  uint8_t retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 40) {
    delay(500);
    retries++;
  }
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
}
}  // namespace

void setup() {
  Serial.begin(115200);

  windSource = kUseDummySource ? static_cast<WindSource*>(&dummySource)
                               : static_cast<WindSource*>(&rpr220Source);
  windSource->begin();

  connectWiFi();
  setupRoutes();

  server.begin();
  lastSampleMs = millis();
}

void loop() {
  sampleIfDue();
  server.handleClient();
}
