#include "common.h"
#include "options.h"
#include "config.h"
#include "crsf_protocol.h"

uint32_t ChannelDataMixed[CRSF_NUM_CHANNELS];

void shrew_mix()
{
    memcpy(ChannelDataMixed, ChannelData, sizeof(uint32_t) * CRSF_NUM_CHANNELS);

    uint32_t mixer_settings = config.GetShrewMixer();
    uint32_t ch_thr   = (mixer_settings & 0x000F);
    uint32_t ch_str   = (mixer_settings & 0x00F0) >> 4;
    uint32_t ch_left  = (mixer_settings & 0x0F00) >> 8;
    uint32_t ch_right = (mixer_settings & 0xF000) >> 12;
    uint32_t rev_left  = (mixer_settings & 0x10000);
    uint32_t rev_right = (mixer_settings & 0x20000);
    if ((ch_thr == 0 && ch_str == 0) || (ch_left == 0 && ch_right == 0)) {
        return;
    }
    int32_t val_y = ch_thr == 0 ? CRSF_CHANNEL_VALUE_MID : ChannelData[ch_thr - 1];
    int32_t val_x = ch_str == 0 ? CRSF_CHANNEL_VALUE_MID : ChannelData[ch_str - 1];
    val_y -= CRSF_CHANNEL_VALUE_MID;
    val_x -= CRSF_CHANNEL_VALUE_MID;
    int32_t val_left  = val_y + val_x;
    int32_t val_right = val_y - val_x;
    val_left  += CRSF_CHANNEL_VALUE_MID;
    val_right += CRSF_CHANNEL_VALUE_MID;
    val_left   = (rev_left  == 0) ? val_left  : ((CRSF_CHANNEL_VALUE_MID * 2) - val_left );
    val_right  = (rev_right == 0) ? val_right : ((CRSF_CHANNEL_VALUE_MID * 2) - val_right);
    val_left   = val_left  > CRSF_CHANNEL_VALUE_MAX ? CRSF_CHANNEL_VALUE_MAX : (val_left  < CRSF_CHANNEL_VALUE_MIN ? CRSF_CHANNEL_VALUE_MIN : val_left);
    val_right  = val_right > CRSF_CHANNEL_VALUE_MAX ? CRSF_CHANNEL_VALUE_MAX : (val_right < CRSF_CHANNEL_VALUE_MIN ? CRSF_CHANNEL_VALUE_MIN : val_right);
    if (ch_left != 0) {
        ChannelDataMixed[ch_left  - 1] = val_left;
    }
    if (ch_right != 0) {
        ChannelDataMixed[ch_right - 1] = val_right;
    }
}

bool shrew_failsafeSwitchIsOn()
{
    uint16_t settings = config.GetShrewFailsafeSwitch();
    uint16_t sw_ch = settings & 0xFF;
    uint16_t sw_pos = (settings & 0xFF00) >> 8;
    if (sw_ch == 0) {
        return false;
    }
    int32_t ch_val = ChannelData[sw_ch - 1];
    #define CRSF_CHANNEL_VALUE_SPAN (CRSF_CHANNEL_VALUE_MAX - CRSF_CHANNEL_VALUE_MIN)
    #define CRSF_CHANNEL_VALUE_3RD  (CRSF_CHANNEL_VALUE_SPAN / 3)
    if ((sw_pos & (1 << 0)) != 0) {
        if (ch_val <= (CRSF_CHANNEL_VALUE_MIN + CRSF_CHANNEL_VALUE_3RD)) {
            return false;
        }
    }
    if ((sw_pos & (1 << 1)) != 0) {
        if (ch_val >= (CRSF_CHANNEL_VALUE_MIN + CRSF_CHANNEL_VALUE_3RD) && ch_val <= (CRSF_CHANNEL_VALUE_MIN + CRSF_CHANNEL_VALUE_3RD + CRSF_CHANNEL_VALUE_3RD)) {
            return false;
        }
    }
    if ((sw_pos & (1 << 2)) != 0) {
        if (ch_val >= (CRSF_CHANNEL_VALUE_MAX - CRSF_CHANNEL_VALUE_3RD)) {
            return false;
        }
    }
    return true;
}
