#if defined(BUILD_BLUEPAD32) && defined(PLATFORM_ESP32)

#include "bluepad.h"
#include "bp32_utils.h"

#include "common.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "ArduinoBluepad32.h"
#include "arduino_platform.h"
#include "bt/uni_bt.h"
#include "bt/uni_bt_service.h"
#include "btstack_port_esp32.h"
#include "btstack_run_loop.h"
#include "crsf_protocol.h"
#include "config.h"
#include "device.h"
#include "controller/uni_gamepad.h"
#include "uni.h"
#include "uni_virtual_device.h"
#include "WebBackend.h"
#if defined(TARGET_TX)
#include "handset.h"
#endif

extern "C" bool btInUse()
{
    return true;
}

extern uint32_t ChannelData[];
int32_t ChannelDataShadow[2];

static uint32_t failsafe_values[BP_AUX_CHANNEL_COUNT];

#if defined(TARGET_RX)
extern uint32_t LastSyncPacket;
extern uint32_t LastValidPacket;
extern connectionState_e connectionState;
extern void crsfRCFrameAvailable();
extern void custommixer_mix();
extern void servoNewChannelsAvailable();
#endif

static constexpr uint32_t BLUEPAD32INIT_TASK_STACK_SIZE = 1024 * 6; //8192;
static constexpr UBaseType_t BLUEPAD32INIT_TASK_PRIORITY = 1;
static constexpr UBaseType_t BLUEPAD32INIT_TASK_PRIORITY_LOW = 0;
static constexpr BaseType_t BLUEPAD32INIT_TASK_CORE = 1;
static constexpr size_t BLUEPAD32_CRSF_NUM_CHANNELS = 16;
static constexpr uint32_t BLUEPAD32_DISCONNECT_TIMEOUT_MS = 1000;
static constexpr uint32_t BLUEPAD32_WEB_SEND_INTERVAL_MS = 250;
static constexpr size_t BLUEPAD32_WEB_DATA_COUNT = CRSF_NUM_CHANNELS * 2U + 1U;
static constexpr size_t BLUEPAD32_WEB_BUFFER_SIZE = 512U;

static TaskHandle_t bluepad32InitTaskHandle = nullptr;

static bool hasSetup = false;
static uint32_t lastControllerDataMillis = 0;
ControllerPtr myControllers[BP32_MAX_GAMEPADS] = {};
static bool hasControllerData = false;
static const bluepad_cfg_t* bluepadConfig = nullptr;
static bool bluepadWifiMode = false;
bluepad_cfg_t bluepadTemporaryConfig = {};
bool bluepadTemporaryConfigValid = false;
bool bluepadTemporaryConfigUpdated = false;

static uint16_t btn_was_pressed = 0; // bit flags

SemaphoreHandle_t bluepadBtstackAccessMutex = nullptr;
SemaphoreHandle_t bluepadBtstackRequestSignal = nullptr;
SemaphoreHandle_t bluepadBtstackDoneSignal = nullptr;
volatile bool bluepadBtstackHookReady = false;
volatile bool bluepadBtstackRequestPending = false;

#if defined(TARGET_RX)
enum class bluepad32_rx_state_e : uint8_t
{
    bothListening,
    loraOnly,
    bluetoothActive,
    wifiMode,
};

static bluepad32_rx_state_e bluepad32RxState = bluepad32_rx_state_e::bothListening;
static volatile bool loraPacketReceived = false;
#endif

extern void (*btstack_run_loop_freertos_execute_hook)(void);

static void btstack_loop_hook()
{
    bluepadBtstackHookReady = true;
    if (bluepadBtstackRequestPending) {
        xSemaphoreGive(bluepadBtstackRequestSignal);
        xSemaphoreTake(bluepadBtstackDoneSignal, portMAX_DELAY);
    }

    #if 0
    if (bluepad32RxState == bluepad32_rx_state_e::loraOnly) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    else if (bluepad32RxState == bluepad32_rx_state_e::bothListening) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    else if (bluepad32RxState == bluepad32_rx_state_e::wifiMode) {
        taskYIELD();
    }
    #endif
}

