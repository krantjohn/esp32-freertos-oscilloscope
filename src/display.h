#pragma once

#include <Arduino.h>
#include "oscilloscope.h"

enum DisplayMode {
    DISPLAY_MODE_WAVEFORM = 0, // 时域波形图
    DISPLAY_MODE_FFT      = 1  // 频域 FFT 频谱图
};

void displayInit();

void displayScreen(
    const uint16_t *samples, 
    const Measurement &m,
    DisplayMode mode,
    bool isRunning,
    const char *timebaseName
);



