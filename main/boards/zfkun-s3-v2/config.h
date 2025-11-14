#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_INPUT_REFERENCE    true

#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_13
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_11
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_12
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_10
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_9

#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_1
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_2
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR
#define AUDIO_CODEC_ES7210_ADDR  0x82

#define AUDIO_PA_EN_GPIO GPIO_NUM_8

#define BUILTIN_LED_GPIO  GPIO_NUM_48
#define BOOT_BUTTON_GPIO  GPIO_NUM_0
#define USER_BUTTON_GPIO  GPIO_NUM_38

#define DISPLAY_WIDTH   128

#if CONFIG_OLED_SSD1306_128X32
#define DISPLAY_HEIGHT  32
#elif CONFIG_OLED_SSD1306_128X64
#define DISPLAY_HEIGHT  64
#elif CONFIG_OLED_SH1106_128X64
#define DISPLAY_HEIGHT  64
#define SH1106
#else
#error "未选择 OLED 屏幕类型"
#endif

#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true

// SD卡 (1线模式)
#ifdef CONFIG_SD_ENABLE
#define SD_CARD_CLK_GPIO GPIO_NUM_21
#define SD_CARD_CMD_GPIO GPIO_NUM_47
#define SD_CARD_D0_GPIO  GPIO_NUM_14

#define SD_CARD_DETECT_GPIO GPIO_NUM_NC
#define SD_CARD_BASE_PATH   "/sdcard"
#endif

// 舵机 (SG90)
#ifdef CONFIG_SERVO_ENABLE
#define SERVO_0_GPIO GPIO_NUM_4           // 舵机0
#define SERVO_1_GPIO GPIO_NUM_5           // 舵机1
#define SERVO_2_GPIO GPIO_NUM_6           // 舵机2
#define SERVO_3_GPIO GPIO_NUM_7           // 舵机3

#define SERVO_MIN_WIDTH_US  500           // 最小脉宽（微秒）对应0度
#define SERVO_MAX_WIDTH_US  2500          // 最大脉宽（微秒）对应180度
#define SERVO_MIN_ANGLE     0             // 最小角度
#define SERVO_MAX_ANGLE     180           // 最大角度
#define SERVO_DEFAULT_ANGLE 90            // 默认中心位置
#endif

#endif // _BOARD_CONFIG_H_
