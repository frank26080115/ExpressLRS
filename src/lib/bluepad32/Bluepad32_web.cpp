#include "bluepad.h"

#if defined(BUILD_BLUEPAD32) && defined(PLATFORM_ESP32)

#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <pgmspace.h>

#include <algorithm>
#include <ArduinoBluepad32.h>
#include <bt/uni_bt.h>
#include <btstack.h>

extern ControllerPtr myControllers[BP32_MAX_GAMEPADS];

// Bluepad32 uses a lot of heap memory, and if AsyncWebServer doesn't have enough memory
// it does this weird thing where it sends the compressed payload without any HTTP headers
// the solution is to use a response handler that sends back the response in smaller chunks
class BluepadWebAssetResponse final : public AsyncWebServerResponse
{
public:
    BluepadWebAssetResponse(const char* contentType, const uint8_t* content, size_t len)
        : _content(content), _headSent(0)
    {
        _code = 200;
        _contentType = contentType;
        _contentLength = len;
    }

    bool _sourceValid() const override
    {
        return _content != nullptr;
    }

    void _respond(AsyncWebServerRequest* request) override
    {
        addHeader("Connection", "close", false);
        _assembleHead(_head, request->version());
        _state = RESPONSE_HEADERS;
        _ack(request, 0, 0);
    }

    size_t _ack(AsyncWebServerRequest* request, size_t len, uint32_t time) override
    {
        (void)time;
        _ackedLength += len;

        AsyncClient* client = request->client();
        if (client == nullptr) {
            _state = RESPONSE_FAILED;
            return 0;
        }

        size_t wrote = 0;
        size_t space = client->space();

        if (_state == RESPONSE_HEADERS) {
            const size_t headRemaining = _head.length() - _headSent;
            const size_t toWrite = std::min(space, headRemaining);
            if (toWrite == 0) {
                return 0;
            }

            const size_t written = client->write(_head.c_str() + _headSent, toWrite);
            _headSent += written;
            _writtenLength += written;
            wrote += written;
            space -= written;

            if (written == 0 || _headSent < _head.length()) {
                return wrote;
            }

            _head = "";
            _headSent = 0;
            _state = RESPONSE_CONTENT;
        }

        if (_state == RESPONSE_CONTENT) {
            const size_t contentRemaining = _contentLength - _sentLength;
            if (contentRemaining == 0) {
                _state = RESPONSE_WAIT_ACK;
                return wrote;
            }

            static constexpr size_t CHUNK_SIZE = 512;
            uint8_t buffer[CHUNK_SIZE];
            const size_t toWrite = std::min(std::min(space, contentRemaining), CHUNK_SIZE);
            if (toWrite == 0) {
                return wrote;
            }

            memcpy_P(buffer, _content + _sentLength, toWrite);
            const size_t written = client->write(reinterpret_cast<const char*>(buffer), toWrite);
            _sentLength += written;
            _writtenLength += written;
            wrote += written;

            if (_sentLength == _contentLength) {
                _state = RESPONSE_WAIT_ACK;
            }
            return wrote;
        }

        if (_state == RESPONSE_WAIT_ACK && _ackedLength >= _writtenLength) {
            _state = RESPONSE_END;
        }

        return wrote;
    }

private:
    const uint8_t* _content;
    String _head;
    size_t _headSent;
};

AsyncWebServerResponse* bluepad_create_web_asset_response(const char* contentType, const uint8_t* content, size_t len)
{
    return new BluepadWebAssetResponse(contentType, content, len);
}

