#if defined(GPIO_PIN_PWM_OUTPUTS)

#include "devServoOutput.h"
#include "PWM.h"
#include "CRSF.h"
#include "config.h"
#include "logging.h"
#include "rxtx_intf.h"
#ifdef BUILD_SHREW_HBRIDGE
#include "hbridge.h"
#endif

static int8_t servoPins[PWM_MAX_CHANNELS];
static pwm_channel_t pwmChannels[PWM_MAX_CHANNELS];
static uint16_t pwmChannelValues[PWM_MAX_CHANNELS];
extern uint32_t ChannelDataMixed[CRSF_NUM_CHANNELS];

#if (defined(PLATFORM_ESP32))
extern bool shrew_isWebCtrlActive();

const uint8_t RMT_MAX_CHANNELS = 8;
static DShotRMT *dshotInstances[PWM_MAX_CHANNELS] = {nullptr};

#define DSHOT_ENABLE_AUTO_ARM     1

static uint32_t dshotArmingTime = 0;       // time stamp of when arming sequence starts
#define DSHOT_ARMING_TIME_NEEDED  1000
static bool dshotAllZero, dshotWasFailsafe, dshotArmOnConnect = false;

static bool dshotCh5Assigned = false;      // if channel 5, the arming channel, is not assigned as a output, then if the user toggles it, it forces all dshot outputs to issue an arm signal
static bool dshotCh5State = false;

bool dshotAllArmed = false;
#endif

// true when the RX has a new channels packet
static bool newChannelsAvailable;
// Absolute max failsafe time if no update is received, regardless of LQ
static constexpr uint32_t FAILSAFE_ABS_TIMEOUT_MS = 1000U;

#ifdef BUILD_SHREW_RGBLED
extern void shrew_updateRgbLed();
#endif
#ifdef BUILD_SHREW_GENERAL
extern void shrew_brownoutSetup();
extern esp_reset_reason_t shrew_reset_get_reason();
extern void shrewbo_onDataHook();
extern void shrewbo_onDisconnectHook();
extern volatile uint32_t shrew_hasBrownedOut;
#endif

extern void shrew_mix();
extern bool shrew_failsafeSwitchIsOn();

void ICACHE_RAM_ATTR servoNewChannelsAvailable()
{
    newChannelsAvailable = true;
    #ifdef BUILD_SHREW_GENERAL
    shrewbo_onDataHook();
    #endif
}

uint16_t servoOutputModeToFrequency(eServoOutputMode mode)
{
    switch (mode)
    {
    case som50Hz:
        return 50U;
    case som60Hz:
        return 60U;
    case som100Hz:
        return 100U;
    case som160Hz:
        return 160U;
    case som333Hz:
        return 333U;
    case som400Hz:
        return 400U;
    case som10KHzDuty:
        return 10000U;
    default:
        return 0;
    }
}

