#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "common.h"

void customcodehooks_setup();
void customcodehooks_postsetup();
void customcodehooks_onloop();

void customcodehooks_onservo(unsigned long now, bool newChannelsAvailable);
bool customcodehooks_onledevent(int *retval, int connectionState, bool hasRGBLeds);
bool customcodehooks_onrgbevent(int *retval, int connectionState, int blinkyState);

void customcodehooks_onservofailsafe(bool no_pulse);
void customcodehooks_onrfconnect(unsigned long now, int connectionState);
void customcodehooks_onrfdisconnect(bool resumeRx);

bool customcodehooks_oncrsfmessage(void* message);
bool customcodehooks_onam32kisstelemetry(void* data);
bool customcodehooks_onvesctelemetry(void* data);
