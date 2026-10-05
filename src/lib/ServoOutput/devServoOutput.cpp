#if defined(TARGET_RX)

#include "devServoOutput.h"
#include "OTA.h"
#include "PWM.h"
#include "config.h"
#include "crsf_protocol.h"
#include "logging.h"
#include "rxtx_intf.h"
#include "CustomMixer.h"
#include "ShrewHBridge.h"
#include "WebBackend.h"
#if defined(PLATFORM_ESP32) && defined(BUILD_SHREW_PWM_ONESHOT)
#include "PWM_ESP32_OneShot.h"
#endif
#if defined(PLATFORM_ESP8266) && defined(BUILD_SHREW_PWM_ONESHOT)
#include "waveform_8266.h"
#endif

static int8_t servoPins[PWM_MAX_CHANNELS];
static pwm_channel_t pwmChannels[PWM_MAX_CHANNELS];
static uint16_t pwmChannelValues[PWM_MAX_CHANNELS];

#if defined(PLATFORM_ESP32)
static DShotRMT *dshotInstances[PWM_MAX_CHANNELS] = {nullptr};
const uint8_t RMT_MAX_CHANNELS = 8;
#if defined(BUILD_SHREW_PWM_ONESHOT)
static uint8_t nextRmtChannel = 0;
static PwmOneShot *pwmOneShotInstances[PWM_MAX_CHANNELS] = {nullptr};

static bool servoInitializePwmOneShot(uint8_t ch, uint8_t pin, uint16_t frequency,
                                      eServoOutputFailsafeMode failsafeMode)
{
    const bool wasInitialized = PwmOneShot::initialized();
#if defined(PLATFORM_ESP32_C3)
    const rmt_channel_t channel = RMT_CHANNEL_1; // DShot uses channel 0.
#else
    if (!wasInitialized && nextRmtChannel >= RMT_MAX_CHANNELS) {
        DBGLN("PWM one-shot: no RMT channel for pin %u", pin);
        return false;
    }
    const rmt_channel_t channel = (rmt_channel_t)nextRmtChannel;
#endif
    PwmOneShot *instance = new PwmOneShot((gpio_num_t)pin, frequency, failsafeMode);
    if (!instance->begin(channel)) {
        DBGLN("PWM one-shot: RMT setup failed for pin %u", pin);
        delete instance;
        return false;
    }
#if !defined(PLATFORM_ESP32_C3)
    if (!wasInitialized) {
        // The first instance reserves the one shared RMT channel.
        ++nextRmtChannel;
    }
#endif
    pwmOneShotInstances[ch] = instance;
    return true;
}
#endif
#endif

// true when the RX has a new channels packet
static bool newChannelsAvailable;
// Absolute max failsafe time if no update is received, regardless of LQ
static constexpr uint32_t FAILSAFE_ABS_TIMEOUT_MS = 1000U;

bool servo_initialized = false;
void servo_initializeEnable();
typedef void (*servoWrite_fn)(uint8_t ch, uint16_t us);

#ifdef BUILD_SERVOS_MOVE_BLINK
bool servos_movedBlinkLed = false;
#endif

