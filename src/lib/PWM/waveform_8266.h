#pragma once

void startWaveform8266(uint8_t gpio, uint32_t timeHighUS, uint32_t timeLowUS);
void stopWaveform8266(uint8_t gpio);
#ifdef BUILD_SHREW_PWM_ONESHOT
void enableWaveformOneShot8266(uint8_t gpio);
void disableWaveformOneShot8266(uint8_t gpio);
void refreshWaveformOneShot8266(uint8_t gpio, uint32_t pulses);
#endif

#define startWaveform DO_NOT_USE
#define startWaveformClockCycles DO_NOT_USE
#define stopWaveform DO_NOT_USE
#define setTimer1Callback DO_NOT_USE
#define _setPWMFreq DO_NOT_USE
#define _stopPWM DO_NOT_USE
#define _setPWM DO_NOT_USE
