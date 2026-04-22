#include "lcd_init.h"

UDOUBLE Imagesize = LCD_1IN28_HEIGHT * LCD_1IN28_WIDTH * 2;
UWORD *BlackImage = NULL;

static constexpr bool kEnableTouchInit = false;

void initPSRAM() {
  // Standalone PSRAM init - not currently used
  if (psramInit()) {
    Serial.println("PSRAM is correctly initialized");
  }
}

void initLCD() {
  Serial.println("[LCD] Starting init");
  Serial.printf("[LCD] Framebuffer bytes: %u\n", (unsigned int)Imagesize);
  Serial.printf("[LCD] Heap before init: %u\n", (unsigned int)ESP.getFreeHeap());

  // Touch is not needed yet; keep it disabled while isolating startup/runtime issues.
  if (kEnableTouchInit) {
    Serial.println("[LCD] Initializing touch");
    touch.begin();
  } else {
    Serial.println("[LCD] Touch init disabled");
  }
  
  // PSRAM Initialize
  if (psramInit()) {
    Serial.println("PSRAM is correctly initialized");
    Serial.printf("[LCD] Free PSRAM: %u\n", (unsigned int)ESP.getFreePsram());
  } else {
    Serial.println("PSRAM not available");
  }
  
  // Allocate image buffer from PSRAM
  if ((BlackImage = (UWORD *)ps_malloc(Imagesize)) == NULL) {
    Serial.println("Failed to allocate image buffer");
    exit(0);
  }
  Serial.printf("[LCD] BlackImage allocated at: 0x%08X\n", (unsigned int)(uintptr_t)BlackImage);
  Serial.printf("[LCD] Heap after alloc: %u\n", (unsigned int)ESP.getFreeHeap());
  Serial.printf("[LCD] Free PSRAM after alloc: %u\n", (unsigned int)ESP.getFreePsram());
  
  // Initialize GPIO
  if (DEV_Module_Init() != 0) {
    Serial.println("GPIO Init Failed!");
  } else {
    Serial.println("GPIO Init successful");
  }
  
  // Initialize LCD
  LCD_1IN28_Init(HORIZONTAL);
  LCD_1IN28_Clear(WHITE);
  
  // Create image cache
  Paint_NewImage((UBYTE *)BlackImage, LCD_1IN28.WIDTH, LCD_1IN28.HEIGHT, 0, WHITE);
  Paint_SetScale(65);
  Paint_SetRotate(ROTATE_0);
  Serial.println("[LCD] Init complete");
}

void displayStartupMessage() {
  // The global BlackImage is already allocated using ps_malloc in initLCD.
  // This check and malloc call are redundant if initLCD has already run successfully.
  // If BlackImage is intended to be a separate buffer for startup, it should be named differently
  // or the global BlackImage should be re-purposed carefully.
  // For now, we'll assume the intent is to use the existing global BlackImage.
  if (BlackImage == NULL) {
    // This block should ideally not be reached if initLCD ran successfully.
    // Using malloc here might conflict with ps_malloc used for the global BlackImage.
    if ((BlackImage = (UWORD *)malloc(LCD_1IN28_HEIGHT * LCD_1IN28_WIDTH * 2)) == NULL) {
      Serial.println("Failed to allocate memory for BlackImage");
      return;
    }
  }

  Paint_NewImage((UBYTE *)BlackImage, LCD_1IN28.WIDTH, LCD_1IN28.HEIGHT, 0, BLACK);
  Paint_SetScale(65);
  Paint_Clear(BLACK);
  
  Paint_DrawString_EN(40, 90, "32Guage", &Font24, BLACK, WHITE);
  Paint_DrawString_EN(40, 120, "Initializing...", &Font16, BLACK, WHITE);
  
  LCD_1IN28_Display(BlackImage);
  delay(1000); 
}
