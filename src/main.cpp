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
#include "ConfigStorage.h"
#include "ConfigPortal.h"

// External display memory variables allocated by the Waveshare board initialization drivers
extern UWORD *BlackImage;
extern UDOUBLE Imagesize;

// Create touch controller object using the CST816S driver
CST816S touch(6, 7, 13, 5);  // Pin definition: sda, scl, rst, irq

TaskHandle_t obdTaskHandle = NULL;
TaskHandle_t displayTaskHandle = NULL;

static uint32_t gLastSwipeMs = 0; // Debounce tracking timestamp for touch swipe gestures
static bool gSettingsOpen = false;

/**
 * @brief Continuous background FreeRTOS task handling CAN bus communications.
 * 
 * Runs on core 1 with high priority (3). Responsibilities:
 * 1. Monitors and auto-recovers OBD-II/CAN transceiver connectivity.
 * 2. Rapidly drains the incoming TWAI hardware CAN FIFO buffer.
 * 3. Schedules and sends outgoing PID requests at a stable 20Hz cadence (50ms interval).
 * 4. Continuously polls the local physical ADC analog pins at 50Hz (20ms interval).
 * 5. Recomputes final display variables in the background cache.
 * 
 * @param pvParameters FreeRTOS task configuration parameters (unused).
 */
void obdTask(void *pvParameters) {
  const TickType_t xLoopDelay = pdMS_TO_TICKS(10);
  const uint32_t analogIntervalMs = 20;
  uint32_t lastRequestMs = 0;
  uint32_t lastAnalogMs = 0;
  uint32_t lastStatusLogMs = 0;
  uint8_t nextPid = 0;

  Serial.println("[OBD] Task started");
  vTaskDelay(pdMS_TO_TICKS(500));

  while (1) {
    const uint32_t now = millis();

    // Block logic if OBD link drops out; attempt reconnection in background
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
    // The timer screen requests only vehicle speed. Poll it at 50 Hz so its
    // start and finish timestamps do not depend on the 100 ms display loop.
    const uint32_t requestIntervalMs = isAccelerationTimerProfileActive() ? 20 : 50;
    if ((now - lastRequestMs) >= requestIntervalMs) {
      if (getNextScheduledPid(&nextPid)) {
        sendObdFrame(nextPid);
      }
      lastRequestMs = now;
    }

    // Dynamic analog physical pin readings
    if ((now - lastAnalogMs) >= analogIntervalMs) {
      updateAnalogSources();
      lastAnalogMs = now;
    }

    // Compute display values from whatever is currently cached.
    computeOBDValuesFromCache();
    if (isAccelerationTimerProfileActive()) {
      updateAccelerationTimer();
    }

    vTaskDelay(xLoopDelay);
  }
}

/**
 * @brief Continuous background FreeRTOS task handling UI rendering.
 * 
 * Runs on core 0 with standard priority (2). Responsibilities:
 * 1. Checks the CST816S touch controller for swipes.
 * 2. Switches profiles on left/right swipes with a 250ms swipe debounce.
 * 3. Polls accelerometer metrics and applies low-pass noise filters.
 * 4. Triggers display renders on the LCD display buffer via renderDisplay().
 * 
 * Note: QMI8658 IMU and CST816S Touch controller must reside in the same thread 
 * as they share the same physical I2C Wire hardware bus.
 * 
 * @param pvParameters FreeRTOS task configuration parameters (unused).
 */
void displayTask(void *pvParameters) {
  const TickType_t xDelay = pdMS_TO_TICKS(100);
  uint32_t loopCount = 0;

  Serial.println("[DISPLAY] Task started");

  while (1) {
    // Process swipe gestures
    if (touch.available()) {
      if (isConfigPortalActive()) noteConfigPortalActivity();
      const uint32_t now = millis();
      if ((now - gLastSwipeMs) > 250) {
        if (gSettingsOpen && touch.data.gestureID == SWIPE_DOWN) {
          gSettingsOpen = false;
          gLastSwipeMs = now;
        } else if (gSettingsOpen && (touch.data.gestureID == SINGLE_CLICK || touch.data.gestureID == DOUBLE_CLICK)) {
          if (!isConfigPortalActive() && touch.data.y >= 82 && touch.data.y <= 156) startConfigPortal();
          else if (isConfigPortalActive() && touch.data.y >= 154) stopConfigPortal();
          gLastSwipeMs = now;
        } else if (!gSettingsOpen && touch.data.gestureID == SWIPE_UP) {
          gSettingsOpen = true;
          gLastSwipeMs = now;
        } else if (!gSettingsOpen && touch.data.gestureID == SWIPE_LEFT) {
          nextGaugeProfile();
          gLastSwipeMs = now;
        } else if (!gSettingsOpen && touch.data.gestureID == SWIPE_RIGHT) {
          prevGaugeProfile();
          gLastSwipeMs = now;
        }
      }
    }

    updateImuSensors(); // Reads IMU via I2C bus
    if (gSettingsOpen) renderConfigPortalScreen(isConfigPortalActive(), getConfigPortalSsid(), getConfigPortalPassword());
    else renderDisplay();

    // Monitor thread stability by periodically printing FreeRTOS stack high watermarks
    loopCount++;
    if ((loopCount % 50) == 0) {
      UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);
      Serial.printf("[DISPLAY] Stack watermark: %u words\n", (unsigned int)watermark);
    }

    vTaskDelay(xDelay);
  }
}

/**
 * @brief Primary Arduino startup boot configurations.
 * 
 * Sets up serial consoles, initializes standard CAN communications, boots the JSON layout 
 * memory engine, prepares the ADC pins, starts standard hardware SPI displays, and spawns the 
 * primary FreeRTOS scheduling tasks (OBD & DISPLAY) on dedicated cores.
 */
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n===== OPENGAUGE INITIALIZATION =====");

  // Initialize OBD/CAN
  setupOBD();
  Serial.println("OBD initialized");

  // Seed defaults
  initPidScheduleDefaults();

  // Load the last valid on-device configuration, with embedded defaults as fallback.
  char configError[160] = {};
  initConfigStorage();
  if (!loadPersistedConfig(configError, sizeof(configError))) {
    if (configError[0]) Serial.printf("[CONFIG] Stored configuration unavailable: %s\n", configError);
    loadConfigFromJson();
  }

  // Setup raw analog pins
  initAnalogInputs();
  updateAnalogSources();

  // Initialize LCD and display buffer after OBD, matching the original project closely.
  initLCD();
  Serial.println("LCD initialized");

  // Spin up spatial accelerometer hardware
  initImuSensor();

  // Render first screen profile immediately
  renderDisplay();

  // Spawn CAN processing thread
  xTaskCreate(
    obdTask,
    "OBD",
    4096,
    NULL,
    3,
    &obdTaskHandle
  );

  // Spawn UI and Rendering thread
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

/**
 * @brief Default Arduino execution loop.
 * 
 * Empty by design. Because we use a multi-tasking FreeRTOS scheduler, the main thread 
 * task is deleted immediately after setup is complete using vTaskDelete(NULL) to 
 * conserve MCU memory resources.
 */
void loop() {
  vTaskDelete(NULL);
}
