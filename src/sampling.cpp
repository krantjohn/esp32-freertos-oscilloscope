#include "sampling.h"
#include "config.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

static uint16_t samplesBufferA[SAMPLE_COUNT];
static uint16_t samplesBufferB[SAMPLE_COUNT];

static uint16_t *captureBuffer = samplesBufferA;
static uint16_t *readyBuffer = samplesBufferB;

static SemaphoreHandle_t sampleSemaphore;
static QueueHandle_t waveformQueue;

static hw_timer_t *timer = NULL;


/* =========================
 * 定时器中断
 * ========================= */

void IRAM_ATTR onTimer()
{
    BaseType_t higherPriorityTaskWoken = pdFALSE;

    xSemaphoreGiveFromISR(
        sampleSemaphore,
        &higherPriorityTaskWoken
    );

    if (higherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}


/* =========================
 * 采样任务
 * ========================= */

static void samplingTask(void *parameter)
{
    while (true)
    {
        int index = 0;
        bool triggered = false;

        uint16_t previous = analogRead(ADC_PIN);

        unsigned long triggerStart = millis();


        /* =========================
         * 寻找触发点
         * ========================= */

        while (!triggered)
        {
            xSemaphoreTake(
                sampleSemaphore,
                portMAX_DELAY
            );

            uint16_t current = analogRead(ADC_PIN);


            if (previous < TRIGGER_LEVEL &&
                current >= TRIGGER_LEVEL)
            {
                triggered = true;

                captureBuffer[0] = current;

                index = 1;
            }

            previous = current;


            /* Auto Trigger */
            if (millis() - triggerStart > 50)
            {
                break;
            }
        }


        /* =========================
         * 采集 128 个点
         * ========================= */

        while (index < SAMPLE_COUNT)
        {
            xSemaphoreTake(
                sampleSemaphore,
                portMAX_DELAY
            );

            captureBuffer[index] =
                analogRead(ADC_PIN);

            index++;
        }


        /* =========================
         * 双缓冲交换
         * ========================= */

        uint16_t *completedBuffer =
            captureBuffer;

        captureBuffer =
            readyBuffer;

        readyBuffer =
            completedBuffer;


        /* =========================
         * 把完成的 buffer 放入队列
         * ========================= */

        xQueueSend(
            waveformQueue,
            &completedBuffer,
            portMAX_DELAY
        );
    }
}


/* =========================
 * 初始化
 * ========================= */

void samplingInit()
{
    analogReadResolution(12);

    pinMode(ADC_PIN, INPUT);


    /* 二值信号量 */

    sampleSemaphore =
        xSemaphoreCreateBinary();


    /* 波形队列 */

    waveformQueue =
        xQueueCreate(
            2,
            sizeof(uint16_t *)
        );


    /* =========================
     * 硬件定时器
     * ========================= */

    timer = timerBegin(
        0,
        80,
        true
    );

    timerAttachInterrupt(
        timer,
        &onTimer,
        true
    );

    timerAlarmWrite(
        timer,
        1000000 / SAMPLE_RATE,
        true
    );

    timerAlarmEnable(timer);


    /* =========================
     * 测试信号
     * ========================= */

    pinMode(TEST_PIN, OUTPUT);

    ledcAttachPin(
        TEST_PIN,
        0
    );

    ledcSetup(
        0,
        TEST_FREQ,
        TEST_RESOLUTION
    );

    ledcWrite(
        0,
        128
    );


    /* =========================
     * 创建采样任务
     * ========================= */

    xTaskCreate(
        samplingTask,
        "SamplingTask",
        4096,
        NULL,
        5,
        NULL
    );
}


/* =========================
 * 获取一组完整波形
 * ========================= */

bool getWaveform(uint16_t **samples)
{
    return (
        xQueueReceive(
            waveformQueue,
            samples,
            0
        ) == pdTRUE
    );
}