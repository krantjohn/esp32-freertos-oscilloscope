#pragma once

#include <Arduino.h>

/**
 * @brief 模式 1：软件直接生成正弦波样本帧（纯数学模拟，无需外接飞线）
 * @param buffer 输出缓冲区（长度为 SAMPLE_COUNT = 128）
 * @param freq 正弦波目标频率 (单位: Hz，如 500.0, 759.0, 1000.0)
 * @param vpp 峰峰值电压 (单位: V，如 2.5f)
 * @param voffset 直流偏置电压 (单位: V，如 1.65f)
 */
void generateMockSineWave(uint16_t *buffer, float freq, float vpp = 2.5f, float voffset = 1.65f);

/**
 * @brief 模式 2：硬件引脚输出真实正弦波信号（SPWM 正弦脉宽调制）
 *        通过硬件定时器以高速修改 PWM 占空比查表输出模拟正弦波。
 *        将输出引脚与 ADC_PIN 连接即可进行真实物理信号采集与测量。
 * @param pin 输出 GPIO 引脚（默认可使用 TEST_PIN）
 * @param freq 正弦波输出频率 (单位: Hz，如 623)
 */
void initSinePWM(uint8_t pin, float freq = 623.0f);
