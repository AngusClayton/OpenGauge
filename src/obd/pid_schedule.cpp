#include "pid_schedule.h"

#include <cJSON.h>

#include "pid_config.h"

namespace {
constexpr size_t kMaxPidSchedule = 32;

portMUX_TYPE g_pidScheduleMux = portMUX_INITIALIZER_UNLOCKED;
uint8_t g_pidSchedule[kMaxPidSchedule] = {0};
size_t g_pidCount = 0;
size_t g_pidIndex = 0;

PidMetricConfig g_configs[kMaxPidSchedule] = {};
size_t g_configCount = 0;
PidMetricValue g_values[kMaxPidSchedule] = {};

constexpr uint8_t kDefaultPids[] = {
  PID_ENGINE_RPM,
  PID_VEHICLE_SPEED,
  PID_COOLANT_TEMP,
  PID_INTAKE_AIR_TEMP,
  PID_ENGINE_LOAD,
  PID_THROTTLE_POS,
  PID_INTAKE_PRESSURE,
  PID_MAF_AIRFLOW,
  PID_FUEL_LEVEL,
  PID_IGNITION_TIMING,
  PID_FUEL_PRESSURE,
  PID_O2_VOLTAGE,
  PID_O2_SENSOR1_LAMBDA,
  PID_ETHANOL_FUEL,
};

bool containsPidUnlocked(uint8_t pid) {
  for (size_t i = 0; i < g_pidCount; i++) {
    if (g_pidSchedule[i] == pid) {
      return true;
    }
  }
  return false;
}

int findConfigIndexUnlocked(uint8_t pid) {
  for (size_t i = 0; i < g_configCount; i++) {
    if (g_configs[i].pid == pid) {
      return (int)i;
    }
  }
  return -1;
}

int findValueIndexUnlocked(uint8_t pid) {
  for (size_t i = 0; i < g_configCount; i++) {
    if (g_values[i].pid == pid) {
      return (int)i;
    }
  }
  return -1;
}

float applyFormula(const PidMetricConfig& cfg, uint16_t raw) {
  const uint8_t A = (uint8_t)((raw >> 8) & 0xFF);
  const uint8_t B = (uint8_t)(raw & 0xFF);

  float base = 0.0f;
  switch (cfg.formula) {
    case PID_FORMULA_RAW_A:
      base = (float)A;
      break;
    case PID_FORMULA_RAW_AB:
      base = (float)((A << 8) | B);
      break;
    case PID_FORMULA_A_MINUS_40:
      base = (float)A - 40.0f;
      break;
    case PID_FORMULA_AB_DIV_4:
      base = (float)(((A << 8) | B) / 4.0f);
      break;
    case PID_FORMULA_AB_DIV_100:
      base = (float)(((A << 8) | B) / 100.0f);
      break;
    case PID_FORMULA_A_DIV_2_55:
      base = (float)A / 2.55f;
      break;
    case PID_FORMULA_LINEAR_A:
      base = (float)A;
      break;
    case PID_FORMULA_LINEAR_AB:
      base = (float)((A << 8) | B);
      break;
    default:
      base = (float)A;
      break;
  }

  return (base * cfg.scale) + cfg.offset;
}

const char* formulaToString(PidFormulaType f) {
  switch (f) {
    case PID_FORMULA_RAW_A: return "RAW_A";
    case PID_FORMULA_RAW_AB: return "RAW_AB";
    case PID_FORMULA_A_MINUS_40: return "A_MINUS_40";
    case PID_FORMULA_AB_DIV_4: return "AB_DIV_4";
    case PID_FORMULA_AB_DIV_100: return "AB_DIV_100";
    case PID_FORMULA_A_DIV_2_55: return "A_DIV_2_55";
    case PID_FORMULA_LINEAR_A: return "LINEAR_A";
    case PID_FORMULA_LINEAR_AB: return "LINEAR_AB";
    default: return "RAW_A";
  }
}

PidFormulaType formulaFromString(const char* s) {
  if (s == NULL) return PID_FORMULA_RAW_A;
  if (strcmp(s, "RAW_AB") == 0) return PID_FORMULA_RAW_AB;
  if (strcmp(s, "A_MINUS_40") == 0) return PID_FORMULA_A_MINUS_40;
  if (strcmp(s, "AB_DIV_4") == 0) return PID_FORMULA_AB_DIV_4;
  if (strcmp(s, "AB_DIV_100") == 0) return PID_FORMULA_AB_DIV_100;
  if (strcmp(s, "A_DIV_2_55") == 0) return PID_FORMULA_A_DIV_2_55;
  if (strcmp(s, "LINEAR_A") == 0) return PID_FORMULA_LINEAR_A;
  if (strcmp(s, "LINEAR_AB") == 0) return PID_FORMULA_LINEAR_AB;
  return PID_FORMULA_RAW_A;
}

void rebuildScheduleFromConfigsUnlocked() {
  g_pidCount = 0;
  g_pidIndex = 0;
  for (size_t i = 0; i < g_configCount && g_pidCount < kMaxPidSchedule; i++) {
    if (g_configs[i].enabled && !containsPidUnlocked(g_configs[i].pid)) {
      g_pidSchedule[g_pidCount] = g_configs[i].pid;
      g_pidCount++;
    }
  }
}

void loadDefaultConfigsUnlocked() {
  g_configCount = 0;

  auto add = [&](uint8_t pid, const char* name, PidFormulaType formula, float scale, float offset, uint8_t bytes) {
    if (g_configCount >= kMaxPidSchedule) return;
    PidMetricConfig& c = g_configs[g_configCount];
    c.pid = pid;
    strncpy(c.name, name, sizeof(c.name) - 1);
    c.name[sizeof(c.name) - 1] = '\0';
    c.formula = formula;
    c.scale = scale;
    c.offset = offset;
    c.bytes = bytes;
    c.enabled = true;

    g_values[g_configCount].pid = pid;
    g_values[g_configCount].value = 0.0f;
    g_values[g_configCount].valid = false;
    g_values[g_configCount].updatedAtMs = 0;
    g_configCount++;
  };

  add(PID_ENGINE_RPM, "Engine RPM", PID_FORMULA_AB_DIV_4, 1.0f, 0.0f, 2);
  add(PID_VEHICLE_SPEED, "Vehicle Speed", PID_FORMULA_RAW_A, 1.0f, 0.0f, 1);
  add(PID_COOLANT_TEMP, "Coolant Temp", PID_FORMULA_A_MINUS_40, 1.0f, 0.0f, 1);
  add(PID_INTAKE_AIR_TEMP, "Intake Air Temp", PID_FORMULA_A_MINUS_40, 1.0f, 0.0f, 1);
  add(PID_ENGINE_LOAD, "Engine Load", PID_FORMULA_A_DIV_2_55, 1.0f, 0.0f, 1);
  add(PID_THROTTLE_POS, "Throttle", PID_FORMULA_A_DIV_2_55, 1.0f, 0.0f, 1);
  add(PID_INTAKE_PRESSURE, "Intake Press", PID_FORMULA_RAW_A, 1.0f, 0.0f, 1);
  add(PID_MAF_AIRFLOW, "MAF", PID_FORMULA_AB_DIV_100, 1.0f, 0.0f, 2);
  add(PID_FUEL_LEVEL, "Fuel Level", PID_FORMULA_A_DIV_2_55, 1.0f, 0.0f, 1);
  add(PID_IGNITION_TIMING, "Ign Timing", PID_FORMULA_LINEAR_A, 0.5f, -64.0f, 1);
  add(PID_FUEL_PRESSURE, "Fuel Press", PID_FORMULA_LINEAR_A, 0.079f, 0.0f, 1);
  add(PID_O2_VOLTAGE, "O2 Voltage", PID_FORMULA_LINEAR_A, 0.005f, 0.0f, 1);
  add(PID_O2_SENSOR1_LAMBDA, "Lambda", PID_FORMULA_LINEAR_AB, 1.0f / 32768.0f, 0.0f, 2); // Now PID 0x34
  add(PID_ETHANOL_FUEL, "Ethanol", PID_FORMULA_RAW_A, 1.0f, 0.0f, 1);

  rebuildScheduleFromConfigsUnlocked();
}
} // namespace

