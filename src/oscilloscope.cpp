#include "oscilloscope.h"
#include "config.h"
#include "sampling.h"
#include <math.h>

#include "dsps_fft2r.h"
#include "dsps_wind_hann.h"

// FFT 内部缓冲区与 Hann 窗表 (16 字节对齐以支持 ESP32 DSP 硬件指令)
__attribute__((aligned(16))) static float hannWindow[FFT_SIZE];
__attribute__((aligned(16))) static float fftBuffer[FFT_SIZE * 2]; // 复数交错数组 [Re0, Im0, Re1, Im1, ...]
static bool dspInitialized = false;

void oscilloscopeInit() {
    if (!dspInitialized) {
        dsps_fft2r_init_fc32(NULL, FFT_SIZE);
        dsps_wind_hann_f32(hannWindow, FFT_SIZE);
        dspInitialized = true;
    }
}

Measurement measureWaveform(uint16_t *samples, uint32_t sampleRate) {
    Measurement result;

    if (!dspInitialized) {
        oscilloscopeInit();
    }

    // 初始化 FFT 结果
    result.fftPeakFreq = 0.0f;
    for (int k = 0; k < FFT_BINS; k++) {
        result.fftMagnitudes[k] = 0.0f;
    }

    // =========================================================================
    // 1. 测量电压峰值、峰峰值、直流偏置(Vavg)与真有效值(Vrms)
    // =========================================================================
    int minValue = 4095;
    int maxValue = 0;
    float sumVoltage = 0.0f;
    float sumSqVoltage = 0.0f;

    for (int i = 0; i < SAMPLE_COUNT; i++) {
        if (samples[i] < minValue) minValue = samples[i];
        if (samples[i] > maxValue) maxValue = samples[i];

        float v = (float)samples[i] / 4095.0f * 3.3f;
        sumVoltage += v;
        sumSqVoltage += v * v;
    }

    result.voltageMax = (float)maxValue / 4095.0f * 3.3f;
    result.voltageMin = (float)minValue / 4095.0f * 3.3f;
    result.voltagePP  = result.voltageMax - result.voltageMin;
    result.voltageAvg = sumVoltage / (float)SAMPLE_COUNT;
    result.voltageRMS = sqrtf(sumSqVoltage / (float)SAMPLE_COUNT);

    // =========================================================================
    // 2. 自适应施密特触发器反馈
    // =========================================================================
    if (maxValue - minValue >= 100) {
        uint16_t center = (uint16_t)((maxValue + minValue) / 2);
        uint16_t hyst   = (uint16_t)((maxValue - minValue) / 8);
        setTriggerThreshold(center, hyst);
    }

    // 如果信号幅值过小（< 100 LSB / 0.08V），视为纯噪声
    if (maxValue - minValue < 100) {
        result.frequency = 0;
        result.period = 0;
        result.dutyCycle = 0;
        return result;
    }

    // 动态中点阈值
    float triggerThreshold = (float)(maxValue + minValue) / 2.0f;

    // =========================================================================
    // 3. 寻找上升沿与下降沿（亚采样点线性插值）
    // =========================================================================
    float riseEdges[SAMPLE_COUNT];
    int riseCount = 0;

    float fallEdges[SAMPLE_COUNT];
    int fallCount = 0;

    for (int i = 1; i < SAMPLE_COUNT; i++) {
        // 上升沿检测
        if (samples[i - 1] < triggerThreshold && samples[i] >= triggerThreshold) {
            float frac = (triggerThreshold - samples[i - 1]) / (float)(samples[i] - samples[i - 1]);
            if (riseCount < SAMPLE_COUNT) {
                riseEdges[riseCount++] = (float)(i - 1) + frac;
            }
        }
        // 下降沿检测
        else if (samples[i - 1] >= triggerThreshold && samples[i] < triggerThreshold) {
            float frac = (triggerThreshold - samples[i - 1]) / (float)(samples[i] - samples[i - 1]);
            if (fallCount < SAMPLE_COUNT) {
                fallEdges[fallCount++] = (float)(i - 1) + frac;
            }
        }
    }

    // =========================================================================
    // 4. ESP-DSP 硬件加速 FFT 频谱分析与抛物线亚频点插值
    // =========================================================================
    float avgAdc = (float)sumVoltage * (4095.0f / 3.3f) / (float)SAMPLE_COUNT;

    for (int i = 0; i < FFT_SIZE; i++) {
        fftBuffer[i * 2 + 0] = ((float)samples[i] - avgAdc) * hannWindow[i];
        fftBuffer[i * 2 + 1] = 0.0f;
    }

    dsps_fft2r_fc32(fftBuffer, FFT_SIZE);
    dsps_bit_rev_fc32(fftBuffer, FFT_SIZE);

    float maxMag = 0.0f;
    int peakBin = 0;

    // 清零直流分量 (k=0)
    result.fftMagnitudes[0] = 0.0f;

    for (int k = 1; k < FFT_BINS; k++) {
        float re = fftBuffer[k * 2 + 0];
        float im = fftBuffer[k * 2 + 1];
        float mag = sqrtf(re * re + im * im);
        result.fftMagnitudes[k] = mag;

        // 寻找交流主峰
        if (mag > maxMag) {
            maxMag = mag;
            peakBin = k;
        }
    }

    // 仅当检测到真实交流信号峰值时进行归一化和插值
    if (maxMag > 30.0f && peakBin > 0) {
        // 抛物线高精度插值：利用主峰相邻两个频点的幅值修正真实频点
        float deltaBin = 0.0f;
        if (peakBin > 0 && peakBin < FFT_BINS - 1) {
            float alpha = result.fftMagnitudes[peakBin - 1];
            float beta  = result.fftMagnitudes[peakBin];
            float gamma = result.fftMagnitudes[peakBin + 1];
            float denom = 2.0f * beta - alpha - gamma;
            if (denom > 1e-5f) {
                deltaBin = 0.5f * (alpha - gamma) / denom;
            }
        }
        result.fftPeakFreq = ((float)peakBin + deltaBin) * ((float)sampleRate / (float)FFT_SIZE);

        // 归一化频谱幅值到 0.0 ~ 1.0
        for (int k = 1; k < FFT_BINS; k++) {
            result.fftMagnitudes[k] /= maxMag;
        }
    } else {
        result.fftPeakFreq = 0.0f;
        for (int k = 0; k < FFT_BINS; k++) {
            result.fftMagnitudes[k] = 0.0f;
        }
    }

    // =========================================================================
    // 5. 综合多级频率与占空比计算 (结合 Core 0 硬件触发流与时域插值)
    // =========================================================================
    float rawFrequency = 0.0f;
    float rawDuty = 50.0f;

    if (riseCount >= 2) {
        // 第一级：128 点帧内完整多周期上升沿平均
        float totalSamples = riseEdges[riseCount - 1] - riseEdges[0];
        float avgPeriod = totalSamples / (float)(riseCount - 1);
        rawFrequency = (float)sampleRate / avgPeriod;

        // 占空比计算
        float totalDuty = 0.0f;
        int validCycles = 0;
        for (int r = 0; r < riseCount - 1; r++) {
            float rStart = riseEdges[r];
            float rEnd   = riseEdges[r + 1];
            float periodLen = rEnd - rStart;
            for (int f = 0; f < fallCount; f++) {
                if (fallEdges[f] > rStart && fallEdges[f] < rEnd) {
                    totalDuty += ((fallEdges[f] - rStart) / periodLen) * 100.0f;
                    validCycles++;
                    break;
                }
            }
        }
        if (validCycles > 0) rawDuty = totalDuty / (float)validCycles;
    }
    else if (fallCount >= 2) {
        // 帧内下降沿多周期平均
        float totalSamples = fallEdges[fallCount - 1] - fallEdges[0];
        float avgPeriod = totalSamples / (float)(fallCount - 1);
        rawFrequency = (float)sampleRate / avgPeriod;
    }

    // 第二级：帧内边沿不足时（如 80kSPS/50kSPS 档位 128 点仅有 1 个边沿），
    // 直接读取 Core 0 连续硬件触发跨周期测频结果
    if (rawFrequency <= 1.0f || rawFrequency > (float)sampleRate * 0.49f) {
        float hwFreq = getHardwareTriggerFrequency();
        if (hwFreq >= 1.0f && hwFreq <= (float)sampleRate * 0.49f) {
            rawFrequency = hwFreq;
            rawDuty = getHardwareTriggerDuty();
        } else if (result.fftPeakFreq > 5.0f) {
            // 第三级：FFT 频谱插值峰值保底
            rawFrequency = result.fftPeakFreq;
        }
    }

    // EMA 平滑滤波
    static float smoothedFreq = 0.0f;
    static float smoothedDuty = 50.0f;

    if (rawFrequency > 1.0f) {
        if (smoothedFreq <= 1.0f || fabsf(smoothedFreq - rawFrequency) > smoothedFreq * 0.4f) {
            smoothedFreq = rawFrequency;
        } else {
            smoothedFreq = smoothedFreq * 0.7f + rawFrequency * 0.3f;
        }
        result.frequency = smoothedFreq;
        result.period = (result.frequency > 0.0f) ? (1.0f / result.frequency) : 0.0f;

        smoothedDuty = smoothedDuty * 0.7f + rawDuty * 0.3f;
        result.dutyCycle = smoothedDuty;
    } else {
        smoothedFreq = 0.0f;
        result.frequency = 0.0f;
        result.period = 0.0f;
        result.dutyCycle = 0.0f;
    }

    return result;
}