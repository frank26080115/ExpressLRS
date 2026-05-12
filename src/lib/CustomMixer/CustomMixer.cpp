#include "device.h" // only here to force build chain to not miss the file
#include "crsf_protocol.h"
#include "CustomMixer.h"
#include <ArduinoJson.h>
#include <math.h>

custom_mixer_t* custom_mixer;
uint32_t ChannelDataMixed[CRSF_NUM_CHANNELS];
static bool armed_switch_armed = false;

static void custommixer_arcadetankmix();
static void custommixer_customarmswitch();
static void custommixer_mixaux();
static int32_t apply_deadzone(int32_t x, uint8_t dz);
static int32_t apply_expo(int32_t x, int8_t c);
static int32_t apply_scale(int32_t x, int8_t scale);
static int32_t apply_kickstart(int32_t x, uint8_t ks);
static int32_t apply_offset(int32_t x, int8_t offset);

extern bool webbe_installed;

void custommixer_init(const custom_mixer_t* cfg_ptr)
{
    custom_mixer = (custom_mixer_t*)cfg_ptr;
}

void custommixer_mix()
{
    // copy first to make sure that even if nothing happens, the PWM code still has data to use
    memcpy(ChannelDataMixed, ChannelData, sizeof(uint32_t) * CRSF_NUM_CHANNELS);

    #ifdef BUILD_CUSTOM_MIXER
    custommixer_arcadetankmix();
    custommixer_mixaux();
    custommixer_customarmswitch();
    #endif
}

bool custommixer_isArmed()
{
    #ifdef BUILD_CUSTOM_MIXER
    if (webbe_installed) {
        // allow the servos to run if using the websocket
        return true;
    }
    return armed_switch_armed;
    #else
    return true;
    #endif
}

static void custommixer_arcadetankmix()
{
    #ifdef BUILD_CUSTOM_MIXER

    uint8_t ch_thr   = custom_mixer->ch_throttle;
    uint8_t ch_str   = custom_mixer->ch_steering;
    uint8_t ch_left  = custom_mixer->ch_left;
    uint8_t ch_right = custom_mixer->ch_right;

    if ((ch_thr == 0 && ch_str == 0) || (ch_left == 0 && ch_right == 0)) {
        // no channels assigned, do nothing
        return;
    }

    // get the current value, or use middle value if unassigned
    int32_t val_y = ch_thr == 0 ? CRSF_CHANNEL_VALUE_MID : ChannelData[ch_thr - 1];
    int32_t val_x = ch_str == 0 ? CRSF_CHANNEL_VALUE_MID : ChannelData[ch_str - 1];

    // pass unset values to output
    if (val_y == CRSF_CHANNEL_VALUE_UNSET || val_x == CRSF_CHANNEL_VALUE_UNSET) {
        if (ch_left != 0) {
            ChannelDataMixed[ch_left  - 1] = CRSF_CHANNEL_VALUE_UNSET;
        }
        if (ch_right != 0) {
            ChannelDataMixed[ch_right - 1] = CRSF_CHANNEL_VALUE_UNSET;
        }
        return;
    }

    // 0 means center
    val_y -= CRSF_CHANNEL_VALUE_MID;
    val_x -= CRSF_CHANNEL_VALUE_MID;

    // apply preconditioning curve
    val_y = apply_deadzone (val_y, custom_mixer->curve_throttle.deadzone);
    val_x = apply_deadzone (val_x, custom_mixer->curve_steering.deadzone);
    val_y = apply_expo     (val_y, custom_mixer->curve_throttle.curve);
    val_x = apply_expo     (val_x, custom_mixer->curve_steering.curve);
    val_y = apply_scale    (val_y, custom_mixer->curve_throttle.scale);
    val_x = apply_scale    (val_x, custom_mixer->curve_steering.scale);
    val_y = apply_kickstart(val_y, custom_mixer->curve_throttle.antideadzone);
    val_x = apply_kickstart(val_x, custom_mixer->curve_steering.antideadzone);
    val_y = apply_offset   (val_y, custom_mixer->curve_throttle.offset);
    val_x = apply_offset   (val_x, custom_mixer->curve_steering.offset);

    // do the actual mixing
    int32_t val_l = val_y + val_x;
    int32_t val_r = val_y - val_x;

    // apply output conditioning curves
    val_l = apply_deadzone (val_l, custom_mixer->curve_left .deadzone);
    val_r = apply_deadzone (val_r, custom_mixer->curve_right.deadzone);
    val_l = apply_expo     (val_l, custom_mixer->curve_left .curve);
    val_r = apply_expo     (val_r, custom_mixer->curve_right.curve);
    val_l = apply_scale    (val_l, custom_mixer->curve_left .scale);
    val_r = apply_scale    (val_r, custom_mixer->curve_right.scale);
    val_l = apply_kickstart(val_l, custom_mixer->curve_left .antideadzone);
    val_r = apply_kickstart(val_r, custom_mixer->curve_right.antideadzone);
    val_l = apply_offset   (val_l, custom_mixer->curve_left .offset);
    val_r = apply_offset   (val_r, custom_mixer->curve_right.offset);

    // back to original CRSF range
    val_l += CRSF_CHANNEL_VALUE_MID;
    val_r += CRSF_CHANNEL_VALUE_MID;

    // clamp to range
    val_l = val_l > CRSF_CHANNEL_VALUE_MAX ? CRSF_CHANNEL_VALUE_MAX : (val_l < CRSF_CHANNEL_VALUE_MIN ? CRSF_CHANNEL_VALUE_MIN : val_l);
    val_r = val_r > CRSF_CHANNEL_VALUE_MAX ? CRSF_CHANNEL_VALUE_MAX : (val_r < CRSF_CHANNEL_VALUE_MIN ? CRSF_CHANNEL_VALUE_MIN : val_r);

    // put data in buffer at targeted channels
    if (ch_left != 0) {
        ChannelDataMixed[ch_left  - 1] = val_l;
    }
    if (ch_right != 0) {
        ChannelDataMixed[ch_right - 1] = val_r;
    }

    #endif
}