static void Bluepad32InitTask(void *)
{
    btstack_init();
    uni_platform_set_custom(get_arduino_platform());
    uni_init(0, nullptr);
    btstack_run_loop_freertos_execute_hook = &btstack_loop_hook;
    btstack_run_loop_execute();
}

static bool processControllers();
static void onConnectedController(ControllerPtr ctl);
static void onDisconnectedController(ControllerPtr ctl);
static bool hasRecentControllerData(uint32_t now);
static void handleControllerData(uint32_t now);
static void loadBluepadFailsafeValues();
static void initializeBluepadAuxShadows();
static uint32_t getAuxLowerLimitCrsf(uint8_t aux_num);
static void sendToWeb();

#if defined(TARGET_RX)
static bool consumeLoraPacketReceived();
static void setBluepad32TaskPriority(UBaseType_t priority);
static void transitionToLoraOnly();
static void transitionToBluetoothActive();
static void refreshRxPacketTimers(uint32_t now);

static void setBluepad32ConnectionState(connectionState_e newState)
{
    if (connectionState == newState) {
        return;
    }

    connectionState = newState;
    devicesTriggerEvent(EVENT_CONNECTION_CHANGED);
}
#endif

bool bluepad_init()
{
    #if defined(ENABLE_BLUEPAD32_DEBUG)
    Serial.begin(115200, SERIAL_8N1, U0RXD_GPIO_NUM, U0TXD_GPIO_NUM);
    #endif

    if (bluepad32InitTaskHandle != nullptr)
    {
        return true;
    }

    if (bluepadBtstackAccessMutex == nullptr) {
        bluepadBtstackAccessMutex = xSemaphoreCreateMutex();
    }
    if (bluepadBtstackRequestSignal == nullptr) {
        bluepadBtstackRequestSignal = xSemaphoreCreateBinary();
    }
    if (bluepadBtstackDoneSignal == nullptr) {
        bluepadBtstackDoneSignal = xSemaphoreCreateBinary();
    }

    if (bluepadBtstackAccessMutex == nullptr || bluepadBtstackRequestSignal == nullptr ||
        bluepadBtstackDoneSignal == nullptr)
    {
        return false;
    }

    return xTaskCreatePinnedToCore(
        Bluepad32InitTask,
        "Bluepad32InitTask",
        BLUEPAD32INIT_TASK_STACK_SIZE,
        nullptr,
        BLUEPAD32INIT_TASK_PRIORITY,
        &bluepad32InitTaskHandle,
        BLUEPAD32INIT_TASK_CORE) == pdPASS;
}

void bluepad_poll()
{
    if (!hasSetup)
    {
        if (!bluepadBtstackHookReady) {
            return;
        }

        hasSetup = true;
        BLUEPAD_BTSTACK_DO_UNSAFE({
            BP32.setup(&onConnectedController, &onDisconnectedController);
            uni_virtual_device_set_enabled(false); // BP32.enableVirtualDevice(false);
            uni_bt_service_set_enabled(false);     // BP32.enableBLEService(false);
        });
        lastControllerDataMillis = millis();
        bluepadConfig = config.GetBluepadConfig();
        loadBluepadFailsafeValues();
        initializeBluepadAuxShadows();
    }

    #if defined(TARGET_RX)
    // do not use Bluetooth if a real transmitter is currently communicating with us
    if (bluepad32RxState == bluepad32_rx_state_e::loraOnly
        || (bluepad32RxState != bluepad32_rx_state_e::wifiMode && consumeLoraPacketReceived())
    )
    {
        transitionToLoraOnly();
        return;
    }
    #endif

    // get new data from BP32 and process it if possible
    bool dataUpdated = BP32.update();
    if (dataUpdated) {
        dataUpdated = processControllers();
    }

    // if we are in wifi mode, then send data to the front-end
    if (bluepadWifiMode) {
        if (bluepadTemporaryConfigValid && bluepadTemporaryConfigUpdated) {
            bluepadConfig = &bluepadTemporaryConfig;
            bluepadTemporaryConfigUpdated = false;
            loadBluepadFailsafeValues();
        }

        sendToWeb();
    }

    const uint32_t now = millis();

    if (dataUpdated) {
        #if defined(TARGET_RX)
        // change modes if needed when we have new data
        if (bluepad32RxState == bluepad32_rx_state_e::bothListening) {
            transitionToBluetoothActive();
        }
        #endif

        handleControllerData(now); // this makes the LEDs behave as if we have a connection, and passes the new data down to the servos and other modules
    }
    #if defined(TARGET_RX)
    else if (bluepad32RxState == bluepad32_rx_state_e::bluetoothActive) {
        // even without new data, keep the connection appear alive for another second or so
        if (hasRecentControllerData(now)) {
            refreshRxPacketTimers(now);
            setBluepad32ConnectionState(connected);
        }
    }
    #endif
}

