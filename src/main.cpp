#include <Arduino.h>
#include <math.h>
#include "Display/lcd_init.h"
#include "Display/GUI_Paint.h"
#include "Display/fonts.h"
#include "Display/Font_nokia.h"
#include "Display/LCD_1in28.h"
#include "Display/CST816S.h"
#include "Display/QMI8658.h"
#include "obd/obd.h"
#include "obd/pid_config.h"
#include "obd/pid_schedule.h"
#include "Sensors.h"
#include "GaugeRenderer.h"
#include "ConfigManager.h"

// External display buffer
extern UWORD *BlackImage;
extern UDOUBLE Imagesize;

// Create touch object
CST816S touch(6, 7, 13, 5);  // sda, scl, rst, irq

TaskHandle_t obdTaskHandle = NULL;
TaskHandle_t displayTaskHandle = NULL;

static uint32_t gLastSwipeMs = 0;

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
      updateAnalogSources();
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
          nextGaugeProfile();
          gLastSwipeMs = now;
        } else if (touch.data.gestureID == SWIPE_RIGHT) {
          prevGaugeProfile();
          gLastSwipeMs = now;
        }
      }
    }

    updateImuSensors(); // Must run in displayTask — shares Wire I2C bus with touch controller
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

  // Seed defaults
  initPidScheduleDefaults();

  loadConfigFromJson();

  initAnalogInputs();
  updateAnalogSources();

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