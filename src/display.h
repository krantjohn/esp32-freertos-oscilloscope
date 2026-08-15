#pragma once

#include <Arduino.h>

void displayInit();

void displayWaveform(
    uint16_t *samples, 
    float frequency, 
    float voltagePP,
    float voltageMax
);

