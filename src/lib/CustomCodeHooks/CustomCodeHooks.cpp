#include "CustomCodeHooks.h"

// Keep changes to this file to a minimum
// Expect to solve merge conflicts when updating to a new version of ExpressLRS if you have modified this file
// Use this file to call into functions from your own custom code, which you can add to the project in a separate file
// Do not modify the function signatures, as they are called from other files

// always called at the beginning of `setup()`
// it is likely you will never use this
void customcodehooks_setup()
{

}

// always called at the end of `setup()`
// it is likely you will never use this
void customcodehooks_postsetup()
{

}

// always called at the top of `loop()`
// it is likely you will never use this, code inside here is dangerous
// do not add any blocking code here, as it will break the loop timing
void customcodehooks_onloop()
{

}

// always called at the top of `void servosUpdate(unsigned long now)`
// it should *probably* not do anything if `newChannelsAvailable` is false
// it is NOT called at your PWM rate, it is called at the scheduler rate, which is very fast, as it returns `DURATION_IMMEDIATELY` to the scheduler
// hence why checking `newChannelsAvailable` is important
void customcodehooks_onservo(unsigned long now, bool newChannelsAvailable)
{
    (void)now;
    (void)newChannelsAvailable;
}

// always called at the top of the devLED or devRGB `event()` function
// returning true will override the LED event and return the value in `*retval`
// returning false will allow the normal LED event to continue
// `p1` is the `connectionState`, `p2` is the `blinkyState` (for `devRGB`) or `hasRGBLeds` (for `devLED`)
// the contents of `retval` is supposed to be the milliseconds between LED updates, as it will be used as the return value of the `event()` function
bool customcodehooks_onledevent(int* retval, int p1, int p2)
{
    (void)retval;
    (void)p1;
    (void)p2;
    return false;
}

// always called at the bottom of `servosFailsafe(bool no_pulse)`
void customcodehooks_onservofailsafe(bool no_pulse)
{
    (void)no_pulse;
}

// always called at the bottom of `GotConnection(unsigned long now)`
void customcodehooks_onrfconnect(unsigned long now, int connectionState)
{
    (void)now;
    (void)connectionState;
}

// always called at the bottom of `LostConnection(bool resumeRx)`
void customcodehooks_onrfdisconnect(bool resumeRx)
{
    (void)resumeRx;
}

// always called at the top of `void SerialCRSF::forwardMessage(const crsf_header_t *message)`
// suggest casting `message` to `crsf_header_t *` before using it
// you can edit the message contents before it gets forwarded to the FC, but be careful, as it will break the CRSF protocol if you change the length or CRC
// returning true will cause the message to be not forwarded
bool customcodehooks_oncrsfmessage(void* message)
{
    (void)message;
    return false;
}

// always called at the top of `static void am32kiss_sendTelemetry(const kiss_telem_pkt_t *data)`
// suggest casting `data` to `kiss_telem_pkt_t *` before using it
// you can view and/or edit the data contents before it gets sent
// returning true will cause the telemetry to be not sent
bool customcodehooks_onam32kisstelemetry(void* data)
{
    (void)data;
    return false;
}

// always called at the top of `static void vesc_sendTelemetry(vesc_telem_t* data)`
// suggest casting `data` to `vesc_telem_t *` before using it
// you can view and/or edit the data contents before it gets sent
// returning true will cause the telemetry to be not sent
bool customcodehooks_onvesctelemetry(void* data)
{
    (void)data;
    return false;
}
