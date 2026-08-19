#include "sampling.h"
#include "config.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <driver/adc.h>
#include <esp_err.h>

#define DMA_MASTER_SAMPLE_RATE 80000 // DMA 硬件主采样率固定 80kSPS 全速运行

/* =========================================================================
 * 时基预设与数字抽取倍率 (工业数字示波器 DSO 架构)
 * ========================================================================= */
const TimebaseConfig TIMEBASE_PRESETS[TIMEBASE_COUNT] = {
    {"80kSPS",  80000,  1}, // 128点 = 1.6ms
    {"40kSPS",  40000,  2}, // 128点 = 3.2ms
    {"20kSPS",  20000,  4}, // 128点 = 6.4ms
    {"10kSPS",  10000,  8}, // 128点 = 12.8ms
    {"5kSPS",    5000, 16}, // 128点 = 25.6ms
    {"2kSPS",    2000, 40}  // 128点 = 64.0ms
};

static volatile int s_timebaseIndex = 3; // 默认 10kSPS (索引 3)
static volatile uint32_t s_currentSampleRate = 10000;
static volatile uint32_t s_decimationFactor = 8;
static volatile bool s_isRunning = true;

/* =========================================================================
 * 硬件连续采样频率 & 占空比实时统计
 * ========================================================================= */
static volatile float s_hwFrequency = 0.0f;
static volatile float s_hwDuty = 50.0f;
static uint32_t s_totalSampleCounter = 0;
static uint32_t s_lastTriggerSampleCount = 0;
static uint32_t s_currentHighSampleCount = 0;

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

static QueueHandle_t waveformQueue = NULL;

/* =========================================================================
 * 触发状态机内部变量
 * ========================================================================= */
static volatile SamplingState samplingState = STATE_ARM_TRIGGER;
static volatile int sampleIndex = 0;
static volatile uint32_t triggerTicks = 0;

static volatile uint16_t triggerHighLevel = TRIGGER_LEVEL + 150;
static volatile uint16_t triggerLowLevel  = TRIGGER_LEVEL - 150;

/* =========================================================================
 * FreeRTOS 采样任务 (运行在 Core 0，零中断损耗高速处理 DMA 流)
 * ========================================================================= */
