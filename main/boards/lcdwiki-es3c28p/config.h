#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

// LCDWIKI ES3C28P / ES3N28P - 2.8" ESP32-S3 display module
// https://www.lcdwiki.com/2.8inch_ESP32-S3_Display

#include <driver/gpio.h>
#include <driver/spi_master.h>

#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// ES8311 codec (I2S) + FM8002E amplifier
#define AUDIO_I2S_GPIO_MCLK      GPIO_NUM_4
#define AUDIO_I2S_GPIO_BCLK      GPIO_NUM_5
#define AUDIO_I2S_GPIO_DIN       GPIO_NUM_6   // codec -> ESP32 (microphone)
#define AUDIO_I2S_GPIO_WS        GPIO_NUM_7
#define AUDIO_I2S_GPIO_DOUT      GPIO_NUM_8   // ESP32 -> codec (speaker)
#define AUDIO_CODEC_PA_PIN       GPIO_NUM_1   // amplifier enable, LOW = on
#define AUDIO_CODEC_PA_INVERTED  true

// I2C bus shared by the ES8311 codec and the FT6336G touch controller
#define AUDIO_CODEC_I2C_NUM      I2C_NUM_0
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_16
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_15
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR

#define BOOT_BUTTON_GPIO         GPIO_NUM_0
#define BUILTIN_LED_GPIO         GPIO_NUM_42  // WS2812B RGB LED

// Battery voltage (ADC1 channel 8 = IO9)
#define BATTERY_ADC_UNIT         ADC_UNIT_1
#define BATTERY_ADC_CHANNEL      ADC_CHANNEL_8

// ILI9341V 2.8" IPS, 4-wire SPI
#define LCD_SPI_HOST             SPI3_HOST
#define DISPLAY_SCK_PIN          GPIO_NUM_12
#define DISPLAY_MOSI_PIN         GPIO_NUM_11
#define DISPLAY_MISO_PIN         GPIO_NUM_13
#define DISPLAY_CS_PIN           GPIO_NUM_10
#define DISPLAY_DC_PIN           GPIO_NUM_46
#define DISPLAY_RST_PIN          GPIO_NUM_NC  // shared with ESP32-S3 EN
#define DISPLAY_SPI_SCLK_HZ      (40 * 1000 * 1000)
#define DISPLAY_SPI_MODE         0

#define DISPLAY_BACKLIGHT_PIN    GPIO_NUM_45
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

// Portrait 240x320 (native orientation of the module)
#define DISPLAY_WIDTH            240
#define DISPLAY_HEIGHT           320
#define DISPLAY_MIRROR_X         false
#define DISPLAY_MIRROR_Y         false
#define DISPLAY_SWAP_XY          false
#define DISPLAY_INVERT_COLOR     true
#define DISPLAY_RGB_ORDER        LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X         0
#define DISPLAY_OFFSET_Y         0

// FT6336G capacitive touch (ES3C28P only, ES3N28P has no touch)
#define TOUCH_I2C_ADDR           0x38
#define TOUCH_RST_PIN            GPIO_NUM_18
#define TOUCH_INT_PIN            GPIO_NUM_17

#endif  // _BOARD_CONFIG_H_
