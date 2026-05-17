#include <Arduino.h>
#include <math.h>
#include "Display/lcd_init.h"
#include "Display/GUI_Paint.h"
#include "Display/fonts.h"
#include "Display/LCD_1in28.h"
#include "Display/CST816S.h"
#include "Display/QMI8658.h"
#include "obd/obd.h"
#include "obd/pid_config.h"
#include "obd/pid_schedule.h"

// External display buffer
extern UWORD *BlackImage;
extern UDOUBLE Imagesize;

// Create touch object
CST816S touch(6, 7, 13, 5);  // sda, scl, rst, irq

TaskHandle_t obdTaskHandle = NULL;
TaskHandle_t displayTaskHandle = NULL;

// Analog sensor inputs (GPIO17 now, GPIO18 reserved for later)
static constexpr uint8_t kAnalogBoostPin = 18;
static constexpr uint8_t kAnalogSparePin = 17;
static constexpr float kAdcReferenceVolts = 3.3f;
static constexpr float kAdcDividerCompensation = 2.0f; // 50/50 divider -> actual sensor voltage is 2x ADC input
static constexpr float kOneGMs2 = 9.807f;
static constexpr float kImuFilterAlpha = 0.25f;
static constexpr float kGmeterDisplayRange = 1.5f;

// Axis mapping for car orientation.
// "Top" of the QMI8658 package points to the front of the car.
// Adjust signs if polarity appears inverted in vehicle testing.
static constexpr int kLongitudinalAxis = 2;
static constexpr int kLateralAxis = 1;
static constexpr float kLongitudinalSign = 1.0f;
static constexpr float kLateralSign = 1.0f;

static volatile float gBoostSensorVoltage = 0.0f;
static volatile float gBoostPressure = 0.0f;
static volatile float gHorsepowerEstimate = 0.0f;
static volatile float gLongitudinalG = 0.0f;
static volatile float gLateralG = 0.0f;
static volatile float gLongitudinalOffsetG = 0.0f;
static volatile float gLateralOffsetG = 0.0f;
static volatile bool gImuReady = false;

static constexpr float kGasolineStoichAfr = 14.7f;
static constexpr float kBoostDisplayMin = -10.0f;
static constexpr float kBoostDisplayMax = 25.0f;
static constexpr float kHorsepowerDisplayMin = 0.0f;
static constexpr float kHorsepowerDisplayMax = 300.0f;
static constexpr float kAfrDisplayMin = 10.0f;
static constexpr float kAfrDisplayMax = 20.0f;
static constexpr float kIgnitionTimingDisplayMin = -10.0f;
static constexpr float kIgnitionTimingDisplayMax = 40.0f;
static constexpr UWORD kColdWaterColor = BLUE;
static constexpr UWORD kNormalWaterColor = GBLUE;
static constexpr UWORD kHotWaterColor = RED;
static constexpr UWORD kShiftTrackColor = GRAY;
static constexpr UWORD kShiftOrangeColor = 0xFD20;

struct GaugeProfile {
  const char* title;
  const PidMetricConfig* metrics;
  size_t metricCount;
};

struct ShiftGearConfig {
  uint8_t gearNumber;
  float rpmPerKph;
  uint16_t targetShiftRpm;
};

static const PidMetricConfig kBoostGaugeMetrics[] = {
  {PID_COOLANT_TEMP, "Coolant Temp", PID_FORMULA_A_MINUS_40, 1.0f, 0.0f, 1, true},
  {PID_INTAKE_AIR_TEMP, "Intake Air Temp", PID_FORMULA_A_MINUS_40, 1.0f, 0.0f, 1, true},
};

static const PidMetricConfig kHorsepowerGaugeMetrics[] = {
  {PID_MAF_AIRFLOW, "MAF", PID_FORMULA_AB_DIV_100, 1.0f, 0.0f, 2, true},
};

static const PidMetricConfig kAfrGaugeMetrics[] = {
  {PID_O2_SENSOR1_LAMBDA, "Lambda", PID_FORMULA_LINEAR_AB, 1.0f / 32768.0f, 0.0f, 2, true}, // Now PID 0x34
};

static const PidMetricConfig kIgnitionGaugeMetrics[] = {
  {PID_IGNITION_TIMING, "Ign Timing", PID_FORMULA_LINEAR_A, 0.5f, -64.0f, 1, true},
};