void ICACHE_RAM_ATTR servoNewChannelsAvailable()
{
    newChannelsAvailable = true;
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

static void servoWriteDshot(eServoOutputMode chMode, uint8_t ch, uint16_t us)
{
#if defined(PLATFORM_ESP32)
    // DBGLN("Writing DShot output: us: %u, ch: %d", us, ch);
    if (dshotInstances[ch] == nullptr)
        return;

    // check if we actually want a pulse (for no-pulse failsafe)
    if (us > 0)
    {
        uint16_t dshotVal;
        us = constrain(us, 1000, 2000);
        if (chMode == somDShot)
        {
            if (us == 1000) { // stopped
                dshotVal = DSHOT_CMD_MOTOR_STOP;
            }
            else {
                dshotVal = fmap(us, 1001, 2000, DSHOT_THROTTLE_MIN, DSHOT_THROTTLE_MAX); // Convert PWM signal in us to DShot value
            }
        }
        else // somDShot3D
        {
            if (us == 1500) { // stopped
                dshotVal = DSHOT_CMD_MOTOR_STOP;
            }
            else if (us > 1500) { // forward
                dshotVal = fmap(us, 1501, 2000, 1048, 2047);
            }
            else { // reverse
                dshotVal = fmap(us, 1499, 1000, 48, 1047);
            }
        }
        dshotInstances[ch]->send_dshot_value(dshotVal);
    }
    else
    {
        // getting an actual zero microsecond command means the failsafe mode is no-pulse
        dshotInstances[ch]->set_looping(false);
    }
#endif /* PLATFORM_ESP32 */
}

static void servoWrite(uint8_t ch, uint16_t us)
{
    const rx_config_pwm_t *chConfig = config.GetPwmChannel(ch);
    const eServoOutputMode chMode = (eServoOutputMode)chConfig->val.mode;
    if (chMode == somDShot || chMode == somDShot3D)
    {
        servoWriteDshot(chMode, ch, us);
    }
    else if (servoPins[ch] != UNDEF_PIN && pwmChannelValues[ch] != us)
    {
        pwmChannelValues[ch] = us;
        if (chMode == somOnOff)
        {
            digitalWrite(servoPins[ch], us > 1500);
        }
        else if (chMode == som10KHzDuty)
        {
            PWM.setDuty(pwmChannels[ch], constrain(us, 1000, 2000) - 1000);
        }
        else
        {
#if defined(PLATFORM_ESP32) && defined(BUILD_SHREW_PWM_ONESHOT)
            if (pwmOneShotInstances[ch] != nullptr) {
                pwmOneShotInstances[ch]->setPulse(us);
            } else if (pwmChannels[ch] != -1)
#endif
            PWM.setMicroseconds(pwmChannels[ch], us);
        }
    }
}

void servosFailsafe(bool no_pulse)
{
    if (!servo_initialized) {
        // we might be running the servos from another place in the code
        servo_initialized = true;
        servo_initializeEnable();
    }

    #ifdef BUILD_SHREW_HBRIDGE
    hbridge_failsafe();
    #endif

    for (int ch = 0 ; ch < GPIO_PIN_PWM_OUTPUTS_COUNT ; ++ch)
    {
        const rx_config_pwm_t *chConfig = config.GetPwmChannel(ch);
        if (chConfig->val.failsafeMode == PWMFAILSAFE_SET_POSITION && !no_pulse) {
            // Note: Failsafe values do not respect the inverted flag, failsafe values are absolute
            uint16_t us = chConfig->val.failsafe + US_CHANNEL_VALUE_MIN;
            // Always write the failsafe position even if the servo has never been started,
            // so all the servos go to their expected position
            servoWrite(ch, us);
        }
        else if (chConfig->val.failsafeMode == PWMFAILSAFE_NO_PULSES || no_pulse) {
            servoWrite(ch, 0);
        }
        else if (chConfig->val.failsafeMode == PWMFAILSAFE_LAST_POSITION) {
            // do nothing
        }
    }
}

static void servoCalcAllChannels(servoWrite_fn write)
{
    #ifdef BUILD_SERVOS_MOVE_BLINK
    static int32_t prev_ch_us[CRSF_NUM_CHANNELS];
    static uint32_t movement_sum = 0;
    #endif

    for (int ch = 0 ; ch < GPIO_PIN_PWM_OUTPUTS_COUNT ; ++ch)
    {
        const rx_config_pwm_t *chConfig = config.GetPwmChannel(ch);
        const unsigned crsfVal = ChannelDataMixed[chConfig->val.inputChannel];
        // crsfVal might be unset if this is a switch channel, and it has not been
        // received yet. Delay initializing the servo until the channel is valid
        if (crsfVal == CRSF_CHANNEL_VALUE_UNSET)
        {
            continue;
        }


        uint16_t us;
        if (chConfig->val.stretched)
        {
            if (OtaIsFullRes)
                us = fmap(crsfVal, CRSF_CHANNEL_VALUE_EXT_MIN, CRSF_CHANNEL_VALUE_EXT_MAX, US_CHANNEL_VALUE_MIN, US_CHANNEL_VALUE_MAX);
            else
                us = fmap(crsfVal, CRSF_CHANNEL_VALUE_STD_MIN, CRSF_CHANNEL_VALUE_STD_MAX, US_CHANNEL_VALUE_MIN, US_CHANNEL_VALUE_MAX);
        }
        else
        {
            us = CRSF_to_US(crsfVal);
        }
        // Flip the output around the mid-value if inverted
        // (1500 - usOutput) + 1500
        if (chConfig->val.inverted)
        {
            us = (2 * US_CHANNEL_VALUE_CENTER) - us;
        }

        #ifdef BUILD_SERVOS_MOVE_BLINK
        int32_t prev_us = prev_ch_us[ch];
        prev_ch_us[ch] = us;
        movement_sum += abs(us - prev_us);
        if (movement_sum >= 1000) {
            movement_sum = 0;
            servos_movedBlinkLed = true;
            devicesTriggerEvent(EVENT_SERVO_ACTIVITY);
        }
        #endif

        write(ch, us);
    } /* for each servo */
}

static void servoUsToFailsafeConfig(uint8_t ch, uint16_t us)
{
    rx_config_pwm_t newPwmCh;
    newPwmCh.raw = config.GetPwmChannel(ch)->raw;
    newPwmCh.val.failsafe = constrain(us, US_CHANNEL_VALUE_MIN, US_CHANNEL_VALUE_MAX) - US_CHANNEL_VALUE_MIN;
    //DBGLN("FSCH(%u) us=%u", ch, us);
    config.SetPwmChannelRaw(ch, newPwmCh.raw);
}

void servoCurrentToFailsafeConfig()
{
    servoCalcAllChannels(&servoUsToFailsafeConfig);
}

void servosUpdate(unsigned long now)
{
    static uint32_t lastUpdate;

    if (!servo_initialized) {
        // we might be running the servos from another place in the code
        servo_initialized = true;
        servo_initializeEnable();
    }

#if defined(PLATFORM_ESP8266) && defined(BUILD_SHREW_PWM_ONESHOT)
    // Feed active outputs for 100 ms; no-pulse failsafe outputs stop when the RF link is down.
    for (int ch = 0; ch < GPIO_PIN_PWM_OUTPUTS_COUNT; ++ch)
    {
        if (pwmChannels[ch] != -1 && servoPins[ch] != UNDEF_PIN)
        {
            const rx_config_pwm_t *chConfig = config.GetPwmChannel(ch);
            const uint16_t frequency = servoOutputModeToFrequency((eServoOutputMode)chConfig->val.mode);
            const bool noPulseFailsafe = chConfig->val.failsafeMode == PWMFAILSAFE_NO_PULSES;
            if (frequency && frequency != 10000U && (!noPulseFailsafe || connectionState == connected))
            {
                refreshWaveformOneShot8266(servoPins[ch], (frequency + 9U) / 10U);
            }
        }
    }
#endif

    #if defined(PLATFORM_ESP32_C3)
    // for ESP32-C3's implementation of DShotRMT, there's extra tasks to take care of even if no update is needed
    DShotRMT::poll();
    #endif

    if (newChannelsAvailable)
    {
        newChannelsAvailable = false;
        lastUpdate = now;

        #ifdef BUILD_SHREW_HBRIDGE
        hbridge_update(now);
        #endif

        if (custommixer_isArmed()) {
            servoCalcAllChannels(&servoWrite);
        }
        else {
            servosFailsafe(false);
        }
    }     /* if newChannelsAvailable */

    // LQ goes to 0 (100 packets missed in a row)
    // OR last update older than FAILSAFE_ABS_TIMEOUT_MS
    // go to failsafe
    else if (lastUpdate && ((getLq() == 0 && webbe_installed == false) || (now - lastUpdate > FAILSAFE_ABS_TIMEOUT_MS)))
    {
        servosFailsafe(connectionState == wifiUpdate && !webbe_ws_started);
        lastUpdate = 0;
    }
#if defined(PLATFORM_ESP32) && defined(BUILD_SHREW_PWM_ONESHOT)
    PwmOneShot::poll(connectionState == connected);
#endif
}

void servo_initializeAll()
{
    custommixer_init(config.GetCustomMixer());
    #ifdef BUILD_SHREW_HBRIDGE
    hbridge_init();
    #endif

    if (!OPT_HAS_SERVO_OUTPUT)
    {
        return;
    }

#if defined(PLATFORM_ESP32)
    #if defined(BUILD_SHREW_PWM_ONESHOT)
    uint8_t &rmtCH = nextRmtChannel;
    #else
    uint8_t rmtCH = 0;
    #endif
#endif
    for (int ch = 0; ch < GPIO_PIN_PWM_OUTPUTS_COUNT; ++ch)
    {
        pwmChannelValues[ch] = UINT16_MAX;
        pwmChannels[ch] = -1;
        int8_t pin = GPIO_PIN_PWM_OUTPUTS[ch];
#if defined(DEBUG_LOG) || defined(DEBUG_RCVR_LINKSTATS)
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
            if (rmtCH < RMT_MAX_CHANNELS)
            {
                auto gpio = (gpio_num_t)pin;
                auto rmtChannel = (rmt_channel_t)rmtCH;
                DBGLN("Initializing DShot: gpio: %u, ch: %d, rmtChannel: %u", gpio, ch, rmtChannel);
                pinMode(pin, OUTPUT);
                digitalWrite(pin, LOW);
                dshotInstances[ch] = new DShotRMT(gpio, rmtChannel); // Initialize the DShotRMT instance
                rmtCH++;
            }
            pin = UNDEF_PIN;
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

            pinMode(pin, OUTPUT);
            digitalWrite(pin, LOW);
        }
    }
}

