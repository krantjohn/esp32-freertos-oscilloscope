#include "sine_gen.h"
#include "config.h"
#include <math.h>

/* =========================================================================
 * 模式 1：纯软件模拟正弦波生成
 * ========================================================================= */
void generateMockSineWave(uint16_t *buffer, float freq, float vpp, float voffset)
{
    static float basePhase = 0.0f;
    float phaseStep = (2.0f * (float)M_PI * freq) / 10000.0f;

    float halfVpp = vpp / 2.0f;

    for (int i = 0; i < SAMPLE_COUNT; i++)
    {
        float currentPhase = basePhase + i * phaseStep;
        float voltage = voffset + halfVpp * sinf(currentPhase);

        if (voltage < 0.0f) voltage = 0.0f;
        if (voltage > 3.3f) voltage = 3.3f;

        uint16_t adcVal = (uint16_t)((voltage / 3.3f) * 4095.0f);

        int noise = (rand() % 17) - 8;
        int noisyVal = (int)adcVal + noise;
        if (noisyVal < 0) noisyVal = 0;
        if (noisyVal > 4095) noisyVal = 4095;

        buffer[i] = (uint16_t)noisyVal;
    }

    basePhase += 0.1f;
    if (basePhase > 2.0f * (float)M_PI)
    {
        basePhase -= 2.0f * (float)M_PI;
    }
}

/* =========================================================================
 * 模式 2：硬件测试信号输出 (使用 ESP32-S3 硬件 LEDC PWM 输出稳定方波)
 * ========================================================================= */
void initSinePWM(uint8_t pin, float freq)
{
    pinMode(pin, OUTPUT);
    ledcSetup(0, (uint32_t)freq, TEST_RESOLUTION);
    ledcAttachPin(pin, 0);
    ledcWrite(0, 128); // 50% 占空比
}

