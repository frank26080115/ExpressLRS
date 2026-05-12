#include "targets.h"
#include "common.h"

#include "devGsensor.h"
#include <functional>

#include "gsensor.h"
#include "POWERMGNT.h"
#include "config.h"
#include "logging.h"

Gsensor gsensor;

static int system_quiet_state = GSENSOR_SYSTEM_STATE_MOVING;
static int system_quiet_pre_state = GSENSOR_SYSTEM_STATE_MOVING;

#define GSENSOR_DURATION    10
#define GSENSOR_SYSTEM_IDLE_INTERVAL 1000U

#define MULTIPLE_BUMP_INTERVAL 400U
#define BUMP_COMMAND_IDLE_TIME 10000U

static bool initialize()
{
#if !defined(BUILD_SHREW_UNNECESSARY) && defined(PLATFORM_ESP8266)
    return false;
#else
    if (OPT_HAS_GSENSOR)
    {
        return gsensor.init();
    }
    return false;
#endif
}

static int start()
{
#if !defined(BUILD_SHREW_UNNECESSARY) && defined(PLATFORM_ESP8266)
    return DURATION_NEVER;
#else
    return DURATION_IMMEDIATELY;
#endif
}

static int timeout()
{
#if !defined(BUILD_SHREW_UNNECESSARY) && defined(PLATFORM_ESP8266)
    return DURATION_NEVER;
#else
    static unsigned long lastIdleCheckMs = 0;
    unsigned long now = millis();
    if (now - lastIdleCheckMs > GSENSOR_SYSTEM_IDLE_INTERVAL)
    {
        gsensor.handle();

        system_quiet_state = gsensor.getSystemState();
        //When system is idle, set power to minimum
        if(config.GetMotionMode() == 1)
        {
            if((system_quiet_state == GSENSOR_SYSTEM_STATE_QUIET) && (system_quiet_pre_state == GSENSOR_SYSTEM_STATE_MOVING) && !isArmed)
            {
                POWERMGNT::setPower(MinPower);
            }
            if((system_quiet_state == GSENSOR_SYSTEM_STATE_MOVING) && (system_quiet_pre_state == GSENSOR_SYSTEM_STATE_QUIET) && POWERMGNT::currPower() < (PowerLevels_e)config.GetPower())
            {
                POWERMGNT::setPower((PowerLevels_e)config.GetPower());
            }
        }
        system_quiet_pre_state = system_quiet_state;
        lastIdleCheckMs = now;
    }
    return GSENSOR_DURATION;
#endif
}

device_t Gsensor_device = {
    .initialize = initialize,
    .start = start,
    .event = NULL,
    .timeout = timeout
};
