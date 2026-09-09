#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"

/*
 * Waveshare ESP32-S3-Touch-LCD-1.54
 *
 * MCU:
 *   ESP32-S3R8
 *
 * Display:
 *   ST7789
 *   240 x 240
 *   RGB565
 */

#define SPARKY_LCD_HOST              SPI3_HOST

#define SPARKY_LCD_H_RES             240
#define SPARKY_LCD_V_RES             240

#define SPARKY_LCD_SCLK_GPIO         GPIO_NUM_38
#define SPARKY_LCD_MOSI_GPIO         GPIO_NUM_39
#define SPARKY_LCD_CS_GPIO           GPIO_NUM_21
#define SPARKY_LCD_DC_GPIO           GPIO_NUM_45
#define SPARKY_LCD_RST_GPIO          GPIO_NUM_40
#define SPARKY_LCD_BL_GPIO           GPIO_NUM_46

#define SPARKY_LCD_PIXEL_CLOCK_HZ    (40 * 1000 * 1000)


/* ---- Touch: CST816T ---- */

#define SPARKY_TOUCH_SDA_GPIO        GPIO_NUM_42
#define SPARKY_TOUCH_SCL_GPIO        GPIO_NUM_41
#define SPARKY_TOUCH_INT_GPIO        GPIO_NUM_48
#define SPARKY_TOUCH_RST_GPIO        GPIO_NUM_47

#define SPARKY_TOUCH_I2C_ADDR        0x15
#define SPARKY_TOUCH_I2C_SPEED_HZ    400000