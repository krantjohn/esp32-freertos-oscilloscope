#include "display.h"
#include "config.h"
#include "oscilloscope.h"

#include <U8g2lib.h>
#include <Wire.h>

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(
    U8G2_R0, 
    U8X8_PIN_NONE
);

void displayInit() {
    Wire.begin(OLED_SDA, OLED_SCL);
    Wire.setClock(400000); // 启用 400kHz 快速 I2C 模式，帧率提升至 40~60 FPS

    oled.begin();
    oled.clearBuffer();

    oled.setFont(u8g2_font_6x10_tr);
    oled.drawStr(25, 30, "Oscilloscope");
    oled.drawStr(40, 45, "DMA + DSP");

    oled.sendBuffer();
    delay(800);
}

void displayScreen(
    const uint16_t *samples, 
    const Measurement &m,
    DisplayMode mode,
    bool isRunning,
    const char *timebaseName
) 
{
    oled.clearBuffer();

    // =========================================================================
    // 1. 顶部状态栏 (Y: 0 ~ 9)
    // =========================================================================
    oled.setFont(u8g2_font_5x7_tf);

    // 运行/冻结状态指示 (RUN 反色填充框 / STOP 空心框)
    if (isRunning) {
        oled.drawBox(0, 0, 22, 9);
        oled.setDrawColor(0);
        oled.drawStr(2, 7, "RUN");
        oled.setDrawColor(1);
    } else {
        oled.drawFrame(0, 0, 26, 9);
        oled.drawStr(2, 7, "STOP");
    }

    // 模式与时基提示
    if (mode == DISPLAY_MODE_WAVEFORM) {
        char tbBuf[16];
        snprintf(tbBuf, sizeof(tbBuf), "TB:%s", timebaseName);
        oled.drawStr(70, 7, tbBuf);
    } else {
        oled.drawStr(40, 7, "[FFT SPECTRUM]");
    }

    // 状态栏分割线
    oled.drawHLine(0, 9, 128);

    // =========================================================================
    // 2. 中部数据呈现区 (Y: 11 ~ 46)
    // =========================================================================
    if (mode == DISPLAY_MODE_WAVEFORM) {
        // --- 时域模式：绘制连续平滑折线 ---
        for (int i = 0; i < SAMPLE_COUNT - 1; i++) {
            int y1 = constrain(map(samples[i], 0, 4095, 46, 11), 11, 46);
            int y2 = constrain(map(samples[i + 1], 0, 4095, 46, 11), 11, 46);
            oled.drawLine(i, y1, i + 1, y2);
        }
    } else {
        // --- 频域模式：绘制 60 柱 FFT 频谱柱状图 (每柱 1 像素宽，1 像素暗间隙，清爽立体) ---
        oled.drawHLine(4, 46, 120); // 频谱基准地线

        for (int k = 1; k <= 60; k++) {
            float mag = m.fftMagnitudes[k];
            // 非线性对比度增强（平方衰减抑制噪声，让主频和谐波尖峰清晰凸显）
            float scaled = mag * mag;
            int h = (int)(scaled * 34.0f);
            if (h > 34) h = 34;

            int x = 4 + (k - 1) * 2;
            int y = 46 - h;
            if (h > 0) {
                oled.drawVLine(x, y, h);
            }
        }
    }

    // 分隔线：区分波形/频谱区与底部测量参数区
    oled.drawHLine(0, 47, 128);

    // =========================================================================
    // 3. 底部测量参数栏 (Y: 48 ~ 63)
    // =========================================================================
    oled.setFont(u8g2_font_5x8_tf);

    char buf1[32];
    char buf2[32];

    if (mode == DISPLAY_MODE_WAVEFORM) {
        // 第一行：频率与占空比
        if (m.frequency > 0.0f) {
            if (m.frequency >= 1000.0f) {
                snprintf(buf1, sizeof(buf1), "F:%.2fkHz  D:%.1f%%", m.frequency / 1000.0f, m.dutyCycle);
            } else {
                snprintf(buf1, sizeof(buf1), "F:%.0fHz    D:%.1f%%", m.frequency, m.dutyCycle);
            }
        } else {
            snprintf(buf1, sizeof(buf1), "F:---Hz    D:--%%");
        }
    } else {
        // 频谱模式第一行：FFT 主峰频率
        if (m.fftPeakFreq >= 1000.0f) {
            snprintf(buf1, sizeof(buf1), "Peak:%.2fkHz (FFT)", m.fftPeakFreq / 1000.0f);
        } else {
            snprintf(buf1, sizeof(buf1), "Peak:%.0fHz (FFT)", m.fftPeakFreq);
        }
    }
    oled.drawStr(0, 55, buf1);

    // 第二行：峰峰值与真有效值 (RMS)
    snprintf(buf2, sizeof(buf2), "Vpp:%.2fV  Vrms:%.2fV", m.voltagePP, m.voltageRMS);
    oled.drawStr(0, 63, buf2);

    oled.sendBuffer();
}
