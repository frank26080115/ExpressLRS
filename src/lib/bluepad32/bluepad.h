#pragma once

// file is named bluepad.h instead of bluepad32.h to avoid a conflict

#if defined(BUILD_BLUEPAD32) && defined(PLATFORM_ESP32)

#include "targets.h"
#include "bluepad_types.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <btstack_run_loop_freertos.h>
#include <ArduinoJson.h>

// public functions
bool bluepad_init();
void bluepad_poll();
void bluepad_disable();
void bluepad_wifi_mode();
#if defined(TARGET_RX)
void bluepad_rx_lora_packet_received();
void bluepad_rx_wifi_mode();
#endif
#if defined(TARGET_TX)
bool bluepad_has_recent_channel_data();
#endif

extern SemaphoreHandle_t bluepadBtstackAccessMutex;
extern SemaphoreHandle_t bluepadBtstackRequestSignal;
extern SemaphoreHandle_t bluepadBtstackDoneSignal;
extern volatile bool bluepadBtstackHookReady;
extern volatile bool bluepadBtstackRequestPending;
extern bluepad_cfg_t bluepadTemporaryConfig;
extern bool bluepadTemporaryConfigValid;
extern bool bluepadTemporaryConfigUpdated;

#define BLUEPAD_BTSTACK_DO_UNSAFE(code) do { \
    if (bluepadBtstackHookReady && bluepadBtstackAccessMutex != nullptr && \
        bluepadBtstackRequestSignal != nullptr && bluepadBtstackDoneSignal != nullptr) { \
        xSemaphoreTake(bluepadBtstackAccessMutex, portMAX_DELAY); \
        bluepadBtstackRequestPending = true; \
        btstack_run_loop_freertos_trigger(); \
        xSemaphoreTake(bluepadBtstackRequestSignal, portMAX_DELAY); \
        do { code; } while (0); \
        bluepadBtstackRequestPending = false; \
        xSemaphoreGive(bluepadBtstackDoneSignal); \
        xSemaphoreGive(bluepadBtstackAccessMutex); \
    } \
} while (0)

void bluepad_config_to_json(const bluepad_cfg_t* cfg, JsonObject obj);
void json_to_bluepad_config(JsonObjectConst obj, bluepad_cfg_t* cfg);

#endif
