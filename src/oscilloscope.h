#pragma once

#include <Arduino.h>

#define FFT_SIZE 128
#define FFT_BINS (FFT_SIZE / 2) // 64 个频谱柱

struct Measurement {
    float frequency;     // 频率 (Hz)
    float period;        // 周期 (s)
    float dutyCycle;     // 占空比 (%)

    float voltageMax;    // 最大电压 (V)
    float voltageMin;    // 最小电压 (V)
    float voltagePP;     // 峰峰值电压 (V)
    float voltageAvg;    // 平均电压 (V)
    float voltageRMS;    // 均方根/有效值电压 (V)

    float fftPeakFreq;             // FFT 主频峰值 (Hz)
    float fftMagnitudes[FFT_BINS]; // 64 频点归一化幅值 (0.0 ~ 1.0)
};

/**
 * @brief 初始化 DSP 与 FFT 旋转因子表
 */
void oscilloscopeInit();

/**
 * @brief 波形特征分析与 FFT 计算
 * @param samples 128 采样点
 * @param sampleRate 当前采样率 (Hz)
 */
Measurement measureWaveform(uint16_t *samples, uint32_t sampleRate);
