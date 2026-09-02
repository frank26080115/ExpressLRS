#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "common.h"

void customcodehooks_setup();
void customcodehooks_postsetup();
void customcodehooks_onloop();

void customcodehooks_onservo(unsigned long now, bool newChannelsAvailable);
bool customcodehooks_onledevent(int* retval, int p1, int p2);

void customcodehooks_onservofailsafe(bool no_pulse);
void customcodehooks_onrfconnect(unsigned long now, int connectionState);
void customcodehooks_onrfdisconnect(bool resumeRx);

bool customcodehooks_oncrsfmessage(void* message);
bool customcodehooks_onam32kisstelemetry(void* data);
bool customcodehooks_onvesctelemetry(void* data);
