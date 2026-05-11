#pragma once

#include <stdint.h>
#include <stdbool.h>

enum : uint8_t
{
    BP_MAINCTRL_FLATMAPPING,
    BP_MAINCTRL_BOTHSTICKSFULLAUTO,
    BP_MAINCTRL_BOTHSTICKSDIRECTY,
    BP_MAINCTRL_LEFTSTICKONLY,
    BP_MAINCTRL_RIGHTSTICKONLY,
    BP_MAINCTRL_DPADONLY,
    BP_MAINCTRL_LEFTTHROTTLE_RIGHTSTEERING,
    BP_MAINCTRL_RIGHTTHROTTLE_LEFTSTEERING,
    BP_MAINCTRL_RACING_LEFTSTEERING,
    BP_MAINCTRL_RACING_RIGHTSTEERING,
    BP_MAINCTRL_COUNT,
};

enum : uint8_t
{
    BP_ANALOGCTRL_DONOTHING,
    BP_ANALOGCTRL_LEFTSTICK_Y_DIRECT,
    BP_ANALOGCTRL_RIGHTSTICK_Y_DIRECT,
    BP_ANALOGCTRL_LEFTSTICK_Y_RELATIVE,
    BP_ANALOGCTRL_RIGHTSTICK_Y_RELATIVE,
    BP_ANALOGCTRL_LEFTTRIGGER_DIRECT,
    BP_ANALOGCTRL_RIGHTTRIGGER_DIRECT,
    BP_ANALOGCTRL_LEFTTRIGGER_LOWER_RIGHTTRIGGER_RAISE,
    BP_ANALOGCTRL_RIGHTTRIGGER_LOWER_LEFTTRIGGER_RAISE,
    BP_ANALOGCTRL_COUNT,
};

enum : uint8_t
{
    BP_BUTTONCTRL_DONOTHING,
    BP_BUTTONCTRL_TAP2LATCH,
    BP_BUTTONCTRL_HELD,
    BP_BUTTONCTRL_INCREMENT,
    BP_BUTTONCTRL_DECREMENT,
    BP_BUTTONCTRL_COUNT,
};

enum : uint8_t
{
    BP_BUTTON_FACE_A,
    BP_BUTTON_FACE_B,
    BP_BUTTON_FACE_X,
    BP_BUTTON_FACE_Y,
    BP_BUTTON_DPAD_UP,
    BP_BUTTON_DPAD_DOWN,
    BP_BUTTON_DPAD_LEFT,
    BP_BUTTON_DPAD_RIGHT,
    BP_BUTTON_L1,
    BP_BUTTON_R1,
    BP_BUTTON_COUNT,
};

enum : uint16_t
{
    BP_OCCUPANCY_NONE          = 0,
    BP_OCCUPANCY_LEFT_STICK    = 1 << 0,
    BP_OCCUPANCY_RIGHT_STICK   = 1 << 1,
    BP_OCCUPANCY_DPAD          = 1 << 2,
    BP_OCCUPANCY_LEFT_TRIGGER  = 1 << 3,
    BP_OCCUPANCY_RIGHT_TRIGGER = 1 << 4,
    BP_OCCUPANCY_FACE_A        = 1 << 5,
    BP_OCCUPANCY_FACE_B        = 1 << 6,
    BP_OCCUPANCY_FACE_X        = 1 << 7,
    BP_OCCUPANCY_FACE_Y        = 1 << 8,
    BP_OCCUPANCY_L1            = 1 << 9,
    BP_OCCUPANCY_R1            = 1 << 10,
};

enum : uint8_t
{
    BP_AUX_CHANNEL_COUNT = 2,
    BP_BUTTON_CONFIG_COUNT = BP_BUTTON_COUNT,
};

typedef struct __attribute__((packed))
{
    uint8_t actual_channel; // 0 means unused, 1 indexed, reminder, ChannelData is 0 indexed
    #if defined(TARGET_TX)
    uint16_t failsafe;   // unit in microseconds
    #endif
    uint8_t analog_mode; // BP_ANALOGCTRL_*
}
bluepad_auxchan_cfg_t;

typedef struct __attribute__((packed))
{
    uint8_t aux_chan:1; // 0 for the first, 1 for the second
    uint8_t mode:7;     // BP_BUTTONCTRL_*
    uint16_t value;     // either the absolute set value, or the increment/decrement amount, unit is in microseconds
}
bluepad_btn_cfg_t;

