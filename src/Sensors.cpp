#include "Sensors.h"
#include "Display/QMI8658.h"

// Analog sensor inputs (GPIO17 now, GPIO18 reserved for later)
static constexpr uint8_t kAnalogBoostPin = 18;
static constexpr uint8_t kAnalogSparePin = 17;
static constexpr float kAdcReferenceVolts = 3.3f;
static constexpr float kAdcDividerCompensation = 2.0f; // 50/50 divider -> actual sensor voltage is 2x ADC input

static volatile float gBoostSensorVoltage = 0.0f;
static volatile float gBoostPressure = 0.0f;

// IMU Constants
static constexpr float kOneGMs2 = 9.807f;
static constexpr float kImuFilterAlpha = 0.25f;

static constexpr int kLongitudinalAxis = 2;
static constexpr int kLateralAxis = 1;
static constexpr float kLongitudinalSign = 1.0f;
static constexpr float kLateralSign = 1.0f;

static volatile float gLongitudinalG = 0.0f;
static volatile float gLateralG = 0.0f;
static volatile float gLongitudinalOffsetG = 0.0f;
static volatile float gLateralOffsetG = 0.0f;
static volatile bool gImuReady = false;

static GForcePeak gGforcePeakBuffer[kGforcePeakBufferSize] = {};
static size_t gGforcePeakIndex = 0;
static uint32_t gLastPeakSampleMs = 0;
static constexpr uint32_t kGforcePeakSampleIntervalMs = 100; // Sample every 100ms
static constexpr uint32_t kGforceTrailWindowMs = 5000; // Keep 5 second history

void initAnalogInputs() {
  analogReadResolution(12);
  analogSetPinAttenuation(kAnalogBoostPin, ADC_11db); // >2.5V range at ADC pin
  analogSetPinAttenuation(kAnalogSparePin, ADC_11db); // reserve pin 18 for future sensor

  pinMode(kAnalogBoostPin, INPUT);
  pinMode(kAnalogSparePin, INPUT);

  Serial.println("[ANALOG] ADC initialized on GPIO17/GPIO18");
}

void updateAnalogSensors() {
  const int raw = analogRead(kAnalogBoostPin);
  const float adcInputVolts = (raw / 4095.0f) * kAdcReferenceVolts;
  const float sensorVolts = adcInputVolts * kAdcDividerCompensation;

  gBoostSensorVoltage = sensorVolts;

  const float kAtmosphereVolts = 0.91875f;
  float relativeVolts = sensorVolts - kAtmosphereVolts;

  float gaugeInHg = relativeVolts / 0.0325f;

  if (gaugeInHg < 0.0f) {
    gBoostPressure = gaugeInHg; 
  } else {
    gBoostPressure = gaugeInHg / 2.03602f; 
  }
}

void calibrateImuZero() {
  if (!gImuReady) {
    return;
  }

  constexpr uint32_t kSettleDelayMs = 400;
  constexpr int kSamples = 160;

  delay(kSettleDelayMs);

  float longitudinalSum = 0.0f;
  float lateralSum = 0.0f;
  for (int i = 0; i < kSamples; i++) {
    float accMs2[3] = {0.0f, 0.0f, 0.0f};
    QMI8658_read_acc_xyz(accMs2);

    longitudinalSum += (accMs2[kLongitudinalAxis] / kOneGMs2) * kLongitudinalSign;
    lateralSum += (accMs2[kLateralAxis] / kOneGMs2) * kLateralSign;
    delay(4);
  }

  gLongitudinalOffsetG = longitudinalSum / (float)kSamples;
  gLateralOffsetG = lateralSum / (float)kSamples;
  gLongitudinalG = 0.0f;
  gLateralG = 0.0f;

  Serial.printf("[IMU] Zero calibrated lat=%0.3f long=%0.3f\n",
                (double)gLateralOffsetG,
                (double)gLongitudinalOffsetG);
}

void initImuSensor() {
  gImuReady = (QMI8658_init() != 0);
  if (gImuReady) {
    Serial.println("[IMU] QMI8658 initialized");
    calibrateImuZero();
  } else {
    Serial.println("[IMU] QMI8658 init failed");
  }
}

void updateImuSensors() {
  if (!gImuReady) {
    return;
  }

  float accMs2[3] = {0.0f, 0.0f, 0.0f};
  QMI8658_read_acc_xyz(accMs2);

  const float longitudinalRawG = ((accMs2[kLongitudinalAxis] / kOneGMs2) * kLongitudinalSign) - gLongitudinalOffsetG;
  const float lateralRawG = ((accMs2[kLateralAxis] / kOneGMs2) * kLateralSign) - gLateralOffsetG;

  gLongitudinalG += kImuFilterAlpha * (longitudinalRawG - gLongitudinalG);
  gLateralG += kImuFilterAlpha * (lateralRawG - gLateralG);

  const uint32_t now = millis();
  if ((now - gLastPeakSampleMs) >= kGforcePeakSampleIntervalMs) {
    gLastPeakSampleMs = now;
    GForcePeak& peak = gGforcePeakBuffer[gGforcePeakIndex];
    peak.lateralG = gLateralG;
    peak.longitudinalG = gLongitudinalG;
    peak.timestampMs = now;
    gGforcePeakIndex = (gGforcePeakIndex + 1) % kGforcePeakBufferSize;
  }
}

float getBoostPressure() { return gBoostPressure; }
float getBoostSensorVoltage() { return gBoostSensorVoltage; }
bool isImuReady() { return gImuReady; }
float getLateralG() { return gLateralG; }
float getLongitudinalG() { return gLongitudinalG; }
const GForcePeak* getGforcePeakBuffer() { return gGforcePeakBuffer; }
