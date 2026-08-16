#include "GaugeRenderer.h"
#include "Sensors.h"
#include "obd/obd.h"
#include "obd/pid_config.h"
#include "obd/pid_schedule.h"
#include "Display/GUI_Paint.h"
#include "Display/fonts.h"
#include "Display/Font_nokia.h"
#include "Display/LCD_1in28.h"
#include "ConfigManager.h"
#include <math.h>

extern UWORD *BlackImage;

static constexpr UWORD kShiftTrackColor = GRAY;
static constexpr UWORD kShiftOrangeColor = 0xFD20;


static uint32_t gStatusIssueSinceMs = 0;
static int gLastDetectedGear = 0;
static uint32_t gLastDetectedGearMs = 0;

struct ShiftGearConfig {
  uint8_t gearNumber;
  float rpmPerKph;
  uint16_t targetShiftRpm;
};

static ShiftGearConfig kShiftGearTable[] = {
  {1, 115.4f, 6500},
  {2, 73.2f, 6300},
  {3, 49.2f, 6100},
  {4, 36.6f, 6000},
  {5, 28.6f, 5800},
  {6, 24.0f, 0},
};

void setShiftTargetRpm(uint8_t gear, uint16_t targetRpm) {
  const size_t gearCount = sizeof(kShiftGearTable) / sizeof(kShiftGearTable[0]);
  for (size_t i = 0; i < gearCount; i++) {
    if (kShiftGearTable[i].gearNumber == gear) {
      kShiftGearTable[i].targetShiftRpm = targetRpm;
      return;
    }
  }
}

/**
 * @brief Manages the display delay timer when an OBD connection issue arises.
 * 
 * Prevents the screen from immediately flickering or flashing warning text 
 * by waiting for a contiguous 5-second connection failure state first.
 * 
 * @param statusIssue True if the OBD protocol is currently experiencing failure.
 */
void updateStatusIssueTimer(bool statusIssue) {
  if (statusIssue) {
    if (gStatusIssueSinceMs == 0) {
      gStatusIssueSinceMs = millis();
    }
  } else {
    gStatusIssueSinceMs = 0;
  }
}

/**
 * @brief Standard mathematical clamp function for floats.
 * 
 * @param value Floating-point value to clamp.
 * @param minValue Lower boundary constraint.
 * @param maxValue Upper boundary constraint.
 * @return float Clamped value.
 */