typedef struct __attribute__((packed))
{
    uint8_t               main_mode; // BP_MAINCTRL_*
    bluepad_auxchan_cfg_t aux_mode[BP_AUX_CHANNEL_COUNT];
    bluepad_btn_cfg_t     btn_mode[BP_BUTTON_CONFIG_COUNT];
}
bluepad_cfg_t;

static inline uint16_t bluepad_main_mode_occupancy(uint8_t mode)
{
    switch (mode)
    {
    case BP_MAINCTRL_FLATMAPPING:
        return 0; // flat mapping is meant to have tons of stuff overriding it, so no occupancy
    case BP_MAINCTRL_BOTHSTICKSFULLAUTO:
    case BP_MAINCTRL_BOTHSTICKSDIRECTY:
        return BP_OCCUPANCY_LEFT_STICK | BP_OCCUPANCY_RIGHT_STICK;
    case BP_MAINCTRL_LEFTSTICKONLY:
        return BP_OCCUPANCY_LEFT_STICK;
    case BP_MAINCTRL_RIGHTSTICKONLY:
        return BP_OCCUPANCY_RIGHT_STICK;
    case BP_MAINCTRL_DPADONLY:
        return BP_OCCUPANCY_DPAD;
    case BP_MAINCTRL_LEFTTHROTTLE_RIGHTSTEERING:
    case BP_MAINCTRL_RIGHTTHROTTLE_LEFTSTEERING:
        return BP_OCCUPANCY_LEFT_STICK | BP_OCCUPANCY_RIGHT_STICK;
    case BP_MAINCTRL_RACING_LEFTSTEERING:
        return BP_OCCUPANCY_LEFT_STICK | BP_OCCUPANCY_LEFT_TRIGGER |
               BP_OCCUPANCY_RIGHT_TRIGGER;
    case BP_MAINCTRL_RACING_RIGHTSTEERING:
        return BP_OCCUPANCY_RIGHT_STICK | BP_OCCUPANCY_LEFT_TRIGGER |
               BP_OCCUPANCY_RIGHT_TRIGGER;
    default:
        return BP_OCCUPANCY_NONE;
    }
}

static inline uint16_t bluepad_analog_mode_occupancy(uint8_t mode)
{
    switch (mode)
    {
    case BP_ANALOGCTRL_LEFTSTICK_Y_DIRECT:
    case BP_ANALOGCTRL_LEFTSTICK_Y_RELATIVE:
        return BP_OCCUPANCY_LEFT_STICK;
    case BP_ANALOGCTRL_RIGHTSTICK_Y_DIRECT:
    case BP_ANALOGCTRL_RIGHTSTICK_Y_RELATIVE:
        return BP_OCCUPANCY_RIGHT_STICK;
    case BP_ANALOGCTRL_LEFTTRIGGER_DIRECT:
        return BP_OCCUPANCY_LEFT_TRIGGER;
    case BP_ANALOGCTRL_RIGHTTRIGGER_DIRECT:
        return BP_OCCUPANCY_RIGHT_TRIGGER;
    case BP_ANALOGCTRL_LEFTTRIGGER_LOWER_RIGHTTRIGGER_RAISE:
    case BP_ANALOGCTRL_RIGHTTRIGGER_LOWER_LEFTTRIGGER_RAISE:
        return BP_OCCUPANCY_LEFT_TRIGGER | BP_OCCUPANCY_RIGHT_TRIGGER;
    default:
        return BP_OCCUPANCY_NONE;
    }
}

static inline uint16_t bluepad_button_occupancy(uint8_t button)
{
    switch (button)
    {
    case BP_BUTTON_FACE_A:
        return BP_OCCUPANCY_FACE_A;
    case BP_BUTTON_FACE_B:
        return BP_OCCUPANCY_FACE_B;
    case BP_BUTTON_FACE_X:
        return BP_OCCUPANCY_FACE_X;
    case BP_BUTTON_FACE_Y:
        return BP_OCCUPANCY_FACE_Y;
    case BP_BUTTON_DPAD_UP:
    case BP_BUTTON_DPAD_DOWN:
    case BP_BUTTON_DPAD_LEFT:
    case BP_BUTTON_DPAD_RIGHT:
        return BP_OCCUPANCY_DPAD;
    case BP_BUTTON_L1:
        return BP_OCCUPANCY_L1;
    case BP_BUTTON_R1:
        return BP_OCCUPANCY_R1;
    default:
        return BP_OCCUPANCY_NONE;
    }
}
