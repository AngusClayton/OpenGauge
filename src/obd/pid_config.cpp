#include "pid_config.h"

// Global OBD values - initialized to zero
OBDValues obdValues = {
  .coolant_temp_c = 0,
  .coolant_temp_f = 0,
  .rpm = 0,
  .vehicle_speed_kmh = 0,
  .vehicle_speed_mph = 0,
  .engine_load = 0,
  .fuel_pressure_bar = 0,
  .fuel_level = 0,
  .intake_pressure_kpa = 0,
  .ignition_timing = 0,
  .maf_airflow = 0,
  .throttle_pos = 0,
  .o2_voltage = 0,
  .ethanol_percent = 0
};

void computeOBDValuesFromCache() {
  float coolant_raw = getOBDRawValue(PID_COOLANT_TEMP, 1);
  obdValues.coolant_temp_c = computeCoolantTemp((uint8_t)coolant_raw);
  obdValues.coolant_temp_f = celsiusToFahrenheit(obdValues.coolant_temp_c);

  uint16_t rpm_raw = (uint16_t)getOBDRawValue(PID_ENGINE_RPM, 2);
  uint8_t rpmA = (rpm_raw >> 8) & 0xFF;
  uint8_t rpmB = rpm_raw & 0xFF;
  obdValues.rpm = computeRPM(rpmA, rpmB);

  float speed_raw = getOBDRawValue(PID_VEHICLE_SPEED, 1);
  obdValues.vehicle_speed_kmh = computeSpeedKMH((uint8_t)speed_raw);
  obdValues.vehicle_speed_mph = kmhToMph(obdValues.vehicle_speed_kmh);

  float load_raw = getOBDRawValue(PID_ENGINE_LOAD, 1);
  obdValues.engine_load = computeEngineLoad((uint8_t)load_raw);

  float fuel_pressure_raw = getOBDRawValue(PID_FUEL_PRESSURE, 1);
  obdValues.fuel_pressure_bar = computeFuelPressureBar((uint8_t)fuel_pressure_raw);

  float fuel_level_raw = getOBDRawValue(PID_FUEL_LEVEL, 1);
  obdValues.fuel_level = computeFuelLevel((uint8_t)fuel_level_raw);

  float intake_raw = getOBDRawValue(PID_INTAKE_PRESSURE, 1);
  obdValues.intake_pressure_kpa = computeIntakePressure((uint8_t)intake_raw);

  float ignition_raw = getOBDRawValue(PID_IGNITION_TIMING, 1);
  obdValues.ignition_timing = computeIgnitionTiming((uint8_t)ignition_raw);

  uint16_t maf_raw = (uint16_t)getOBDRawValue(PID_MAF_AIRFLOW, 2);
  uint8_t mafA = (maf_raw >> 8) & 0xFF;
  uint8_t mafB = maf_raw & 0xFF;
  obdValues.maf_airflow = computeMAFAirflow(mafA, mafB);

  float throttle_raw = getOBDRawValue(PID_THROTTLE_POS, 1);
  obdValues.throttle_pos = computeThrottlePos((uint8_t)throttle_raw);

  float o2_raw = getOBDRawValue(PID_O2_VOLTAGE, 1);
  obdValues.o2_voltage = computeO2Voltage((uint8_t)o2_raw);

  float ethanol_raw = getOBDRawValue(PID_ETHANOL_FUEL, 1);
  obdValues.ethanol_percent = computeEthanolPercent((uint8_t)ethanol_raw);
}

void updateOBDValues() {
  computeOBDValuesFromCache();
}

void printOBDValues() {
  Serial.println("\n===== OBD VALUES =====");
  Serial.printf("Coolant Temp: %.1f C / %.1f F\n", obdValues.coolant_temp_c, obdValues.coolant_temp_f);
  Serial.printf("RPM: %d\n", obdValues.rpm);
  Serial.printf("Speed: %d km/h / %d mph\n", obdValues.vehicle_speed_kmh, obdValues.vehicle_speed_mph);
  Serial.printf("Engine Load: %d%%\n", obdValues.engine_load);
  Serial.printf("Fuel Pressure: %.2f bar\n", obdValues.fuel_pressure_bar);
  Serial.printf("Fuel Level: %d%%\n", obdValues.fuel_level);
  Serial.printf("Intake Pressure: %.1f kPa\n", obdValues.intake_pressure_kpa);
  Serial.printf("Ignition Timing: %.1f deg\n", obdValues.ignition_timing);
  Serial.printf("MAF Airflow: %.2f g/s\n", obdValues.maf_airflow);
  Serial.printf("Throttle: %d%%\n", obdValues.throttle_pos);
  Serial.printf("O2 Voltage: %.2f V\n", obdValues.o2_voltage);
  Serial.printf("Ethanol: %d%%\n", obdValues.ethanol_percent);
  Serial.println("=====================\n");
}
