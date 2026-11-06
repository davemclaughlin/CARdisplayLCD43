// Waveshare ESP32-S3-Touch-LCD-4.3B hardware: CH422G IO expander, 800x480 RGB panel, GT911 touch.
#pragma once
#include <Arduino_GFX_Library.h>

#define LCD_WIDTH  800
#define LCD_HEIGHT 480

extern Arduino_RGB_Display *gfx;

// Resets the LCD and touch controller through the CH422G and turns on the
// backlight.
// Must run before gfx->begin().
bool board_power_init();

// Finds the GT911 on SDA8/SCL9. Returns false if it was not found.
bool touch_init();

// Reads the first touch point, already rotated to match DISPLAY_ROTATION.
bool touch_read(int16_t &x, int16_t &y);