static void servoWrite(uint8_t ch, uint16_t us)
{
    const rx_config_pwm_t *chConfig = config.GetPwmChannel(ch);
#if defined(PLATFORM_ESP32)
    if ((eServoOutputMode)chConfig->val.mode == somDShot || (eServoOutputMode)chConfig->val.mode == somDShot3D)
    {
        // DBGLN("Writing DShot output: us: %u, ch: %d", us, ch);
        if (dshotInstances[ch])
        {
            if (us != 0) {
                if ((us & 0x4000) != 0) {
                    dshotInstances[ch]->send_dshot_value(us & 0x3FFF); // send raw dshot, use for commands and such
                    // note: 6 consecutive commands are needed for the command to be effective
                }
                else if (dshotArmingTime != 0) {
                    dshotInstances[ch]->send_dshot_value(0); // send arming pulse
                }
                else if ((eServoOutputMode)chConfig->val.mode == somDShot3D) {
                    uint16_t x;
                    if (us == 1500) { // stopped
                        x = 0;
                    }
                    else if (us > 1500) { // forward
                        x = fmap(us, 1501, 2012, 1048, 2047);
                    }
                    else { // reverse
                        x = fmap(us, 1499, 988, 48, 1047);
                    }
                    dshotInstances[ch]->send_dshot_value(x);
                    if (x != 0) {
                        dshotAllZero = false;
                    }
                }
                else {
                    uint16_t x = ((us - 1000) * 2) + (DSHOT_THROTTLE_MIN - 1); // Convert PWM signal in us to DShot value
                    x = x <= DSHOT_THROTTLE_MIN ? 0 : (x > DSHOT_THROTTLE_MAX ? DSHOT_THROTTLE_MAX : x); // limit within range, and send special 0 for stop
                    dshotInstances[ch]->send_dshot_value(x);
                    if (x != 0) {
                        dshotAllZero = false;
                    }
                }
            }
            else {
                // no pulse
                dshotInstances[ch]->set_looping(false);
            }
        }
    }
    else
#endif
    if (servoPins[ch] != UNDEF_PIN && pwmChannelValues[ch] != us)
    {
        pwmChannelValues[ch] = us;
        if ((eServoOutputMode)chConfig->val.mode == somOnOff)
        {
            digitalWrite(servoPins[ch], us > 1500);
        }
        else if ((eServoOutputMode)chConfig->val.mode == som10KHzDuty)
        {
            PWM.setDuty(pwmChannels[ch], constrain(us, 1000, 2000) - 1000);
        }
        else
        {
            PWM.setMicroseconds(pwmChannels[ch], us);
        }
    }
}

static void servosFailsafe()
{
    constexpr unsigned SERVO_FAILSAFE_MIN = 988U;
    for (int ch = 0 ; ch < GPIO_PIN_PWM_OUTPUTS_COUNT ; ++ch)
    {
        const rx_config_pwm_t *chConfig = config.GetPwmChannel(ch);
        if (chConfig->val.failsafeMode == PWMFAILSAFE_SET_POSITION) {
            // Note: Failsafe values do not respect the inverted flag, failsafe values are absolute
            uint16_t us = chConfig->val.failsafe + SERVO_FAILSAFE_MIN; // 1500 = x + 988
            if (chConfig->val.stretched) {
                us = fmap(us, SERVO_FAILSAFE_MIN, 2012, 476, 2524);
            }
            // Always write the failsafe position even if the servo has never been started,
            // so all the servos go to their expected position
            servoWrite(ch, us);
        }
        else if (chConfig->val.failsafeMode == PWMFAILSAFE_NO_PULSES) {
            servoWrite(ch, 0);
        }
        else if (chConfig->val.failsafeMode == PWMFAILSAFE_LAST_POSITION) {
            // do nothing
        }
    }
    #if defined(PLATFORM_ESP32)
    dshotWasFailsafe = true;
    dshotCh5State = false;
    dshotAllArmed = false;
    #endif
    #if defined(BUILD_SHREW_HBRIDGE) && defined(PLATFORM_ESP32)
    hbridge_failsafe();
    #endif
}

