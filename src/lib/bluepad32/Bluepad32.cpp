#if defined(BUILD_BLUEPAD32) && defined(PLATFORM_ESP32)

#include "bluepad.h"
#include "bp32_utils.h"

#include "common.h"

#include <Arduino.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ArduinoBluepad32.h"
#include "arduino_platform.h"
#include "btstack_port_esp32.h"
#include "btstack_run_loop.h"
#include "crsf_protocol.h"
#include "config.h"
#include "device.h"
#include "controller/uni_gamepad.h"
#include "uni.h"
#if defined(TARGET_TX)
#include "handset.h"
#endif

extern uint32_t ChannelData[];
#if defined(TARGET_RX)
extern uint32_t LastSyncPacket;
extern uint32_t LastValidPacket;
extern connectionState_e connectionState;
extern void crsfRCFrameAvailable();
extern void custommixer_mix();
extern void servoNewChannelsAvailable();
#endif

static constexpr uint32_t BLUEPAD32INIT_TASK_STACK_SIZE = 8192;
static constexpr UBaseType_t BLUEPAD32INIT_TASK_PRIORITY = 1;
static constexpr BaseType_t BLUEPAD32INIT_TASK_CORE = 1;
static constexpr size_t BLUEPAD32_CRSF_NUM_CHANNELS = 16;
static constexpr uint32_t BLUEPAD32_DISCONNECT_TIMEOUT_MS = 1000;

static TaskHandle_t bluepad32InitTaskHandle = nullptr;

static bool hasSetup = false;
static uint32_t lastControllerDataMillis = 0;
static ControllerPtr myControllers[BP32_MAX_GAMEPADS] = {};
static uint32_t bluepadChannelData[BLUEPAD32_CRSF_NUM_CHANNELS] = {};
static bool hasBluepadChannelData = false;
static const bluepad_cfg_t* bluepadConfig = nullptr;

#if defined(TARGET_RX)
enum class bluepad32_rx_state_e : uint8_t
{
    bothListening,
    loraOnly,
    bluetoothActive,
};

static bluepad32_rx_state_e bluepad32RxState = bluepad32_rx_state_e::bothListening;
static volatile bool loraPacketReceived = false;
#endif

static void Bluepad32InitTask(void *)
{
    btstack_init();
    uni_platform_set_custom(get_arduino_platform());
    uni_init(0, nullptr);
    btstack_run_loop_execute();
}

static bool processControllers();
static void onConnectedController(ControllerPtr ctl);
static void onDisconnectedController(ControllerPtr ctl);
static void cacheBluepadChannelData();
#if defined(TARGET_TX)
static void restoreBluepadChannelData();
#endif
static bool hasRecentControllerData(uint32_t now);
static void handleControllerData(uint32_t now);

#if defined(TARGET_RX)
static bool consumeLoraPacketReceived();
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

bool bluepad32_init()
{
    if (bluepad32InitTaskHandle != nullptr)
    {
        return true;
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

void bluepad32_poll()
{
    if (!hasSetup)
    {
        hasSetup = true;
        BP32.setup(&onConnectedController, &onDisconnectedController);
        BP32.enableVirtualDevice(false);
        BP32.enableBLEService(false);
        lastControllerDataMillis = millis();
        bluepadConfig = config.GetBluepadConfig();
    }

    #if defined(TARGET_RX)
    if (bluepad32RxState == bluepad32_rx_state_e::loraOnly) {
        return;
    }

    if (consumeLoraPacketReceived()) {
        transitionToLoraOnly();
        return;
    }
    #endif

    bool dataUpdated = BP32.update();
    if (dataUpdated) {
        dataUpdated = processControllers();
    }

    const uint32_t now = millis();

    if (dataUpdated) {
        #if defined(TARGET_RX)
        if (bluepad32RxState == bluepad32_rx_state_e::bothListening) {
            transitionToBluetoothActive();
        }
        #endif

        handleControllerData(now);
    }
    #if defined(TARGET_RX)
    else if (bluepad32RxState == bluepad32_rx_state_e::bluetoothActive) {
        if (hasRecentControllerData(now)) {
            refreshRxPacketTimers(now);
            setBluepad32ConnectionState(connected);
        }
        else {
            transitionToLoraOnly();
        }
    }
    #elif defined(TARGET_TX)
    else if (hasRecentControllerData(now)) {
        restoreBluepadChannelData();
    }
    #endif
}

void bluepad32_disable()
{
    #if defined(TARGET_RX)
    if (hasSetup) {
        transitionToLoraOnly();
    }
    #endif
}

#if defined(TARGET_RX)
void bluepad32_rx_lora_packet_received()
{
    loraPacketReceived = true;
}
#endif

#if defined(TARGET_TX)
bool bluepad32_has_recent_channel_data()
{
    return hasRecentControllerData(millis());
}

bool bluepad32_apply_channel_data_if_recent()
{
    if (!bluepad32_has_recent_channel_data()) {
        return false;
    }

    restoreBluepadChannelData();
    return true;
}
#endif

static void cacheBluepadChannelData()
{
    std::memcpy(bluepadChannelData, ChannelData, sizeof(bluepadChannelData));
    hasBluepadChannelData = true;
}

#if defined(TARGET_TX)
static void restoreBluepadChannelData()
{
    if (!hasBluepadChannelData) {
        return;
    }

    std::memcpy(ChannelData, bluepadChannelData, sizeof(bluepadChannelData));
}
#endif

static bool hasRecentControllerData(uint32_t now)
{
    return hasBluepadChannelData && now - lastControllerDataMillis < BLUEPAD32_DISCONNECT_TIMEOUT_MS;
}

static void handleControllerData(uint32_t now)
{
    lastControllerDataMillis = now;
    cacheBluepadChannelData();

    InBindingMode = false;

    #if defined(TARGET_RX)
    refreshRxPacketTimers(now);
    setBluepad32ConnectionState(connected);
    custommixer_mix(); // the mix will be way more complicated later
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

static void transitionToLoraOnly()
{
    if (bluepad32RxState == bluepad32_rx_state_e::loraOnly) {
        return;
    }

    bluepad32RxState = bluepad32_rx_state_e::loraOnly;
    BP32.enableNewBluetoothConnections(false);

    for (auto &controller : myControllers)
    {
        if (controller != nullptr)
        {
            controller->disconnect();
            controller = nullptr;
        }
    }
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

static bool processGamepad(ControllerPtr ctl) {
    for (size_t i = 0; i < BLUEPAD32_CRSF_NUM_CHANNELS; ++i)
    {
        ChannelData[i] = CRSF_CHANNEL_VALUE_1000;
    }

    // this is a placeholder implementation

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

    return true;
}

static bool processMouse(ControllerPtr ctl) {
    return false;
}

static bool processKeyboard(ControllerPtr ctl) {

    if (!ctl->isAnyKeyPressed()) {
        return false;
    }

    // This is just an example.
    if (ctl->isKeyPressed(Keyboard_A)) {
        // Do Something
    }
    return false;
}

static bool processBalanceBoard(ControllerPtr ctl) {
    return false;
}

static bool processControllers()
{
    bool success = false;
    for (auto myController : myControllers) {
        if (myController && myController->isConnected() && myController->hasData()) {
            if (myController->isGamepad()) {
                success |= processGamepad(myController);
            } else if (myController->isMouse()) {
                success |= processMouse(myController);
            } else if (myController->isKeyboard()) {
                success |= processKeyboard(myController);
            } else if (myController->isBalanceBoard()) {
                success |= processBalanceBoard(myController);
            } else {
                //Console.printf("Unsupported controller\n");
            }
        }
    }
    return success;
}

#endif
