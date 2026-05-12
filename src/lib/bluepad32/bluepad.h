#pragma once

// file is named bluepad.h instead of bluepad32.h to avoid a conflict

#if defined(BUILD_BLUEPAD32) && defined(PLATFORM_ESP32)

#include "targets.h"
#include "bluepad_types.h"
#include <stddef.h>
#include <ArduinoJson.h>

// public functions
bool bluepad_init();
void bluepad_poll();
void bluepad_disable();
#if defined(TARGET_RX)
void bluepad_rx_lora_packet_received();
void bluepad_rx_wifi_mode();
#endif
#if defined(TARGET_TX)
bool bluepad_has_recent_channel_data();
#endif

enum : size_t
{
    BLUEPAD_PAIRED_DEVICE_ADDRESS_SIZE = 18,
    BLUEPAD_MAX_PAIRED_DEVICES = 16,
};

typedef struct
{
    char address[BLUEPAD_PAIRED_DEVICE_ADDRESS_SIZE];
    uint8_t type;
}
bluepad_paired_device_t;

void bluepad_set_pairing_enabled(bool enabled);
void bluepad_delete_all_paired_devices();
bool bluepad_delete_paired_device(const char* address);
size_t bluepad_get_paired_devices(bluepad_paired_device_t* devices, size_t maxDevices, bool* pairingEnabled);
void bluepad_refresh_paired_devices();

void bluepad_config_to_json(const bluepad_cfg_t* cfg, JsonObject obj);
void json_to_bluepad_config(JsonObjectConst obj, bluepad_cfg_t* cfg);

#endif
