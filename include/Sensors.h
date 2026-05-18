#pragma once

#include <Arduino.h>

// Initialize analog pins and ADC resolution
void initAnalogInputs();


// Initialize the QMI8658 IMU
void initImuSensor();

// Read from IMU and filter values
void updateImuSensors();

// Manually calibrate IMU offsets
void calibrateImuZero();

// Getters for IMU
bool isImuReady();
float getLateralG();
float getLongitudinalG();

// G-Force Peak Tracking
struct GForcePeak {
  float lateralG;
  float longitudinalG;
  uint32_t timestampMs;
};
constexpr size_t kGforcePeakBufferSize = 50;
const GForcePeak* getGforcePeakBuffer();
