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
    oled.begin();

    oled.clearBuffer();

    oled.setFont(u8g2_font_6x10_tr);

    oled.drawStr(
        25, 
        30, 
        "Oscilloscope"
    );
    oled.drawStr(
        40, 
        45, 
        "FreeRTOS"
    );

    oled.sendBuffer();

    delay(1000);
}

void displayWaveform(
    uint16_t *samples, 
    float frequency, 
    float voltagePP,
    float voltageMax
) 
{

    oled.clearBuffer();

    // 绘制波形
    for(int i = 0; i < SAMPLE_COUNT - 1; i++) {
        int y1 = 
            map(
                samples[i], 
                0, 
                4095, 
                52, 
                0
            );
        int y2 = 
            map(
                samples[i + 1], 
                0, 
                4095, 
                52, 
                0
            );
        oled.drawLine(i, y1, i + 1, y2);
    }

    // 显示频率和电压峰峰值

    oled.setFont(u8g2_font_5x8_tf);

    char buffer[32];

    snprintf(
        buffer, 
        sizeof(buffer), 
        "F:%.0fHz Vpp:%.2f Vmax:%.2fV ", 
        frequency, 
        voltagePP, 
        voltageMax
    );
    oled.drawStr(
        0, 
        61, 
        buffer
    );


    oled.sendBuffer();
}