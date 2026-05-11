#include "bluepad.h"

#if defined(BUILD_BLUEPAD32) && defined(PLATFORM_ESP32)

#include <ESPAsyncWebServer.h>

#include <ArduinoBluepad32.h>
#include <bt/uni_bt.h>
#include <btstack.h>

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

static void bluepad_send_json(AsyncWebServerRequest* request, JsonDocument& doc, int code = 200)
{
    String responseBody;
    serializeJson(doc, responseBody);
    request->send(code, "application/json", responseBody);
}

static void bluepad_send_status(AsyncWebServerRequest* request, bool ok, const char* message, int code = 200)
{
    JsonDocument doc;
    doc["ok"] = ok;
    doc["message"] = message;
    doc["pairing"] = uni_bt_enable_new_connections_is_enabled();
    bluepad_send_json(request, doc, code);
}

static const AsyncWebParameter* bluepad_find_address_param(AsyncWebServerRequest* request)
{
    if (request->hasParam("addr")) {
        return request->getParam("addr");
    }
    if (request->hasParam("address")) {
        return request->getParam("address");
    }
    if (request->hasParam("addr", true)) {
        return request->getParam("addr", true);
    }
    if (request->hasParam("address", true)) {
        return request->getParam("address", true);
    }
    return nullptr;
}

static bool bluepad_parse_bd_addr(const String& value, bd_addr_t address)
{
    return sscanf_bd_addr(value.c_str(), address) != 0;
}

static void bluepad_add_paired_devices(JsonArray devices)
{
    bd_addr_t address;
    link_key_t linkKey;
    link_key_type_t type;
    btstack_link_key_iterator_t iterator;

    int ok = gap_link_key_iterator_init(&iterator);
    if (!ok) {
        return;
    }

    while (gap_link_key_iterator_get_next(&iterator, address, linkKey, &type)) {
        JsonObject device = devices.add<JsonObject>();
        device["address"] = bd_addr_to_str(address);
        device["type"] = (uint8_t)type;
    }

    gap_link_key_iterator_done(&iterator);
}

static void bluepad_handle_devices(AsyncWebServerRequest* request)
{
    JsonDocument doc;
    doc["ok"] = true;
    doc["pairing"] = uni_bt_enable_new_connections_is_enabled();
    JsonArray devices = doc["devices"].to<JsonArray>();
    bluepad_add_paired_devices(devices);
    bluepad_send_json(request, doc);
}

static void bluepad_handle_pairing(AsyncWebServerRequest* request, bool enabled)
{
    BP32.enableNewBluetoothConnections(enabled);
    bluepad_send_status(request, true, enabled ? "Pairing enabled" : "Pairing disabled");
}

static void bluepad_handle_delete_device(AsyncWebServerRequest* request)
{
    const AsyncWebParameter* param = bluepad_find_address_param(request);
    if (param == nullptr) {
        bluepad_send_status(request, false, "Missing Bluetooth address", 400);
        return;
    }

    bd_addr_t address;
    if (!bluepad_parse_bd_addr(param->value(), address)) {
        bluepad_send_status(request, false, "Invalid Bluetooth address", 400);
        return;
    }

    gap_drop_link_key_for_bd_addr(address);
    bluepad_send_status(request, true, "Paired device deleted");
}

static void bluepad_handle_delete_all_devices(AsyncWebServerRequest* request)
{
    BP32.forgetBluetoothKeys();
    bluepad_send_status(request, true, "All paired devices deleted");
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

void bluepad_setupServer(AsyncWebServer* srv)
{
    srv->on("/bluepad/devices.json", HTTP_GET, bluepad_handle_devices);
    srv->on("/bluepad/paired.json", HTTP_GET, bluepad_handle_devices);
    srv->on("/bluepad/pairing/enable", HTTP_POST, [](AsyncWebServerRequest* request) {
        bluepad_handle_pairing(request, true);
    });
    srv->on("/bluepad/pairing/disable", HTTP_POST, [](AsyncWebServerRequest* request) {
        bluepad_handle_pairing(request, false);
    });
    srv->on("/bluepad/devices/delete", HTTP_POST, bluepad_handle_delete_device);
    srv->on("/bluepad/devices/delete-all", HTTP_POST, bluepad_handle_delete_all_devices);
}

#endif
