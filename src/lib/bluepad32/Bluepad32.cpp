#if defined(RADIO_BLUEPAD32) && defined(PLATFORM_ESP32)

#include "Bluepad32Class.h"
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
#include "device.h"
#include "controller/uni_gamepad.h"
#include "uni.h"

extern uint32_t ChannelData[];
extern uint32_t LastSyncPacket;
extern uint32_t LastValidPacket;
extern connectionState_e connectionState;
extern void crsfRCFrameAvailable();
extern void custommixer_mix();
extern void servoNewChannelsAvailable();

Bluepad32Driver *Bluepad32Driver::instance = nullptr;

static constexpr uint32_t BLUEPAD32INIT_TASK_STACK_SIZE = 8192;
static constexpr UBaseType_t BLUEPAD32INIT_TASK_PRIORITY = 1;
static constexpr BaseType_t BLUEPAD32INIT_TASK_CORE = 1;
static constexpr size_t BLUEPAD32_CRSF_NUM_CHANNELS = 16;
static constexpr uint32_t BLUEPAD32_DISCONNECT_TIMEOUT_MS = 1000;

static TaskHandle_t bluepad32InitTaskHandle = nullptr;

static bool hasSetup = false;
static uint32_t lastControllerDataMillis = 0;
static ControllerPtr myControllers[BP32_MAX_GAMEPADS] = {};

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

static void setBluepad32ConnectionState(connectionState_e newState)
{
    if (connectionState == newState)
    {
        return;
    }

    connectionState = newState;
    devicesTriggerEvent(EVENT_CONNECTION_CHANGED);
}

// No current caller until RADIO_BLUEPAD32 is wired into src/common.cpp; intended caller is src/common.cpp global Radio construction.
Bluepad32Driver::Bluepad32Driver(): SX12xxDriverCommon()
{
    instance = this;
}

// Called from src/rx_main.cpp setupRadio() line 1635; src/tx_main.cpp setup() line 1479;
// lib/WIFI/devWIFI.cpp HandleContinuousWave() line 1074.
bool Bluepad32Driver::Begin(uint32_t, uint32_t)
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

// Called from src/rx_main.cpp main_loop() line 1858; lib/WIFI/devWIFI.cpp HandleContinuousWave() line 1131;
// lib/BLE/devBLE.cpp BluetoothJoystickBegin() line 54; lib/Backpack/devBackpack.cpp initialize() line 42;
// lib/SerialUpdate/devSerialUpdate.cpp event() line 31.
void Bluepad32Driver::End()
{
}

void Bluepad32Driver::Poll()
{
    if (!hasSetup)
    {
        hasSetup = true;
        std::memset(RXdataBuffer, 0, sizeof(RXdataBuffer));
        std::memset(RXdataBufferSecond, 0, sizeof(RXdataBufferSecond));
        PayloadLength = 0;
        BP32.setup(&onConnectedController, &onDisconnectedController);
        BP32.enableVirtualDevice(false);
        BP32.enableBLEService(false);
        lastControllerDataMillis = millis();
    }

    bool dataUpdated = BP32.update();
    if (dataUpdated) {
        dataUpdated = processControllers();
    }

    if (dataUpdated) {
        const uint32_t now = millis();
        lastControllerDataMillis = now;
        LastValidPacket = now;
        LastSyncPacket = now;
        InBindingMode = false;
        setBluepad32ConnectionState(connected);
        custommixer_mix();
        crsfRCFrameAvailable();
        servoNewChannelsAvailable();
    }
    else if (millis() - lastControllerDataMillis >= BLUEPAD32_DISCONNECT_TIMEOUT_MS) {
        setBluepad32ConnectionState(disconnected);
    }
}

// Called from src/rx_main.cpp ExitBindingMode() line 1763; src/tx_main.cpp CheckConfigChangePending() line 858.
void Bluepad32Driver::SetTxIdleMode()
{
}

// Called from src/rx_main.cpp SetRFLinkRate() line 330; src/tx_main.cpp SetRFLinkRate() line 478;
// lib/OTA/OTA_Legacy.cpp SetRFLinkRate_v3() line 903.
void Bluepad32Driver::Config(uint8_t, uint8_t, uint8_t, uint32_t, uint8_t, bool, uint8_t, SX12XX_Radio_Number_t)
{
}

