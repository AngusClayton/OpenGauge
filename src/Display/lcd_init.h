#ifndef LCD_INIT_H
#define LCD_INIT_H

#include <Arduino.h>
#include "LCD_1in28.h"
#include "GUI_Paint.h"
#include "CST816S.h"

// External display buffer
extern UWORD *BlackImage;
extern UDOUBLE Imagesize;
extern CST816S touch;

/**
 * Initialize PSRAM and allocate display buffer
 * MUST be called BEFORE WiFi/ESP-NOW to avoid memory fragmentation
 */
void initPSRAM();

/**
 * Initialize LCD hardware
 * Should be called after ESP-NOW init
 */
void initLCD();

/**
 * Clear LCD and display startup message
 */
void displayStartupMessage();

#endif // LCD_INIT_H
