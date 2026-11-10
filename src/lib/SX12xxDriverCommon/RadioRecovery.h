#pragma once

#include "targets.h"

enum class RadioRecoveryStatus : uint8_t
{
    InProgress,
    Ready,
    Failed,
    Unsupported
};

// Driver recovery leaves radio IRQs disabled and user callbacks cleared. The
// receiver restores callbacks, configuration and power before enabling reception.

// Runtime resets must not spend the radio's boot delay blocking the device scheduler.
class RadioResetSequence
{
public:
    RadioRecoveryStatus start(uint32_t assertUs, uint32_t settleMs, bool floatOnRelease = false)
    {
        if (GPIO_PIN_RST == UNDEF_PIN)
            return RadioRecoveryStatus::Unsupported;

        floatRelease = floatOnRelease;
        holdMs = (assertUs + 999U) / 1000U;
        bootMs = settleMs;
        setReset(true);
        started = millis();
        phase = Asserting;
        if (assertUs <= 10U)
        {
            // SX1280 reset pulses must stay short (some targets share an ESC sleep pin).
            delayMicroseconds(assertUs);
            release(millis());
        }
        return RadioRecoveryStatus::InProgress;
    }

    RadioRecoveryStatus poll(uint32_t now)
    {
        if (phase == Asserting && uint32_t(now - started) >= holdMs)
            release(now);
        if (phase == Booting && uint32_t(now - started) >= bootMs)
        {
            phase = Idle;
            return RadioRecoveryStatus::Ready;
        }
        return RadioRecoveryStatus::InProgress;
    }

    void cancel()
    {
        if (phase == Asserting)
            setReset(false);
        phase = Idle;
    }

private:
    enum { Idle, Asserting, Booting } phase = Idle;
    uint32_t started = 0, holdMs = 0, bootMs = 0;
    bool floatRelease = false;

    void setReset(bool asserted)
    {
        const int pins[] = {GPIO_PIN_RST, GPIO_PIN_RST_2};
        for (int pin : pins)
        {
            if (pin == UNDEF_PIN)
                continue;
            if (!asserted && floatRelease)
                pinMode(pin, INPUT);
            else
            {
                pinMode(pin, OUTPUT);
                digitalWrite(pin, asserted ? LOW : HIGH);
            }
        }
    }

    void release(uint32_t now)
    {
        setReset(false);
        started = now;
        phase = Booting;
    }
};
