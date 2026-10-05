#pragma once

#if defined(PLATFORM_ESP32) && defined(BUILD_SHREW_PWM_ONESHOT)

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/rmt.h>
#include "common.h"

class PwmOneShot {
public:
    PwmOneShot(gpio_num_t pin, uint16_t frequency, eServoOutputFailsafeMode failsafeMode);
    ~PwmOneShot();

    bool begin(rmt_channel_t channel);
    void setPulse(uint16_t microseconds);

    static bool initialized();
    static void poll(bool rfConnected);

private:
    gpio_num_t pin;
    uint32_t periodUs;
    eServoOutputFailsafeMode failsafeMode;
    uint16_t pulseUs = 0;
    uint32_t lastPulseUs = 0;
    bool hasSent = false;
    bool registered = false;
    PwmOneShot *next = nullptr;

    static PwmOneShot *head;
    static PwmOneShot *tail;
    static PwmOneShot *current;
    static rmt_channel_t rmtChannel;
    static int previousPin;
    static uint32_t lastStartUs;
    static uint16_t lastDurationUs;
};

#endif
