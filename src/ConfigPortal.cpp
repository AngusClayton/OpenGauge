#include "ConfigPortal.h"
#include "ConfigCodec.h"
#include "ConfigManager.h"
#include "ConfigStorage.h"
#include <Arduino.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>

namespace {
constexpr uint32_t kPortalIdleTimeoutMs = 10UL * 60UL * 1000UL;
WebServer server(80);
DNSServer dns;
TaskHandle_t portalTaskHandle = nullptr;
volatile bool active = false;
volatile bool stopRequested = false;
uint32_t lastActivityMs = 0;
char ssid[24] = {};
char password[16] = {};
bool routesConfigured = false;

void touchActivity() { lastActivityMs = millis(); }
void sendJson(int status, const String& body) { touchActivity(); server.send(status, "application/json; charset=utf-8", body); }
void redirectToPortal() { touchActivity(); server.sendHeader("Location", "http://192.168.4.1/", true); server.send(302, "text/plain", "OpenGauge"); }

void configureRoutes() {
  server.on("/api/config", HTTP_GET, [] { sendJson(200, getCurrentConfigJson()); });
  server.on("/api/default-config", HTTP_GET, [] { sendJson(200, getDefaultOpenGaugeConfigJson()); });
  server.on("/api/status", HTTP_GET, [] { sendJson(200, String("{\"active\":true,\"ssid\":\"") + ssid + "\",\"ip\":\"192.168.4.1\",\"rendererAbi\":1}"); });
  server.on("/api/config", HTTP_PUT, [] {
    touchActivity();
    if (!server.hasArg("plain") || server.arg("plain").length() > OPEN_GAUGE_MAX_CONFIG_BYTES) { sendJson(413, "{\"errors\":[\"Configuration is missing or larger than 32 KB.\"]}"); return; }
    const String body = server.arg("plain"); char error[192] = {};
    if (!saveAndApplyConfig(body.c_str(), body.length(), error, sizeof(error))) {
      String response = "{\"errors\":[\""; for (const char* p = error; *p; ++p) response += (*p == '\"' ? '\'' : *p); response += "\"]}";
      sendJson(400, response); return;
    }
    sendJson(200, "{\"ok\":true,\"message\":\"Configuration saved and applied.\"}");
  });
  server.on("/api/portal/stop", HTTP_POST, [] { sendJson(200, "{\"ok\":true}"); stopRequested = true; });
  server.on("/api/preview", HTTP_POST, [] { sendJson(501, "{\"errors\":[\"The WASM renderer asset is not installed.\"]}"); });
  server.on("/generate_204", redirectToPortal); server.on("/hotspot-detect.html", redirectToPortal);
  server.on("/connecttest.txt", redirectToPortal); server.on("/ncsi.txt", redirectToPortal);
  server.on("/fwlink", redirectToPortal);
  server.on("/opengauge-renderer.wasm", HTTP_GET, [] { touchActivity(); File file=LittleFS.open("/opengauge-renderer.wasm","r"); if(!file){server.send(404,"text/plain","Missing WASM renderer");return;} server.streamFile(file,"application/wasm"); file.close(); });
  server.on("/", HTTP_GET, [] { touchActivity(); File file=LittleFS.open("/index.html","r"); server.streamFile(file,"text/html; charset=utf-8"); file.close(); });
  server.serveStatic("/", LittleFS, "/");
  server.onNotFound(redirectToPortal);
}

void portalTask(void*) {
  uint32_t lastStackLogMs = 0;
  while (active && !stopRequested) {
    dns.processNextRequest(); server.handleClient();
    if ((millis() - lastActivityMs) >= kPortalIdleTimeoutMs) stopRequested = true;
    if ((millis() - lastStackLogMs) >= 30000) {
      Serial.printf("[PORTAL] Stack watermark: %u bytes\n",
                    (unsigned int)(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));
      lastStackLogMs = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  server.stop(); dns.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF);
  active = false; stopRequested = false; portalTaskHandle = nullptr; vTaskDelete(nullptr);
}

void loadCredentials() {
  uint8_t mac[6] = {}; WiFi.macAddress(mac);
  snprintf(ssid, sizeof(ssid), "OpenGauge-%02X%02X%02X", mac[3], mac[4], mac[5]);
  Preferences preferences; preferences.begin("opengauge", false);
  String saved = preferences.isKey("apPassword") ? preferences.getString("apPassword", "") : String();
  if (saved.length() != 12) {
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    saved.reserve(12); for (int i = 0; i < 12; ++i) saved += alphabet[esp_random() % (sizeof(alphabet) - 1)];
    preferences.putString("apPassword", saved);
  }
  preferences.end(); snprintf(password, sizeof(password), "%s", saved.c_str());
}
}  // namespace

bool startConfigPortal() {
  if (active) return true;
  Serial.printf("[PORTAL] Starting; free heap=%u, largest block=%u\n",
                (unsigned int)ESP.getFreeHeap(),
                (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
  if (!LittleFS.begin(false)) {
    Serial.println("[PORTAL] LittleFS mount failed");
    return false;
  }

  // Start from a known radio state. A short settling delay avoids an AP start
  // racing the asynchronous Wi-Fi driver transition on the ESP32-S3.
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  if (!WiFi.mode(WIFI_AP)) {
    Serial.println("[PORTAL] Failed to enter AP mode");
    return false;
  }
  delay(100);
  loadCredentials();
  if (!WiFi.softAP(ssid, password, 1, false, 2)) {
    Serial.println("[PORTAL] softAP start failed");
    WiFi.mode(WIFI_OFF);
    return false;
  }

  // Do not allow the Espressif-only long-range mode on the configuration AP.
  // Phones and computers require the standard 802.11b/g/n beacon rates.
  const esp_err_t protocolResult = esp_wifi_set_protocol(
      WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  const esp_err_t powerResult = esp_wifi_set_max_tx_power(78);  // 19.5 dBm
  if (protocolResult != ESP_OK || powerResult != ESP_OK) {
    Serial.printf("[PORTAL] Radio setup warning: protocol=%s, power=%s\n",
                  esp_err_to_name(protocolResult), esp_err_to_name(powerResult));
  }
  delay(100);

  const IPAddress portalIp = WiFi.softAPIP();
  Serial.printf("[PORTAL] AP ready: SSID=%s, IP=%s, MAC=%s, channel=%d\n",
                ssid, portalIp.toString().c_str(),
                WiFi.softAPmacAddress().c_str(), WiFi.channel());
  if (!dns.start(53, "*", portalIp)) {
    Serial.println("[PORTAL] DNS server failed to start");
  }
  if (!routesConfigured) { configureRoutes(); routesConfigured = true; }
  server.begin();
  lastActivityMs = millis(); stopRequested = false; active = true;
  // WebServer request handling, JSON validation, and LittleFS transactions form
  // a deep call chain. Keep adequate headroom for a full 32 KB config upload.
  if (xTaskCreatePinnedToCore(portalTask, "CONFIG_PORTAL", 12288, nullptr, 1, &portalTaskHandle, 0) != pdPASS) {
    active = false; dns.stop(); WiFi.softAPdisconnect(true); return false;
  }
  return true;
}

void stopConfigPortal() { if (active) stopRequested = true; }
bool isConfigPortalActive() { return active; }
void noteConfigPortalActivity() { if (active) lastActivityMs = millis(); }
const char* getConfigPortalSsid() { return ssid; }
const char* getConfigPortalPassword() { return password; }
const char* getConfigPortalIp() { return "192.168.4.1"; }
