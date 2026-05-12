#pragma once

// file is named bluepad.h instead of bluepad32.h to avoid a conflict

#if defined(BUILD_BLUEPAD32) && defined(PLATFORM_ESP32)

#include "targets.h"
#include "bluepad_types.h"
#include <ArduinoJson.h>

// public functions
bool bluepad_init();
void bluepad_poll();
void bluepad_disable();
#if defined(TARGET_RX)
void bluepad_rx_lora_packet_received();
#endif
#if defined(TARGET_TX)
bool bluepad_has_recent_channel_data();
#endif

void bluepad_config_to_json(const bluepad_cfg_t* cfg, JsonObject obj);
void json_to_bluepad_config(JsonObjectConst obj, bluepad_cfg_t* cfg);

#endif
