#pragma once

#include <Arduino.h>

// Render the current display profile
void renderDisplay();

// Update the 0-100 timer from the high-frequency OBD task.
void updateAccelerationTimer();

// Helper to update the status issue timer
void updateStatusIssueTimer(bool statusIssue);