static const PidMetricConfig kShiftLightGaugeMetrics[] = {
  {PID_ENGINE_RPM, "RPM", PID_FORMULA_AB_DIV_4, 1.0f, 0.0f, 2, true},
  {PID_VEHICLE_SPEED, "Speed", PID_FORMULA_RAW_A, 1.0f, 0.0f, 1, true},
};

static const PidMetricConfig kGmeterGaugeMetrics[] = {
  {PID_ENGINE_RPM, "RPM", PID_FORMULA_AB_DIV_4, 1.0f, 0.0f, 2, true},
  {PID_VEHICLE_SPEED, "Speed", PID_FORMULA_RAW_A, 1.0f, 0.0f, 1, true},
};

static const ShiftGearConfig kShiftGearTable[] = {
  {1, 115.4f, 6500},
  {2, 73.2f, 6300},
  {3, 49.2f, 6100},
  {4, 36.6f, 6000},
  {5, 28.6f, 5800},
  {6, 24.0f, 0},
};

static const GaugeProfile kProfiles[] = {
  {"Gauge 1: Boost", kBoostGaugeMetrics, sizeof(kBoostGaugeMetrics) / sizeof(kBoostGaugeMetrics[0])},
  {"Gauge 2: Horsepower", kHorsepowerGaugeMetrics, sizeof(kHorsepowerGaugeMetrics) / sizeof(kHorsepowerGaugeMetrics[0])},
  {"Gauge 3: Lambda / AFR", kAfrGaugeMetrics, sizeof(kAfrGaugeMetrics) / sizeof(kAfrGaugeMetrics[0])},
  {"Gauge 4: Ignition Timing", kIgnitionGaugeMetrics, sizeof(kIgnitionGaugeMetrics) / sizeof(kIgnitionGaugeMetrics[0])},
  {"Gauge 5: Shift Lights", kShiftLightGaugeMetrics, sizeof(kShiftLightGaugeMetrics) / sizeof(kShiftLightGaugeMetrics[0])},
  {"Gauge 6: G Meter", kGmeterGaugeMetrics, sizeof(kGmeterGaugeMetrics) / sizeof(kGmeterGaugeMetrics[0])},
};

static volatile size_t gCurrentProfileIndex = 0;
static uint32_t gLastSwipeMs = 0;
static uint32_t gStatusIssueSinceMs = 0;
static int gLastDetectedGear = 0;
static uint32_t gLastDetectedGearMs = 0;

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

  // Conversion: Output (V) = -0.0325 * Vacuum (inHg)
  // Vacuum (inHg) = -V / 0.0325
  // For positive pressure (boost), convert to PSI (1 PSI = 2.03602 inHg)
  float boostPsi = 0.0f;
  float boostInHg = 0.0f;
  const float kAtmospherePsi = 14.7f;
  if (sensorVolts < 0.0f) {
    // Should not happen, but clamp to zero
    gBoostPressure = 0.0f;
  } else if (sensorVolts < 0.01f) {
    // Treat as atmospheric (zero boost/vacuum)
    gBoostPressure = 0.0f;
  } else {
    float vacuumInHg = -sensorVolts / 0.0325f;
    if (vacuumInHg > 0.0f) {
      // Negative pressure (vacuum)
      gBoostPressure = vacuumInHg; // inHg (negative)
    } else {
      // Positive pressure (boost)
      boostPsi = ((-vacuumInHg) / 2.03602f) - kAtmospherePsi;
      if (boostPsi < 0.0f) boostPsi = 0.0f; // Clamp to zero below atmosphere
      gBoostPressure = boostPsi; // PSI (positive, gauge)
    }
  }
}

void calibrateImuZero() {
  if (!gImuReady) {
    return;
  }

  constexpr uint32_t kSettleDelayMs = 400;
  constexpr int kSamples = 160;

  // Let mounting vibrations settle before sampling zero offsets.
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
}

void updateDerivedValues() {
  gHorsepowerEstimate = obdValues.maf_airflow * 1.25f;
}

void updateStatusIssueTimer(bool statusIssue) {
  if (statusIssue) {
    if (gStatusIssueSinceMs == 0) {
      gStatusIssueSinceMs = millis();
    }
  } else {
    gStatusIssueSinceMs = 0;
  }
}