void initPidScheduleDefaults() {
  portENTER_CRITICAL(&g_pidScheduleMux);
  loadDefaultConfigsUnlocked();
  portEXIT_CRITICAL(&g_pidScheduleMux);
}

bool setPidSchedule(const uint8_t* pids, size_t count) {
  if (pids == NULL || count == 0 || count > kMaxPidSchedule) {
    return false;
  }

  portENTER_CRITICAL(&g_pidScheduleMux);
  g_pidCount = 0;
  g_pidIndex = 0;

  for (size_t i = 0; i < count; i++) {
    const uint8_t pid = pids[i];
    if (!containsPidUnlocked(pid) && g_pidCount < kMaxPidSchedule) {
      g_pidSchedule[g_pidCount] = pid;
      g_pidCount++;
    }
  }
  portEXIT_CRITICAL(&g_pidScheduleMux);

  return g_pidCount > 0;
}

bool setPidMetricConfigList(const PidMetricConfig* configs, size_t count) {
  if (configs == NULL || count == 0 || count > kMaxPidSchedule) {
    return false;
  }

  portENTER_CRITICAL(&g_pidScheduleMux);
  g_configCount = 0;
  for (size_t i = 0; i < count; i++) {
    const PidMetricConfig& in = configs[i];
    if (findConfigIndexUnlocked(in.pid) >= 0) {
      continue;
    }

    PidMetricConfig& out = g_configs[g_configCount];
    out = in;
    out.name[sizeof(out.name) - 1] = '\0';

    g_values[g_configCount].pid = out.pid;
    g_values[g_configCount].value = 0.0f;
    g_values[g_configCount].valid = false;
    g_values[g_configCount].updatedAtMs = 0;
    g_configCount++;

    if (g_configCount >= kMaxPidSchedule) {
      break;
    }
  }
  rebuildScheduleFromConfigsUnlocked();
  portEXIT_CRITICAL(&g_pidScheduleMux);

  return g_configCount > 0;
}

