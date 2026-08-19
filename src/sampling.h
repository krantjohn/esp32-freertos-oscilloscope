#pragma once

#include <Arduino.h>

// 时基与采样率档位配置
struct TimebaseConfig {
    const char *name;      // 显示名称，例如 "80kSPS", "40kSPS"
    uint32_t sampleRate;   // 采样率 (Hz)
    uint32_t decimation;   // 硬件抽取系数 (基于 80kSPS DMA 主频)
};

#define TIMEBASE_COUNT 6
extern const TimebaseConfig TIMEBASE_PRESETS[TIMEBASE_COUNT];

/**
 * @brief 初始化 ADC DMA 连续采样驱动与后台任务
 */
void samplingInit();

/**
 * @brief 获取一组完整采样的波形数据（非阻塞）
 * @param samples 指向波形数据缓冲区的指针的指针
 * @return bool true: 成功获取新一帧波形; false: 无新波形
 */
bool getWaveform(uint16_t **samples);

/**
 * @brief 动态设置施密特触发中心电平与迟滞窗口
 */
void setTriggerThreshold(uint16_t centerLevel, uint16_t hysteresis);

/**
 * @brief 切换或查询 RUN / STOP 状态
 */
void toggleRunStop();
bool isRunning();
void setRunStop(bool run);

/**
 * @brief 时基切换与查询接口
 */
void nextTimebase();
int getTimebaseIndex();
uint32_t getCurrentSampleRate();
const char* getCurrentTimebaseName();

/**
 * @brief 获取 Core 0 DMA 连续硬件触发测频结果 (高精度跨周期追踪)
 */
float getHardwareTriggerFrequency();
float getHardwareTriggerDuty();