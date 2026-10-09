#pragma once

#include <driver/gpio.h>

#define POCKET_BOARD_ID "seeed-xiao-esp32s3-pocket-ai"
#define TFT_CS GPIO_NUM_1
#define TFT_DC GPIO_NUM_2
#define TFT_RST GPIO_NUM_3
#define POCKET_BUTTON GPIO_NUM_4
#define I2S_BCLK GPIO_NUM_5
#define I2S_WS GPIO_NUM_6
#define TFT_SCK GPIO_NUM_7
// GPIO8/D9 is deliberately unused. No battery ADC or backlight GPIO.
#define TFT_MOSI GPIO_NUM_9
#define I2S_MIC_DATA_IN GPIO_NUM_43
#define I2S_SPEAKER_DATA_OUT GPIO_NUM_44
#define TFT_WIDTH 240
#define TFT_HEIGHT 280
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 20
#define TFT_SPI_HZ (20 * 1000 * 1000)
#define POCKET_SAMPLE_RATE 48000
#define POCKET_DEFAULT_VOLUME 15
#define POCKET_MAX_VOLUME 30