static void custommixer_customarmswitch()
{
    #ifdef BUILD_CUSTOM_MIXER

    uint8_t sw_ch = custom_mixer->ch_arm;
    uint8_t sw_pos = custom_mixer->arming_range;

    // if nothing configured, then assume armed
    if (sw_ch == 0) {
        armed_switch_armed = true;
        return;
    }

    // something configured, so default to safe (unarmed)
    armed_switch_armed = false;

    int32_t ch_val = ChannelDataMixed[sw_ch - 1];

    if (ch_val == CRSF_CHANNEL_VALUE_UNSET) {
        return;
    }


    const int32_t CRSF_CHANNEL_VALUE_SPAN = (CRSF_CHANNEL_VALUE_MAX - CRSF_CHANNEL_VALUE_MIN);
    const int32_t CRSF_CHANNEL_VALUE_3RD  = (CRSF_CHANNEL_VALUE_SPAN / 3);
    if ((sw_pos & (1 << 0)) != 0) {
        if (ch_val <= (CRSF_CHANNEL_VALUE_MIN + CRSF_CHANNEL_VALUE_3RD)) {
            armed_switch_armed = true;
        }
    }
    if ((sw_pos & (1 << 1)) != 0) {
        if (ch_val >= (CRSF_CHANNEL_VALUE_MIN + CRSF_CHANNEL_VALUE_3RD) && ch_val <= (CRSF_CHANNEL_VALUE_MIN + CRSF_CHANNEL_VALUE_3RD + CRSF_CHANNEL_VALUE_3RD)) {
            armed_switch_armed = true;
        }
    }
    if ((sw_pos & (1 << 2)) != 0) {
        if (ch_val >= (CRSF_CHANNEL_VALUE_MAX - CRSF_CHANNEL_VALUE_3RD)) {
            armed_switch_armed = true;
        }
    }

    #endif
}