// Called from src/rx_main.cpp SetRFLinkRate() line 355 and HandleFHSS() lines 395-408;
// src/tx_main.cpp SetRFLinkRate() line 502 and HandleFHSS() lines 904-915;
// lib/OTA/OTA_Legacy.cpp ProcessRFPacket_v3() lines 132-136 and SetRFLinkRate_v3() line 927.
void Bluepad32Driver::SetFrequencyReg(uint32_t, SX12XX_Radio_Number_t, bool, uint32_t)
{
}

// Called from lib/POWERMGNT/POWERMGNT.cpp incSX1280Output() line 118, decSX1280Output() line 128,
// setPower() lines 262 and 273; the two-argument LR1121 form is used from line 279.
void Bluepad32Driver::SetOutputPower(int8_t, bool)
{
}

// Called from lib/WIFI/devWIFI.cpp HandleContinuousWave() line 1082.
void Bluepad32Driver::startCWTest(uint32_t, SX12XX_Radio_Number_t)
{
}

// Called from src/rx_main.cpp ProcessRFPacket() lines 1179, 1186, 1190, and 1195.
bool Bluepad32Driver::GetFrequencyErrorbool(SX12XX_Radio_Number_t)
{
    return false;
}

// Called from src/rx_main.cpp ProcessRFPacket() line 1176.
bool Bluepad32Driver::FrequencyErrorAvailable() const
{
    return false;
}

// Called from src/rx_main.cpp HandleSendDataDl() lines 564 and 568; src/tx_main.cpp SendRCdataToRF() line 659;
// lib/OTA/OTA_Legacy.cpp HandleSendTelemetryResponse_v3() line 292 and SendRCdataToRF_v3() line 1178.
void Bluepad32Driver::TXnb(uint8_t *, bool, uint8_t *, SX12XX_Radio_Number_t)
{
}

// Called from src/rx_main.cpp HandleFHSS() line 416, LostConnection() line 846, TXdoneISR() line 1243,
// cycleRfMode() line 1702, EnterBindingMode() line 1746, updateBindingMode() line 1813,
// updateSwitchMode() line 2013, main_loop() line 2133; src/tx_main.cpp HandleFHSS() line 920;
// lib/OTA/OTA_Legacy.cpp ProcessRFPacket_v3() line 139 and LostConnection legacy path line 596.
void Bluepad32Driver::RXnb()
{
}

// No current external caller; included to match LR1121Driver's public IRQ surface used internally from
// lib/LR1121Driver/LR1121.cpp IsrCallback() line 797.
uint32_t Bluepad32Driver::GetIrqStatus(SX12XX_Radio_Number_t)
{
    return 0;
}

// No current external caller; included to match LR1121Driver's public IRQ surface used internally from
// lib/LR1121Driver/LR1121.cpp IsrCallback() line 803.
void Bluepad32Driver::ClearIrqStatus(SX12XX_Radio_Number_t)
{
}

// Called from lib/LBT/LBT.cpp LbtCcaTimerStart() line 190 when RADIO_LR1121-style LBT paths are enabled.
void Bluepad32Driver::StartRssiInst(SX12XX_Radio_Number_t)
{
}

// Called from lib/LBT/LBT.cpp LbtCcaTimerStart() lines 198 and 209.
int8_t Bluepad32Driver::GetRssiInst(SX12XX_Radio_Number_t)
{
    // BTstack can report RSSI asynchronously via GAP_EVENT_RSSI_MEASUREMENT,
    // but this driver does not keep a synchronous per-radio RSSI cache yet.
    return -128;
}

// Called from src/rx_main.cpp ProcessRFPacket() line 1172; src/tx_main.cpp ProcessDownlinkPacket() line 260;
// lib/OTA/OTA_Legacy.cpp ProcessDownlinkPacket_v3() line 980.
void Bluepad32Driver::GetLastPacketStats()
{
}

// Called from src/rx_main.cpp ProcessRFPacket() line 1145; src/tx_main.cpp ProcessDownlinkPacket() line 251.
void Bluepad32Driver::CheckForSecondPacket()
{
}

static void onConnectedController(ControllerPtr ctl)
{
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