void bluepad_disable()
{
    #if defined(TARGET_RX)
    if (hasSetup) {
        transitionToLoraOnly();
    }
    #endif
}

void bluepad_wifi_mode()
{
    bluepadWifiMode = true;

    #if defined(TARGET_RX)
    bluepad32RxState = bluepad32_rx_state_e::wifiMode;
    setBluepad32TaskPriority(BLUEPAD32INIT_TASK_PRIORITY);
    #endif
}

#if defined(TARGET_RX)
void bluepad_rx_lora_packet_received()
{
    loraPacketReceived = true;
}

void bluepad_rx_wifi_mode()
{
    bluepad_wifi_mode();
}
#endif

#if defined(TARGET_TX)
bool bluepad_has_recent_channel_data()
{
    return hasRecentControllerData(millis());
}
#endif

static bool hasRecentControllerData(uint32_t now)
{
    return hasControllerData && now - lastControllerDataMillis < BLUEPAD32_DISCONNECT_TIMEOUT_MS;
}

static void handleControllerData(uint32_t now)
{
    lastControllerDataMillis = now;
    hasControllerData = true;

    InBindingMode = false;

    #if defined(TARGET_RX)
    // the following functions makes the receiver behave as if it has a valid connection
    refreshRxPacketTimers(now);
    setBluepad32ConnectionState(connected);

    // the following 3 function calls mirrors what `ProcessRfPacket_RC` does
    custommixer_mix();
    crsfRCFrameAvailable();
    servoNewChannelsAvailable();

    #elif defined(TARGET_TX)
    handset->RCDataReceived(ChannelData, BLUEPAD32_CRSF_NUM_CHANNELS);
    #endif
}

#if defined(TARGET_RX)
static bool consumeLoraPacketReceived()
{
    if (!loraPacketReceived) {
        return false;
    }

    loraPacketReceived = false;
    return true;
}

static void setBluepad32TaskPriority(UBaseType_t priority)
{
    if (bluepad32InitTaskHandle != nullptr)
    {
        vTaskPrioritySet(bluepad32InitTaskHandle, priority);
    }
}

static void transitionToLoraOnly()
{
    setBluepad32TaskPriority(BLUEPAD32INIT_TASK_PRIORITY_LOW);
    bluepadWifiMode = false;

    if (bluepad32RxState == bluepad32_rx_state_e::loraOnly) {
        return;
    }

    bluepad32RxState = bluepad32_rx_state_e::loraOnly;
    BLUEPAD_BTSTACK_DO_UNSAFE({
        uni_bt_enable_new_connections_unsafe(false);
        for (auto &controller : myControllers)
        {
            if (controller != nullptr)
            {
                controller->disconnect();
                controller = nullptr;
            }
        }
    });
}

static void transitionToBluetoothActive()
{
    if (bluepad32RxState == bluepad32_rx_state_e::bothListening) {
        bluepad32RxState = bluepad32_rx_state_e::bluetoothActive;
    }
}

static void refreshRxPacketTimers(uint32_t now)
{
    LastValidPacket = now;
    LastSyncPacket = now;
}
#endif

