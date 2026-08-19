#include "oscilloscope.h"
#include "config.h"

Measurement measureWaveform(uint16_t *samples) {

    Measurement result;

    // =========================================================================
    // 1. 测量电压峰值与峰峰值
    // =========================================================================
    int minValue = 4095;
    int maxValue = 0;

    for (int i = 0; i < SAMPLE_COUNT; i++) {
        if (samples[i] < minValue) minValue = samples[i];
        if (samples[i] > maxValue) maxValue = samples[i];
    }

    result.voltageMax = (float)maxValue / 4095.0f * 3.3f;
    result.voltageMin = (float)minValue / 4095.0f * 3.3f;
    result.voltagePP  = result.voltageMax - result.voltageMin;

    // 如果信号峰峰值过小（例如小于 100LSB / 0.08V），认为是噪声，不测频率
    if (maxValue - minValue < 100) {
        result.frequency = 0;
        result.period = 0;
        return result;
    }

    // 动态取信号中点作为最佳触发阈值（自适应阈值抗偏置）
    float triggerThreshold = (maxValue + minValue) / 2.0f;

    // =========================================================================
    // 2. 寻找所有上升沿，并进行【线性插值】计算亚采样点时间
    //
    // 原理：原先 edges[i] 是离散整数索引，导致计算出的总点数只能是 118 或 119，
    // 从而导致频率在 10000/(118/9)=762Hz 和 10000/(119/9)=756Hz 之间跳动。
    // 使用线性插值可精确求出过阈值的浮点坐标，精度提升至 0.01 采样点！
    // =========================================================================
    float edgeTimes[SAMPLE_COUNT];
    int edgeCount = 0;

    for (int i = 1; i < SAMPLE_COUNT; i++) {
        if (samples[i - 1] < triggerThreshold && samples[i] >= triggerThreshold) {
            // 线性插值计算精确的过阈值坐标 (fraction 在 0.0 ~ 1.0 之间)
            float fraction = (triggerThreshold - samples[i - 1]) / (float)(samples[i] - samples[i - 1]);
            float exactEdgeTime = (float)(i - 1) + fraction;

            if (edgeCount < SAMPLE_COUNT) {
                edgeTimes[edgeCount++] = exactEdgeTime;
            }
        }
    }

    // =========================================================================
    // 3. 多周期平均 + 平滑滤波计算频率
    // =========================================================================
    static float smoothedFreq = 0.0f;

    if (edgeCount >= 2) {
        float totalSamples = edgeTimes[edgeCount - 1] - edgeTimes[0];
        int periodCount = edgeCount - 1;

        float averagePeriodSamples = totalSamples / (float)periodCount;
        float rawFrequency = (float)SAMPLE_RATE / averagePeriodSamples;

        // 指数平滑移动平均滤波 (EMA) 消除偶发噪声跳动
        if (smoothedFreq <= 1.0f) {
            smoothedFreq = rawFrequency;
        } else {
            smoothedFreq = smoothedFreq * 0.7f + rawFrequency * 0.3f;
        }

        result.frequency = smoothedFreq;
        result.period = 1.0f / result.frequency;
    } else {
        smoothedFreq = 0.0f;
        result.frequency = 0;
        result.period = 0;
    }

    return result;
}
