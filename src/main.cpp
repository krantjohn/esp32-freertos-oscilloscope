#include <Arduino.h>
#include "config.h"
#include "sampling.h"
#include "display.h"
#include "oscilloscope.h"
#include "sine_gen.h"

static uint16_t *samples = NULL;
static DisplayMode currentDisplayMode = DISPLAY_MODE_WAVEFORM;
static Measurement lastMeasurement = {};

// =============================================================================
// 按键防抖与长按/短按检测
// =============================================================================
static uint32_t btnPressStartTime = 0;
static bool btnLastState = HIGH;
static bool btnLongPressHandled = false;

static bool tbLastState = HIGH;
static uint32_t tbLastDebounceTime = 0;

static void checkButtons() {
    uint32_t now = millis();

    // 1. 外部按键 (GPIO 5) 处理：短按 RUN/STOP，长按(按住达600ms即刻触发)切换 FFT/时域
    bool btnState = digitalRead(BUTTON_RUN_STOP);
    if (btnLastState == HIGH && btnState == LOW) {
        // 按下瞬间
        btnPressStartTime = now;
        btnLongPressHandled = false;
    } else if (btnState == LOW) {
        // 持续按住状态：若达到长按阈值 (600ms) 且本轮按下尚未触发，则立即触发长按切换
        if (!btnLongPressHandled && (now - btnPressStartTime >= 600)) {
            btnLongPressHandled = true;
            // 切换 时域波形 <-> FFT 频谱模式
            if (currentDisplayMode == DISPLAY_MODE_WAVEFORM) {
                currentDisplayMode = DISPLAY_MODE_FFT;
            } else {
                currentDisplayMode = DISPLAY_MODE_WAVEFORM;
            }
            Serial.printf("[Button] Display Mode switched: %s\n", 
                currentDisplayMode == DISPLAY_MODE_FFT ? "FFT Spectrum" : "Waveform");
        }
    } else if (btnLastState == LOW && btnState == HIGH) {
        // 释放瞬间
        uint32_t pressDuration = now - btnPressStartTime;
        // 仅在未触发长按且满足消抖时间(>=50ms)时，触发短按动作
        if (!btnLongPressHandled && pressDuration >= 50) {
            // 短按：切换 RUN / STOP 冻结状态
            toggleRunStop();
            Serial.printf("[Button] RUN/STOP toggled: %s\n", isRunning() ? "RUN" : "STOP");
        }
        btnLongPressHandled = false;
    }
    btnLastState = btnState;

    // 2. 外部时基切换按键 (GPIO 4) 处理：短按循环切换采样率
    bool tbState = digitalRead(BUTTON_TIMEBASE);
    if (tbLastState == HIGH && tbState == LOW) {
        if (now - tbLastDebounceTime > 200) {
            nextTimebase();
            tbLastDebounceTime = now;
            Serial.printf("[Button] Timebase switched to: %s (%u Hz)\n", 
                getCurrentTimebaseName(), getCurrentSampleRate());
        }
    }
    tbLastState = tbState;
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[BOOT] Starting ESP32-S3 Oscilloscope...");

    // 配置按键输入（上拉）
    pinMode(BUTTON_RUN_STOP, INPUT_PULLUP);
    pinMode(BUTTON_TIMEBASE, INPUT_PULLUP);

    displayInit();
    oscilloscopeInit();
    samplingInit();

    Serial.println("=================================================");
    Serial.println(" ESP32-S3 Mini Oscilloscope (DMA + DSP FFT)");
    Serial.println(" - GPIO 5 (BTN):  Short press = RUN/STOP");
    Serial.println("                  Long press  = FFT / Waveform");
    Serial.println(" - GPIO 4 (TB):   Press       = Next Timebase");
    Serial.println("=================================================");
}


void loop() {
    // 扫描按键状态
    checkButtons();

    // 获取并显示波形
    if (getWaveform(&samples)) {
        lastMeasurement = measureWaveform(samples, getCurrentSampleRate());
        displayScreen(
            samples, 
            lastMeasurement, 
            currentDisplayMode, 
            isRunning(), 
            getCurrentTimebaseName()
        );
    } else if (!isRunning() && samples != NULL) {
        // STOP 模式下保持刷新界面以响应模式切换
        displayScreen(
            samples, 
            lastMeasurement, 
            currentDisplayMode, 
            false, 
            getCurrentTimebaseName()
        );
    }

    vTaskDelay(pdMS_TO_TICKS(5));
}