#include <Arduino.h>
#include "espnow_controller.h"
#include "lcd_init.h"
#include "gauge_drawing.h"
#include "obd.h"
#include "gauge_config.h"
#include "ble_config.h"
#include "math_helpers.h"

// Touch sensor
CST816S touch(6, 7, 13, 5);  // sda, scl, rst, irq

// Gauge data
long touchDisabled = 0;
int gaugeNo = 0;

// Timing
unsigned long lastDataPoll = 0;
const int DATA_POLL_INTERVAL = 50; // Poll data every 50ms

// Application State
enum AppMode {
  MODE_RUNNING,
  MODE_CONFIG
};
AppMode currentMode = MODE_RUNNING;

// Flag for config update
volatile bool configUpdatePending = false;

// Callback for BLE JSON updates
void onConfigUpdate(const String& newJson) {
    // Signal received - actual data is in bleConfigBuffer
    configUpdatePending = true;
    Serial.println("Config update queued...");
}

void setup() {
  Serial.begin(115200);
  delay(100);

  // Initialize Math LUT
  initMath();

  // Initialize OBD (CAN bus)
  setupOBD();

  // Load Gauge Configuration
  loadGaugeConfig();
  
  // Initialize ESP-NOW - before display
  Serial.println("\n\n=== ESP-NOW CONTROLLER ===");
  initESPNow();
  Serial.println("ESP-NOW initialized");
  
  // Initialize LCD and display
  initLCD();
  displayStartupMessage();
  
  Serial.println("=== Setup Complete ===\n");
}

