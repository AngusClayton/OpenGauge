#include "GaugeRenderer.h"
#include "Sensors.h"
#include "obd/obd.h"
#include "obd/pid_config.h"
#include "obd/pid_schedule.h"
#include "Display/GUI_Paint.h"
#include "Display/fonts.h"
#include "Display/Font_nokia.h"
#include "Display/LCD_1in28.h"
#include <math.h>

extern UWORD *BlackImage;

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
static uint32_t gStatusIssueSinceMs = 0;
static int gLastDetectedGear = 0;
static uint32_t gLastDetectedGearMs = 0;

void initGaugeProfiles() {
  applyGaugeProfile(0);
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

void nextGaugeProfile() {
  const size_t profileCount = sizeof(kProfiles) / sizeof(kProfiles[0]);
  const size_t next = (gCurrentProfileIndex + 1) % profileCount;
  applyGaugeProfile(next);
}

void prevGaugeProfile() {
  const size_t profileCount = sizeof(kProfiles) / sizeof(kProfiles[0]);
  const size_t prev = (gCurrentProfileIndex + profileCount - 1) % profileCount;
  applyGaugeProfile(prev);
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
  drawCenteredTextFixed(y, buffer, &Font_nokia_8, color, BLACK);
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

  const float arcStartClockDeg = 240.0f;
  const float arcSweepClockDeg = 240.0f;

  const float boost = getBoostPressure();
  const float normalized = (clampFloat(boost, kBoostDisplayMin, kBoostDisplayMax) - kBoostDisplayMin) /
                           (kBoostDisplayMax - kBoostDisplayMin);
  const float filledSweep = arcSweepClockDeg * normalized;

  if (filledSweep > 0.5f) {
    drawClockArc(cx, cy, arcRadius, arcThickness, arcStartClockDeg, filledSweep, WHITE);
  }

  char buffer[64];

  snprintf(buffer, sizeof(buffer), "%.1f", (double)boost);
  drawCenteredTextFixed(80, buffer, &Font_nokia_20, WHITE, BLACK);
  if (boost < 0.0f) {
    drawCenteredTextFixed(108, "Boost (inHg)", &Font_nokia_8, WHITE, BLACK);
  } else {
    drawCenteredTextFixed(108, "Boost (PSI)", &Font_nokia_8, WHITE, BLACK);
  }

  drawStatusIfNeeded(160, GRAY);

  const UWORD waterColor = waterTempColor(obdValues.coolant_temp_c);
  snprintf(buffer, sizeof(buffer), "Water: %.0fC", (double)obdValues.coolant_temp_c);
  drawCenteredTextFixed(180, buffer, &Font_nokia_12, waterColor, BLACK);

  snprintf(buffer, sizeof(buffer), "AIT: %.0fC", (double)obdValues.intake_air_temp_c);
  drawCenteredTextFixed(208, buffer, &Font_nokia_12, GBLUE, BLACK);

  LCD_1IN28_Display(BlackImage);
}

extern float gHorsepowerEstimate; // Wait, we need to handle gHorsepowerEstimate. Let's compute it here or use obdValues.
void renderHorsepowerGauge() {
  Paint_Clear(BLACK);

  const int cx = 120;
  const int cy = 120;
  const int arcRadius = 112;
  const int arcThickness = 14;
  const float arcStartClockDeg = 240.0f;
  const float arcSweepClockDeg = 240.0f;

  float hp = obdValues.maf_airflow * 1.25f; // Calculated from MAF here!
  const float normalized = (clampFloat(hp, kHorsepowerDisplayMin, kHorsepowerDisplayMax) - kHorsepowerDisplayMin) /
                           (kHorsepowerDisplayMax - kHorsepowerDisplayMin);
  const float filledSweep = arcSweepClockDeg * normalized;

  if (filledSweep > 0.5f) {
    drawClockArc(cx, cy, arcRadius, arcThickness, arcStartClockDeg, filledSweep, WHITE);
  }

  char buffer[64];

  snprintf(buffer, sizeof(buffer), "%.0f", (double)hp);
  drawCenteredTextFixed(84, buffer, &Font_nokia_20, WHITE, BLACK);
  drawCenteredTextFixed(112, "HP", &Font_nokia_12, WHITE, BLACK);
  drawCenteredTextFixed(146, "MAF x 1.25", &Font_nokia_8, GBLUE, BLACK);
  snprintf(buffer, sizeof(buffer), "MAF: %.1f g/s", (double)obdValues.maf_airflow);
  drawCenteredTextFixed(168, buffer, &Font_nokia_12, GBLUE, BLACK);

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
  drawCenteredTextFixed(80, buffer, &Font_nokia_20, WHITE, BLACK);
  drawCenteredTextFixed(108, "AFR", &Font_nokia_12, WHITE, BLACK);

  snprintf(buffer, sizeof(buffer), "Lambda: %.3f", (double)obdValues.lambda_ratio);
  drawCenteredTextFixed(144, buffer, &Font_nokia_12, GBLUE, BLACK);

  snprintf(buffer, sizeof(buffer), "Stoich: %.1f", (double)kGasolineStoichAfr);
  drawCenteredTextFixed(168, buffer, &Font_nokia_8, GRAY, BLACK);

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
  drawCenteredTextFixed(80, buffer, &Font_nokia_20, WHITE, BLACK);
  drawCenteredTextFixed(108, "IGN DEG", &Font_nokia_12, WHITE, BLACK);

  snprintf(buffer, sizeof(buffer), "RPM: %u", (unsigned int)obdValues.rpm);
  drawCenteredTextFixed(144, buffer, &Font_nokia_12, GBLUE, BLACK);

  drawCenteredTextFixed(168, "BTDC", &Font_nokia_8, GRAY, BLACK);

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
  drawCenteredTextFixed(72, buffer, &Font_nokia_20, WHITE, BLACK);
  drawCenteredTextFixed(102, "GEAR", &Font_nokia_8, GRAY, BLACK);

  snprintf(buffer, sizeof(buffer), "RPM: %u", (unsigned int)obdValues.rpm);
  drawCenteredTextFixed(136, buffer, &Font_nokia_12, arcColor, BLACK);

  snprintf(buffer, sizeof(buffer), "SPD: %u", (unsigned int)obdValues.vehicle_speed_kmh);
  drawCenteredTextFixed(156, buffer, &Font_nokia_12, GBLUE, BLACK);
  drawCenteredTextFixed(170, "km/h", &Font_nokia_8, GRAY, BLACK);

  if (gear > 0 && targetShiftRpm > 0) {
    snprintf(buffer, sizeof(buffer), "Shift @ %u", (unsigned int)targetShiftRpm);
    drawCenteredTextFixed(184, buffer, &Font_nokia_8, arcColor, BLACK);
  } else if (gear == 6) {
    drawCenteredTextFixed(184, "Top Gear", &Font_nokia_8, GRAY, BLACK);
  } else if (gear == 0) {
    drawCenteredTextFixed(184, "Neutral / Clutch", &Font_nokia_8, GRAY, BLACK);
  } else {
    drawCenteredTextFixed(184, "Gear Detecting", &Font_nokia_8, GRAY, BLACK);
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

  Paint_DrawString_EN(cx + 51, cy - 57, "1.5g", &Font_nokia_8, BLACK, GRAY);
  Paint_DrawString_EN(cx + 25, cy - 31, "0.75g", &Font_nokia_8, BLACK, GRAY);

  if (!isImuReady()) {
    drawCenteredTextFixed(100, "IMU OFFLINE", &Font_nokia_12, RED, BLACK);
    drawCenteredTextFixed(184, "Check QMI8658 wiring", &Font_nokia_8, GRAY, BLACK);
    LCD_1IN28_Display(BlackImage);
    return;
  }

  const uint32_t now = millis();
  const GForcePeak* gGforcePeakBuffer = getGforcePeakBuffer();
  float kGmeterDisplayRange = 1.5f;
  uint32_t kGforceTrailWindowMs = 5000;
  for (size_t i = 0; i < kGforcePeakBufferSize; i++) {
    const GForcePeak& peak = gGforcePeakBuffer[i];
    if (peak.timestampMs == 0 || (now - peak.timestampMs) > kGforceTrailWindowMs) {
      continue;
    }
    const float lateral = clampFloat(peak.lateralG, -kGmeterDisplayRange, kGmeterDisplayRange);
    const float longitudinal = clampFloat(peak.longitudinalG, -kGmeterDisplayRange, kGmeterDisplayRange);
    const int trailX = cx + (int)((lateral / kGmeterDisplayRange) * (float)maxDotTravel);
    const int trailY = cy - (int)((longitudinal / kGmeterDisplayRange) * (float)maxDotTravel);
    Paint_DrawCircle((UWORD)trailX, (UWORD)trailY, 3, GRAY, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  }

  const float lateral = clampFloat(getLateralG(), -kGmeterDisplayRange, kGmeterDisplayRange);
  const float longitudinal = clampFloat(getLongitudinalG(), -kGmeterDisplayRange, kGmeterDisplayRange);

  const int dotX = cx + (int)((lateral / kGmeterDisplayRange) * (float)maxDotTravel);
  const int dotY = cy - (int)((longitudinal / kGmeterDisplayRange) * (float)maxDotTravel);
  Paint_DrawCircle((UWORD)dotX, (UWORD)dotY, (UWORD)dotRadius, RED, DOT_PIXEL_1X1, DRAW_FILL_FULL);

  char buffer[64];
  snprintf(buffer, sizeof(buffer), "Lat: %+0.2fg", (double)getLateralG());
  drawCenteredTextFixed(198, buffer, &Font_nokia_8, GBLUE, BLACK);

  snprintf(buffer, sizeof(buffer), "Long: %+0.2fg", (double)getLongitudinalG());
  drawCenteredTextFixed(212, buffer, &Font_nokia_8, WHITE, BLACK);

  snprintf(buffer, sizeof(buffer), "Forward ^");
  drawCenteredTextFixed(226, buffer, &Font_nokia_8, GRAY, BLACK);

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
