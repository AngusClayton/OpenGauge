#include "ConfigStorage.h"
#include "ConfigCodec.h"
#include "ConfigManager.h"
#include <Arduino.h>
#include <LittleFS.h>

namespace {
constexpr const char* kCurrentPath = "/config.json";
constexpr const char* kBackupPath = "/config.bak";
constexpr const char* kTemporaryPath = "/config.tmp";
bool gMounted = false;

bool loadPath(const char* path, char* error, size_t errorSize) {
  if (!gMounted || !LittleFS.exists(path)) return false;
  File file = LittleFS.open(path, "r");
  if (!file || file.size() == 0 || file.size() > OPEN_GAUGE_MAX_CONFIG_BYTES) {
    if (error && errorSize) snprintf(error, errorSize, "Stored configuration has an invalid size.");
    return false;
  }
  String json = file.readString(); file.close();
  return applyConfigJson(json.c_str(), json.length(), error, errorSize);
}
}

bool initConfigStorage() {
  gMounted = LittleFS.begin(true);
  if (!gMounted) Serial.println("[CONFIG] LittleFS mount failed; embedded defaults will be used.");
  return gMounted;
}

bool loadPersistedConfig(char* error, size_t errorSize) {
  if (loadPath(kCurrentPath, error, errorSize)) return true;
  if (loadPath(kBackupPath, error, errorSize)) {
    LittleFS.remove(kCurrentPath);
    LittleFS.rename(kBackupPath, kCurrentPath);
    return true;
  }
  return false;
}

bool saveAndApplyConfig(const char* json, size_t length, char* error, size_t errorSize) {
  if (!gMounted) { if (error && errorSize) snprintf(error, errorSize, "Configuration storage is unavailable."); return false; }
  ParsedOpenGaugeConfig candidate{};
  if (!parseOpenGaugeConfig(json, length, candidate, error, errorSize)) return false;

  LittleFS.remove(kTemporaryPath);
  File temporary = LittleFS.open(kTemporaryPath, "w");
  if (!temporary || temporary.write((const uint8_t*)json, length) != length) {
    if (temporary) temporary.close(); LittleFS.remove(kTemporaryPath);
    if (error && errorSize) snprintf(error, errorSize, "Could not write the temporary configuration.");
    return false;
  }
  temporary.flush(); temporary.close();

  LittleFS.remove(kBackupPath);
  if (LittleFS.exists(kCurrentPath) && !LittleFS.rename(kCurrentPath, kBackupPath)) {
    LittleFS.remove(kTemporaryPath);
    if (error && errorSize) snprintf(error, errorSize, "Could not preserve the previous configuration.");
    return false;
  }
  if (!LittleFS.rename(kTemporaryPath, kCurrentPath)) {
    if (LittleFS.exists(kBackupPath)) LittleFS.rename(kBackupPath, kCurrentPath);
    if (error && errorSize) snprintf(error, errorSize, "Could not commit the new configuration.");
    return false;
  }
  if (!applyConfigJson(json, length, error, errorSize)) {
    LittleFS.remove(kCurrentPath);
    if (LittleFS.exists(kBackupPath)) LittleFS.rename(kBackupPath, kCurrentPath);
    return false;
  }
  return true;
}
