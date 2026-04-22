#include <Arduino.h>
#include "Display/lcd_init.h"
#include "Display/GUI_Paint.h"
#include "Display/fonts.h"
#include "Display/LCD_1in28.h"
#include "Display/CST816S.h"
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
static constexpr uint8_t kAnalogBoostPin = 17;
static constexpr uint8_t kAnalogSparePin = 18;
static constexpr float kAdcReferenceVolts = 3.3f;
static constexpr float kAdcDividerCompensation = 2.0f; // 50/50 divider -> actual sensor voltage is 2x ADC input

// Boost pressure conversion from compensated sensor voltage.
// Adjust these later for your specific sensor calibration.
static constexpr float kBoostMultiplier = 10.0f;
static constexpr float kBoostOffset = -10.0f;

static volatile float gBoostSensorVoltage = 0.0f;
static volatile float gBoostPressure = 0.0f;

struct GaugeProfile {
  const char* title;
  const PidMetricConfig* metrics;
  size_t metricCount;
};

static const PidMetricConfig kGauge1Metrics[] = {
  {PID_COOLANT_TEMP, "Coolant Temp", PID_FORMULA_A_MINUS_40, 1.0f, 0.0f, 1, true},
};

static const PidMetricConfig kGauge2Metrics[] = {
  {PID_COOLANT_TEMP, "Coolant Temp", PID_FORMULA_A_MINUS_40, 1.0f, 0.0f, 1, true},
  {PID_INTAKE_AIR_TEMP, "Intake Air Temp", PID_FORMULA_A_MINUS_40, 1.0f, 0.0f, 1, true},
};

static const GaugeProfile kProfiles[] = {
  {"Gauge 1: Coolant", kGauge1Metrics, sizeof(kGauge1Metrics) / sizeof(kGauge1Metrics[0])},
  {"Gauge 2: Coolant+IAT", kGauge2Metrics, sizeof(kGauge2Metrics) / sizeof(kGauge2Metrics[0])},
};

static volatile size_t gCurrentProfileIndex = 0;
static uint32_t gLastSwipeMs = 0;

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
  gBoostPressure = (sensorVolts * kBoostMultiplier) + kBoostOffset;
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

void renderDisplay() {
  Paint_Clear(WHITE);

  char buffer[100];
  const GaugeProfile& profile = kProfiles[gCurrentProfileIndex];

  Paint_DrawString_EN(10, 10, "32 Gauge", &Font20, BLACK, WHITE);
  Paint_DrawString_EN(10, 34, profile.title, &Font12, BLACK, WHITE);

  float value = 0.0f;
  bool valid = false;

  const uint8_t pid1 = profile.metrics[0].pid;
  getPidMetricValue(pid1, &value, &valid);
  if (valid) {
    snprintf(buffer, sizeof(buffer), "%s: %.1f C", profile.metrics[0].name, value);
  } else {
    snprintf(buffer, sizeof(buffer), "%s: --", profile.metrics[0].name);
  }
  Paint_DrawString_EN(10, 56, buffer, &Font16, BLACK, WHITE);

  if (profile.metricCount > 1) {
    const uint8_t pid2 = profile.metrics[1].pid;
    getPidMetricValue(pid2, &value, &valid);
    if (valid) {
      snprintf(buffer, sizeof(buffer), "%s: %.1f C", profile.metrics[1].name, value);
    } else {
      snprintf(buffer, sizeof(buffer), "%s: --", profile.metrics[1].name);
    }
    Paint_DrawString_EN(10, 78, buffer, &Font16, BLACK, WHITE);
  }

  if (gCurrentProfileIndex == 1) {
    snprintf(buffer, sizeof(buffer), "Boost: %.2f", (double)gBoostPressure);
    Paint_DrawString_EN(10, 100, buffer, &Font16, BLACK, WHITE);
  }

  snprintf(buffer, sizeof(buffer), "Status: %s", getOBDStatusText());
  Paint_DrawString_EN(10, 122, buffer, &Font12, BLACK, WHITE);

  snprintf(buffer, sizeof(buffer), "PIDs: %u  Swipe < >", (unsigned int)getPidScheduleCount());
  Paint_DrawString_EN(10, 142, buffer, &Font12, BLACK, WHITE);

  LCD_1IN28_Display(BlackImage);
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
      lastAnalogMs = now;
    }

    // Compute display values from whatever is currently cached.
    computeOBDValuesFromCache();

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