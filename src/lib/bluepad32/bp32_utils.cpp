#include "bp32_utils.h"
#include "crsf_protocol.h"

static constexpr int32_t BLUEPAD32_AXIS_MIN = -512;
static constexpr int32_t BLUEPAD32_AXIS_MAX = 512;
static constexpr int32_t BLUEPAD32_TRIGGER_MIN = 0;
static constexpr int32_t BLUEPAD32_TRIGGER_MAX = 1023;
static constexpr int32_t BLUEPAD32_CHANNEL_SHADOW_MULTIPLIER = 1024;
static constexpr int32_t BLUEPAD32_FULL_TRAVEL_TIME_MS = 1000;

int32_t clampValue(int32_t value, int32_t minValue, int32_t maxValue)
{
    if (value < minValue)
    {
        return minValue;
    }
    if (value > maxValue)
    {
        return maxValue;
    }
    return value;
}

uint32_t axisToCrsf(int32_t value, bool invert)
{
    value = clampValue(value, BLUEPAD32_AXIS_MIN, BLUEPAD32_AXIS_MAX);
    if (invert)
    {
        value = -value;
    }

    if (value >= 0)
    {
        return CRSF_CHANNEL_VALUE_MID + (value * (CRSF_CHANNEL_VALUE_MAX - CRSF_CHANNEL_VALUE_MID)) / BLUEPAD32_AXIS_MAX;
    }

    return CRSF_CHANNEL_VALUE_MID + (value * (CRSF_CHANNEL_VALUE_MID - CRSF_CHANNEL_VALUE_MIN)) / -BLUEPAD32_AXIS_MIN;
}

uint32_t triggerToCrsf(int32_t value)
{
    value = clampValue(value, BLUEPAD32_TRIGGER_MIN, BLUEPAD32_TRIGGER_MAX);
    return CRSF_CHANNEL_VALUE_MIN + (value * (CRSF_CHANNEL_VALUE_MAX - CRSF_CHANNEL_VALUE_MIN)) / BLUEPAD32_TRIGGER_MAX;
}

uint32_t triggerDiffToCrsf(int32_t value)
{
    value = clampValue(value, -BLUEPAD32_TRIGGER_MAX, BLUEPAD32_TRIGGER_MAX);
    if (value >= 0)
    {
        return CRSF_CHANNEL_VALUE_MID + (value * (CRSF_CHANNEL_VALUE_MAX - CRSF_CHANNEL_VALUE_MID)) / BLUEPAD32_TRIGGER_MAX;
    }

    return CRSF_CHANNEL_VALUE_MID + (value * (CRSF_CHANNEL_VALUE_MID - CRSF_CHANNEL_VALUE_MIN)) / BLUEPAD32_TRIGGER_MAX;
}

uint32_t buttonToCrsf(bool pressed)
{
    return pressed ? CRSF_CHANNEL_VALUE_2000 : CRSF_CHANNEL_VALUE_1000;
}

uint32_t dpadToCrsfAxis(uint8_t dpad, uint8_t negativeMask, uint8_t positiveMask)
{
    const bool negativePressed = (dpad & negativeMask) != 0;
    const bool positivePressed = (dpad & positiveMask) != 0;

    if (negativePressed == positivePressed)
    {
        return CRSF_CHANNEL_VALUE_MID;
    }

    return negativePressed ? CRSF_CHANNEL_VALUE_MIN : CRSF_CHANNEL_VALUE_MAX;
}

void update_servo_shadow(int32_t* data, int16_t ctl, uint32_t dt_ms)
{
    ctl = clampValue(ctl, BLUEPAD32_AXIS_MIN, BLUEPAD32_AXIS_MAX);

    const int32_t shadow_min = CRSF_CHANNEL_VALUE_STD_MIN * BLUEPAD32_CHANNEL_SHADOW_MULTIPLIER;
    const int32_t shadow_max = CRSF_CHANNEL_VALUE_STD_MAX * BLUEPAD32_CHANNEL_SHADOW_MULTIPLIER;

    const int32_t shadow_full_range = (CRSF_CHANNEL_VALUE_STD_MAX - CRSF_CHANNEL_VALUE_STD_MIN) * BLUEPAD32_CHANNEL_SHADOW_MULTIPLIER;

    /*
       At ctl == 512:
       full travel takes 1000 ms.

       delta = full_range * ctl/512 * dt/1000
    */
    int32_t delta = (int32_t)(
        ((int64_t)shadow_full_range * ctl * dt_ms) /
        ((int64_t)BLUEPAD32_AXIS_MAX * BLUEPAD32_FULL_TRAVEL_TIME_MS)
    );

    (*data) += delta;

    if ((*data) < shadow_min) {
        (*data) = shadow_min;
    } else if ((*data) > shadow_max) {
        (*data) = shadow_max;
    }
}