static void servosUpdate(unsigned long now)
{
    static uint32_t lastUpdate;

    #if defined(PLATFORM_ESP32_C3)
    DShotRMT::poll();
    #endif

    if (newChannelsAvailable)
    {
        newChannelsAvailable = false;
        lastUpdate = now;

        #ifdef BUILD_SHREW_GENERAL
        #if DSHOT_ENABLE_AUTO_ARM
        if (dshotWasFailsafe) {
            dshotArmingTime = millis();
        }
        #endif
        dshotWasFailsafe = false;
        if (dshotArmOnConnect) {
            dshotArmingTime = millis();
            dshotArmOnConnect = false;
        }

        if (shrew_failsafeSwitchIsOn()) {
            servosFailsafe();
            return;
        }

        shrew_mix();

        #if defined(BUILD_SHREW_RGBLED) && defined(PLATFORM_ESP32)
        shrew_updateRgbLed();
        #endif

        #if defined(BUILD_SHREW_HBRIDGE) && defined(PLATFORM_ESP32)
        hbridge_update(now);
        #endif
        #if defined(PLATFORM_ESP32)
        dshotAllZero = true;
        #endif
        #endif // #ifdef BUILD_SHREW_GENERAL

        for (int ch = 0 ; ch < GPIO_PIN_PWM_OUTPUTS_COUNT ; ++ch)
        {
            const rx_config_pwm_t *chConfig = config.GetPwmChannel(ch);
            const unsigned crsfVal = ChannelDataMixed[chConfig->val.inputChannel];
            // crsfVal might 0 if this is a switch channel, and it has not been
            // received yet. Delay initializing the servo until the channel is valid
            if (crsfVal == 0)
            {
                continue;
            }

            uint16_t us;
            if (chConfig->val.stretched) {
                us = fmap(crsfVal, CRSF_CHANNEL_VALUE_MIN, CRSF_CHANNEL_VALUE_MAX, 476, 2524);
            }
            else {
                us = CRSF_to_US(crsfVal);
            }
            // Flip the output around the mid-value if inverted
            // (1500 - usOutput) + 1500
            if (chConfig->val.inverted)
            {
                us = 3000U - us;
            }
            servoWrite(ch, us);
        } /* for each servo */

        #if defined(PLATFORM_ESP32)
        if (dshotAllZero || (millis() - dshotArmingTime) >= DSHOT_ARMING_TIME_NEEDED) {
            dshotArmingTime = 0;
            dshotAllArmed = true;
        }

        // if channel 5, the arming channel, is not assigned as a output, then if the user toggles it, it forces all dshot outputs to issue an arm signal
        if (dshotCh5State == false) {
            if (ChannelDataMixed[4] > (CRSF_CHANNEL_VALUE_MID + 100)) {
                dshotCh5State = true;
                if (dshotArmingTime == 0 && dshotCh5Assigned == false) {
                    dshotArmingTime = millis();
                }
            }
        }
        else {
            if (ChannelDataMixed[4] < CRSF_CHANNEL_VALUE_MID) {
                dshotCh5State = false;
            }
        }
        #endif
    }     /* if newChannelsAvailable */

    // LQ goes to 0 (100 packets missed in a row)
    // OR last update older than FAILSAFE_ABS_TIMEOUT_MS
    // go to failsafe
    else if (lastUpdate && ((getLq() == 0) || (now - lastUpdate > FAILSAFE_ABS_TIMEOUT_MS)))
    {
        servosFailsafe();
        #ifdef BUILD_SHREW_GENERAL
        shrewbo_onDisconnectHook();
        #endif
        lastUpdate = 0;
    }
}

static void initialize()
{
    #if defined(BUILD_SHREW_GENERAL)
    shrew_brownoutSetup();
    #endif
    #if defined(BUILD_SHREW_HBRIDGE) && defined(PLATFORM_ESP32)
    hbridge_init();
    #endif

    if (!OPT_HAS_SERVO_OUTPUT)
    {
        return;
    }

#if defined(PLATFORM_ESP32)
    uint8_t rmtCH = 0;
#endif
    for (int ch = 0; ch < GPIO_PIN_PWM_OUTPUTS_COUNT; ++ch)
    {
        pwmChannelValues[ch] = UINT16_MAX;
        pwmChannels[ch] = -1;
        int8_t pin = GPIO_PIN_PWM_OUTPUTS[ch];
#if (defined(DEBUG_LOG) || defined(DEBUG_RCVR_LINKSTATS)) && (defined(PLATFORM_ESP8266) || defined(PLATFORM_ESP32))
        // Disconnect the debug UART pins if DEBUG_LOG
        if (pin == U0RXD_GPIO_NUM || pin == U0TXD_GPIO_NUM)
        {
            pin = UNDEF_PIN;
        }
#endif
        // Mark servo pins that are being used for serial (or other purposes) as disconnected
        auto mode = (eServoOutputMode)config.GetPwmChannel(ch)->val.mode;
        if (mode >= somSerial)
        {
            pin = UNDEF_PIN;
        }
#if defined(PLATFORM_ESP32)
        else if (mode == somDShot || mode == somDShot3D)
        {
            auto gpio = (gpio_num_t)pin;
            auto rmtChannel = (rmt_channel_t)rmtCH;
            if (rmtCH < RMT_MAX_CHANNELS)
            {
                DBGLN("Initializing DShot: gpio: %u, ch: %d, rmtChannel: %u", gpio, ch, rmtChannel);
                pinMode(pin, OUTPUT);
                dshotInstances[ch] = new DShotRMT(gpio, rmtChannel); // Initialize the DShotRMT instance
                rmtCH++;
                pin = UNDEF_PIN;
            }
            else {
                mode = som400Hz;
                DBGLN("Init DShot failed: gpio: %u, ch: %d, fallback to PWM", gpio, ch);
                rx_config_pwm_t * chcfg = (rx_config_pwm_t *)config.GetPwmChannel(ch);
                chcfg->val.mode = mode;
            }
        }
#endif
        servoPins[ch] = pin;
        // Initialize all servos to low ASAP
        if (pin != UNDEF_PIN)
        {
            if (mode == somOnOff)
            {
                DBGLN("Initializing digital output: ch: %d, pin: %d", ch, pin);
            }
            else
            {
                DBGLN("Initializing PWM output: ch: %d, pin: %d", ch, pin);
            }

            #if defined(PLATFORM_ESP32)
            if (config.GetPwmChannel(ch)->val.inputChannel == 4) {
                dshotCh5Assigned = true;
            }
            #endif

            pinMode(pin, OUTPUT);
            digitalWrite(pin, LOW);
        }
    }
}

