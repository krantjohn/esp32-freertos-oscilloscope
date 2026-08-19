#pragma once

#include <Arduino.h>

struct Measurement {
    float frequency;
    float period;

    float voltageMax;
    float voltageMin;
    float voltagePP;
};

/**
 * @brief 测量波形参数（电压、频率、周期）
 * @param samples 采样点数组指针
 * @return Measurement 测量结果结构体
 */
Measurement measureWaveform(uint16_t *samples);
