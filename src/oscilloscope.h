#pragma once

#include <Arduino.h>

struct Measurement{

    float frequency;
    float period;

    float voltageMax;
    float voltageMin;
    float voltagePP;
};

Measurement measureWaveform(uint16_t *samples);

//void measureFrequency(uint16_t *samples);