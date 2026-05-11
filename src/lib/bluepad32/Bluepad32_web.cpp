#include "bluepad.h"

#if defined(BUILD_BLUEPAD32) && defined(PLATFORM_ESP32)

static void bluepad_auxchan_to_json(const bluepad_auxchan_cfg_t* aux, JsonObject obj)
{
    if (!aux || obj.isNull()) {
        return;
    }

    obj["actual_channel"] = aux->actual_channel;
    #if defined(TARGET_TX)
    obj["failsafe"] = aux->failsafe;
    #endif
    obj["analog_mode"] = aux->analog_mode;
}

static void json_to_bluepad_auxchan(JsonObjectConst obj, bluepad_auxchan_cfg_t* aux)
{
    if (!aux || obj.isNull()) {
        return;
    }

    if (obj["actual_channel"].is<uint8_t>()) {
        aux->actual_channel = obj["actual_channel"].as<uint8_t>();
    }

    #if defined(TARGET_TX)
    if (obj["failsafe"].is<uint16_t>()) {
        aux->failsafe = obj["failsafe"].as<uint16_t>();
    }
    #endif

    if (obj["analog_mode"].is<uint8_t>()) {
        aux->analog_mode = obj["analog_mode"].as<uint8_t>();
    }
}

static void bluepad_button_to_json(const bluepad_btn_cfg_t* button, JsonObject obj)
{
    if (!button || obj.isNull()) {
        return;
    }

    obj["aux_chan"] = button->aux_chan;
    obj["mode"] = button->mode;
    obj["value"] = button->value;
}

static void json_to_bluepad_button(JsonObjectConst obj, bluepad_btn_cfg_t* button)
{
    if (!button || obj.isNull()) {
        return;
    }

    if (obj["aux_chan"].is<uint8_t>()) {
        button->aux_chan = obj["aux_chan"].as<uint8_t>();
    }

    if (obj["mode"].is<uint8_t>()) {
        button->mode = obj["mode"].as<uint8_t>();
    }

    if (obj["value"].is<uint16_t>()) {
        button->value = obj["value"].as<uint16_t>();
    }
}

void bluepad_config_to_json(const bluepad_cfg_t* cfg, JsonObject obj)
{
    if (!cfg || obj.isNull()) {
        return;
    }

    obj["main_mode"] = cfg->main_mode;

    JsonArray auxModes = obj["aux_mode"].to<JsonArray>();
    for (uint8_t i = 0; i < BP_AUX_CHANNEL_COUNT; ++i)
    {
        bluepad_auxchan_to_json(&cfg->aux_mode[i], auxModes.add<JsonObject>());
    }

    JsonArray buttonModes = obj["btn_mode"].to<JsonArray>();
    for (uint8_t i = 0; i < BP_BUTTON_CONFIG_COUNT; ++i)
    {
        bluepad_button_to_json(&cfg->btn_mode[i], buttonModes.add<JsonObject>());
    }
}

void json_to_bluepad_config(JsonObjectConst obj, bluepad_cfg_t* cfg)
{
    if (!cfg || obj.isNull()) {
        return;
    }

    if (obj["main_mode"].is<uint8_t>()) {
        cfg->main_mode = obj["main_mode"].as<uint8_t>();
    }

    JsonArrayConst auxModes = obj["aux_mode"].as<JsonArrayConst>();
    for (uint8_t i = 0; i < BP_AUX_CHANNEL_COUNT && i < auxModes.size(); ++i)
    {
        json_to_bluepad_auxchan(auxModes[i].as<JsonObjectConst>(), &cfg->aux_mode[i]);
    }

    JsonArrayConst buttonModes = obj["btn_mode"].as<JsonArrayConst>();
    for (uint8_t i = 0; i < BP_BUTTON_CONFIG_COUNT && i < buttonModes.size(); ++i)
    {
        json_to_bluepad_button(buttonModes[i].as<JsonObjectConst>(), &cfg->btn_mode[i]);
    }
}

#endif