void applyGaugeProfile(size_t index) {
  const size_t profileCount = sizeof(kProfiles) / sizeof(kProfiles[0]);
  if (index >= profileCount) {
    return;
  }

  if (setPidMetricConfigList(kProfiles[index].metrics, kProfiles[index].metricCount)) {
    gCurrentProfileIndex = index;
    Serial.printf("[GAUGE] Switched to profile %u: %s (%u PIDs)\n",
                  (unsigned int)(index + 1),
                  kProfiles[index].title,
                  (unsigned int)getPidScheduleCount());
  } else {
    Serial.println("[GAUGE] Failed to apply gauge profile");
  }
}

float clampFloat(float value, float minValue, float maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

UWORD waterTempColor(float waterTempC) {
  if (waterTempC < 80.0f) {
    return kColdWaterColor;
  }
  if (waterTempC > 105.0f) {
    return kHotWaterColor;
  }
  return kNormalWaterColor;
}

void drawClockArc(int cx, int cy, int radius, int thickness, float startClockDeg, float sweepClockDeg, UWORD color) {
  const float degToRad = 3.14159265f / 180.0f;
  const int steps = (int)fabsf(sweepClockDeg);
  if (steps <= 0) {
    return;
  }

  const float stepDeg = sweepClockDeg / (float)steps;
  for (int i = 0; i <= steps; i++) {
    const float clockDeg = startClockDeg + (stepDeg * i);
    const float rad = clockDeg * degToRad;
    const float s = sinf(rad);
    const float c = cosf(rad);

    for (int t = 0; t < thickness; t++) {
      const int r = radius - t;
      const int x = cx + (int)(s * r);
      const int y = cy - (int)(c * r);
      Paint_DrawPoint((UWORD)x, (UWORD)y, color, DOT_PIXEL_2X2, DOT_FILL_AROUND);
    }
  }
}

// GUI_Paint swaps foreground/background in Paint_DrawString_EN internally.
// Use this wrapper so call sites can pass colors in intuitive order.
void drawTextFixed(UWORD x, UWORD y, const char* text, sFONT* font, UWORD fg, UWORD bg) {
  Paint_DrawString_EN(x, y, text, font, bg, fg);
}

int textWidthPx(const char* text, sFONT* font) {
  if (text == NULL || font == NULL) {
    return 0;
  }
  return (int)strlen(text) * (int)font->Width;
}

void drawCenteredTextFixed(UWORD y, const char* text, sFONT* font, UWORD fg, UWORD bg) {
  const int width = textWidthPx(text, font);
  int x = (240 - width) / 2;
  if (x < 0) {
    x = 0;
  }
  drawTextFixed((UWORD)x, y, text, font, fg, bg);
}

void drawStatusIfNeeded(UWORD y, UWORD color) {
  const OBDLinkStatus status = getOBDLinkStatus();
  const bool statusIssue = (status == OBD_STATUS_NO_BUS || status == OBD_STATUS_ERROR);
  updateStatusIssueTimer(statusIssue);
  if (!statusIssue || gStatusIssueSinceMs == 0 || (millis() - gStatusIssueSinceMs) < 5000) {
    return;
  }

  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%s", getOBDStatusText());
  drawCenteredTextFixed(y, buffer, &Font12, color, BLACK);
}

int determineCurrentGear(float currentRpm, float currentKph) {
  constexpr float kMinKph = 3.0f;
  constexpr float kMinRpm = 850.0f;
  constexpr float kRatioToleranceBase = 2.5f;
  constexpr float kKphQuantizationHalfStep = 0.5f;
  constexpr float kQuantizationToleranceCap = 18.0f;
  constexpr uint32_t kGearHoldMs = 350;

  if (currentKph < kMinKph || currentRpm < kMinRpm) {
    gLastDetectedGear = 0;
    return 0;
  }

  const float currentRatio = currentRpm / currentKph;
  const float quantizationTolerance = (currentRpm * kKphQuantizationHalfStep) / (currentKph * currentKph);
  const float dynamicTolerance = kRatioToleranceBase + clampFloat(quantizationTolerance, 0.0f, kQuantizationToleranceCap);

  const size_t gearCount = sizeof(kShiftGearTable) / sizeof(kShiftGearTable[0]);
  int bestGear = -1;
  float bestDiff = 1e9f;
  for (size_t i = 0; i < gearCount; i++) {
    const float diff = fabsf(currentRatio - kShiftGearTable[i].rpmPerKph);
    if (diff <= dynamicTolerance && diff < bestDiff) {
      bestDiff = diff;
      bestGear = (int)kShiftGearTable[i].gearNumber;
    }
  }

  if (bestGear > 0) {
    gLastDetectedGear = bestGear;
    gLastDetectedGearMs = millis();
    return bestGear;
  }

  if (gLastDetectedGear > 0 && (millis() - gLastDetectedGearMs) <= kGearHoldMs) {
    return gLastDetectedGear;
  }

  return -1;
}

uint16_t getTargetShiftRpmForGear(int gear) {
  const size_t gearCount = sizeof(kShiftGearTable) / sizeof(kShiftGearTable[0]);
  for (size_t i = 0; i < gearCount; i++) {
    if ((int)kShiftGearTable[i].gearNumber == gear) {
      return kShiftGearTable[i].targetShiftRpm;
    }
  }
  return 0;
}

UWORD shiftLightColor(float rpm, uint16_t targetShiftRpm) {
  if (targetShiftRpm == 0) {
    return WHITE;
  }

  const float redThreshold = (float)targetShiftRpm;
  const float orangeThreshold = redThreshold * 0.96f;
  const float yellowThreshold = redThreshold * 0.90f;
  if (rpm >= redThreshold) {
    return RED;
  }
  if (rpm >= orangeThreshold) {
    return kShiftOrangeColor;
  }
  if (rpm >= yellowThreshold) {
    return YELLOW;
  }
  return WHITE;
}

float shiftArcSweep(float rpm, uint16_t targetShiftRpm) {
  if (targetShiftRpm == 0) {
    return 0.0f;
  }

  const float normalized = clampFloat(rpm / (float)targetShiftRpm, 0.0f, 1.0f);
  return 240.0f * normalized;
}

void renderBoostGauge() {
  Paint_Clear(BLACK);

  const int cx = 120;
  const int cy = 120;
  const int arcRadius = 112;
  const int arcThickness = 14;

  // 8 o'clock -> 4 o'clock sweep (clockwise 240 degrees)
  const float arcStartClockDeg = 240.0f;
  const float arcSweepClockDeg = 240.0f;

  const float boost = gBoostPressure;
  const float normalized = (clampFloat(boost, kBoostDisplayMin, kBoostDisplayMax) - kBoostDisplayMin) /
                           (kBoostDisplayMax - kBoostDisplayMin);
  const float filledSweep = arcSweepClockDeg * normalized;

  // Filled value arc only (no gray track), rendered in white.
  if (filledSweep > 0.5f) {
    drawClockArc(cx, cy, arcRadius, arcThickness, arcStartClockDeg, filledSweep, WHITE);
  }

  char buffer[64];

  // Center boost value in large white text, with units
  if (boost < 0.0f) {
    snprintf(buffer, sizeof(buffer), "%.1f inHg", (double)boost);
  } else {
    snprintf(buffer, sizeof(buffer), "%.1f PSI", (double)boost);
  }
  drawCenteredTextFixed(84, buffer, &Font24, WHITE, BLACK);
  drawCenteredTextFixed(112, "BOOST", &Font12, WHITE, BLACK);

  // Secondary data in blue on separate, larger lines.
  const UWORD waterColor = waterTempColor(obdValues.coolant_temp_c);
  snprintf(buffer, sizeof(buffer), "Water: %.0fC", (double)obdValues.coolant_temp_c);
  drawCenteredTextFixed(146, buffer, &Font16, waterColor, BLACK);

  snprintf(buffer, sizeof(buffer), "AIT: %.0fC", (double)obdValues.intake_air_temp_c);
  drawCenteredTextFixed(168, buffer, &Font16, GBLUE, BLACK);

  drawStatusIfNeeded(194, GRAY);

  LCD_1IN28_Display(BlackImage);
}

void renderHorsepowerGauge() {
  Paint_Clear(BLACK);

  const int cx = 120;
  const int cy = 120;
  const int arcRadius = 112;
  const int arcThickness = 14;
  const float arcStartClockDeg = 240.0f;
  const float arcSweepClockDeg = 240.0f;

  const float hp = gHorsepowerEstimate;
  const float normalized = (clampFloat(hp, kHorsepowerDisplayMin, kHorsepowerDisplayMax) - kHorsepowerDisplayMin) /
                           (kHorsepowerDisplayMax - kHorsepowerDisplayMin);
  const float filledSweep = arcSweepClockDeg * normalized;

  if (filledSweep > 0.5f) {
    drawClockArc(cx, cy, arcRadius, arcThickness, arcStartClockDeg, filledSweep, WHITE);
  }

  char buffer[64];

  snprintf(buffer, sizeof(buffer), "%.0f", (double)hp);
  drawCenteredTextFixed(84, buffer, &Font24, WHITE, BLACK);
  drawCenteredTextFixed(112, "HP", &Font16, WHITE, BLACK);
  drawCenteredTextFixed(146, "MAF x 1.25", &Font12, GBLUE, BLACK);
  snprintf(buffer, sizeof(buffer), "MAF: %.1f g/s", (double)obdValues.maf_airflow);
  drawCenteredTextFixed(168, buffer, &Font16, GBLUE, BLACK);

  drawStatusIfNeeded(194, GRAY);

  LCD_1IN28_Display(BlackImage);
}

void renderAfrGauge() {
  Paint_Clear(BLACK);

  const int cx = 120;
  const int cy = 120;
  const int arcRadius = 112;
  const int arcThickness = 14;
  const float arcStartClockDeg = 240.0f;
  const float arcSweepClockDeg = 240.0f;

  const float afr = obdValues.afr_gasoline;
  const float normalized = (clampFloat(afr, kAfrDisplayMin, kAfrDisplayMax) - kAfrDisplayMin) /
                           (kAfrDisplayMax - kAfrDisplayMin);
  const float filledSweep = arcSweepClockDeg * normalized;

  if (filledSweep > 0.5f) {
    drawClockArc(cx, cy, arcRadius, arcThickness, arcStartClockDeg, filledSweep, WHITE);
  }

  char buffer[64];
  snprintf(buffer, sizeof(buffer), "%.2f", (double)afr);
  drawCenteredTextFixed(80, buffer, &Font24, WHITE, BLACK);
  drawCenteredTextFixed(108, "AFR", &Font16, WHITE, BLACK);

  snprintf(buffer, sizeof(buffer), "Lambda: %.3f", (double)obdValues.lambda_ratio);
  drawCenteredTextFixed(144, buffer, &Font16, GBLUE, BLACK);

  snprintf(buffer, sizeof(buffer), "Stoich: %.1f", (double)kGasolineStoichAfr);
  drawCenteredTextFixed(168, buffer, &Font12, GRAY, BLACK);

  drawStatusIfNeeded(194, GRAY);

  LCD_1IN28_Display(BlackImage);
}

void renderIgnitionGauge() {
  Paint_Clear(BLACK);

  const int cx = 120;
  const int cy = 120;
  const int arcRadius = 112;
  const int arcThickness = 14;
  const float arcStartClockDeg = 240.0f;
  const float arcSweepClockDeg = 240.0f;

  const float timing = obdValues.ignition_timing;
  const float normalized = (clampFloat(timing, kIgnitionTimingDisplayMin, kIgnitionTimingDisplayMax) -
                            kIgnitionTimingDisplayMin) /
                           (kIgnitionTimingDisplayMax - kIgnitionTimingDisplayMin);
  const float filledSweep = arcSweepClockDeg * normalized;

  if (filledSweep > 0.5f) {
    drawClockArc(cx, cy, arcRadius, arcThickness, arcStartClockDeg, filledSweep, WHITE);
  }

  char buffer[64];
  snprintf(buffer, sizeof(buffer), "%.1f", (double)timing);
  drawCenteredTextFixed(80, buffer, &Font24, WHITE, BLACK);
  drawCenteredTextFixed(108, "IGN DEG", &Font16, WHITE, BLACK);

  snprintf(buffer, sizeof(buffer), "RPM: %u", (unsigned int)obdValues.rpm);
  drawCenteredTextFixed(144, buffer, &Font16, GBLUE, BLACK);

  drawCenteredTextFixed(168, "BTDC", &Font12, GRAY, BLACK);

  drawStatusIfNeeded(194, GRAY);

  LCD_1IN28_Display(BlackImage);
}

void renderShiftLightGauge() {
  Paint_Clear(BLACK);

  const int cx = 120;
  const int cy = 120;
  const int arcRadius = 112;
  const int arcThickness = 14;
  const float arcStartClockDeg = 240.0f;
  const float rpm = (float)obdValues.rpm;
  const float speedKph = (float)obdValues.vehicle_speed_kmh;
  const int gear = determineCurrentGear(rpm, speedKph);
  const uint16_t targetShiftRpm = getTargetShiftRpmForGear(gear);
  const float filledSweep = shiftArcSweep(rpm, targetShiftRpm);
  const UWORD arcColor = shiftLightColor(rpm, targetShiftRpm);

  drawClockArc(cx, cy, arcRadius, arcThickness, arcStartClockDeg, 240.0f, kShiftTrackColor);

  if (filledSweep > 0.5f) {
    drawClockArc(cx, cy, arcRadius, arcThickness, arcStartClockDeg, filledSweep, arcColor);
  }

  char buffer[64];
  if (gear > 0) {
    snprintf(buffer, sizeof(buffer), "%d", gear);
  } else if (gear == 0) {
    snprintf(buffer, sizeof(buffer), "N");
  } else {
    snprintf(buffer, sizeof(buffer), "-");
  }
  drawCenteredTextFixed(72, buffer, &Font24, WHITE, BLACK);
  drawCenteredTextFixed(102, "GEAR", &Font12, GRAY, BLACK);

  snprintf(buffer, sizeof(buffer), "RPM: %u", (unsigned int)obdValues.rpm);
  drawCenteredTextFixed(136, buffer, &Font16, arcColor, BLACK);

  snprintf(buffer, sizeof(buffer), "Speed: %u km/h", (unsigned int)obdValues.vehicle_speed_kmh);
  drawCenteredTextFixed(160, buffer, &Font16, GBLUE, BLACK);

  if (gear > 0 && targetShiftRpm > 0) {
    snprintf(buffer, sizeof(buffer), "Shift @ %u", (unsigned int)targetShiftRpm);
    drawCenteredTextFixed(184, buffer, &Font12, arcColor, BLACK);
  } else if (gear == 6) {
    drawCenteredTextFixed(184, "Top Gear", &Font12, GRAY, BLACK);
  } else if (gear == 0) {
    drawCenteredTextFixed(184, "Neutral / Clutch", &Font12, GRAY, BLACK);
  } else {
    drawCenteredTextFixed(184, "Gear Detecting", &Font12, GRAY, BLACK);
  }

  drawStatusIfNeeded(206, GRAY);

  LCD_1IN28_Display(BlackImage);
}

void renderGmeterGauge() {
  Paint_Clear(BLACK);

  const int cx = 120;
  const int cy = 120;
  const int radius = 72;
  const int dotRadius = 7;
  const int maxDotTravel = radius - dotRadius - 2;

  Paint_DrawCircle((UWORD)cx, (UWORD)cy, (UWORD)radius, GRAY, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
  Paint_DrawCircle((UWORD)cx, (UWORD)cy, (UWORD)(radius / 2), GRAY, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
  Paint_DrawLine((UWORD)(cx - radius), (UWORD)cy, (UWORD)(cx + radius), (UWORD)cy, GRAY, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
  Paint_DrawLine((UWORD)cx, (UWORD)(cy - radius), (UWORD)cx, (UWORD)(cy + radius), GRAY, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
  Paint_DrawCircle((UWORD)cx, (UWORD)cy, 2, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);

  if (!gImuReady) {
    drawCenteredTextFixed(100, "IMU OFFLINE", &Font16, RED, BLACK);
    drawCenteredTextFixed(184, "Check QMI8658 wiring", &Font12, GRAY, BLACK);
    LCD_1IN28_Display(BlackImage);
    return;
  }

  const float lateral = clampFloat(gLateralG, -kGmeterDisplayRange, kGmeterDisplayRange);
  const float longitudinal = clampFloat(gLongitudinalG, -kGmeterDisplayRange, kGmeterDisplayRange);

  const int dotX = cx + (int)((lateral / kGmeterDisplayRange) * (float)maxDotTravel);
  const int dotY = cy - (int)((longitudinal / kGmeterDisplayRange) * (float)maxDotTravel);
  Paint_DrawCircle((UWORD)dotX, (UWORD)dotY, (UWORD)dotRadius, RED, DOT_PIXEL_1X1, DRAW_FILL_FULL);

  char buffer[64];
  snprintf(buffer, sizeof(buffer), "Lat: %+0.2fg", (double)gLateralG);
  drawCenteredTextFixed(198, buffer, &Font12, GBLUE, BLACK);

  snprintf(buffer, sizeof(buffer), "Long: %+0.2fg", (double)gLongitudinalG);
  drawCenteredTextFixed(212, buffer, &Font12, WHITE, BLACK);

  snprintf(buffer, sizeof(buffer), "Forward ^");
  drawCenteredTextFixed(226, buffer, &Font12, GRAY, BLACK);

  drawStatusIfNeeded(16, GRAY);

  LCD_1IN28_Display(BlackImage);
}

void renderDisplay() {
  switch (gCurrentProfileIndex) {
    case 0:
      renderBoostGauge();
      return;
    case 1:
      renderHorsepowerGauge();
      return;
    case 2:
      renderAfrGauge();
      return;
    case 3:
      renderIgnitionGauge();
      return;
    case 4:
      renderShiftLightGauge();
      return;
    case 5:
      renderGmeterGauge();
      return;
    default:
      renderBoostGauge();
      return;
  }
}

void obdTask(void *pvParameters) {
  const TickType_t xLoopDelay = pdMS_TO_TICKS(10);
  const uint32_t requestIntervalMs = 50;
  const uint32_t analogIntervalMs = 20;
  uint32_t lastRequestMs = 0;
  uint32_t lastAnalogMs = 0;
  uint32_t lastStatusLogMs = 0;
  uint8_t nextPid = 0;

  Serial.println("[OBD] Task started");
  vTaskDelay(pdMS_TO_TICKS(500));

  while (1) {
    const uint32_t now = millis();

    if (!isOBDReady()) {
      if (!tryRecoverOBD() && (now - lastStatusLogMs) >= 1000) {
        Serial.println("[OBD] Waiting for CAN ready...");
        lastStatusLogMs = now;
      }
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    // Drain received frames first so cache remains fresh.
    for (int i = 0; i < 3; i++) {
      processOBDFrame();
    }

    // Send one PID request per interval (mirrors the original single-request cadence).
    if ((now - lastRequestMs) >= requestIntervalMs) {
      if (getNextScheduledPid(&nextPid)) {
        sendObdFrame(nextPid);
      }
      lastRequestMs = now;
    }

    if ((now - lastAnalogMs) >= analogIntervalMs) {
      updateAnalogSensors();
      updateImuSensors();
      lastAnalogMs = now;
    }

    // Compute display values from whatever is currently cached.
    computeOBDValuesFromCache();
    updateDerivedValues();

    vTaskDelay(xLoopDelay);
  }
}

void displayTask(void *pvParameters) {
  const TickType_t xDelay = pdMS_TO_TICKS(100);
  uint32_t loopCount = 0;

  Serial.println("[DISPLAY] Task started");

  while (1) {
    if (touch.available()) {
      const uint32_t now = millis();
      if ((now - gLastSwipeMs) > 250) {
        if (touch.data.gestureID == SWIPE_LEFT) {
          const size_t profileCount = sizeof(kProfiles) / sizeof(kProfiles[0]);
          const size_t next = (gCurrentProfileIndex + 1) % profileCount;
          applyGaugeProfile(next);
          gLastSwipeMs = now;
        } else if (touch.data.gestureID == SWIPE_RIGHT) {
          const size_t profileCount = sizeof(kProfiles) / sizeof(kProfiles[0]);
          const size_t next = (gCurrentProfileIndex + profileCount - 1) % profileCount;
          applyGaugeProfile(next);
          gLastSwipeMs = now;
        }
      }
    }

    renderDisplay();

    loopCount++;
    if ((loopCount % 50) == 0) {
      UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);
      Serial.printf("[DISPLAY] Stack watermark: %u words\n", (unsigned int)watermark);
    }

    vTaskDelay(xDelay);
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n===== 32 GAUGE INITIALIZATION =====");

  // Initialize OBD/CAN
  setupOBD();
  Serial.println("OBD initialized");

  // Seed defaults and apply first gauge profile.
  initPidScheduleDefaults();
  applyGaugeProfile(0);

  initAnalogInputs();
  updateAnalogSensors();

  // Initialize LCD and display buffer after OBD, matching the original project more closely.
  initLCD();
  Serial.println("LCD initialized");

  initImuSensor();

  renderDisplay();

  xTaskCreate(
    obdTask,
    "OBD",
    4096,
    NULL,
    3,
    &obdTaskHandle
  );

  xTaskCreate(
    displayTask,
    "DISPLAY",
    6144,
    NULL,
    2,
    &displayTaskHandle
  );

  Serial.println("=== Setup Complete ===\n");
}

void loop() {
  vTaskDelete(NULL);
}