void loop() {
  // Process low-level ESP-NOW tasks (timeouts, queues)
  processESPNOW();

  // ---------------------------------------------------------
  // MODE: CONFIGURATION (BLE)
  // ---------------------------------------------------------
  if (currentMode == MODE_CONFIG) {
       // Check for pending config update (poll based, no callback)
       if (bleDataReady) {
           Serial.println("========== PROCESSING CONFIG ==========");
           Serial.printf("Processing BLE config... Free Heap: %d\n", ESP.getFreeHeap());
           Serial.printf("Buffer length: %d\n", strlen(bleConfigBuffer));
           
           reloadConfigFromString(String(bleConfigBuffer));
           
           bleDataReady = false;
           Serial.printf("Config processed. Free Heap: %d\n", ESP.getFreeHeap());
           Serial.println("=======================================");
       }

       drawConfigScreen();
       
       if (touch.available() && (touchDisabled < millis())) {
            // Check for EXIT button (x:60-180, y:180-220)
            if (touch.data.gestureID == SINGLE_CLICK || touch.data.gestureID == DOUBLE_CLICK) {
                 int x = touch.data.x;
                 int y = touch.data.y;
                 if (y >= 180 && y <= 220 && x >= 60 && x <= 180) {
                      // Exit Config Mode
                      stopBLEConfigMode();
                      // Re-init ESP-NOW (often needed after BLE use)
                      initESPNow(); 
                      currentMode = MODE_RUNNING;
                      touchDisabled = millis() + 500;
                 }
            }
       }
       delay(20); // Small delay in config mode to save power (UI not time critical)
       return;
  }

  // ---------------------------------------------------------
  // MODE: RUNNING (Gauges + Menu)
  // ---------------------------------------------------------

  // Poll Data Sources (every 50ms)
  if (millis() - lastDataPoll > DATA_POLL_INTERVAL) {
    if (gaugeNo < gaugeCount) {
      GaugeConfig* currentGauge = &activeGauges[gaugeNo];
      if (currentGauge->source == DATA_SOURCE_OBD) {
        pollOBD(currentGauge->dataIndex);
      }
    }
    lastDataPoll = millis();
  }
  
  static bool isMenuOpen = false;
  static bool isSettingsMode = false;
  static bool isDemoMode = false;

  if (isMenuOpen) {
    // Draw Menu
    uint8_t o1, o2, o3;
    uint8_t s1, s2, s3;
    getOutputValues(o1, o2, o3);
    getOutputStatus(s1, s2, s3);
    
    drawMenu(isSettingsMode, isInPairingMode(), isDemoMode, o1 > 0, o2 > 0, o3 > 0, 
             s1, s2, s3,
             buttonLabels[0], 
             buttonLabels[1], 
             buttonLabels[2]);
  } else {
      // Update current gauge display
    if (gaugeNo < gaugeCount) {
      GaugeConfig* currentGauge = &activeGauges[gaugeNo];
      float value = 0.0;
      
      if (isDemoMode) {
          // Demo Mode: Simulate value with sine wave
          // Cycle period ~2 seconds
          float t = millis() / 1000.0f;
          float factor = 0.5f + 0.5f * fastSin(t * 180.0f); // fastSin uses degrees
          value = currentGauge->minVal + (currentGauge->maxVal - currentGauge->minVal) * factor;
      } else {
          if (currentGauge->source == DATA_SOURCE_OBD) {
            // Get latest cached value
            value = getOBDValue(currentGauge->dataIndex, currentGauge->dataBytes);
          } else if (currentGauge->source == DATA_SOURCE_ESPNOW) {
            value = (float)getSensorValue(currentGauge->dataIndex);
          }
           // Apply scaling/offset (if needed) for REAL data
           // Demo data is already in target range
          value = (value * currentGauge->scale) + currentGauge->offset;
      }
  
      drawGenericGauge(currentGauge, value);
    } else {
      // Fallback if no gauges or invalid index
      gaugeNo = 0; 
    }
  }

  // Handle touch input
  if (touch.available() && (touchDisabled < millis())) {
    
    // Menu is open ? Handle clicks
    if (isMenuOpen) {
       if (touch.data.gestureID == SWIPE_UP) {
          isMenuOpen = false;
          isSettingsMode = false; // Reset to default view
          touchDisabled = millis() + 300;
       } 
       else if (touch.data.gestureID == SINGLE_CLICK || touch.data.gestureID == DOUBLE_CLICK) {
           int x = touch.data.x;
           int y = touch.data.y;
           
           // 1. CHEVRON AREA (Top ~40px) - Close Menu
           if (y < 40) {
              isMenuOpen = false;
              isSettingsMode = false;
              touchDisabled = millis() + 300;
           }
           // 2. GEAR ICON AREA (Bottom ~40px) - Toggle Mode
           else if (y > 200) {
              isSettingsMode = !isSettingsMode;
              touchDisabled = millis() + 300;
           }
           // 3. MIDDLE AREA
           else {
              if (isSettingsMode) {
                 // --- SETTINGS MODE ---
                 // Pair Button (Y: 50-90)
                 if (y >= 50 && y <= 90) {
                    if (isInPairingMode()) exitPairingMode();
                    else {
                      enterPairingMode();
                      broadcastPairingPacket();
                    }
                    touchDisabled = millis() + 300;
                 }
                 // Config Button (Y: 100-140)
                 else if (y >= 100 && y <= 140) {
                    // Enter Config Mode
                    currentMode = MODE_CONFIG;
                    isMenuOpen = false; 
                    isSettingsMode = false;
                    
                    deinitESPNow();
                    delay(100); 
                    
                    Serial.printf("Starting BLE. Free Heap: %d\n", ESP.getFreeHeap());
                    startBLEConfigMode("32Guage_Config", getCurrentConfigJson(), onConfigUpdate);
                    touchDisabled = millis() + 500;
                 }
                 // Demo Mode Button (Y: 150-190)
                 else if (y >= 150 && y <= 190) {
                     isDemoMode = !isDemoMode;
                     touchDisabled = millis() + 300;
                 }
              } 
              else {
                 // --- CONTROL MODE (Relays) ---
                 uint8_t o1, o2, o3;
                 getOutputValues(o1, o2, o3);
                 
                  // Button 1 (Y: 50-90)
                  if (y >= 50 && y <= 90) {
                     setOutputValues(o1 ? 0 : 255, o2, o3);
                     sendControlPacket(); // Queues packet
                     touchDisabled = millis() + 300;
                  }
                  // Button 2 (Y: 100-140)
                  else if (y >= 100 && y <= 140) {
                     setOutputValues(o1, o2 ? 0 : 255, o3);
                     sendControlPacket(); // Queues packet
                     touchDisabled = millis() + 300;
                  }
                  // Button 3 (Y: 150-190)
                  else if (y >= 150 && y <= 190) {
                     setOutputValues(o1, o2, o3 ? 0 : 255);
                     sendControlPacket(); // Queues packet
                     touchDisabled = millis() + 300;
                  }
              }
           }
       }
    } 
    else {
        // Normal Gauge Mode
        switch (touch.data.gestureID) {
          case SWIPE_LEFT:  // Go to previous gauge
            if (gaugeCount > 0) {
              gaugeNo = (gaugeNo - 1 + gaugeCount) % gaugeCount;
            }
            touchDisabled = millis() + 200;
            break;
          case SWIPE_RIGHT: // Go to next gauge
            if (gaugeCount > 0) {
              gaugeNo = (gaugeNo + 1) % gaugeCount;
            }
            touchDisabled = millis() + 200;
            break;
          case SWIPE_DOWN: // Open Menu
            isMenuOpen = true;
            touchDisabled = millis() + 300;
            break;
          default:
            break;
        }
    }
  }
}
