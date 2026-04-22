#ifndef PID_SCHEDULE_H
#define PID_SCHEDULE_H

#include <Arduino.h>

enum PidFormulaType : uint8_t {
	PID_FORMULA_RAW_A = 0,
	PID_FORMULA_RAW_AB,
	PID_FORMULA_A_MINUS_40,
	PID_FORMULA_AB_DIV_4,
	PID_FORMULA_AB_DIV_100,
	PID_FORMULA_A_DIV_2_55,
	PID_FORMULA_LINEAR_A,
	PID_FORMULA_LINEAR_AB,
};

struct PidMetricConfig {
	uint8_t pid;
	char name[32];
	PidFormulaType formula;
	float scale;
	float offset;
	uint8_t bytes;
	bool enabled;
};

struct PidMetricValue {
	uint8_t pid;
	float value;
	bool valid;
	uint32_t updatedAtMs;
};

// Initialize the scheduler with the default PID list.
void initPidScheduleDefaults();

// Replace the active PID list at runtime.
// Duplicate PIDs are automatically removed while preserving first-seen order.
bool setPidSchedule(const uint8_t* pids, size_t count);

// Replace the full dynamic metric list (PID+Name+Formula), then rebuild schedule.
bool setPidMetricConfigList(const PidMetricConfig* configs, size_t count);

// cJSON-backed runtime config load/save.
bool loadPidMetricConfigFromJson(const char* jsonText);
bool exportPidMetricConfigToJson(String& jsonOut);

// Append/remove a single PID at runtime.
bool appendPidToSchedule(uint8_t pid);
bool removePidFromSchedule(uint8_t pid);

// Round-robin accessor for the OBD polling task.
bool getNextScheduledPid(uint8_t* pidOut);

// Current number of active PIDs.
size_t getPidScheduleCount();

// Update/get actual computed value cache by PID.
bool updatePidMetricValueFromRaw(uint8_t pid, uint16_t raw);
bool getPidMetricValue(uint8_t pid, float* valueOut, bool* validOut = NULL);

// Query metric config by PID.
bool getPidMetricConfig(uint8_t pid, PidMetricConfig* configOut);

#endif // PID_SCHEDULE_H
