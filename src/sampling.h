#pragma once

#include <Arduino.h>


void samplingInit();

bool getWaveform(uint16_t **samples);

//void measureFrequency(uint16_t *samples);