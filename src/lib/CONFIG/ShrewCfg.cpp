#include "ShrewCfg.h"
#if defined(PLATFORM_ESP8266)
#include <FS.h>
#else
#include <SPIFFS.h>
#endif

#if defined(TARGET_RX)

void shrew_appendDefaults(RxConfig* cfg, rx_config_t* rxcfg)
{
    #ifdef BUILD_SHREW_HBRIDGE_PRO
    firmwareOptions.shrew = 2;
    #endif
    #ifdef BUILD_SHREW_HBRIDGE_LITE
    firmwareOptions.shrew = 1;
    #endif

    #if defined(GPIO_PIN_PWM_OUTPUTS)
    for (unsigned int ch = 0; ch < PWM_MAX_CHANNELS - 2; ch++)
    {
        rx_config_pwm_t *pwm = &(rxcfg->pwmChannels)[ch];
        int8_t ch_pin = GPIO_PIN_PWM_OUTPUTS[ch];
        (void)ch_pin;
        if (firmwareOptions.shrew != 0) { // if shrew is a brushed ESC, then the first two channels are already used for driving
            pwm->val.inputChannel += 2;
        }
        pwm->val.failsafeMode = PWMFAILSAFE_NO_PULSES;
        #if defined(BUILD_SHREW_LACERATION_PWM)
        pwm->val.failsafeMode = PWMFAILSAFE_NO_PULSES;
        pwm->val.failsafe = 512;
        pwm->val.mode = som160Hz;
        #elif defined(BUILD_SHREW_LACERATION_DSHOT)
        pwm->val.failsafeMode = PWMFAILSAFE_SET_POSITION;
        pwm->val.failsafe = 512;
        pwm->val.mode = somDShot3D;
        #endif
        #ifdef BUILD_SHREW_VESC_UART
        if (ch_pin == GPIO_PIN_RCSIGNAL_TX) {
            pwm->val.mode = somVesc;
        }
        #endif
    }
    #endif

    rxcfg->locked_datarate = firmwareOptions.locked_datarate;
    rxcfg->shrew_mixer = firmwareOptions.shrew_mixer;
    if (firmwareOptions.permanent_binding) {
        rxcfg->bindStorage = BINDSTORAGE_PERMANENT;
    }

    #ifdef BUILD_SHREW_VESC_UART
    rxcfg->serialProtocol = PROTOCOL_VESC;
    #endif
}

void shrew_cfgReset()
{
    // SPIFFS will have already been begun
    SPIFFS.remove("/options.json");
    SPIFFS.remove("/hardware.json");
}

#endif

#if defined(TARGET_TX)

void shrew_appendDefaults(TxConfig* cfg, tx_config_t* txcfg)
{
}

#endif