static void onConnectedController(ControllerPtr ctl)
{
    #if defined(TARGET_RX)
    if (bluepad32RxState == bluepad32_rx_state_e::loraOnly)
    {
        ctl->disconnect();
        return;
    }
    #endif

    for (auto &controller : myControllers)
    {
        if (controller == nullptr)
        {
            controller = ctl;
            break;
        }
    }

    uint8_t connectedCount = 0;
    for (auto controller : myControllers)
    {
        if (controller != nullptr)
        {
            ++connectedCount;
        }
    }

    if (connectedCount == 1)
    {
        btn_was_pressed = 0;
    }
}

static void onDisconnectedController(ControllerPtr ctl)
{
    for (auto &controller : myControllers)
    {
        if (controller == ctl)
        {
            controller = nullptr;
            break;
        }
    }
}

static int32_t getCtlValForButton(ControllerPtr ctl, uint8_t btn_enum)
{
    switch (btn_enum)
    {
        case BP_BUTTON_FACE_A:
            return ctl->a();
        case BP_BUTTON_FACE_B:
            return ctl->b();
        case BP_BUTTON_FACE_X:
            return ctl->x();
        case BP_BUTTON_FACE_Y:
            return ctl->y();
        case BP_BUTTON_DPAD_UP:
            return (ctl->dpad() & DPAD_UP) != 0;
        case BP_BUTTON_DPAD_DOWN:
            return (ctl->dpad() & DPAD_DOWN) != 0;
        case BP_BUTTON_DPAD_LEFT:
            return (ctl->dpad() & DPAD_LEFT) != 0;
        case BP_BUTTON_DPAD_RIGHT:
            return (ctl->dpad() & DPAD_RIGHT) != 0;
        case BP_BUTTON_L1:
            return ctl->l1();
        case BP_BUTTON_R1:
            return ctl->r1();
        default:
            return 0;
    }
}

static void loadBluepadFailsafeValues()
{
    for (uint8_t aux_num = 0; aux_num < BP_AUX_CHANNEL_COUNT; ++aux_num)
    {
        failsafe_values[aux_num] = CRSF_CHANNEL_VALUE_UNSET;
    }

    if (!bluepadConfig)
    {
        return;
    }

    for (uint8_t aux_num = 0; aux_num < BP_AUX_CHANNEL_COUNT; ++aux_num)
    {
        failsafe_values[aux_num] = usToCrsfValue(bluepadConfig->aux_mode[aux_num].failsafe);
    }
}

static void initializeBluepadAuxShadows()
{
    for (uint8_t aux_num = 0; aux_num < BP_AUX_CHANNEL_COUNT; ++aux_num)
    {
        const uint32_t failsafe = failsafe_values[aux_num];
        ChannelDataShadow[aux_num] = crsfToShadow(
            failsafe == CRSF_CHANNEL_VALUE_UNSET ? CRSF_CHANNEL_VALUE_MID : failsafe);
    }
}

static uint32_t getAuxLowerLimitCrsf(uint8_t aux_num)
{
    if (aux_num >= BP_AUX_CHANNEL_COUNT)
    {
        return CRSF_CHANNEL_VALUE_STD_MIN;
    }

    const uint32_t failsafe = failsafe_values[aux_num];
    if (failsafe != CRSF_CHANNEL_VALUE_UNSET && failsafe <= usToCrsfValue(1100))
    {
        return failsafe;
    }

    return CRSF_CHANNEL_VALUE_STD_MIN;
}

