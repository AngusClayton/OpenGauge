#include "ConfigCodec.h"
#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

void fail(char* error, size_t size, const char* message) {
  if (error && size) snprintf(error, size, "%s", message);
}

template <size_t N>
bool copyText(char (&target)[N], const char* value) {
  if (!value || !value[0] || strlen(value) >= N) return false;
  snprintf(target, N, "%s", value);
  return true;
}

bool hasSource(const ParsedOpenGaugeConfig& config, const char* id) {
  for (size_t i = 0; i < config.dataSourceCount; ++i) {
    if (strcmp(config.dataSources[i].id, id) == 0) return true;
  }
  return false;
}

bool supportedColor(const char* color) {
  static const char* colors[] = {"white", "gray", "blue", "cyan", "green", "yellow", "orange", "red"};
  for (const char* candidate : colors) if (color && strcmp(color, candidate) == 0) return true;
  return false;
}

GaugeType gaugeType(const char* type, bool& valid) {
  valid = true;
  if (type && strcmp(type, "standard") == 0) return GAUGE_TYPE_STANDARD;
  if (type && strcmp(type, "gmeter") == 0) return GAUGE_TYPE_GMETER;
  if (type && strcmp(type, "shiftlight") == 0) return GAUGE_TYPE_SHIFTLIGHT;
  if (type && strcmp(type, "accelTimer") == 0) return GAUGE_TYPE_ACCEL_TIMER;
  valid = false;
  return GAUGE_TYPE_STANDARD;
}

}  // namespace

bool parseOpenGaugeConfig(const char* json, size_t length,
                          ParsedOpenGaugeConfig& output,
                          char* error, size_t errorSize) {
  memset(&output, 0, sizeof(output));
  if (!json || length == 0 || length > OPEN_GAUGE_MAX_CONFIG_BYTES) {
    fail(error, errorSize, "Configuration must be between 1 byte and 32 KB."); return false;
  }

  JsonDocument document;
  const DeserializationError jsonError = deserializeJson(document, json, length);
  if (jsonError) { fail(error, errorSize, jsonError.c_str()); return false; }
  JsonObject root = document.as<JsonObject>();
  if (root.isNull() || (root["version"] | 0) != OPEN_GAUGE_CONFIG_VERSION) {
    fail(error, errorSize, "Unsupported or missing configuration version."); return false;
  }
  JsonArray sources = root["dataSources"].as<JsonArray>();
  JsonArray gauges = root["gauges"].as<JsonArray>();
  if (sources.isNull() || sources.size() == 0 || sources.size() > MAX_DATA_SOURCES) {
    fail(error, errorSize, "dataSources must contain 1 to 10 entries."); return false;
  }
  if (gauges.isNull() || gauges.size() == 0 || gauges.size() > MAX_GAUGES) {
    fail(error, errorSize, "gauges must contain 1 to 10 entries."); return false;
  }

  for (JsonObject source : sources) {
    DataSourceConfig& target = output.dataSources[output.dataSourceCount];
    const char* id = source["id"] | "";
    if (!copyText(target.id, id)) { fail(error, errorSize, "Each data source needs an ID of 1 to 15 characters."); return false; }
    if (hasSource(output, target.id)) { fail(error, errorSize, "Data source IDs must be unique."); return false; }
    const char* type = source["type"] | "";
    if (strcmp(type, "obd") == 0) {
      target.type = SOURCE_OBD;
      if (!source["pid"].is<int>() || !source["formula"].is<int>()) { fail(error, errorSize, "OBD sources need integer pid and formula fields."); return false; }
      target.pid = source["pid"].as<uint8_t>(); target.formula = source["formula"].as<uint8_t>();
    } else if (strcmp(type, "analog") == 0) {
      target.type = SOURCE_ANALOG;
      if (!source["pin"].is<int>() || !source["multiplier"].is<float>() || !source["offset"].is<float>()) { fail(error, errorSize, "Analog sources need pin, multiplier, and offset fields."); return false; }
      target.pin = source["pin"].as<uint8_t>(); target.multiplier = source["multiplier"].as<float>(); target.offset = source["offset"].as<float>();
    } else { fail(error, errorSize, "Data source type must be obd or analog."); return false; }
    ++output.dataSourceCount;
  }

  for (JsonObject gauge : gauges) {
    GaugeConfig& target = output.gauges[output.gaugeCount];
    bool validType = false; target.type = gaugeType(gauge["type"] | "", validType);
    if (!validType || !copyText(target.name, gauge["name"] | "")) { fail(error, errorSize, "Each gauge needs a supported type and a name of 1 to 31 characters."); return false; }
    snprintf(target.mainSourceId, sizeof(target.mainSourceId), "%s", gauge["mainSourceId"] | "");
    target.minVal = gauge["minVal"] | 0.0f; target.maxVal = gauge["maxVal"] | 100.0f;
    const char* unitLabel = gauge["unitLabel"] | "";
    if (strlen(unitLabel) >= sizeof(target.unitLabel)) { fail(error, errorSize, "Gauge unit labels must be at most 15 characters."); return false; }
    snprintf(target.unitLabel, sizeof(target.unitLabel), "%s", unitLabel);
    target.boostUnits = gauge["boostUnits"] | false;

    if (target.type == GAUGE_TYPE_STANDARD || target.type == GAUGE_TYPE_ACCEL_TIMER) {
      if (!hasSource(output, target.mainSourceId) || !isfinite(target.minVal) || !isfinite(target.maxVal) || target.minVal >= target.maxVal) {
        fail(error, errorSize, "Standard and timer gauges need a valid source and increasing range."); return false;
      }
    }
    if (target.type == GAUGE_TYPE_SHIFTLIGHT) {
      JsonArray shifts = gauge["shiftTargets"].as<JsonArray>();
      if (shifts.size() != 6) { fail(error, errorSize, "shiftTargets must contain six RPM values."); return false; }
      for (size_t gear = 0; gear < 6; ++gear) {
        const int rpm = shifts[gear] | -1;
        if (rpm < 0 || rpm > 12000) { fail(error, errorSize, "Shift targets must be from 0 to 12000 RPM."); return false; }
        target.shiftTargets[gear] = (uint16_t)rpm;
      }
    }

    JsonArray secondaries = gauge["secondaries"].as<JsonArray>();
    if (secondaries.size() > 3) { fail(error, errorSize, "A gauge supports at most three secondary readings."); return false; }
    for (JsonObject secondary : secondaries) {
      SecondaryMetric& metric = target.secondaries[target.secondaryCount];
      if (!copyText(metric.sourceId, secondary["sourceId"] | "") || !hasSource(output, metric.sourceId)) { fail(error, errorSize, "A secondary reading references an unknown source."); return false; }
      const char* prefix = secondary["prefix"] | ""; const char* suffix = secondary["suffix"] | "";
      if (strlen(prefix) >= sizeof(metric.prefix) || strlen(suffix) >= sizeof(metric.suffix)) { fail(error, errorSize, "Secondary labels must be at most 15 characters and units at most 7."); return false; }
      snprintf(metric.prefix, sizeof(metric.prefix), "%s", prefix); snprintf(metric.suffix, sizeof(metric.suffix), "%s", suffix);
      metric.rangeColors = secondary["rangeColors"] | false;
      metric.lowerThreshold = secondary["lowerThreshold"] | 0.0f; metric.upperThreshold = secondary["upperThreshold"] | 100.0f;
      const char* below = secondary["colorBelow"] | "blue"; const char* between = secondary["colorBetween"] | "cyan"; const char* above = secondary["colorAbove"] | "red";
      if (metric.rangeColors && (metric.lowerThreshold >= metric.upperThreshold || !supportedColor(below) || !supportedColor(between) || !supportedColor(above))) { fail(error, errorSize, "A secondary colour range is invalid."); return false; }
      snprintf(metric.colorBelow, sizeof(metric.colorBelow), "%s", below); snprintf(metric.colorBetween, sizeof(metric.colorBetween), "%s", between); snprintf(metric.colorAbove, sizeof(metric.colorAbove), "%s", above);
      ++target.secondaryCount;
    }
    ++output.gaugeCount;
  }
  if (error && errorSize) error[0] = '\0';
  return true;
}

