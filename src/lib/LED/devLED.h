#pragma once

#include "common.h"
#include <stdint.h>
#include "device.h"

struct blinkyColor_t
{
    uint8_t h;
    uint8_t s;
    uint8_t v;
};

uint32_t HsvToRgb(const blinkyColor_t &color);

void WS281BsetLED(int index, uint32_t color);
void WS281BsetLED(uint32_t color);

uint8_t WS281BgetPixelCount();
void WS281Bshow();
void WS281Bclear();

// Set a non-addressable LED while accounting for active-low wiring.
void LEDsetState(int8_t pin, bool inverted, bool on);

extern device_t RGB_device;
extern device_t LED_device;
