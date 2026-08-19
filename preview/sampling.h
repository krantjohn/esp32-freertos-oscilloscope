#pragma once

#include <Arduino.h>

/**
 * @brief 初始化硬件定时器与 ADC 采样
 */
void samplingInit();

/**
 * @brief 获取一组完整采样的波形数据（非阻塞）
 * @param samples 指向波形数据缓冲区的指针的指针
 * @return bool true: 成功获取新一帧波形; false: 无新波形
 */
bool getWaveform(uint16_t **samples);