static void sendToWeb()
{
    uint32_t now = millis();

    // rate limit
    static uint32_t last_time = 0;
    if ((now - last_time) <= BLUEPAD32_WEB_SEND_INTERVAL_MS) {
        return;
    }
    last_time = now;

    int32_t data[BLUEPAD32_WEB_DATA_COUNT];
    memset(data, 0, sizeof(data));

    for (auto ctl : myControllers) {
        if (ctl && ctl->isConnected() && ctl->hasData() && ctl->isGamepad()) {
            data[ 0] = ctl->axisX();
            data[ 1] = ctl->axisY();
            data[ 2] = ctl->axisRX();
            data[ 3] = ctl->axisRY();
            data[ 4] = ctl->brake();
            data[ 5] = ctl->throttle();
            data[ 6] = ctl->a();
            data[ 7] = ctl->b();
            data[ 8] = ctl->x();
            data[ 9] = ctl->y();
            data[10] = ctl->l1();
            data[11] = ctl->r1();
            data[12] = ctl->thumbL();
            data[13] = ctl->thumbR();
            data[14] = ctl->dpad();
            data[BLUEPAD32_WEB_DATA_COUNT - 1] = 1;
        }
    }

    // copy processed channel data so it gets surfaced to front-end
    for (int i = 0; i < CRSF_NUM_CHANNELS; i++)
    {
        data[CRSF_NUM_CHANNELS + i] = ChannelData[i];
    }
    char buffer[BLUEPAD32_WEB_BUFFER_SIZE];
    size_t offset = 0;
    int written = snprintf(buffer, sizeof(buffer), "BP:");
    if (written < 0 || (size_t)written >= sizeof(buffer)) {
        return;
    }
    offset = (size_t)written;

    for (size_t i = 0; i < BLUEPAD32_WEB_DATA_COUNT; ++i)
    {
        written = snprintf(buffer + offset, sizeof(buffer) - offset, "%s%ld", i == 0 ? "" : ",", (long)data[i]);
        if (written < 0 || (size_t)written >= sizeof(buffer) - offset) {
            return;
        }
        offset += (size_t)written;
    }

    written = snprintf(buffer + offset, sizeof(buffer) - offset, ";\n");
    if (written < 0 || (size_t)written >= sizeof(buffer) - offset) {
        return;
    }

    webbe_sendStr(buffer);
}