const char* getDefaultOpenGaugeConfigJson() {
  return R"json({
  "version": 1,
  "dataSources": [
    {"id":"waterTemp","type":"obd","pid":5,"formula":2},{"id":"intakeTemp","type":"obd","pid":15,"formula":2},{"id":"rpm","type":"obd","pid":12,"formula":3},{"id":"speed","type":"obd","pid":13,"formula":0},{"id":"maf","type":"obd","pid":16,"formula":4},{"id":"lambda","type":"obd","pid":52,"formula":7},{"id":"afr","type":"obd","pid":52,"formula":7},{"id":"ignition","type":"obd","pid":14,"formula":6},{"id":"boostPress","type":"analog","pin":18,"multiplier":30.7692,"offset":-28.2692}
  ],
  "gauges": [
    {"type":"standard","name":"Boost","mainSourceId":"boostPress","minVal":-10,"maxVal":25,"unitLabel":"Boost (PSI)","boostUnits":true,"secondaries":[{"sourceId":"waterTemp","prefix":"Water: ","suffix":"C","rangeColors":true,"lowerThreshold":80,"upperThreshold":105,"colorBelow":"blue","colorBetween":"cyan","colorAbove":"red"},{"sourceId":"intakeTemp","prefix":"AIT: ","suffix":"C","rangeColors":false}]},
    {"type":"standard","name":"Horsepower","mainSourceId":"maf","minVal":0,"maxVal":300,"unitLabel":"HP","secondaries":[{"sourceId":"maf","prefix":"MAF: ","suffix":" g/s","rangeColors":false}]},
    {"type":"standard","name":"AFR","mainSourceId":"afr","minVal":10,"maxVal":20,"unitLabel":"AFR","secondaries":[{"sourceId":"lambda","prefix":"Lambda: ","suffix":"","rangeColors":false}]},
    {"type":"standard","name":"Ignition","mainSourceId":"ignition","minVal":-10,"maxVal":40,"unitLabel":"IGN DEG","secondaries":[{"sourceId":"rpm","prefix":"RPM: ","suffix":"","rangeColors":false}]},
    {"type":"shiftlight","name":"Shift Lights","shiftTargets":[6500,6300,6100,6000,5800,0]},
    {"type":"gmeter","name":"G Meter"},
    {"type":"accelTimer","name":"0-100 Timer","mainSourceId":"speed","minVal":0,"maxVal":100,"unitLabel":"km/h"}
  ]
})json";
}