float clampFloat(float value, float minValue, float maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

/**
 * @brief Resolve a configured colour name to the display's RGB565 value.
 */
UWORD configuredColor(const char* name) {
  if (strcmp(name, "white") == 0) return WHITE;
  if (strcmp(name, "gray") == 0) return GRAY;
  if (strcmp(name, "blue") == 0) return BLUE;
  if (strcmp(name, "cyan") == 0) return GBLUE;
  if (strcmp(name, "green") == 0) return GREEN;
  if (strcmp(name, "yellow") == 0) return YELLOW;
  if (strcmp(name, "orange") == 0) return kShiftOrangeColor;
  if (strcmp(name, "red") == 0) return RED;
  return GBLUE;
}

UWORD rangeColor(float value, const SecondaryMetric& metric) {
  if (value < metric.lowerThreshold) return configuredColor(metric.colorBelow);
  if (value > metric.upperThreshold) return configuredColor(metric.colorAbove);
  return configuredColor(metric.colorBetween);
}

/**
 * @brief Draws a circular dial arc representing the primary gauge readout.
 * 
 * Maps standard clock angles to polar coordinates, drawing a continuous arc of 
 * custom thickness by calculating dynamic sine/cosine offsets.
 * 
 * @param cx Center X coordinate on the screen.
 * @param cy Center Y coordinate on the screen.
 * @param radius Radius of the dial arc.
 * @param thickness Stroke thickness of the drawn dial.
 * @param startClockDeg Polar starting angle (in degrees).
 * @param sweepClockDeg Total sweep length of the arc (in degrees).
 * @param color 16-bit color value of the drawn segments.
 */
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

/**
 * @brief Wraps the Waveshare hardware paint library string rendering.
 */
void drawTextFixed(UWORD x, UWORD y, const char* text, sFONT* font, UWORD fg, UWORD bg) {
  Paint_DrawString_EN(x, y, text, font, bg, fg);
}

/**
 * @brief Computes exact pixel width of a text string based on font constraints.
 * 
 * @param text The input character string.
 * @param font Pointer to the font metadata table.
 * @return int Computed pixel width.
 */
int textWidthPx(const char* text, sFONT* font) {
  if (text == NULL || font == NULL) {
    return 0;
  }
  return (int)strlen(text) * (int)font->Width;
}

/**
 * @brief Utility function to draw text perfectly aligned horizontally on the LCD.
 * 
 * Automatically centers text horizontally within the 240px circular boundary.
 * 
 * @param y Coordinate Y position of the baseline text.
 * @param text Pointer to characters to draw.
 * @param font Pointer to LCD font specification.
 * @param fg Foreground color.
 * @param bg Background color.
 */
void drawCenteredTextFixed(UWORD y, const char* text, sFONT* font, UWORD fg, UWORD bg) {
  const int width = textWidthPx(text, font);
  int x = (240 - width) / 2;
  if (x < 0) {
    x = 0;
  }
  drawTextFixed((UWORD)x, y, text, font, fg, bg);
}

/**
 * @brief Draw a bundled bitmap font at an integer scale without antialiasing.
 *
 * This keeps the LCD's deliberately crisp pixel style while allowing important
 * values to be much larger than the biggest bundled font.
 */
void drawCenteredTextScaled(UWORD y, const char* text, sFONT* font, uint8_t scale, UWORD fg, UWORD bg) {
  if (text == NULL || font == NULL || scale == 0) {
    return;
  }

  const int width = (int)strlen(text) * (int)font->Width * (int)scale;
  int startX = (240 - width) / 2;
  if (startX < 0) {
    startX = 0;
  }

  int charX = startX;
  const uint16_t rowBytes = font->Width / 8 + (font->Width % 8 ? 1 : 0);
  while (*text != '\0') {
    const uint32_t charOffset = (*text - ' ') * font->Height * rowBytes;
    const unsigned char* glyph = &font->table[charOffset];
    for (uint16_t row = 0; row < font->Height; row++) {
      for (uint16_t col = 0; col < font->Width; col++) {
        const bool set = glyph[(row * rowBytes) + (col / 8)] & (0x80 >> (col % 8));
        const UWORD color = set ? fg : bg;
        for (uint8_t sy = 0; sy < scale; sy++) {
          for (uint8_t sx = 0; sx < scale; sx++) {
            Paint_SetPixel((UWORD)(charX + col * scale + sx),
                           (UWORD)(y + row * scale + sy), color);
          }
        }
      }
    }
    charX += font->Width * scale;
    text++;
  }
}

/**
 * @brief Centre a scaled bitmap string by its lit pixels, not its fixed font cell.
 *
 * Large single glyphs (notably the shift gear) can have substantial blank space
 * within their font cell, which makes ordinary fixed-cell centring look offset.
 */
void drawCenteredTextScaledByInk(UWORD y, const char* text, sFONT* font, uint8_t scale, UWORD fg, UWORD bg) {
  if (text == NULL || font == NULL || scale == 0) {
    return;
  }

  const uint16_t rowBytes = font->Width / 8 + (font->Width % 8 ? 1 : 0);
  int minInkX = 32767;
  int maxInkX = -1;
  for (size_t character = 0; text[character] != '\0'; character++) {
    const uint32_t charOffset = (text[character] - ' ') * font->Height * rowBytes;
    const unsigned char* glyph = &font->table[charOffset];
    for (uint16_t row = 0; row < font->Height; row++) {
      for (uint16_t col = 0; col < font->Width; col++) {
        if (glyph[(row * rowBytes) + (col / 8)] & (0x80 >> (col % 8))) {
          const int inkX = (int)(character * font->Width) + col;
          if (inkX < minInkX) minInkX = inkX;
          if (inkX > maxInkX) maxInkX = inkX;
        }
      }
    }
  }

  if (maxInkX < minInkX) {
    return;
  }

  const int startX = (240 - (maxInkX - minInkX + 1) * scale) / 2 - minInkX * scale;
  int charX = startX;
  while (*text != '\0') {
    const uint32_t charOffset = (*text - ' ') * font->Height * rowBytes;
    const unsigned char* glyph = &font->table[charOffset];
    for (uint16_t row = 0; row < font->Height; row++) {
      for (uint16_t col = 0; col < font->Width; col++) {
        const UWORD color = (glyph[(row * rowBytes) + (col / 8)] & (0x80 >> (col % 8))) ? fg : bg;
        for (uint8_t sy = 0; sy < scale; sy++) {
          for (uint8_t sx = 0; sx < scale; sx++) {
            Paint_SetPixel((UWORD)(charX + col * scale + sx),
                           (UWORD)(y + row * scale + sy), color);
          }
        }
      }
    }
    charX += font->Width * scale;
    text++;
  }
}

/**
 * @brief Displays connection/fault warnings at the top/bottom if OBD bus drops out.
 */
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

/**
 * @brief Dynamic transmission gear detection algorithm.
 * 
 * Compares current Engine Speed (RPM) to Vehicle Speed (KPH) to compute the 
 * mechanical drive ratio. Using dynamic mathematical tolerance scaling, this maps 
 * the ratio to standard gear configurations defined in kShiftGearTable.
 * 
 * Includes a brief hold window to prevent gear readouts from fluctuating or dropping 
 * during shifts.
 * 
 * @param currentRpm Real-time engine speed (RPM).
 * @param currentKph Real-time vehicle velocity (KM/H).
 * @return int Detected gear number (1-6), 0 if Neutral/Clutch depressed, -1 if unresolved.
 */
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

/**
 * @brief Lookup helper to find the target shift threshold RPM for a given gear.
 */
uint16_t getTargetShiftRpmForGear(int gear) {
  const size_t gearCount = sizeof(kShiftGearTable) / sizeof(kShiftGearTable[0]);
  for (size_t i = 0; i < gearCount; i++) {
    if ((int)kShiftGearTable[i].gearNumber == gear) {
      return kShiftGearTable[i].targetShiftRpm;
    }
  }
  return 0;
}

/**
 * @brief Dynamic colors for Shift Light display segments.
 * 
 * Yellow at 90%, Orange at 96%, and blinking Red at 100% of target RPM.
 */
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

/**
 * @brief Computes standard sweep bounds (0-240 degrees) of the shift light arc.
 */
float shiftArcSweep(float rpm, uint16_t targetShiftRpm) {
  if (targetShiftRpm == 0) {
    return 0.0f;
  }

  const float normalized = clampFloat(rpm / (float)targetShiftRpm, 0.0f, 1.0f);
  return 240.0f * normalized;
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
  drawCenteredTextScaledByInk(38, buffer, &Font_nokia_24, 3, WHITE, BLACK);
  drawCenteredTextFixed(112, "GEAR", &Font_nokia_12, GRAY, BLACK);

  snprintf(buffer, sizeof(buffer), "%u", (unsigned int)obdValues.rpm);
  drawCenteredTextFixed(132, buffer, &Font_nokia_20, arcColor, BLACK);
  drawCenteredTextFixed(153, "RPM", &Font_nokia_8, GRAY, BLACK);

  snprintf(buffer, sizeof(buffer), "%u", (unsigned int)obdValues.vehicle_speed_kmh);
  drawCenteredTextFixed(170, buffer, &Font_nokia_20, GBLUE, BLACK);
  drawCenteredTextFixed(192, "km/h", &Font_nokia_8, GRAY, BLACK);

  LCD_1IN28_Display(BlackImage);
}

enum AccelerationTimerState {
  ACCEL_TIMER_ARMED,
  ACCEL_TIMER_RUNNING,
  ACCEL_TIMER_COMPLETE,
};

static volatile AccelerationTimerState gAccelerationTimerState = ACCEL_TIMER_ARMED;
static volatile uint32_t gAccelerationTimerStartMs = 0;
static volatile uint32_t gAccelerationTimerElapsedMs = 0;

/**
 * @brief Advance the 0-100 timer state from the high-frequency OBD task.
 */
void updateAccelerationTimer() {
  const size_t profileIndex = getCurrentGaugeProfileIndex();
  if (profileIndex >= activeGaugeCount || activeGauges[profileIndex].type != GAUGE_TYPE_ACCEL_TIMER) {
    return;
  }
  const GaugeConfig& config = activeGauges[profileIndex];
  const float startSpeed = config.minVal;
  const float finishSpeed = config.maxVal;
  const float speed = getValueForSource(config.mainSourceId);
  const uint32_t now = millis();

  if (speed <= startSpeed) {
    gAccelerationTimerState = ACCEL_TIMER_ARMED;
    gAccelerationTimerElapsedMs = 0;
  } else if (gAccelerationTimerState == ACCEL_TIMER_ARMED) {
    gAccelerationTimerState = ACCEL_TIMER_RUNNING;
    gAccelerationTimerStartMs = now;
  } else if (gAccelerationTimerState == ACCEL_TIMER_RUNNING && speed >= finishSpeed) {
    gAccelerationTimerElapsedMs = now - gAccelerationTimerStartMs;
    gAccelerationTimerState = ACCEL_TIMER_COMPLETE;
  }
}

/**
 * @brief Render an automatic 0-100 km/h timer from the OBD vehicle-speed PID.
 *
 * The timer arms at 1 km/h or below, starts as the vehicle moves above 1 km/h,
 * and stops at 100 km/h. Stopping the vehicle arms it for the next run.
 */
void renderAccelerationTimerGauge() {
  Paint_Clear(BLACK);

  const size_t profileIndex = getCurrentGaugeProfileIndex();
  if (profileIndex >= activeGaugeCount) {
    return;
  }
  const GaugeConfig& config = activeGauges[profileIndex];
  const float speed = getValueForSource(config.mainSourceId);
  const float speedRange = config.maxVal - config.minVal;
  const uint32_t now = millis();
  // Keep direct renderer calls, including native previews, self-contained.
  // During normal use the OBD task updates this state at a higher cadence.
  updateAccelerationTimer();

  const float speedSweep = speedRange > 0.0f ? 240.0f * (clampFloat(speed, config.minVal, config.maxVal) - config.minVal) / speedRange : 0.0f;
  drawClockArc(120, 120, 112, 14, 240.0f, 240.0f, GRAY);
  if (speedSweep > 0.5f) {
    drawClockArc(120, 120, 112, 14, 240.0f, speedSweep, GBLUE);
  }

  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.0f-%.0f", (double)config.minVal, (double)config.maxVal);
  drawCenteredTextFixed(38, buffer, &Font_nokia_12, WHITE, BLACK);
  snprintf(buffer, sizeof(buffer), "%.0f", (double)speed);
  drawCenteredTextScaledByInk(56, buffer, &Font_nokia_20, 2, GBLUE, BLACK);
  drawCenteredTextFixed(98, config.unitLabel, &Font_nokia_8, GRAY, BLACK);

  uint32_t elapsedMs = gAccelerationTimerElapsedMs;
  if (gAccelerationTimerState == ACCEL_TIMER_RUNNING) {
    elapsedMs = now - gAccelerationTimerStartMs;
  }
  if (gAccelerationTimerState == ACCEL_TIMER_ARMED) {
    snprintf(buffer, sizeof(buffer), "--.--s");
  } else {
    snprintf(buffer, sizeof(buffer), "%.2fs", (double)elapsedMs / 1000.0);
  }
  drawCenteredTextFixed(128, buffer, &Font_nokia_20, WHITE, BLACK);
  drawCenteredTextFixed(150, "TIME", &Font_nokia_8, GRAY, BLACK);

  const char* stateText = gAccelerationTimerState == ACCEL_TIMER_ARMED ? "ARMED" :
                          gAccelerationTimerState == ACCEL_TIMER_RUNNING ? "TIMING" : "COMPLETE";
  drawCenteredTextFixed(176, stateText, &Font_nokia_12,
                        gAccelerationTimerState == ACCEL_TIMER_COMPLETE ? GBLUE : GRAY, BLACK);
  drawStatusIfNeeded(208, GRAY);
  LCD_1IN28_Display(BlackImage);
}

void renderGmeterGauge() {
  Paint_Clear(BLACK);

  const int cx = 120;
  const int cy = 120;
  const int radius = 72;
  const int dotRadius = 7;
  const int maxDotTravel = radius - dotRadius - 2;
  static constexpr UWORD kGmeterGridColor = 0x4208;
  constexpr float kGmeterDisplayRange = 1.5f;
  constexpr uint32_t kGforceTrailWindowMs = 5000;

  Paint_DrawCircle((UWORD)cx, (UWORD)cy, (UWORD)radius, kGmeterGridColor, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
  Paint_DrawCircle((UWORD)cx, (UWORD)cy, (UWORD)(radius / 2), kGmeterGridColor, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
  Paint_DrawLine((UWORD)(cx - radius), (UWORD)cy, (UWORD)(cx + radius), (UWORD)cy, kGmeterGridColor, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
  Paint_DrawLine((UWORD)cx, (UWORD)(cy - radius), (UWORD)cx, (UWORD)(cy + radius), kGmeterGridColor, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
  Paint_DrawCircle((UWORD)cx, (UWORD)cy, 2, kGmeterGridColor, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  drawTextFixed(cx + 51, cy - 57, "1.5g", &Font_nokia_8, GRAY, BLACK);
  drawTextFixed(cx + 25, cy - 31, "0.75g", &Font_nokia_8, GRAY, BLACK);

  if (!isImuReady()) {
    drawCenteredTextFixed(92, "IMU OFFLINE", &Font_nokia_12, RED, BLACK);
    drawCenteredTextFixed(174, "Check QMI8658 wiring", &Font_nokia_8, GRAY, BLACK);
    LCD_1IN28_Display(BlackImage);
    return;
  }

  drawStatusIfNeeded(16, GRAY);

  const uint32_t now = millis();
  const GForcePeak* gGforcePeakBuffer = getGforcePeakBuffer();
  float peakLateral = 0.0f;
  float peakLongitudinal = 0.0f;
  for (size_t i = 0; i < kGforcePeakBufferSize; i++) {
    const GForcePeak& peak = gGforcePeakBuffer[i];
    if (peak.timestampMs == 0 || (now - peak.timestampMs) > kGforceTrailWindowMs) {
      continue;
    }

    if (fabsf(peak.lateralG) > fabsf(peakLateral)) {
      peakLateral = peak.lateralG;
    }
    if (fabsf(peak.longitudinalG) > fabsf(peakLongitudinal)) {
      peakLongitudinal = peak.longitudinalG;
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
  drawCenteredTextFixed(156, "PEAK", &Font_nokia_8, GRAY, BLACK);
  drawTextFixed(54, 170, "LAT", &Font_nokia_8, GRAY, BLACK);
  drawTextFixed(156, 170, "LONG", &Font_nokia_8, GRAY, BLACK);

  snprintf(buffer, sizeof(buffer), "%+.2f", (double)peakLateral);
  const int lateralWidth = textWidthPx(buffer, &Font_nokia_16);
  drawTextFixed((UWORD)(70 - lateralWidth / 2), 182, buffer, &Font_nokia_16, GBLUE, BLACK);

  snprintf(buffer, sizeof(buffer), "%+.2f", (double)peakLongitudinal);
  const int longitudinalWidth = textWidthPx(buffer, &Font_nokia_16);
  drawTextFixed((UWORD)(170 - longitudinalWidth / 2), 182, buffer, &Font_nokia_16, WHITE, BLACK);

  LCD_1IN28_Display(BlackImage);
}

/**
 * @brief Primary generalized renderer for circular standard gauges.
 * 
 * Completely data-driven by the active profile's GaugeConfig configuration. 
 * Renders standard dial arc, centers primary digital reading inside the circle, 
 * draws labels dynamically, and handles secondary metric configurations (like 
 * Intake Temp, Coolant Temp, Fuel pressure) dynamically positioned vertically.
 * 
 * @param config Struct containing screen constraints and variable mappings.
 */
void renderGenericGauge(const GaugeConfig& config) {
  Paint_Clear(BLACK);

  const int cx = 120;
  const int cy = 120;
  const int arcRadius = 112;
  const int arcThickness = 14;
  const float arcStartClockDeg = 240.0f;
  const float arcSweepClockDeg = 240.0f;

  float mainVal = getValueForSource(config.mainSourceId);
  const float normalized = (clampFloat(mainVal, config.minVal, config.maxVal) - config.minVal) /
                           (config.maxVal - config.minVal);
  const float filledSweep = arcSweepClockDeg * normalized;

  if (filledSweep > 0.5f) {
    drawClockArc(cx, cy, arcRadius, arcThickness, arcStartClockDeg, filledSweep, WHITE);
  }

  char buffer[64];
  
  if (fabsf(mainVal) >= 100.0f) {
      snprintf(buffer, sizeof(buffer), "%.0f", (double)mainVal);
  } else {
      snprintf(buffer, sizeof(buffer), "%.1f", (double)mainVal);
  }
  // Four-character readings (e.g. 12.4) use a crisp 2x bitmap font. Scaling
  // the 16px face gives the value emphasis without touching the dial arc.
  // Fall back to the largest native font for longer negative values.
  if (strlen(buffer) <= 4) {
      drawCenteredTextScaled(58, buffer, &Font_nokia_16, 2, WHITE, BLACK);
  } else {
      drawCenteredTextFixed(62, buffer, &Font_nokia_24, WHITE, BLACK);
  }
  
  // Custom case: dynamically adjust unit labels if handling vacuum/boost scales
  if (config.boostUnits) {
      if (mainVal < 0.0f) {
          drawCenteredTextFixed(94, "Boost (inHg)", &Font_nokia_12, WHITE, BLACK);
      } else {
          drawCenteredTextFixed(94, "Boost (PSI)", &Font_nokia_12, WHITE, BLACK);
      }
  } else {
      drawCenteredTextFixed(94, config.unitLabel, &Font_nokia_12, WHITE, BLACK);
  }

  // Draw secondary metrics as vertically stacked value/label pairs. Keeping
  // labels separate lets the useful number remain large without a long line
  // of text running outside the circular display.
  for (uint8_t i = 0; i < config.secondaryCount; i++) {
      const SecondaryMetric& sec = config.secondaries[i];
      float secVal = getValueForSource(sec.sourceId);
      
      if (fabsf(secVal) >= 100.0f) {
          snprintf(buffer, sizeof(buffer), "%.0f%s", (double)secVal, sec.suffix);
      } else {
          snprintf(buffer, sizeof(buffer), "%.1f%s", (double)secVal, sec.suffix);
      }
      
      UWORD color = GBLUE;
      if (sec.rangeColors) {
          color = rangeColor(secVal, sec);
      }
      
      const UWORD valueY = config.secondaryCount <= 2 ? (UWORD)(120 + i * 54) : (UWORD)(102 + i * 44);
      const UWORD labelY = config.secondaryCount <= 2 ? (UWORD)(144 + i * 54) : (UWORD)(124 + i * 44);
      drawCenteredTextFixed(valueY, buffer, &Font_nokia_20, color, BLACK);

      char label[sizeof(sec.prefix)];
      snprintf(label, sizeof(label), "%s", sec.prefix);
      size_t labelLength = strlen(label);
      while (labelLength > 0 && (label[labelLength - 1] == ' ' || label[labelLength - 1] == ':')) {
          label[--labelLength] = '\0';
      }
      drawCenteredTextFixed(labelY, label, &Font_nokia_12, GRAY, BLACK);
  }

  drawStatusIfNeeded(160, GRAY);
  LCD_1IN28_Display(BlackImage);
}

/**
 * @brief Dispatch renderer loop triggered at 50Hz (every 20ms) inside main task.
 * 
 * Translates the active JSON profile type to its respective drawing layout.
 */
void renderDisplay() {
  size_t idx = getCurrentGaugeProfileIndex();
  if (idx >= activeGaugeCount) {
    Paint_Clear(BLACK);
    LCD_1IN28_Display(BlackImage);
    return;
  }

  const GaugeConfig& config = activeGauges[idx];
  switch (config.type) {
    case GAUGE_TYPE_STANDARD:
      renderGenericGauge(config);
      return;
    case GAUGE_TYPE_SHIFTLIGHT:
      renderShiftLightGauge();
      return;
    case GAUGE_TYPE_GMETER:
      renderGmeterGauge();
      return;
    case GAUGE_TYPE_ACCEL_TIMER:
      renderAccelerationTimerGauge();
      return;
    default:
      Paint_Clear(BLACK);
      LCD_1IN28_Display(BlackImage);
      return;
  }
}
