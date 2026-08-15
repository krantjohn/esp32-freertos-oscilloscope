#include <Arduino.h>
#include "config.h"
#include "sampling.h"
#include "display.h"
#include "oscilloscope.h"

uint16_t *samples;

void setup() {

    //串口初始化
    Serial.begin(9600);
    
    displayInit();

    samplingInit();

    Serial.println("Oscilloscope Ready");
    
}

void loop() {


    
    if(getWaveform(&samples)) {


        Measurement result = measureWaveform(samples);

        displayWaveform(
            samples, 
            result.frequency, 
            result.voltagePP,
            result.voltageMax
        );
    }


    vTaskDelay(pdMS_TO_TICKS(1));
}