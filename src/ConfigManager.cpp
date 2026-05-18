#include "ConfigManager.h"
#include <ArduinoJson.h>
#include <Arduino.h>

DataSourceConfig activeDataSources[MAX_DATA_SOURCES];
size_t activeDataSourceCount = 0;

GaugeConfig activeGauges[MAX_GAUGES];
size_t activeGaugeCount = 0;

const char* varsJson = R"====([
  {
    "id": "waterTemp",
    "type": "obd",
    "pid": 5, 
    "formula": 1 
  },
  {
    "id": "boostPress",
    "type": "analog",
    "pin": 18,
    "formula": 0,
    "multiplier": 30.76, 
    "offset": -28.26
  }
])====";

const char* gaugesJson = R"====([
  {
    "type": "standard",
    "name": "Gauge 1: Boost",
    "mainSourceId": "boostPress", 
    "minVal": -10.0,
    "maxVal": 25.0,
    "unitLabel": "Boost (PSI)",
    "secondaries": [
      {
        "sourceId": "waterTemp", 
        "prefix": "Water: ",
        "suffix": "C",
        "posY": 180,
        "dynamicColor": true 
      }
    ]
  }
])====";

void loadConfigFromJson() {
  // Parse Variables
  JsonDocument docVars;
  DeserializationError err1 = deserializeJson(docVars, varsJson);
  if (err1) {
    Serial.print(F("deserializeJson(vars) failed: "));
    Serial.println(err1.f_str());
    return;
  }

  activeDataSourceCount = 0;
  JsonArray varsArray = docVars.as<JsonArray>();
  for (JsonObject v : varsArray) {
    if (activeDataSourceCount >= MAX_DATA_SOURCES) break;
    DataSourceConfig& ds = activeDataSources[activeDataSourceCount];
    
    strlcpy(ds.id, v["id"] | "", sizeof(ds.id));
    
    const char* typeStr = v["type"];
    if (strcmp(typeStr, "obd") == 0) ds.type = SOURCE_OBD;
    else if (strcmp(typeStr, "analog") == 0) ds.type = SOURCE_ANALOG;
    
    ds.pid = v["pid"] | 0;
    ds.formula = v["formula"] | 0;
    ds.pin = v["pin"] | 0;
    ds.multiplier = v["multiplier"] | 1.0f;
    ds.offset = v["offset"] | 0.0f;
    
    activeDataSourceCount++;
  }

  // Parse Gauges
  JsonDocument docGauges;
  DeserializationError err2 = deserializeJson(docGauges, gaugesJson);
  if (err2) {
    Serial.print(F("deserializeJson(gauges) failed: "));
    Serial.println(err2.f_str());
    return;
  }

  activeGaugeCount = 0;
  JsonArray gaugesArray = docGauges.as<JsonArray>();
  for (JsonObject g : gaugesArray) {
    if (activeGaugeCount >= MAX_GAUGES) break;
    GaugeConfig& gc = activeGauges[activeGaugeCount];
    
    const char* typeStr = g["type"];
    if (strcmp(typeStr, "standard") == 0) gc.type = GAUGE_TYPE_STANDARD;
    else if (strcmp(typeStr, "gmeter") == 0) gc.type = GAUGE_TYPE_GMETER;
    else if (strcmp(typeStr, "shiftlight") == 0) gc.type = GAUGE_TYPE_SHIFTLIGHT;
    
    strlcpy(gc.name, g["name"] | "", sizeof(gc.name));
    strlcpy(gc.mainSourceId, g["mainSourceId"] | "", sizeof(gc.mainSourceId));
    
    gc.minVal = g["minVal"] | 0.0f;
    gc.maxVal = g["maxVal"] | 100.0f;
    strlcpy(gc.unitLabel, g["unitLabel"] | "", sizeof(gc.unitLabel));
    
    gc.secondaryCount = 0;
    JsonArray secs = g["secondaries"];
    for (JsonObject s : secs) {
      if (gc.secondaryCount >= 3) break;
      SecondaryMetric& sm = gc.secondaries[gc.secondaryCount];
      strlcpy(sm.sourceId, s["sourceId"] | "", sizeof(sm.sourceId));
      strlcpy(sm.prefix, s["prefix"] | "", sizeof(sm.prefix));
      strlcpy(sm.suffix, s["suffix"] | "", sizeof(sm.suffix));
      sm.posY = s["posY"] | 0;
      sm.dynamicColor = s["dynamicColor"] | false;
      gc.secondaryCount++;
    }
    
    activeGaugeCount++;
  }

  Serial.printf("[CONFIG] Loaded %d vars and %d gauges from JSON\n", (int)activeDataSourceCount, (int)activeGaugeCount);
}

float getValueForSource(const char* sourceId) {
    return 0.0f; // Placeholder for now
}