static int start()
{
    #if defined(PLATFORM_ESP32) && defined(BUILD_SHREW_GENERAL)
    if (shrew_reset_get_reason() == ESP_RST_BROWNOUT) {
        dshotArmOnConnect = true;
    }
    #endif

    for (int ch = 0; ch < GPIO_PIN_PWM_OUTPUTS_COUNT; ++ch)
    {
        const rx_config_pwm_t *chConfig = config.GetPwmChannel(ch);
        auto frequency = servoOutputModeToFrequency((eServoOutputMode)chConfig->val.mode);
        if (frequency && servoPins[ch] != UNDEF_PIN)
        {
            pwmChannels[ch] = PWM.allocate(servoPins[ch], frequency);
        }
#if defined(PLATFORM_ESP32)
        else if (((eServoOutputMode)chConfig->val.mode) == somDShot || ((eServoOutputMode)chConfig->val.mode) == somDShot3D)
        {
            dshotInstances[ch]->begin(DSHOT600, false); // Set DShot protocol and bidirectional dshot bool
            dshotInstances[ch]->send_dshot_value(0);    // Set throttle low so the ESC can continue initialsation
        }
#endif
    }
    #ifdef BUILD_SHREW_WIFI
    shrew_markServosInitialized(true);
    #endif
    // set servo outputs to failsafe position on start in case they want to play silly buggers!
    servosFailsafe();
    return DURATION_NEVER;
}

static int event()
{
    #ifdef BUILD_SHREW_GENERAL
    if (shrew_isWebCtrlActive()) {
        return DURATION_IMMEDIATELY;
    }
    #endif
    if (!OPT_HAS_SERVO_OUTPUT || connectionState == disconnected)
    {
        // Disconnected should come after failsafe on the RX,
        // so it is safe to shut down when disconnected
        return DURATION_IMMEDIATELY;
    }
    else if (connectionState == wifiUpdate)
    {
#ifndef BUILD_SHREW_WIFI
        for (int ch = 0; ch < GPIO_PIN_PWM_OUTPUTS_COUNT; ++ch)
        {
            if (pwmChannels[ch] != -1)
            {
                PWM.release(pwmChannels[ch]);
                pwmChannels[ch] = -1;
            }
#if defined(PLATFORM_ESP32)
            if (dshotInstances[ch] != nullptr)
            {
                delete dshotInstances[ch];
                dshotInstances[ch] = nullptr;
            }
#endif
            servoPins[ch] = UNDEF_PIN;
        }
#endif
        #ifdef BUILD_SHREW_WIFI
        shrew_markServosInitialized(false);
        #endif
        return DURATION_IMMEDIATELY;
    }
    return DURATION_IMMEDIATELY;
}

static int timeout()
{
    servosUpdate(millis());
    return DURATION_IMMEDIATELY;
}