static void samplingTask(void *pvParameters)
{
    uint8_t dmaBuf[512];
    uint32_t bytesRead = 0;
    uint32_t sampleDecimCount = 0;

    while (1)
    {
        // 从 DMA 环形缓冲区连续读取数据
        esp_err_t ret = adc_digi_read_bytes(dmaBuf, sizeof(dmaBuf), &bytesRead, pdMS_TO_TICKS(50));
        if (ret == ESP_OK && bytesRead > 0)
        {
            uint32_t sampleCount = bytesRead / sizeof(adc_digi_output_data_t);
            adc_digi_output_data_t *p = (adc_digi_output_data_t *)dmaBuf;

            for (uint32_t i = 0; i < sampleCount; i++)
            {
                s_totalSampleCounter++;

                // 提取 12 位真实 ADC 数据 (0 ~ 4095)
                uint16_t current = p[i].type2.data;

                // 统计高电平点数以计算精确占空比
                if (current >= triggerHighLevel)
                {
                    s_currentHighSampleCount++;
                }

                // 自动触发超时保护：200ms 强制刷新一帧 (80kSPS × 0.2s = 16000 点)
                const uint32_t autoTriggerMaxTicks = 16000;
                triggerTicks++;

                if (samplingState == STATE_ARM_TRIGGER)
                {
                    if (current < triggerLowLevel)
                    {
                        samplingState = STATE_WAIT_TRIGGER;
                    }
                }
                else if (samplingState == STATE_WAIT_TRIGGER)
                {
                    if (current >= triggerHighLevel)
                    {
                        // 真实上升沿触发！计算跨采样点的精确频率与占空比
                        uint32_t periodTicks = s_totalSampleCounter - s_lastTriggerSampleCount;
                        if (periodTicks >= 4 && periodTicks <= DMA_MASTER_SAMPLE_RATE)
                        {
                            float instFreq = (float)DMA_MASTER_SAMPLE_RATE / (float)periodTicks;
                            float instDuty = (float)s_currentHighSampleCount / (float)periodTicks * 100.0f;
                            
                            if (instDuty >= 1.0f && instDuty <= 99.0f)
                            {
                                s_hwDuty = s_hwDuty * 0.7f + instDuty * 0.3f;
                            }
                            if (instFreq >= 1.0f && instFreq <= (float)DMA_MASTER_SAMPLE_RATE * 0.49f)
                            {
                                if (s_hwFrequency <= 1.0f || fabsf(s_hwFrequency - instFreq) > s_hwFrequency * 0.4f)
                                {
                                    s_hwFrequency = instFreq;
                                }
                                else
                                {
                                    s_hwFrequency = s_hwFrequency * 0.7f + instFreq * 0.3f;
                                }
                            }
                        }
                        s_lastTriggerSampleCount = s_totalSampleCounter;
                        s_currentHighSampleCount = 0;

                        if (s_isRunning)
                        {
                            captureBuffer[0] = current;
                            sampleIndex = 1;
                            sampleDecimCount = 0;
                            triggerTicks = 0;
                            samplingState = STATE_COLLECTING;
                        }
                    }
                }
                else if (samplingState == STATE_COLLECTING)
                {
                    if (s_isRunning)
                    {
                        sampleDecimCount++;
                        if (sampleDecimCount >= s_decimationFactor)
                        {
                            sampleDecimCount = 0;
                            captureBuffer[sampleIndex++] = current;

                            if (sampleIndex >= SAMPLE_COUNT)
                            {
                                uint16_t *completedBuffer = (uint16_t *)captureBuffer;
                                captureBuffer = readyBuffer;
                                readyBuffer = completedBuffer;

                                if (waveformQueue != NULL)
                                {
                                    xQueueOverwrite(waveformQueue, &completedBuffer);
                                }

                                sampleIndex = 0;
                                triggerTicks = 0;
                                samplingState = STATE_ARM_TRIGGER;
                            }
                        }
                    }
                    else
                    {
                        samplingState = STATE_ARM_TRIGGER;
                    }
                }

                // 超时强制采集一帧
                if (triggerTicks >= autoTriggerMaxTicks && samplingState != STATE_COLLECTING)
                {
                    if (s_isRunning)
                    {
                        captureBuffer[0] = current;
                        sampleIndex = 1;
                        sampleDecimCount = 0;
                        triggerTicks = 0;
                        samplingState = STATE_COLLECTING;
                    }
                }
            }
        }

        // 短暂让出 CPU，避免 Core 0 任务饥饿
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* =========================================================================
 * 采样模块初始化
 * ========================================================================= */
void samplingInit()
{
    waveformQueue = xQueueCreate(1, sizeof(uint16_t *));

    // 1. 初始化 ADC DMA 驱动 (4096 字节大缓冲区，每次中断 256 次转换)
    adc_digi_init_config_t adc_dma_config = {};
    adc_dma_config.max_store_buf_size = 4096;
    adc_dma_config.conv_num_each_intr = 256;
    adc_dma_config.adc1_chan_mask = (1 << ADC1_CHANNEL_0);
    adc_dma_config.adc2_chan_mask = 0;
    
    esp_err_t err = adc_digi_initialize(&adc_dma_config);
    if (err != ESP_OK)
    {
        Serial.printf("[ADC DMA] adc_digi_initialize failed: 0x%x\n", err);
    }

    // 2. 配置并启动 DMA 控制器 (80,000 Hz 硬件主频稳定常驻运行)
    adc_digi_pattern_config_t adc_pattern = {};
    adc_pattern.atten = ADC_ATTEN_DB_12;
    adc_pattern.channel = ADC1_CHANNEL_0; // GPIO 1
    adc_pattern.unit = 0;                 // 0 表示 ADC1
    adc_pattern.bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

    adc_digi_configuration_t dig_cfg = {};
    dig_cfg.conv_limit_en = false;
    dig_cfg.conv_limit_num = 250;
    dig_cfg.sample_freq_hz = DMA_MASTER_SAMPLE_RATE;
    dig_cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    dig_cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
    dig_cfg.pattern_num = 1;
    dig_cfg.adc_pattern = &adc_pattern;

    err = adc_digi_controller_configure(&dig_cfg);
    if (err != ESP_OK)
    {
        Serial.printf("[ADC DMA] configure failed: 0x%x\n", err);
    }

    err = adc_digi_start();
    if (err != ESP_OK)
    {
        Serial.printf("[ADC DMA] start failed: 0x%x\n", err);
    }

    s_currentSampleRate = TIMEBASE_PRESETS[s_timebaseIndex].sampleRate;
    s_decimationFactor = TIMEBASE_PRESETS[s_timebaseIndex].decimation;

    // 3. 创建 Core 0 独立采样处理任务
    xTaskCreatePinnedToCore(
        samplingTask,
        "samplingTask",
        4096,
        NULL,
        5,
        NULL,
        0
    );

    // 4. 内置测试方波输出 (LEDC PWM 623Hz)
    pinMode(TEST_PIN, OUTPUT);
    ledcSetup(0, TEST_FREQ, TEST_RESOLUTION);
    ledcAttachPin(TEST_PIN, 0);
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

/* =========================================================================
 * 动态自适应设置触发电平
 * ========================================================================= */
void setTriggerThreshold(uint16_t centerLevel, uint16_t hysteresis)
{
    if (hysteresis < 20)  hysteresis = 20;
    if (hysteresis > 300) hysteresis = 300;

    if (centerLevel >= hysteresis && (centerLevel + hysteresis) <= 4095)
    {
        triggerHighLevel = centerLevel + hysteresis;
        triggerLowLevel  = centerLevel - hysteresis;
    }
}

/* =========================================================================
 * RUN / STOP 状态控制
 * ========================================================================= */
void toggleRunStop()
{
    s_isRunning = !s_isRunning;
}

bool isRunning()
{
    return s_isRunning;
}

void setRunStop(bool run)
{
    s_isRunning = run;
}

/* =========================================================================
 * 时基档位控制 (零开销数字抽取切换，零硬件中断，绝对不重启不假死)
 * ========================================================================= */
void nextTimebase()
{
    s_timebaseIndex = (s_timebaseIndex + 1) % TIMEBASE_COUNT;
    s_currentSampleRate = TIMEBASE_PRESETS[s_timebaseIndex].sampleRate;
    s_decimationFactor = TIMEBASE_PRESETS[s_timebaseIndex].decimation;
    sampleIndex = 0;
}

int getTimebaseIndex()
{
    return s_timebaseIndex;
}

uint32_t getCurrentSampleRate()
{
    return s_currentSampleRate;
}

const char* getCurrentTimebaseName()
{
    return TIMEBASE_PRESETS[s_timebaseIndex].name;
}

/* =========================================================================
 * 获取硬件连续测频结果
 * ========================================================================= */
float getHardwareTriggerFrequency()
{
    return s_hwFrequency;
}

float getHardwareTriggerDuty()
{
    return s_hwDuty;
}