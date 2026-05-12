#pragma once

#include <stdint.h>
#include <stdbool.h>

int32_t clampValue(int32_t value, int32_t minValue, int32_t maxValue);
uint32_t axisToCrsf(int32_t value, bool invert);
uint32_t triggerToCrsf(int32_t value);
uint32_t triggerDiffToCrsf(int32_t value);
uint32_t buttonToCrsf(bool pressed);
uint32_t dpadToCrsfAxis(uint8_t dpad, uint8_t negativeMask, uint8_t positiveMask);
void update_servo_shadow(int32_t* data, int16_t ctl, uint32_t dt_ms);
