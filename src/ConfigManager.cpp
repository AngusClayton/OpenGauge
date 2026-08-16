#include "ConfigManager.h"
#include <ArduinoJson.h>
#include <Arduino.h>
#include "obd/pid_schedule.h"
#include "GaugeRenderer.h"

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
      { "sourceId": "waterTemp", "prefix": "Water: ", "suffix": "C", "rangeColors": true, "lowerThreshold": 80.0, "upperThreshold": 105.0, "colorBelow": "blue", "colorBetween": "cyan", "colorAbove": "red" },
      { "sourceId": "intakeTemp", "prefix": "AIT: ", "suffix": "C", "rangeColors": false }
    ]
  },
  {
    "type": "standard",
    "name": "Gauge 2: Horsepower",
    "mainSourceId": "maf", 
    "minVal": 0.0, "maxVal": 300.0,
    "unitLabel": "HP",
    "secondaries": [
      { "sourceId": "maf", "prefix": "MAF: ", "suffix": " g/s", "rangeColors": false }
    ]
  },
  {
    "type": "standard",
    "name": "Gauge 3: AFR",
    "mainSourceId": "afr", 
    "minVal": 10.0, "maxVal": 20.0,
    "unitLabel": "AFR",
    "secondaries": [
      { "sourceId": "lambda", "prefix": "Lambda: ", "suffix": "", "rangeColors": false }
    ]
  },
  {
    "type": "standard",
    "name": "Gauge 4: Ignition",
    "mainSourceId": "ignition", 
    "minVal": -10.0, "maxVal": 40.0,
    "unitLabel": "IGN DEG",
    "secondaries": [
      { "sourceId": "rpm", "prefix": "RPM: ", "suffix": "", "rangeColors": false }
    ]
  },
  {
    "type": "shiftlight",
    "name": "Gauge 5: Shift Lights",
    "shiftTargets": [6500, 6300, 6100, 6000, 5800, 0],
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
  },
  {
    "type": "accelTimer",
    "name": "Gauge 7: 0-100 Timer",
    "mainSourceId": "speed",
    "minVal": 0.0, "maxVal": 100.0,
    "unitLabel": "km/h"
  }
])====";


/**
 * @brief Parses JSON-defined variable registers and gauge profiles once at startup.
 * 
 * This processes the hardcoded JSON strings varsJson and gaugesJson into highly 
 * efficient C-structs in RAM. Once populated, these structs are referenced in 
 * real-time during the display loop, removing the need for runtime JSON parsing.
 */
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
    else if (strcmp(typeStr, "accelTimer") == 0) gc.type = GAUGE_TYPE_ACCEL_TIMER;
    
    strlcpy(gc.name, g["name"] | "", sizeof(gc.name));
    strlcpy(gc.mainSourceId, g["mainSourceId"] | "", sizeof(gc.mainSourceId));
    
    gc.minVal = g["minVal"] | 0.0f;
    gc.maxVal = g["maxVal"] | 100.0f;
    strlcpy(gc.unitLabel, g["unitLabel"] | "", sizeof(gc.unitLabel));
    gc.boostUnits = g["boostUnits"] | false;

    if (gc.type == GAUGE_TYPE_SHIFTLIGHT) {
      JsonArray targets = g["shiftTargets"];
      for (uint8_t gear = 1; gear <= 6 && gear <= targets.size(); gear++) {
        setShiftTargetRpm(gear, targets[gear - 1] | 0);
      }
    }
    
    gc.secondaryCount = 0;
    JsonArray secs = g["secondaries"];
    for (JsonObject s : secs) {
      if (gc.secondaryCount >= 3) break;
      SecondaryMetric& sm = gc.secondaries[gc.secondaryCount];
      strlcpy(sm.sourceId, s["sourceId"] | "", sizeof(sm.sourceId));
      strlcpy(sm.prefix, s["prefix"] | "", sizeof(sm.prefix));
      strlcpy(sm.suffix, s["suffix"] | "", sizeof(sm.suffix));
      sm.rangeColors = s["rangeColors"] | false;
      sm.lowerThreshold = s["lowerThreshold"] | 0.0f;
      sm.upperThreshold = s["upperThreshold"] | 100.0f;
      strlcpy(sm.colorBelow, s["colorBelow"] | "blue", sizeof(sm.colorBelow));
      strlcpy(sm.colorBetween, s["colorBetween"] | "cyan", sizeof(sm.colorBetween));
      strlcpy(sm.colorAbove, s["colorAbove"] | "red", sizeof(sm.colorAbove));
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

/**
 * @brief Unified interface to fetch real-time sensor metrics by variable ID.
 * 
 * Automatically resolves whether a requested ID is driven by the dynamic OBD bus 
 * or an onboard Analog input. It also handles customized non-linear outputs (like 
 * splitting vacuum/boost scales for the boostPress variable).
 * 
 * @param sourceId Unique string identifier of the metric (e.g. "rpm", "boostPress")
 * @return float Calibrated value of the requested metric.
 */
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
            // Convert raw 12-bit ADC reading (0-4095) to compensated sensor voltage.
            // Compensates for the 50/50 hardware voltage divider (2.0 multiplier) 
            // and maps relative to the ESP32 3.3V reference.
            float sensorVolts = ((float)raw / 4095.0f) * 3.3f * 2.0f;
            // Apply standard linear calibration formula defined in the variables JSON config
            activeDataSources[i].cachedValue = (sensorVolts * activeDataSources[i].multiplier) + activeDataSources[i].offset;
        }
    }
}