static void custommixer_mixaux()
{
    #ifdef BUILD_CUSTOM_MIXER

    for (int i = 0; i < 2; i++)
    {
        uint8_t ch = (i == 1) ? custom_mixer->ch_aux2 : custom_mixer->ch_aux1;
        mixercurve_t* cfg = (i == 1) ? &(custom_mixer->curve_aux2) : &(custom_mixer->curve_aux1);
        if (ch != 0) // is configured
        {
            ch -= 1; // the array is 0 indexed
            int32_t v = ChannelDataMixed[ch];

            // pass on unset values directly
            if (v == CRSF_CHANNEL_VALUE_UNSET) {
                ChannelDataMixed[ch] = CRSF_CHANNEL_VALUE_UNSET;
                continue;
            }

            v = apply_deadzone (v, cfg->deadzone);
            v = apply_expo     (v, cfg->curve);
            v = apply_scale    (v, cfg->scale);
            v = apply_kickstart(v, cfg->antideadzone);
            v = apply_offset   (v, cfg->offset);
            ChannelDataMixed[ch] = v; // write back into array
        }
    }

    #endif
}

static int32_t apply_deadzone(int32_t x, uint8_t dz)
{
    int32_t dz32 = dz;
    if (x <= dz32 && x >= -dz32) {
        return 0;
    }
    return x - (dz32 * ((x > 0) ? 1 : -1));
}

static int32_t apply_expo(int32_t x, int8_t curve)
{
    #ifdef BUILD_CUSTOM_MIXER

    if (x == 0 || curve == 0) {
        return x;
    }

    int32_t s  = x >= 0 ? 1 : -1;
    int32_t ax = x >= 0 ? x : -x;

    int32_t input_max = CRSF_CHANNEL_VALUE_MAX - CRSF_CHANNEL_VALUE_MID;

    float t = (float)ax / (float)input_max;
    float c = (float)(curve < 0 ? -curve : curve) * 0.1f;

    float shaped;

    if (c < 1e-6f)
    {
        shaped = t;
    }
    else
    {
        // positive curve version
        float y_pos = t * expf(t * c) / expf(c);

        if (curve > 0)
        {
            shaped = y_pos;
        }
        else
        {
            // negative curve: flip without using negative c
            float u = 1.0f - t;
            float y_u = u * expf(u * c) / expf(c);
            shaped = 1.0f - y_u;
        }
    }

    // scale back to CRSF range
    int32_t y = (int32_t)roundf(shaped * (float)input_max);

    return s * y;

    #else
    return x;
    #endif
}

static int32_t apply_scale(int32_t x, int8_t scale)
{
    const int32_t m = 100;
    int32_t percent = (100 * m) + (((int32_t)CUSTOMMIXER_SCALE_STEP * m) * (int32_t)scale);

    // do multiply first for precision
    int32_t y = x * percent;
    y /= 100 * m;

    return y;
}

static int32_t apply_kickstart(int32_t x, uint8_t ks)
{
    if (x == 0) {
        return x;
    }
    int32_t ks32 = ks;
    return x + ((x > 0) ? ks32 : -ks32);
}

static int32_t apply_offset(int32_t x, int8_t offset)
{
    return x + offset;
}

/*
    Helper: serialize one mixercurve_t into an existing JsonObject.
*/
static void mixercurve_to_json(const mixercurve_t* curve, JsonObject obj)
{
    #ifdef BUILD_CUSTOM_MIXER

    if (!curve || obj.isNull()) {
        return;
    }

    obj["deadzone"]     = curve->deadzone;
    obj["curve"]        = curve->curve;
    obj["scale"]        = curve->scale;
    obj["antideadzone"] = curve->antideadzone;
    obj["offset"]       = curve->offset;

    #endif
}


/*
    Helper: deserialize one mixercurve_t from a JsonObject.

    Missing keys leave the current field unchanged.
    This is nice for partial updates from a web UI.
*/
static void json_to_mixercurve(JsonObjectConst obj, mixercurve_t* curve)
{
    #ifdef BUILD_CUSTOM_MIXER

    if (!curve || obj.isNull()) {
        return;
    }

    if (obj["deadzone"].is<uint8_t>()) {
        curve->deadzone = obj["deadzone"].as<uint8_t>();
    }

    if (obj["curve"].is<int>()) {
        curve->curve = (int8_t)obj["curve"].as<int>();
    }

    if (obj["scale"].is<int>()) {
        curve->scale = (int8_t)obj["scale"].as<int>();
    }

    if (obj["antideadzone"].is<uint8_t>()) {
        curve->antideadzone = obj["antideadzone"].as<uint8_t>();
    }

    if (obj["offset"].is<int>()) {
        curve->offset = (int8_t)obj["offset"].as<int>();
    }

    #endif
}