bool loadPidMetricConfigFromJson(const char* jsonText) {
  if (jsonText == NULL) {
    return false;
  }

  cJSON* root = cJSON_Parse(jsonText);
  if (root == NULL) {
    return false;
  }

  cJSON* metrics = cJSON_GetObjectItemCaseSensitive(root, "metrics");
  if (!cJSON_IsArray(metrics)) {
    cJSON_Delete(root);
    return false;
  }

  PidMetricConfig temp[kMaxPidSchedule] = {};
  size_t tempCount = 0;

  cJSON* item = NULL;
  cJSON_ArrayForEach(item, metrics) {
    if (!cJSON_IsObject(item) || tempCount >= kMaxPidSchedule) {
      continue;
    }

    cJSON* pid = cJSON_GetObjectItemCaseSensitive(item, "pid");
    if (!cJSON_IsNumber(pid)) {
      continue;
    }

    PidMetricConfig cfg = {};
    cfg.pid = (uint8_t)pid->valueint;
    cfg.formula = PID_FORMULA_RAW_A;
    cfg.scale = 1.0f;
    cfg.offset = 0.0f;
    cfg.bytes = 1;
    cfg.enabled = true;
    strncpy(cfg.name, "PID", sizeof(cfg.name) - 1);

    cJSON* name = cJSON_GetObjectItemCaseSensitive(item, "name");
    if (cJSON_IsString(name) && name->valuestring != NULL) {
      strncpy(cfg.name, name->valuestring, sizeof(cfg.name) - 1);
      cfg.name[sizeof(cfg.name) - 1] = '\0';
    }

    cJSON* formula = cJSON_GetObjectItemCaseSensitive(item, "formula");
    if (cJSON_IsString(formula) && formula->valuestring != NULL) {
      cfg.formula = formulaFromString(formula->valuestring);
    }

    cJSON* scale = cJSON_GetObjectItemCaseSensitive(item, "scale");
    if (cJSON_IsNumber(scale)) {
      cfg.scale = (float)scale->valuedouble;
    }

    cJSON* offset = cJSON_GetObjectItemCaseSensitive(item, "offset");
    if (cJSON_IsNumber(offset)) {
      cfg.offset = (float)offset->valuedouble;
    }

    cJSON* bytes = cJSON_GetObjectItemCaseSensitive(item, "bytes");
    if (cJSON_IsNumber(bytes)) {
      cfg.bytes = (uint8_t)bytes->valueint;
    }

    cJSON* enabled = cJSON_GetObjectItemCaseSensitive(item, "enabled");
    if (cJSON_IsBool(enabled)) {
      cfg.enabled = cJSON_IsTrue(enabled);
    }

    temp[tempCount] = cfg;
    tempCount++;
  }

  cJSON_Delete(root);
  if (tempCount == 0) {
    return false;
  }

  return setPidMetricConfigList(temp, tempCount);
}