static size_t gCurrentGaugeProfileIndex = 0;

/**
 * @brief Gets the zero-based index of the currently active gauge profile.
 * 
 * @return size_t Current active gauge index.
 */
size_t getCurrentGaugeProfileIndex() {
  return gCurrentGaugeProfileIndex;
}

bool isAccelerationTimerProfileActive() {
  return gCurrentGaugeProfileIndex < activeGaugeCount &&
         activeGauges[gCurrentGaugeProfileIndex].type == GAUGE_TYPE_ACCEL_TIMER;
}

/**
 * @brief Switch the active display layout and optimize the OBD query schedule.
 * 
 * Performs dynamic dependency mapping on the newly selected gauge:
 * 1. Checks what data sources (OBD variables) are needed for the dial arc and bottom readouts.
 * 2. Compiles a unique, deduplicated list of OBD PIDs needed for these variables.
 * 3. Passes the clean PID list to the TWAI CAN scheduler via setPidSchedule().
 * 4. Custom screens (like Shift Light or G-Meter) automatically include their background requirements.
 * 
 * @param index Zero-based profile index to switch to.
 */
void applyGaugeProfile(size_t index) {
  if (index >= activeGaugeCount) {
    return;
  }
  
  gCurrentGaugeProfileIndex = index;
  const GaugeConfig& gc = activeGauges[index];
  
  uint8_t pids[MAX_DATA_SOURCES];
  size_t pidCount = 0;
  
  // Internal helper to lookup source dependencies and register their associated PIDs
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

  // Compile layout dependency list
  addSourcePid(gc.mainSourceId);
  for (uint8_t i = 0; i < gc.secondaryCount; i++) {
      addSourcePid(gc.secondaries[i].sourceId);
  }

  // Inject hardcoded background requirements for custom-rendered screens
  if (gc.type == GAUGE_TYPE_SHIFTLIGHT || gc.type == GAUGE_TYPE_GMETER) {
     addSourcePid("rpm");
     addSourcePid("speed");
  } else if (strcmp(gc.name, "Gauge 2: Horsepower") == 0) {
     addSourcePid("maf");
  } else if (strcmp(gc.name, "Gauge 3: AFR") == 0) {
     addSourcePid("lambda");
  } else if (strcmp(gc.name, "Gauge 4: Ignition") == 0) {
     addSourcePid("ignition");
     addSourcePid("rpm");
  }

  // Update TWAI CAN schedule dynamically. Only active variables will poll the ECU!
  setPidSchedule(pids, pidCount);
  
  Serial.printf("[CONFIG] Switched to profile %u: %s (Scheduled %u PIDs)\n",
                (unsigned int)(index + 1),
                gc.name,
                (unsigned int)pidCount);
}

/**
 * @brief Cycle to the next sequential gauge profile.
 */
void nextGaugeProfile() {
  if (activeGaugeCount == 0) return;
  const size_t next = (gCurrentGaugeProfileIndex + 1) % activeGaugeCount;
  applyGaugeProfile(next);
}

/**
 * @brief Cycle to the previous sequential gauge profile.
 */
void prevGaugeProfile() {
  if (activeGaugeCount == 0) return;
  const size_t prev = (gCurrentGaugeProfileIndex + activeGaugeCount - 1) % activeGaugeCount;
  applyGaugeProfile(prev);
}
