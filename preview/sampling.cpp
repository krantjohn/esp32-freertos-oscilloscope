#include "sampling.h"
#include "config.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

/* =========================================================================
 * 采样状态机定义（三态施密特触发器，抗噪声与抖动）
 * ========================================================================= */
enum SamplingState {
    STATE_ARM_TRIGGER,    // 等待信号跌落至低阈值（武装状态，防止噪声）
    STATE_WAIT_TRIGGER,   // 等待信号冲破高阈值（真实上升沿触发）
    STATE_COLLECTING      // 连续采集 128 点
};

/* =========================================================================
 * 双缓冲区设计（Ping-Pong Buffer）
 * ========================================================================= */
static uint16_t samplesBufferA[SAMPLE_COUNT];
static uint16_t samplesBufferB[SAMPLE_COUNT];

static volatile uint16_t *captureBuffer = samplesBufferA;
static volatile uint16_t *readyBuffer   = samplesBufferB;

/* =========================================================================
 * 同步与硬件外设句柄
 * ========================================================================= */
static QueueHandle_t waveformQueue = NULL;
static hw_timer_t *timer = NULL;

/* =========================================================================
 * ISR 状态机内部变量
 * ========================================================================= */
static volatile SamplingState samplingState = STATE_ARM_TRIGGER;
static volatile int sampleIndex = 0;
static volatile uint32_t triggerTicks = 0;

// 施密特触发迟滞窗口 (Hysteresis: ±150 LSB)
#define TRIGGER_HYSTERESIS   150
#define TRIGGER_HIGH_LEVEL   (TRIGGER_LEVEL + TRIGGER_HYSTERESIS) // 2048 + 150 = 2198
#define TRIGGER_LOW_LEVEL    (TRIGGER_LEVEL - TRIGGER_HYSTERESIS) // 2048 - 150 = 1898

// 自动触发超时计数值：50ms / 100us = 500 个中断周期
#define AUTO_TRIGGER_MAX_TICKS (50 * (SAMPLE_RATE / 1000))


/* =========================================================================
 * 硬件定时器中断服务函数 (ISR)
 * ========================================================================= */
void IRAM_ATTR onTimer()
{
    uint16_t current = analogRead(ADC_PIN);
    BaseType_t higherPriorityTaskWoken = pdFALSE;

    triggerTicks++;

    // 自动触发超时保护：若信号长时间未满足触发条件，强制开始采集，防止画面冻结
    if (triggerTicks >= AUTO_TRIGGER_MAX_TICKS && samplingState != STATE_COLLECTING)
    {
        captureBuffer[0] = current;
        sampleIndex = 1;
        triggerTicks = 0;
        samplingState = STATE_COLLECTING;
    }
    else if (samplingState == STATE_ARM_TRIGGER)
    {
        // 施密特第一步：等待信号低于低门限，确认为真实低电平
        if (current < TRIGGER_LOW_LEVEL)
        {
            samplingState = STATE_WAIT_TRIGGER;
        }
    }
    else if (samplingState == STATE_WAIT_TRIGGER)
    {
        // 施密特第二步：信号跨越到高门限，确认为真实上升沿！
        if (current >= TRIGGER_HIGH_LEVEL)
        {
            captureBuffer[0] = current;
            sampleIndex = 1;
            triggerTicks = 0;
            samplingState = STATE_COLLECTING;
        }
    }
    else if (samplingState == STATE_COLLECTING)
    {
        captureBuffer[sampleIndex++] = current;

        // 采满 128 点，完成一帧
        if (sampleIndex >= SAMPLE_COUNT)
        {
            // 双缓冲区指针交换
            uint16_t *completedBuffer = (uint16_t *)captureBuffer;
            captureBuffer = readyBuffer;
            readyBuffer = completedBuffer;

            // 覆盖写入队列
            if (waveformQueue != NULL)
            {
                xQueueOverwriteFromISR(
                    waveformQueue,
                    &completedBuffer,
                    &higherPriorityTaskWoken
                );
            }

            // 重置状态机，开始重新武装（ARM）以寻找下一帧的干净上升沿
            sampleIndex = 0;
            triggerTicks = 0;
            samplingState = STATE_ARM_TRIGGER;

            if (higherPriorityTaskWoken)
            {
                portYIELD_FROM_ISR();
            }
        }
    }
}


/* =========================================================================
 * 采样模块初始化
 * ========================================================================= */
void samplingInit()
{
    analogReadResolution(12);
    pinMode(ADC_PIN, INPUT);

    waveformQueue = xQueueCreate(1, sizeof(uint16_t *));

    // 硬件定时器配置 (100us)
    timer = timerBegin(0, 80, true);
    timerAttachInterrupt(timer, &onTimer, true);
    timerAlarmWrite(timer, 1000000 / SAMPLE_RATE, true);
    timerAlarmEnable(timer);

    // 测试方波信号输出 (LEDC PWM)
    pinMode(TEST_PIN, OUTPUT);
    ledcAttachPin(TEST_PIN, 0);
    ledcSetup(0, TEST_FREQ, TEST_RESOLUTION);
    ledcWrite(0, 128);
}


/* =========================================================================
 * 获取一组完整波形
 * ========================================================================= */
bool getWaveform(uint16_t **samples)
{
    if (waveformQueue == NULL)
    {
        return false;
    }

    return (xQueueReceive(waveformQueue, samples, 0) == pdTRUE);
}