void servo_initializeEnable()
{
    for (int ch = 0; ch < GPIO_PIN_PWM_OUTPUTS_COUNT; ++ch)
    {
        const rx_config_pwm_t *chConfig = config.GetPwmChannel(ch);
        const auto frequency = servoOutputModeToFrequency((eServoOutputMode)chConfig->val.mode);
        if (frequency && servoPins[ch] != UNDEF_PIN)
        {
#if defined(PLATFORM_ESP32) && defined(BUILD_SHREW_PWM_ONESHOT)
            if ((eServoOutputMode)chConfig->val.mode != som10KHzDuty) {
                servoInitializePwmOneShot(ch, servoPins[ch], frequency,
                    (eServoOutputFailsafeMode)chConfig->val.failsafeMode);
            } else
#endif
            pwmChannels[ch] = PWM.allocate(servoPins[ch], frequency);
#if defined(PLATFORM_ESP8266) && defined(BUILD_SHREW_PWM_ONESHOT)
            if (pwmChannels[ch] != -1 && (eServoOutputMode)chConfig->val.mode != som10KHzDuty)
            {
                enableWaveformOneShot8266(servoPins[ch]);
            }
#endif
        }
#if defined(PLATFORM_ESP32)
        else if ((eServoOutputMode)chConfig->val.mode == somDShot || (eServoOutputMode)chConfig->val.mode == somDShot3D)
        {
            dshotInstances[ch]->begin(DSHOT300, false); // Set DShot protocol and bidirectional dshot bool
        }
#endif
    }
}

