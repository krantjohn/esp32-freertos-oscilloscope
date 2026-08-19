#pragma once

// 引脚配置
#define ADC_PIN 1           // ADC1_CH0 输入引脚 (GPIO 1)
#define TEST_PIN 2          // 测试信号输出引脚 (GPIO 2)
#define OLED_SDA 9          // OLED I2C SDA (GPIO 9)
#define OLED_SCL 8          // OLED I2C SCL (GPIO 8)

#define BUTTON_RUN_STOP 5   // 外部按键 (GPIO 5): 短按 RUN/STOP，长按切换 FFT/时域
#define BUTTON_TIMEBASE 4   // 外部时基切换按键 (GPIO 4)

// 示波器采样参数
#define SAMPLE_COUNT 128
#define SAMPLE_RATE 10000
#define TRIGGER_LEVEL 2048


// 内置测试信号源
#define TEST_FREQ 623
#define TEST_RESOLUTION 8

