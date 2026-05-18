#include "ConfigManager.h"
#include <ArduinoJson.h>
#include <Arduino.h>
#include "obd/pid_schedule.h"

DataSourceConfig activeDataSources[MAX_DATA_SOURCES];
size_t activeDataSourceCount = 0;

GaugeConfig activeGauges[MAX_GAUGES];
size_t activeGaugeCount = 0;

const char* varsJson = R"====([
  { "id": "waterTemp", "type": "obd", "pid": 5, "formula": 2 },
  { "id": "intakeTemp", "type": "obd", "pid": 15, "formula": 2 },
  { "id": "rpm", "type": "obd", "pid": 12, "formula": 3 },
  { "id": "speed", "type": "obd", "pid": 13, "formula": 0 },
  { "id": "maf", "type": "obd", "pid": 16, "formula": 4 },
  { "id": "lambda", "type": "obd", "pid": 52, "formula": 7 },
  { "id": "afr", "type": "obd", "pid": 52, "formula": 7 },
  { "id": "ignition", "type": "obd", "pid": 14, "formula": 6 },
  { "id": "boostPress", "type": "analog", "pin": 18, "multiplier": 30.7692, "offset": -28.2692 }
])====";

const char* gaugesJson = R"====([
  {
    "type": "standard",
    "name": "Gauge 1: Boost",
    "mainSourceId": "boostPress", 
    "minVal": -10.0, "maxVal": 25.0,
    "unitLabel": "Boost (PSI)",
    "boostUnits": true,
    "secondaries": [
      { "sourceId": "waterTemp", "prefix": "Water: ", "suffix": "C", "posY": 180, "dynamicColor": true },
      { "sourceId": "intakeTemp", "prefix": "AIT: ", "suffix": "C", "posY": 208, "dynamicColor": false }
    ]
  },
  {
    "type": "standard",
    "name": "Gauge 2: Horsepower",
    "mainSourceId": "maf", 
    "minVal": 0.0, "maxVal": 300.0,
    "unitLabel": "HP",
    "secondaries": [
      { "sourceId": "maf", "prefix": "MAF: ", "suffix": " g/s", "posY": 168, "dynamicColor": false }
    ]
  },
  {
    "type": "standard",
    "name": "Gauge 3: AFR",
    "mainSourceId": "afr", 
    "minVal": 10.0, "maxVal": 20.0,
    "unitLabel": "AFR",
    "secondaries": [
      { "sourceId": "lambda", "prefix": "Lambda: ", "suffix": "", "posY": 144, "dynamicColor": false }
    ]
  },
  {
    "type": "standard",
    "name": "Gauge 4: Ignition",
    "mainSourceId": "ignition", 
    "minVal": -10.0, "maxVal": 40.0,
    "unitLabel": "IGN DEG",
    "secondaries": [
      { "sourceId": "rpm", "prefix": "RPM: ", "suffix": "", "posY": 144, "dynamicColor": false }
    ]
  },
  {
    "type": "shiftlight",
    "name": "Gauge 5: Shift Lights",
    "secondaries": [
      { "sourceId": "rpm" },
      { "sourceId": "speed" }
    ]
  },
  {
    "type": "gmeter",
    "name": "Gauge 6: G Meter",
    "secondaries": [
      { "sourceId": "rpm" },
      { "sourceId": "speed" }
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
    gc.boostUnits = g["boostUnits"] | false;
    
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
  
  if (activeGaugeCount > 0) {
      applyGaugeProfile(0);
  }
}

#include "Sensors.h"
#include "obd/pid_config.h"

float getValueForSource(const char* sourceId) {
    for (size_t i = 0; i < activeDataSourceCount; i++) {
        if (strcmp(activeDataSources[i].id, sourceId) == 0) {
            const DataSourceConfig& ds = activeDataSources[i];
            
            if (ds.type == SOURCE_OBD) {
                switch (ds.pid) {
                    case PID_COOLANT_TEMP: return obdValues.coolant_temp_c;
                    case PID_INTAKE_AIR_TEMP: return obdValues.intake_air_temp_c;
                    case PID_ENGINE_RPM: return obdValues.rpm;
                    case PID_VEHICLE_SPEED: return obdValues.vehicle_speed_kmh;
                    case PID_ENGINE_LOAD: return obdValues.engine_load;
                    case PID_FUEL_PRESSURE: return obdValues.fuel_pressure_bar;
                    case PID_FUEL_LEVEL: return obdValues.fuel_level;
                    case PID_INTAKE_PRESSURE: return obdValues.intake_pressure_kpa;
                    case PID_IGNITION_TIMING: return obdValues.ignition_timing;
                    case PID_MAF_AIRFLOW: return obdValues.maf_airflow;
                    case PID_THROTTLE_POS: return obdValues.throttle_pos;
                    case PID_O2_VOLTAGE: return obdValues.o2_voltage;
                    case PID_O2_SENSOR1_LAMBDA: {
                         if (strcmp(sourceId, "afr") == 0) {
                             return obdValues.afr_gasoline;
                         }
                         return obdValues.lambda_ratio;
                     }
                    case PID_ETHANOL_FUEL: return obdValues.ethanol_percent;
                    default: return 0.0f;
                }
            } else if (ds.type == SOURCE_ANALOG) {
                float val = ds.cachedValue;
                if (strcmp(sourceId, "boostPress") == 0) {
                    if (val < 0.0f) {
                        return val; // Vacuum in inHg
                    } else {
                        return val / 2.03602f; // Boost in PSI
                    }
                }
                return val;
            }
        }
    }
    return 0.0f;
}

void updateAnalogSources() {
    for (size_t i = 0; i < activeDataSourceCount; i++) {
        if (activeDataSources[i].type == SOURCE_ANALOG) {
            int raw = analogRead(activeDataSources[i].pin);
            // Convert raw 12-bit ADC reading to compensated sensor voltage (incorporating the 50/50 voltage divider: 3.3V ref * 2.0 divider compensation)
            float sensorVolts = ((float)raw / 4095.0f) * 3.3f * 2.0f;
            activeDataSources[i].cachedValue = (sensorVolts * activeDataSources[i].multiplier) + activeDataSources[i].offset;
        }
    }
}

static size_t gCurrentGaugeProfileIndex = 0;

size_t getCurrentGaugeProfileIndex() {
    return gCurrentGaugeProfileIndex;
}

void applyGaugeProfile(size_t index) {
  if (index >= activeGaugeCount) {
    return;
  }
  
  gCurrentGaugeProfileIndex = index;
  const GaugeConfig& gc = activeGauges[index];
  
  uint8_t pids[MAX_DATA_SOURCES];
  size_t pidCount = 0;
  
  auto addSourcePid = [&](const char* sourceId) {
    if (strlen(sourceId) == 0) return;
    for (size_t i = 0; i < activeDataSourceCount; i++) {
      if (strcmp(activeDataSources[i].id, sourceId) == 0) {
        if (activeDataSources[i].type == SOURCE_OBD) {
          bool found = false;
          for (size_t j = 0; j < pidCount; j++) {
            if (pids[j] == activeDataSources[i].pid) {
              found = true; break;
            }
          }
          if (!found) {
            pids[pidCount++] = activeDataSources[i].pid;
          }
        }
        break;
      }
    }
  };

  addSourcePid(gc.mainSourceId);
  for (uint8_t i = 0; i < gc.secondaryCount; i++) {
      addSourcePid(gc.secondaries[i].sourceId);
  }

  // Hardcoded dependencies for custom gauges
  if (gc.type == GAUGE_TYPE_SHIFTLIGHT || gc.type == GAUGE_TYPE_GMETER) {
     addSourcePid("rpm");
     addSourcePid("speed");
  } else if (strcmp(gc.name, "Gauge 2: Horsepower") == 0) {
     // Because renderHorsepowerGauge is still hardcoded to use MAF 
     addSourcePid("maf");
  } else if (strcmp(gc.name, "Gauge 3: AFR") == 0) {
     addSourcePid("lambda");
  } else if (strcmp(gc.name, "Gauge 4: Ignition") == 0) {
     addSourcePid("ignition");
     addSourcePid("rpm");
  }

  setPidSchedule(pids, pidCount);
  
  Serial.printf("[CONFIG] Switched to profile %u: %s (Scheduled %u PIDs)\n",
                (unsigned int)(index + 1),
                gc.name,
                (unsigned int)pidCount);
}

void nextGaugeProfile() {
  if (activeGaugeCount == 0) return;
  const size_t next = (gCurrentGaugeProfileIndex + 1) % activeGaugeCount;
  applyGaugeProfile(next);
}

void prevGaugeProfile() {
  if (activeGaugeCount == 0) return;
  const size_t prev = (gCurrentGaugeProfileIndex + activeGaugeCount - 1) % activeGaugeCount;
  applyGaugeProfile(prev);
}
