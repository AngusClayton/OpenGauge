#pragma once

#include <Arduino.h>

// Initialize gauge profiles and set defaults
void initGaugeProfiles();

// Switch to the next gauge profile
void nextGaugeProfile();

// Switch to the previous gauge profile
void prevGaugeProfile();

// Apply a specific gauge profile
void applyGaugeProfile(size_t index);

// Render the current display profile
void renderDisplay();

// Helper to update the status issue timer
void updateStatusIssueTimer(bool statusIssue);