/*
    Top-level serialize function.

    Caller must create the JsonObject first, then pass it in.
*/
void custom_mixer_to_json(const custom_mixer_t* mixer, JsonObject obj)
{
    #ifdef BUILD_CUSTOM_MIXER

    if (!mixer || obj.isNull()) {
        return;
    }

    obj["ch_throttle"] = mixer->ch_throttle;
    obj["ch_steering"] = mixer->ch_steering;
    obj["ch_left"]     = mixer->ch_left;
    obj["ch_right"]    = mixer->ch_right;

    mixercurve_to_json(&mixer->curve_throttle, obj["curve_throttle"].to<JsonObject>());
    mixercurve_to_json(&mixer->curve_steering, obj["curve_steering"].to<JsonObject>());
    mixercurve_to_json(&mixer->curve_left,     obj["curve_left"].to<JsonObject>());
    mixercurve_to_json(&mixer->curve_right,    obj["curve_right"].to<JsonObject>());

    obj["ch_aux1"] = mixer->ch_aux1;
    mixercurve_to_json(&mixer->curve_aux1, obj["curve_aux1"].to<JsonObject>());

    obj["ch_aux2"] = mixer->ch_aux2;
    mixercurve_to_json(&mixer->curve_aux2, obj["curve_aux2"].to<JsonObject>());

    obj["ch_arm"]       = mixer->ch_arm;
    obj["arming_range"] = mixer->arming_range;

    #endif
}


/*
    Top-level deserialize function.

    Missing keys leave the existing struct contents unchanged.
    So if you want a full reset-before-load behavior, do memset() first.
*/
void json_to_custom_mixer(JsonObjectConst obj, custom_mixer_t* mixer)
{
    #ifdef BUILD_CUSTOM_MIXER

    if (!mixer || obj.isNull()) {
        return;
    }

    if (obj["ch_throttle"].is<uint8_t>()) {
        mixer->ch_throttle = obj["ch_throttle"].as<uint8_t>();
    }

    if (obj["ch_steering"].is<uint8_t>()) {
        mixer->ch_steering = obj["ch_steering"].as<uint8_t>();
    }

    if (obj["ch_left"].is<uint8_t>()) {
        mixer->ch_left = obj["ch_left"].as<uint8_t>();
    }

    if (obj["ch_right"].is<uint8_t>()) {
        mixer->ch_right = obj["ch_right"].as<uint8_t>();
    }

    json_to_mixercurve(obj["curve_throttle"].as<JsonObjectConst>(), &mixer->curve_throttle);
    json_to_mixercurve(obj["curve_steering"].as<JsonObjectConst>(), &mixer->curve_steering);
    json_to_mixercurve(obj["curve_left"].as<JsonObjectConst>(),     &mixer->curve_left);
    json_to_mixercurve(obj["curve_right"].as<JsonObjectConst>(),    &mixer->curve_right);

    if (obj["ch_aux1"].is<uint8_t>()) {
        mixer->ch_aux1 = obj["ch_aux1"].as<uint8_t>();
    }
    json_to_mixercurve(obj["curve_aux1"].as<JsonObjectConst>(), &mixer->curve_aux1);

    if (obj["ch_aux2"].is<uint8_t>()) {
        mixer->ch_aux2 = obj["ch_aux2"].as<uint8_t>();
    }
    json_to_mixercurve(obj["curve_aux2"].as<JsonObjectConst>(), &mixer->curve_aux2);

    if (obj["ch_arm"].is<uint8_t>()) {
        mixer->ch_arm = obj["ch_arm"].as<uint8_t>();
    }

    if (obj["arming_range"].is<uint8_t>()) {
        mixer->arming_range = obj["arming_range"].as<uint8_t>();
    }

    #endif
}