void servo_shutdown()
{
    for (int ch = 0; ch < GPIO_PIN_PWM_OUTPUTS_COUNT; ++ch)
    {
        if (pwmChannels[ch] != -1)
        {
            PWM.release(pwmChannels[ch]);
            pwmChannels[ch] = -1;
        }
#if defined(PLATFORM_ESP32)
        #if defined(BUILD_SHREW_PWM_ONESHOT)
        delete pwmOneShotInstances[ch];
        pwmOneShotInstances[ch] = nullptr;
        #endif
        if (dshotInstances[ch] != nullptr)
        {
            delete dshotInstances[ch];
            dshotInstances[ch] = nullptr;
        }
#endif
        servoPins[ch] = UNDEF_PIN;
    }
#if defined(PLATFORM_ESP32) && defined(BUILD_SHREW_PWM_ONESHOT)
    nextRmtChannel = 0;
#endif
}

static bool initialize()
{
    servo_initializeAll();
    return true;
}

static int event()
{
    if (connectionState == disconnected)
    {
#if defined(PLATFORM_ESP32) && defined(BUILD_SHREW_PWM_ONESHOT)
        // Failsafe positions still need servosUpdate to poll their RMT pulses.
        return DURATION_IMMEDIATELY;
#else
        // Disconnected should come after failsafe on the RX.
        return DURATION_NEVER;
#endif
    }
    if (connectionState == wifiUpdate)
    {
        //servo_shutdown();
        servosFailsafe(!webbe_installed || !webbe_ws_started);
#if defined(PLATFORM_ESP32) && defined(BUILD_SHREW_PWM_ONESHOT)
        return DURATION_IMMEDIATELY;
#else
        return DURATION_NEVER;
#endif
    }
    if (!servo_initialized && connectionState == connected)
    {
        servo_initialized = true;
        servo_initializeEnable();
    }
    return DURATION_IMMEDIATELY;
}

