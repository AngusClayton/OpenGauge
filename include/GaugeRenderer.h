#pragma once

#include <Arduino.h>

// Render the current display profile
void renderDisplay();
void renderConfigPortalScreen(bool portalActive, const char* ssid, const char* password);

// Update the 0-100 timer from the high-frequency OBD task.
void updateAccelerationTimer();
void resetAccelerationTimer();

// Set the target RPM for one shift-light gear. A target of zero disables it.
void setShiftTargetRpm(uint8_t gear, uint16_t targetRpm);

// Helper to update the status issue timer
void updateStatusIssueTimer(bool statusIssue);
