#include <Arduino.h>
#include "Display/lcd_init.h"
#include "Display/GUI_Paint.h"
#include "Display/fonts.h"
#include "Display/LCD_1in28.h"
#include "Display/CST816S.h"
#include "obd/obd.h"
#include "obd/pid_config.h"

// External display buffer
extern UWORD *BlackImage;
extern UDOUBLE Imagesize;

// Create touch object
CST816S touch(6, 7, 13, 5);  // sda, scl, rst, irq

TaskHandle_t obdTaskHandle = NULL;
TaskHandle_t displayTaskHandle = NULL;

static const uint8_t kPidSchedule[] = {
  PID_ENGINE_RPM,
  PID_VEHICLE_SPEED,
  PID_COOLANT_TEMP,
  PID_ENGINE_LOAD,
  PID_THROTTLE_POS,
  PID_INTAKE_PRESSURE,
  PID_MAF_AIRFLOW,
  PID_FUEL_LEVEL,
  PID_IGNITION_TIMING,
  PID_FUEL_PRESSURE,
  PID_O2_VOLTAGE,
  PID_ETHANOL_FUEL,
};

void renderDisplay() {
  Paint_Clear(WHITE);

  char buffer[100];

  Paint_DrawString_EN(50, 10, "32 Gauge", &Font24, BLACK, WHITE);

  snprintf(buffer, sizeof(buffer), "RPM: %d", obdValues.rpm);
  Paint_DrawString_EN(50, 40, buffer, &Font16, BLACK, WHITE);

  snprintf(buffer, sizeof(buffer), "Speed: %d km/h", obdValues.vehicle_speed_kmh);
  Paint_DrawString_EN(50, 60, buffer, &Font16, BLACK, WHITE);

  snprintf(buffer, sizeof(buffer), "Temp: %.1f C", obdValues.coolant_temp_c);
  Paint_DrawString_EN(50, 80, buffer, &Font16, BLACK, WHITE);

  snprintf(buffer, sizeof(buffer), "Fuel: %d%%", obdValues.fuel_level);
  Paint_DrawString_EN(50, 100, buffer, &Font16, BLACK, WHITE);

  snprintf(buffer, sizeof(buffer), "Status: %s", getOBDStatusText());
  Paint_DrawString_EN(20, 140, buffer, &Font12, BLACK, WHITE);

  LCD_1IN28_Display(BlackImage);
}

void obdTask(void *pvParameters) {
  const TickType_t xLoopDelay = pdMS_TO_TICKS(10);
  const uint32_t requestIntervalMs = 50;
  uint32_t lastRequestMs = 0;
  uint32_t lastStatusLogMs = 0;
  size_t pidIndex = 0;

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
      sendObdFrame(kPidSchedule[pidIndex]);
      pidIndex = (pidIndex + 1) % (sizeof(kPidSchedule) / sizeof(kPidSchedule[0]));
      lastRequestMs = now;
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