static bool processGamepad(ControllerPtr ctl)
{
    uint32_t now = millis();
    static uint32_t last_time = 0;

    // calculate delta time for usage with relative changes, to make the changes happen at a constant rate regardless of report rate
    uint32_t dt = 1;
    if (last_time != 0) {
        dt = now - last_time;
    }
    if (dt >= 50) {
        dt = 50;
    }
    last_time = now;

    // this is the basic default flat mapping
    ChannelData[0] = axisToCrsf(ctl->axisX(), false);
    ChannelData[1] = axisToCrsf(ctl->axisY(), true);
    ChannelData[2] = axisToCrsf(ctl->axisRX(), false);
    ChannelData[3] = axisToCrsf(ctl->axisRY(), true);
    ChannelData[4] = triggerToCrsf(ctl->brake());
    ChannelData[5] = triggerToCrsf(ctl->throttle());
    ChannelData[6] = buttonToCrsf(ctl->a());
    ChannelData[7] = buttonToCrsf(ctl->b());
    ChannelData[8] = buttonToCrsf(ctl->x());
    ChannelData[9] = buttonToCrsf(ctl->y());
    ChannelData[10] = buttonToCrsf(ctl->l1());
    ChannelData[11] = buttonToCrsf(ctl->r1());
    ChannelData[12] = buttonToCrsf(ctl->thumbL());
    ChannelData[13] = buttonToCrsf(ctl->thumbR());
    ChannelData[14] = dpadToCrsfAxis(ctl->dpad(), DPAD_LEFT, DPAD_RIGHT);
    ChannelData[15] = dpadToCrsfAxis(ctl->dpad(), DPAD_DOWN, DPAD_UP);

    if (!bluepadConfig) {
        return false;
    }

    switch (bluepadConfig->main_mode)
    {
        case BP_MAINCTRL_BOTHSTICKSFULLAUTO:
            {
                int absLX = abs(ctl->axisX());
                int absLY = abs(ctl->axisY());
                int absRX = abs(ctl->axisRX());
                int absRY = abs(ctl->axisRY());
                ChannelData[0] = axisToCrsf(absLY >= absRY ? ctl->axisY() : ctl->axisRY(), true);
                ChannelData[1] = axisToCrsf(absLX >= absRX ? ctl->axisX() : ctl->axisRX(), false);
            }
            break;
        case BP_MAINCTRL_BOTHSTICKSDIRECTY:
            ChannelData[0] = axisToCrsf(ctl->axisY(), true);
            ChannelData[1] = axisToCrsf(ctl->axisRY(), true);
            break;
        case BP_MAINCTRL_LEFTSTICKONLY:
            ChannelData[0] = axisToCrsf(ctl->axisY(), true);
            ChannelData[1] = axisToCrsf(ctl->axisX(), false);
            break;
        case BP_MAINCTRL_RIGHTSTICKONLY:
            ChannelData[0] = axisToCrsf(ctl->axisRY(), true);
            ChannelData[1] = axisToCrsf(ctl->axisRX(), false);
            break;
        case BP_MAINCTRL_DPADONLY:
            // treat the D-pad as if it was a stick
            ChannelData[0] = dpadToCrsfAxis(ctl->dpad(), DPAD_DOWN, DPAD_UP);
            ChannelData[1] = dpadToCrsfAxis(ctl->dpad(), DPAD_LEFT, DPAD_RIGHT);
            break;
        case BP_MAINCTRL_LEFTTHROTTLE_RIGHTSTEERING:
            ChannelData[0] = axisToCrsf(ctl->axisY(), true);
            ChannelData[1] = axisToCrsf(ctl->axisRX(), false);
            break;
        case BP_MAINCTRL_RIGHTTHROTTLE_LEFTSTEERING:
            ChannelData[0] = axisToCrsf(ctl->axisRY(), true);
            ChannelData[1] = axisToCrsf(ctl->axisX(), false);
            break;
        case BP_MAINCTRL_RACING_LEFTSTEERING:
        case BP_MAINCTRL_RACING_RIGHTSTEERING:
            {
                // Throttle is the right trigger subtract the left trigger
                // Steering is the X axis of the selected stick
                int thr = ctl->throttle() - ctl->brake();
                int str = (bluepadConfig->main_mode == BP_MAINCTRL_RACING_LEFTSTEERING) ? ctl->axisX() : ctl->axisRX();
                ChannelData[0] = triggerDiffToCrsf(thr);
                ChannelData[1] = axisToCrsf(str, false);
            }
            break;
    }

    for (uint8_t aux_num = 0; aux_num < 2; aux_num++)
    {
        const bluepad_auxchan_cfg_t* ap = &(bluepadConfig->aux_mode[aux_num]);
        const uint32_t lowerLimitCrsf = getAuxLowerLimitCrsf(aux_num);
        if (ap->actual_channel == 0) {
            continue;
        }
        if (ap->analog_mode == BP_ANALOGCTRL_DONOTHING) {
            continue;
        }
        switch (ap->analog_mode)
        {
            case BP_ANALOGCTRL_LEFTSTICK_Y_DIRECT:
                ChannelDataShadow[aux_num] = crsfToShadow(axisToCrsf(ctl->axisY(), true));
                break;
            case BP_ANALOGCTRL_RIGHTSTICK_Y_DIRECT:
                ChannelDataShadow[aux_num] = crsfToShadow(axisToCrsf(ctl->axisRY(), true));
                break;
            case BP_ANALOGCTRL_LEFTSTICK_Y_RELATIVE:
                update_servo_shadow(&ChannelDataShadow[aux_num], ctl->axisY(), dt, lowerLimitCrsf);
                break;
            case BP_ANALOGCTRL_RIGHTSTICK_Y_RELATIVE:
                update_servo_shadow(&ChannelDataShadow[aux_num], ctl->axisRY(), dt, lowerLimitCrsf);
                break;
            case BP_ANALOGCTRL_LEFTTRIGGER_DIRECT:
                ChannelDataShadow[aux_num] = crsfToShadow(triggerToCrsf(ctl->brake()));
                break;
            case BP_ANALOGCTRL_RIGHTTRIGGER_DIRECT:
                ChannelDataShadow[aux_num] = crsfToShadow(triggerToCrsf(ctl->throttle()));
                break;
            case BP_ANALOGCTRL_LEFTTRIGGER_LOWER_RIGHTTRIGGER_RAISE:
            case BP_ANALOGCTRL_RIGHTTRIGGER_LOWER_LEFTTRIGGER_RAISE:
                update_servo_shadow(&ChannelDataShadow[aux_num], (ctl->throttle() - ctl->brake()) * ((ap->analog_mode == BP_ANALOGCTRL_RIGHTTRIGGER_LOWER_LEFTTRIGGER_RAISE) ? 1 : -1), dt, lowerLimitCrsf);
                break;
        }
    }

    bool has_hold[2] = {false};

    for (uint8_t bi = 0; bi < BP_BUTTON_CONFIG_COUNT; bi++)
    {
        const bluepad_btn_cfg_t* bp = &(bluepadConfig->btn_mode[bi]);
        if (bp->mode == BP_BUTTONCTRL_DONOTHING) {
            continue;
        }

        uint8_t aux_num    = bp->aux_chan ? 1 : 0;
        bool is_pressed    = getCtlValForButton(ctl, bi);
        bool was_pressed   = (btn_was_pressed & (1 << bi)) != 0;
        bool is_down_event =  is_pressed && !was_pressed;
        bool is_up_event   = !is_pressed &&  was_pressed;
        switch (bp->mode)
        {
            case BP_BUTTONCTRL_TAP2LATCH:
                if (is_down_event) {
                    ChannelDataShadow[aux_num] = crsfToShadow(usToCrsfValue(bp->value));
                }
                break;
            case BP_BUTTONCTRL_HELD:
                if (is_pressed) {
                    ChannelDataShadow[aux_num] = crsfToShadow(usToCrsfValue(bp->value));
                    has_hold[aux_num] = true;
                }
                else if (is_up_event && !has_hold[aux_num] && failsafe_values[aux_num] != CRSF_CHANNEL_VALUE_UNSET) {
                    ChannelDataShadow[aux_num] = crsfToShadow(failsafe_values[aux_num]);
                    // we do not support simultaneous multiple button holds assigned to the same channel
                    // we do not support mixing holding and latched-tapping together
                }
                break;
            case BP_BUTTONCTRL_INCREMENT:
            case BP_BUTTONCTRL_DECREMENT:
                if (is_down_event) {
                    int32_t delta = crsfToShadow(usDeltaToCrsfDelta(bp->value));
                    if (bp->mode == BP_BUTTONCTRL_INCREMENT) {
                        ChannelDataShadow[aux_num] += delta;
                    }
                    else if (bp->mode == BP_BUTTONCTRL_DECREMENT) {
                        ChannelDataShadow[aux_num] -= delta;
                    }
                    ChannelDataShadow[aux_num] = clampShadow(ChannelDataShadow[aux_num], getAuxLowerLimitCrsf(aux_num));
                }
                break;
        }

        // track press states so we can determine events
        if (is_pressed) {
            btn_was_pressed |= 1 << bi;
        }
        else {
            btn_was_pressed &= ~(1 << bi);
        }
    }

    const bool applyFailsafe = ctl->miscStart();

    for (uint8_t aux_num = 0; aux_num < 2; aux_num++)
    {
        const bluepad_auxchan_cfg_t* ap = &(bluepadConfig->aux_mode[aux_num]);
        if (ap->actual_channel == 0) {
            continue;
        }

        if (applyFailsafe)
        {
            const uint32_t failsafe = failsafe_values[aux_num];
            if (failsafe == CRSF_CHANNEL_VALUE_UNSET)
            {
                ChannelData[ap->actual_channel - 1] = CRSF_CHANNEL_VALUE_UNSET;
                continue;
            }

            ChannelDataShadow[aux_num] = crsfToShadow(failsafe);
        }

        ChannelData[ap->actual_channel - 1] = shadowToCrsf(ChannelDataShadow[aux_num]);
    }

    return true;
}

static bool processControllers()
{
    bool success = false;
    for (auto myController : myControllers) {
        if (myController && myController->isConnected() && myController->hasData()) {
            if (myController->isGamepad()) {
                success |= processGamepad(myController);
            }
            else {
                // consider disconnection or unpairing
            }
        }
    }
    return success;
}

#endif