bool exportPidMetricConfigToJson(String& jsonOut) {
  cJSON* root = cJSON_CreateObject();
  cJSON* metrics = cJSON_CreateArray();
  if (root == NULL || metrics == NULL) {
    if (root != NULL) cJSON_Delete(root);
    return false;
  }

  cJSON_AddItemToObject(root, "metrics", metrics);

  portENTER_CRITICAL(&g_pidScheduleMux);
  for (size_t i = 0; i < g_configCount; i++) {
    cJSON* item = cJSON_CreateObject();
    if (item == NULL) {
      continue;
    }
    cJSON_AddNumberToObject(item, "pid", g_configs[i].pid);
    cJSON_AddStringToObject(item, "name", g_configs[i].name);
    cJSON_AddStringToObject(item, "formula", formulaToString(g_configs[i].formula));
    cJSON_AddNumberToObject(item, "scale", g_configs[i].scale);
    cJSON_AddNumberToObject(item, "offset", g_configs[i].offset);
    cJSON_AddNumberToObject(item, "bytes", g_configs[i].bytes);
    cJSON_AddBoolToObject(item, "enabled", g_configs[i].enabled);
    cJSON_AddItemToArray(metrics, item);
  }
  portEXIT_CRITICAL(&g_pidScheduleMux);

  char* text = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (text == NULL) {
    return false;
  }

  jsonOut = text;
  cJSON_free(text);
  return true;
}

bool appendPidToSchedule(uint8_t pid) {
  bool appended = false;

  portENTER_CRITICAL(&g_pidScheduleMux);
  if (g_pidCount < kMaxPidSchedule && !containsPidUnlocked(pid)) {
    g_pidSchedule[g_pidCount] = pid;
    g_pidCount++;
    appended = true;
  }
  portEXIT_CRITICAL(&g_pidScheduleMux);

  return appended;
}

bool removePidFromSchedule(uint8_t pid) {
  bool removed = false;

  portENTER_CRITICAL(&g_pidScheduleMux);
  for (size_t i = 0; i < g_pidCount; i++) {
    if (g_pidSchedule[i] == pid) {
      for (size_t j = i; j + 1 < g_pidCount; j++) {
        g_pidSchedule[j] = g_pidSchedule[j + 1];
      }
      g_pidCount--;
      if (g_pidCount == 0) {
        g_pidIndex = 0;
      } else {
        g_pidIndex %= g_pidCount;
      }
      removed = true;
      break;
    }
  }
  portEXIT_CRITICAL(&g_pidScheduleMux);

  return removed;
}

bool getNextScheduledPid(uint8_t* pidOut) {
  if (pidOut == NULL) {
    return false;
  }

  bool ok = false;

  portENTER_CRITICAL(&g_pidScheduleMux);
  if (g_pidCount > 0) {
    *pidOut = g_pidSchedule[g_pidIndex];
    g_pidIndex = (g_pidIndex + 1) % g_pidCount;
    ok = true;
  }
  portEXIT_CRITICAL(&g_pidScheduleMux);

  return ok;
}

size_t getPidScheduleCount() {
  size_t count = 0;
  portENTER_CRITICAL(&g_pidScheduleMux);
  count = g_pidCount;
  portEXIT_CRITICAL(&g_pidScheduleMux);
  return count;
}

bool updatePidMetricValueFromRaw(uint8_t pid, uint16_t raw) {
  bool updated = false;
  portENTER_CRITICAL(&g_pidScheduleMux);
  const int cfgIdx = findConfigIndexUnlocked(pid);
  if (cfgIdx >= 0) {
    g_values[cfgIdx].pid = pid;
    g_values[cfgIdx].value = applyFormula(g_configs[cfgIdx], raw);
    g_values[cfgIdx].valid = true;
    g_values[cfgIdx].updatedAtMs = millis();
    updated = true;
  }
  portEXIT_CRITICAL(&g_pidScheduleMux);
  return updated;
}

bool getPidMetricValue(uint8_t pid, float* valueOut, bool* validOut) {
  if (valueOut == NULL) {
    return false;
  }

  bool found = false;
  float value = 0.0f;
  bool valid = false;

  portENTER_CRITICAL(&g_pidScheduleMux);
  const int idx = findValueIndexUnlocked(pid);
  if (idx >= 0) {
    value = g_values[idx].value;
    valid = g_values[idx].valid;
    found = true;
  }
  portEXIT_CRITICAL(&g_pidScheduleMux);

  if (found) {
    *valueOut = value;
    if (validOut != NULL) {
      *validOut = valid;
    }
  }

  return found;
}

bool getPidMetricConfig(uint8_t pid, PidMetricConfig* configOut) {
  if (configOut == NULL) {
    return false;
  }

  bool found = false;
  portENTER_CRITICAL(&g_pidScheduleMux);
  const int idx = findConfigIndexUnlocked(pid);
  if (idx >= 0) {
    *configOut = g_configs[idx];
    found = true;
  }
  portEXIT_CRITICAL(&g_pidScheduleMux);

  return found;
}
