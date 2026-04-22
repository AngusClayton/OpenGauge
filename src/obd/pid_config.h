#ifndef PID_CONFIG_H
#define PID_CONFIG_H

#include <Arduino.h>
#include "obd.h"

// ============= OBD PID DEFINITIONS =============
// Standard OBD2 PIDs

#define PID_COOLANT_TEMP       0x05   // Engine coolant temperature
#define PID_ENGINE_RPM         0x0C   // Engine RPM
#define PID_VEHICLE_SPEED      0x0D   // Vehicle speed
#define PID_INTAKE_AIR_TEMP    0x0F   // Intake air temperature
#define PID_INTAKE_PRESSURE    0x0B   // Intake manifold absolute pressure
#define PID_ENGINE_LOAD        0x04   // Calculated engine load
#define PID_FUEL_PRESSURE      0x0A   // Fuel pressure
#define PID_FUEL_LEVEL         0x2F   // Fuel tank level input
#define PID_IGNITION_TIMING    0x0E   // Ignition timing advance
#define PID_MAF_AIRFLOW        0x10   // Mass airflow sensor
#define PID_THROTTLE_POS       0x11   // Throttle position
#define PID_O2_VOLTAGE         0x14   // O2 sensor voltage
#define PID_O2_SENSOR1_LAMBDA  0x24   // Wideband O2 sensor 1 equivalence ratio
#define PID_ETHANOL_FUEL       0x52   // Ethanol fuel percentage

// ============= COMPUTED VALUES STRUCT =============
/**
 * Structure to hold all computed OBD values
 * Stores human-readable values after applying math to raw OBD data
 */
struct OBDValues {
  float coolant_temp_c;      // Coolant temperature (°C)
  float coolant_temp_f;      // Coolant temperature (°F)
  float intake_air_temp_c;   // Intake air temperature (°C)
  float intake_air_temp_f;   // Intake air temperature (°F)
  uint16_t rpm;              // Engine RPM
  uint8_t vehicle_speed_kmh; // Vehicle speed (km/h)
  uint8_t vehicle_speed_mph; // Vehicle speed (mph)
  uint8_t engine_load;       // Engine load (%)
  float fuel_pressure_bar;   // Fuel pressure (bar)
  uint8_t fuel_level;        // Fuel tank level (%)
  float intake_pressure_kpa; // Intake pressure (kPa)
  float ignition_timing;     // Ignition timing (degrees BTDC)
  float maf_airflow;         // Mass airflow (g/s)
  uint8_t throttle_pos;      // Throttle position (%)
  float o2_voltage;          // O2 sensor voltage (V)
  float lambda_ratio;        // Wideband lambda ratio
  float afr_gasoline;        // Air/fuel ratio referenced to gasoline stoich
  uint8_t ethanol_percent;   // Ethanol fuel (%)
};

// Global OBD values
extern OBDValues obdValues;

// ============= MATH CONVERSION FUNCTIONS =============

/**
 * Convert raw coolant temp to Celsius
 * Formula: A - 40 = °C
 */
inline float computeCoolantTemp(uint8_t rawA) {
  return rawA - 40.0f;
}

/**
 * Convert Celsius to Fahrenheit
 */
inline float celsiusToFahrenheit(float celsius) {
  return (celsius * 9.0f / 5.0f) + 32.0f;
}

/**
 * Convert raw RPM to actual RPM
 * Formula: ((A*256)+B)/4 = RPM
 */
inline uint16_t computeRPM(uint8_t rawA, uint8_t rawB) {
  return ((rawA * 256) + rawB) / 4;
}

/**
 * Convert raw speed to km/h
 * Formula: A = km/h
 */
inline uint8_t computeSpeedKMH(uint8_t rawA) {
  return rawA;
}

/**
 * Convert km/h to mph
 */
inline uint8_t kmhToMph(uint8_t kmh) {
  return kmh * 0.621371f;
}

/**
 * Convert raw engine load to percentage
 * Formula: (A/2.55) = %
 */
inline uint8_t computeEngineLoad(uint8_t rawA) {
  return rawA / 2.55f;
}

/**
 * Convert raw fuel pressure to bar
 * Formula: A * 0.079 = bar (or A * 3 = psi)
 */
inline float computeFuelPressureBar(uint8_t rawA) {
  return rawA * 0.079f;
}

/**
 * Convert raw fuel level to percentage
 * Formula: (A/2.55) = %
 */
inline uint8_t computeFuelLevel(uint8_t rawA) {
  return rawA / 2.55f;
}

/**
 * Convert raw intake pressure to kPa
 * Formula: A = kPa
 */
inline float computeIntakePressure(uint8_t rawA) {
  return (float)rawA;
}

/**
 * Convert raw ignition timing to degrees
 * Formula: (A/2) - 64 = degrees BTDC
 */
inline float computeIgnitionTiming(uint8_t rawA) {
  return (rawA / 2.0f) - 64.0f;
}

/**
 * Convert raw MAF to g/s
 * Formula: ((A*256)+B)/100 = g/s
 */
inline float computeMAFAirflow(uint8_t rawA, uint8_t rawB) {
  return ((rawA * 256) + rawB) / 100.0f;
}

/**
 * Convert raw throttle position to percentage
 * Formula: (A/2.55) = %
 */
inline uint8_t computeThrottlePos(uint8_t rawA) {
  return rawA / 2.55f;
}

/**
 * Convert raw O2 voltage to volts
 * Formula: A/200 = Volts
 */
inline float computeO2Voltage(uint8_t rawA) {
  return rawA / 200.0f;
}

/**
 * Convert raw wideband lambda bytes to equivalence ratio.
 * Formula: ((A*256)+B)/32768 = lambda
 */
inline float computeLambdaRatio(uint8_t rawA, uint8_t rawB) {
  return ((rawA * 256) + rawB) / 32768.0f;
}

/**
 * Convert lambda ratio to gasoline-referenced AFR.
 */
inline float computeGasolineAFR(float lambdaRatio) {
  return lambdaRatio * 14.7f;
}

/**
 * Get ethanol fuel percentage
 * Formula: A = %
 */
inline uint8_t computeEthanolPercent(uint8_t rawA) {
  return rawA;
}

// ============= UPDATE FUNCTION =============

/**
 * Compute all OBD values from cached raw OBD bytes.
 * Does not send CAN requests.
 */
void computeOBDValuesFromCache();

/**
 * Backward-compatible wrapper.
 * Kept for call sites that still use the older name.
 */
void updateOBDValues();

/**
 * Print all OBD values to serial (for debugging)
 */
void printOBDValues();

#endif // PID_CONFIG_H