static void bluepad_auxchan_to_json(const bluepad_auxchan_cfg_t* aux, JsonObject obj)
{
    if (!aux || obj.isNull()) {
        return;
    }

    obj["actual_channel"] = aux->actual_channel;
    obj["failsafe"] = aux->failsafe;
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

    if (obj["failsafe"].is<uint16_t>()) {
        aux->failsafe = obj["failsafe"].as<uint16_t>();
    }

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
    bool pairingEnabled = false;
    int hciState = -1;
    BLUEPAD_BTSTACK_DO_UNSAFE({
        pairingEnabled = uni_bt_enable_new_connections_is_enabled();
        hciState = hci_get_state();
    });
    doc["pairing"] = pairingEnabled;
    doc["hci_state"] = hciState;
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

static const AsyncWebParameter* bluepad_find_param(AsyncWebServerRequest* request, const char* name)
{
    if (request->hasParam(name)) {
        return request->getParam(name);
    }
    if (request->hasParam(name, true)) {
        return request->getParam(name, true);
    }
    return nullptr;
}

static bool bluepad_parse_bool_param(const String& value, bool* enabled)
{
    if (value == "1" || value.equalsIgnoreCase("true") || value.equalsIgnoreCase("enable")
        || value.equalsIgnoreCase("enabled") || value.equalsIgnoreCase("on"))
    {
        *enabled = true;
        return true;
    }

    if (value == "0" || value.equalsIgnoreCase("false") || value.equalsIgnoreCase("disable")
        || value.equalsIgnoreCase("disabled") || value.equalsIgnoreCase("off"))
    {
        *enabled = false;
        return true;
    }

    return false;
}

static bool bluepad_parse_bd_addr(const String& value, bd_addr_t address)
{
    return sscanf_bd_addr(value.c_str(), address) != 0;
}

static void bluepad_add_paired_devices(JsonArray devices)
{
    BLUEPAD_BTSTACK_DO_UNSAFE({
        bd_addr_t address;
        link_key_t linkKey;
        link_key_type_t type;
        btstack_link_key_iterator_t iterator;

        int ok = gap_link_key_iterator_init(&iterator);
        if (ok) {
            while (gap_link_key_iterator_get_next(&iterator, address, linkKey, &type)) {
                JsonObject device = devices.add<JsonObject>();
                device["address"] = bd_addr_to_str(address);
                device["type"] = (uint8_t)type;

                for (int i = 0; i < BP32_MAX_GAMEPADS; ++i) {
                    ControllerPtr controller = myControllers[i];
                    if (controller == nullptr || !controller->isConnected() || !controller->isGamepad()) {
                        continue;
                    }

                    ControllerProperties properties = controller->getProperties();
                    if (bd_addr_cmp(address, properties.btaddr) == 0) {
                        device["name"] = controller->getModelName();
                        break;
                    }
                }
            }

            gap_link_key_iterator_done(&iterator);
        }
    });
}

static void bluepad_handle_devices(AsyncWebServerRequest* request)
{
    bool pairingEnabled = false;
    BLUEPAD_BTSTACK_DO_UNSAFE({
        pairingEnabled = uni_bt_enable_new_connections_is_enabled();
    });

    JsonDocument doc;
    doc["ok"] = true;
    doc["pairing"] = pairingEnabled;
    JsonArray devices = doc["devices"].to<JsonArray>();
    bluepad_add_paired_devices(devices);
    bluepad_send_json(request, doc);
}

static void bluepad_handle_pairing(AsyncWebServerRequest* request)
{
    const AsyncWebParameter* param = bluepad_find_param(request, "enabled");
    if (param == nullptr) {
        param = bluepad_find_param(request, "enable");
    }
    if (param == nullptr) {
        bluepad_send_status(request, false, "Missing pairing state", 400);
        return;
    }

    bool enabled = false;
    if (!bluepad_parse_bool_param(param->value(), &enabled)) {
        bluepad_send_status(request, false, "Invalid pairing state", 400);
        return;
    }

    BLUEPAD_BTSTACK_DO_UNSAFE({
        uni_bt_enable_new_connections_unsafe(enabled);
    });
    bluepad_send_status(request, true, enabled ? "Pairing enabled" : "Pairing disabled");
}

static void bluepad_handle_delete_device(AsyncWebServerRequest* request)
{
    const AsyncWebParameter* param = bluepad_find_address_param(request);
    if (param == nullptr) {
        bluepad_send_status(request, false, "Missing Bluetooth address", 400);
        return;
    }

    if (param->value().equalsIgnoreCase("all")) {
        BLUEPAD_BTSTACK_DO_UNSAFE({
            uni_bt_del_keys_unsafe();
        });
        bluepad_send_status(request, true, "All paired devices deleted");
        return;
    }

    bd_addr_t address;
    if (!bluepad_parse_bd_addr(param->value(), address)) {
        bluepad_send_status(request, false, "Invalid Bluetooth address", 400);
        return;
    }

    BLUEPAD_BTSTACK_DO_UNSAFE({
        gap_drop_link_key_for_bd_addr(address);
    });
    bluepad_send_status(request, true, "Paired device deleted");
}

static void bluepad_handle_temporary_config(AsyncWebServerRequest* request, JsonVariant& json)
{
    JsonObjectConst root = json.as<JsonObjectConst>();
    if (root.isNull()) {
        bluepad_send_status(request, false, "Invalid Bluepad config", 400);
        return;
    }

    JsonObjectConst cfg = root;
    if (root["bluepad"].is<JsonObjectConst>()) {
        cfg = root["bluepad"].as<JsonObjectConst>();
    }
    else if (root["config"]["bluepad"].is<JsonObjectConst>()) {
        cfg = root["config"]["bluepad"].as<JsonObjectConst>();
    }

    bluepad_setDefaults(&bluepadTemporaryConfig);
    json_to_bluepad_config(cfg, &bluepadTemporaryConfig);
    bluepadTemporaryConfigValid = true;
    bluepadTemporaryConfigUpdated = true;

    JsonDocument doc;
    doc["ok"] = true;
    doc["message"] = "Temporary Bluepad config updated";
    doc["temporary"] = true;
    bluepad_config_to_json(&bluepadTemporaryConfig, doc["bluepad"].to<JsonObject>());
    bluepad_send_json(request, doc);
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
    bluepad_wifi_mode();

    srv->on("/bluepad/devices.json", HTTP_GET, bluepad_handle_devices);
    srv->on("/bluepad/pairing", HTTP_POST, bluepad_handle_pairing);
    srv->on("/bluepad/devices/delete", HTTP_POST, bluepad_handle_delete_device);

    auto* temporaryConfigHandler = new AsyncCallbackJsonWebHandler("/bluepad/config.json", bluepad_handle_temporary_config);
    temporaryConfigHandler->setMethod(HTTP_POST);
    srv->addHandler(temporaryConfigHandler);
}

#endif