device_t ServoOut_device = {
    .initialize = initialize,
    .start = start,
    .event = event,
    .timeout = timeout,
};

#ifdef BUILD_SHREW_GENERAL

void shrew_forceAllFailsafe()
{
    servosFailsafe();
    #ifdef BUILD_SHREW_HBRIDGE
    hbridge_failsafe();
    #endif
}

#endif

#ifdef BUILD_SHREW_AM32CONFIG

void servos_deinitAll()
{
    for (int ch = 0; ch < GPIO_PIN_PWM_OUTPUTS_COUNT; ++ch)
    {
        if (pwmChannels[ch] != -1)
        {
            PWM.release(pwmChannels[ch]);
            pwmChannels[ch] = -1;
        }
#if defined(PLATFORM_ESP32)
        if (dshotInstances[ch] != nullptr)
        {
            delete dshotInstances[ch];
            dshotInstances[ch] = nullptr;
        }
#endif
        servoPins[ch] = UNDEF_PIN;
    }
    #ifdef BUILD_SHREW_WIFI
    shrew_markServosInitialized(false);
    #endif
}

bool servos_singleInit(int selected_pin)
{
    bool res = false;
#if defined(PLATFORM_ESP32)
    uint8_t rmtCH = 0;
#endif
    for (int ch = 0; ch < GPIO_PIN_PWM_OUTPUTS_COUNT; ++ch)
    {
        pwmChannelValues[ch] = UINT16_MAX;
        pwmChannels[ch] = -1;
        int8_t pin = GPIO_PIN_PWM_OUTPUTS[ch];
        if (selected_pin != pin) {
            continue;
        }
        const rx_config_pwm_t *chConfig = config.GetPwmChannel(ch);
#if (defined(DEBUG_LOG) || defined(DEBUG_RCVR_LINKSTATS)) && (defined(PLATFORM_ESP8266) || defined(PLATFORM_ESP32))
        // Disconnect the debug UART pins if DEBUG_LOG
        if (pin == U0RXD_GPIO_NUM || pin == U0TXD_GPIO_NUM)
        {
            pin = UNDEF_PIN;
        }
#endif
        // Mark servo pins that are being used for serial (or other purposes) as disconnected
        auto mode = (eServoOutputMode)chConfig->val.mode;
        if (mode >= somSerial)
        {
            pin = UNDEF_PIN;
        }
#if defined(PLATFORM_ESP32)
        else if (mode == somDShot || mode == somDShot3D)
        {
            if (rmtCH < RMT_MAX_CHANNELS)
            {
                auto gpio = (gpio_num_t)pin;
                auto rmtChannel = (rmt_channel_t)rmtCH;
                DBGLN("Initializing DShot: gpio: %u, ch: %d, rmtChannel: %u", gpio, ch, rmtChannel);
                pinMode(pin, OUTPUT);
                dshotInstances[ch] = new DShotRMT(gpio, rmtChannel); // Initialize the DShotRMT instance
                dshotInstances[ch]->begin(DSHOT600, false);
                servoWrite(ch, 0);
                res = true;
                rmtCH++;
            }
            pin = UNDEF_PIN;
        }
#endif
        servoPins[ch] = pin;
        if (pin != UNDEF_PIN)
        {
            if (mode != somOnOff)
            {
                pinMode(pin, OUTPUT);
                digitalWrite(pin, LOW);
                auto frequency = servoOutputModeToFrequency((eServoOutputMode)chConfig->val.mode);
                if (frequency && servoPins[ch] != UNDEF_PIN)
                {
                    pwmChannels[ch] = PWM.allocate(servoPins[ch], frequency);
                    servoWrite(ch, 0);
                    res = true;
                }
            }
        }
    }
    return res;
}

void servos_singleWrite(int selected_pin, int us)
{
    for (int ch = 0; ch < GPIO_PIN_PWM_OUTPUTS_COUNT; ++ch)
    {
        int8_t pin = GPIO_PIN_PWM_OUTPUTS[ch];
        if (selected_pin != pin) {
            continue;
        }
        servoWrite(ch, us);
        break;
    }
}

#endif // BUILD_SHREW_AM32CONFIG

#endif
