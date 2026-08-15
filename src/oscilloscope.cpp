#include "oscilloscope.h"
#include "config.h"

Measurement measureWaveform(uint16_t *samples) {

    Measurement result;

    // =========================
    // 1. 测量电压
    // =========================

    int minValue = 4095;
    int maxValue = 0;

    for (int i = 0; i < SAMPLE_COUNT; i++) {

        if (samples[i] < minValue) {
            minValue = samples[i];
        }

        if (samples[i] > maxValue) {
            maxValue = samples[i];
        }
    }

    result.voltageMax =
        (float)maxValue / 4095.0 * 3.3;

    result.voltageMin =
        (float)minValue / 4095.0 * 3.3;

    result.voltagePP =
        result.voltageMax - result.voltageMin;


    // =========================
    // 2. 寻找所有上升沿
    // =========================

    int edges[SAMPLE_COUNT];
    int edgeCount = 0;

    for (int i = 1; i < SAMPLE_COUNT; i++) {

        if (samples[i - 1] < TRIGGER_LEVEL &&
            samples[i] >= TRIGGER_LEVEL) {

            if (edgeCount < SAMPLE_COUNT) {

                edges[edgeCount++] = i;
            }
        }
    }


    // =========================
    // 3. 多周期平均计算频率
    // =========================

    if (edgeCount >= 2) {

        int totalSamples =
            edges[edgeCount - 1] - edges[0];

        int periodCount =
            edgeCount - 1;

        float averagePeriod =
            (float)totalSamples / periodCount;

        result.frequency =
            SAMPLE_RATE / averagePeriod;

    } else {

        result.frequency = 0;
    }


    return result;
}