void servos_singleWrite(int selected_pin, int us)
{
    for (int ch = 0; ch < GPIO_PIN_PWM_OUTPUTS_COUNT; ++ch)
    {
        // look through all the channels to find the correct pin
        int8_t pin = GPIO_PIN_PWM_OUTPUTS[ch];
        if (selected_pin != pin) { // not the correct pin
            continue;
        }
        // found the correct pin, write the value to it
        servoWrite(ch, us);
        break;
    }
}

bool servos_singleInit(int selected_pin)
{
    bool res = false;
#if defined(PLATFORM_ESP32)
    #if defined(BUILD_SHREW_PWM_ONESHOT)
    uint8_t &rmtCH = nextRmtChannel;
    #else
    uint8_t rmtCH = 0;
    #endif
#endif
    for (int ch = 0; ch < GPIO_PIN_PWM_OUTPUTS_COUNT; ++ch)
    {
        // look through all the channels to find the correct pin
        pwmChannelValues[ch] = UINT16_MAX;
        pwmChannels[ch] = -1;
        int8_t pin = GPIO_PIN_PWM_OUTPUTS[ch];
        if (selected_pin != pin) { // not the correct pin
            continue;
        }
        // found the correct pin, get its channel configuration and act accordingly
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
#if defined(PLATFORM_ESP32) && defined(BUILD_SHREW_PWM_ONESHOT)
                    if (mode != som10KHzDuty) {
                        res = servoInitializePwmOneShot(ch, servoPins[ch], frequency,
                            (eServoOutputFailsafeMode)chConfig->val.failsafeMode);
                    } else {
#endif
                    pwmChannels[ch] = PWM.allocate(servoPins[ch], frequency);
#if defined(PLATFORM_ESP8266) && defined(BUILD_SHREW_PWM_ONESHOT)
                    if (pwmChannels[ch] != -1 && mode != som10KHzDuty)
                    {
                        enableWaveformOneShot8266(servoPins[ch]);
                    }
#endif
                    res = true;
#if defined(PLATFORM_ESP32) && defined(BUILD_SHREW_PWM_ONESHOT)
                    }
#endif
                    servoWrite(ch, 0);
                }
            }
        }
    }
    return res;
}

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
        #if defined(BUILD_SHREW_PWM_ONESHOT)
        delete pwmOneShotInstances[ch];
        pwmOneShotInstances[ch] = nullptr;
        #endif
        if (dshotInstances[ch] != nullptr)
        {
            delete dshotInstances[ch];
            dshotInstances[ch] = nullptr;
        }
#endif
        servoPins[ch] = UNDEF_PIN;
    }
#if defined(PLATFORM_ESP32) && defined(BUILD_SHREW_PWM_ONESHOT)
    nextRmtChannel = 0;
#endif
}

static int timeout()
{
    servosUpdate(millis());
    return DURATION_IMMEDIATELY;
}

device_t ServoOut_device = {
    .initialize = initialize,
    .start = nullptr,
    .event = event,
    .timeout = timeout,
    .subscribe = EVENT_CONNECTION_CHANGED
};

#endif
