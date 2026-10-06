#include "AM32.h"
#include "common.h"
#include "config.h"

#if defined(BUILD_AM32_CONFIG) && defined(TARGET_RX)

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "driver/uart.h"

#define ENABLE_AM32_TCP_BRIDGE
#define ENABLE_AM32_TCP_BRIDGE_IMMEDIATE_ECHO // this is required, otherwise firmware flashing will fail

typedef struct
{
    int pin;
    uint8_t action;
    uint8_t* buffer1;
    uint32_t buffer1_len;
    uint32_t delay;
    uint8_t* buffer2;
    uint32_t buffer2_len;
}
am32_request_t;

enum
{
    AM32_ACTION_PIN_LIST, // reply with a list of pins
    AM32_ACTION_PIN_LOW,  // driven low
    AM32_ACTION_PIN_HIGH, // driven high
    AM32_ACTION_PIN_INIT, // pulled high and initialized as serial input
    AM32_ACTION_READ,
    AM32_ACTION_WRITE,
    AM32_ACTION_TEST_START,
    AM32_ACTION_TEST_SIGNAL,
    AM32_ACTION_CHANGE_DSHOT_MODE,
};

static int pin_num = -1;
static bool test_mode_started = false;
static uint32_t last_test_time = 0;
static bool am32_serial_ready = false;

void am32_setPinMode(int pin, bool isTx);
void am32_waitTxDone(size_t bytes);
void am32_freeStruct(am32_request_t* x);
void am32_hexDecode(const char* str, uint8_t* outbuf, int len);
void am32_servoDeinit();
extern void WebUpdateSendContent(AsyncWebServerRequest *request);

extern bool servo_initialized;
extern void servos_singleWrite(int selected_pin, int us);
extern bool servos_singleInit(int selected_pin);
extern void servos_deinitAll();
extern void servo_initializeEnable();

//extern SerialIO *serialIO;
extern void serialShutdown();

#ifdef ENABLE_AM32_TCP_BRIDGE
static WiFiServer* wserver = NULL;
static WiFiClient wclient;
static constexpr uint32_t TCP_AM32_DEFAULT_PORT = 65103; // 65103 is 65102 + 1, kept next to the VESC TCP bridge default port.
static constexpr uint32_t TCP_SEND_TIMEOUT = 0;
static constexpr size_t TCP_BRIDGE_BUFFER_SIZE = 384;
static constexpr size_t TCP_BRIDGE_PACKET_BUFFER_SIZE = 384;
static constexpr uint32_t TCP_BRIDGE_ACCEPT_INTERVAL_MS = 50;
static constexpr uint32_t TCP_BRIDGE_RESET_GAP_MS = 200;
static constexpr uint32_t TCP_BRIDGE_SET_BUFFER_DELAY_US = 800;

enum am32_tcp_bridge_state_t {
    AM32_TCP_BRIDGE_WAITING,
    AM32_TCP_BRIDGE_COMMAND,
    AM32_TCP_BRIDGE_PAYLOAD,
};

static am32_tcp_bridge_state_t tcpBridgeState = AM32_TCP_BRIDGE_WAITING;
static uint8_t tcpBridgePacketBuffer[TCP_BRIDGE_PACKET_BUFFER_SIZE];
static size_t tcpBridgePacketLength = 0;
static size_t tcpBridgeExpectedLength = 0;
static uint32_t tcpBridgeLastAcceptTime = 0;
#ifdef ENABLE_AM32_TCP_BRIDGE_IMMEDIATE_ECHO
static size_t tcpBridgeEchoBytesToDrop = 0;
#endif
static uint32_t tcpBridgeLastByteTime = 0;
static bool tcpBridgeDelayBeforePayload = false;

static size_t am32_tcpBridge_commandLength(uint8_t cmd);
static void am32_tcpBridge_flush();
static void am32_tcpBridge_handleByte(uint8_t b);
static void am32_tcpBridge_checkTimeout();
static void am32_tcpBridge_reset(bool flush);
static void am32_tcpBridge_write(const uint8_t *data, size_t len);
#endif

void am32_handleIo(AsyncWebServerRequest *request)
{
    am32_request_t req_data = {0};
    req_data.pin = pin_num;

    int paramsNr = request->params();
    for (int i = 0; i < paramsNr; i++) {
        const AsyncWebParameter* p = request->getParam(i);
        if (p->name().equalsIgnoreCase("pin")) {
            req_data.pin = (int)p->value().toInt();
            pin_num = req_data.pin;
        }
        if (p->name().equalsIgnoreCase("action")) {
            req_data.action = (uint8_t)p->value().toInt();
        }
        if (p->name().equalsIgnoreCase("delay")) {
            req_data.delay = (uint32_t)p->value().toInt();
        }
        if (p->name().equalsIgnoreCase("len1")) {
            uint32_t len = (uint32_t)p->value().toInt();
            if (len > 0) {
                req_data.buffer1 = (uint8_t*)malloc(len);
                if (req_data.buffer1) {
                    req_data.buffer1_len = len;
                }
            }
        }
        if (p->name().equalsIgnoreCase("len2")) {
            uint32_t len = (uint32_t)p->value().toInt();
            if (len > 0) {
                req_data.buffer2 = (uint8_t*)malloc(len);
                if (req_data.buffer2) {
                    req_data.buffer2_len = len;
                }
            }
        }
    }
    for (int i = 0; i < paramsNr; i++) {
        const AsyncWebParameter* p = request->getParam(i);
        if (p->name().equalsIgnoreCase("data1")) {
            am32_hexDecode(p->value().c_str(), req_data.buffer1, req_data.buffer1_len);
        }
        if (p->name().equalsIgnoreCase("data2")) {
            am32_hexDecode(p->value().c_str(), req_data.buffer2, req_data.buffer2_len);
        }
    }

    bool default_response = false;

    switch (req_data.action)
    {
        case AM32_ACTION_PIN_LOW:
            am32_servoDeinit();
            test_mode_started = false;
            last_test_time = 0;
            am32_serial_ready = false;
            pinMatrixOutDetach(req_data.pin, false, false);
            pinMatrixInDetach(req_data.pin, false, false);
            pinMode(req_data.pin, OUTPUT);
            digitalWrite(req_data.pin, LOW);
            default_response = true;
            break;
        case AM32_ACTION_PIN_HIGH:
            am32_servoDeinit();
            test_mode_started = false;
            last_test_time = 0;
            am32_serial_ready = false;
            pinMatrixOutDetach(req_data.pin, false, false);
            pinMatrixInDetach(req_data.pin, true, false);
            pinMode(req_data.pin, OUTPUT);
            digitalWrite(req_data.pin, HIGH);
            default_response = true;
            break;
        case AM32_ACTION_PIN_INIT:
            am32_servoDeinit();
            test_mode_started = false;
            last_test_time = 0;
            am32_serial_ready = false;

            Serial.end();
            serialShutdown();

            pinMode(req_data.pin, INPUT_PULLUP);
            pinMatrixOutDetach(req_data.pin, false, false);
            pinMatrixInDetach(req_data.pin, true, false);
            Serial.begin(19200, SERIAL_8N1, req_data.pin, req_data.pin);
            Serial.flush();
            am32_setPinMode(req_data.pin, false);
            am32_serial_ready = true;
            default_response = true;
            break;
        case AM32_ACTION_PIN_LIST:
            {
                am32_servoDeinit();
                test_mode_started = false;
                last_test_time = 0;
                char pin_str[64];
                AsyncResponseStream *response = request->beginResponseStream("text/plain");
                for (int ch = 0 ; ch < GPIO_PIN_PWM_OUTPUTS_COUNT ; ++ch)
                {
                    int8_t pwm_pin = GPIO_PIN_PWM_OUTPUTS[ch];
                    if (pwm_pin >= 0)
                    {
                        const rx_config_pwm_t *chConfig = config.GetPwmChannel(ch);
                        auto mode = (eServoOutputMode)(chConfig->val.mode);
                        if (mode <= som400Hz) {
                            snprintf(pin_str, 62, "PWM %d %d,", ch, pwm_pin);
                            response->print(pin_str);
                        }
                        else if (servoOutputModeIsDshot(mode) && !servoOutputModeIsDshot3D(mode)) {
                            snprintf(pin_str, 62, "DSHOT %d %d,", ch, pwm_pin);
                            response->print(pin_str);
                        }
                        else if (servoOutputModeIsDshot3D(mode)) {
                            snprintf(pin_str, 62, "DSHOT3D %d %d,", ch, pwm_pin);
                            response->print(pin_str);
                        }
                    }
                }
                if (GPIO_PIN_RCSIGNAL_TX >= 0)
                {
                    snprintf(pin_str, 62, "SERTX %d,", GPIO_PIN_RCSIGNAL_TX);
                    response->print(pin_str);
                }
                if (GPIO_PIN_RCSIGNAL_RX >= 0)
                {
                    snprintf(pin_str, 62, "SERRX %d,", GPIO_PIN_RCSIGNAL_RX);
                    response->print(pin_str);
                }
                request->send(response);
                am32_freeStruct(&req_data);
                return;
            }
        case AM32_ACTION_READ:
            {
                AsyncResponseStream *response = request->beginResponseStream("text/plain");
                bool has = false;
                char hex[3];
                while (Serial.available() > 0) {
                    sprintf(hex, "%02X", (uint8_t)Serial.read());
                    response->write(hex, 2);
                    has = true;
                }
                if (has == false) {
                    sprintf(hex, "xx");
                    response->write(hex, 2);
                }
                request->send(response);
                am32_freeStruct(&req_data);
                return;
            }
        case AM32_ACTION_WRITE:
            {
                uint32_t total_bytes = req_data.buffer1_len + req_data.buffer2_len;
                Serial.flush();
                am32_setPinMode(req_data.pin, true);
                // note: 515 microseconds for one byte at 19200 baud, from start bit to end of stop bit
                // note: 468 microseconds for one byte at 19200 baud, from start bit to start of stop bit
                // uart_wait_tx_done doesn't return fast enough, the AM32 bootloader attempts to reply but the first byte is being cut off
                // so my solution is to just calculate how much time it should take for the packet to be sent
                uint64_t ts = esp_timer_get_time();
                uint64_t deadline = ts + (520 * req_data.buffer1_len) + 26;
                for (int j = 0; j < req_data.buffer1_len; j++) {
                    Serial.write((uint8_t)req_data.buffer1[j]);
                }
                if (total_bytes >= 24) {
                    am32_waitTxDone(req_data.buffer1_len);
                }
                else {
                    while (esp_timer_get_time() < deadline) {
                        // do nothing
                    }
                }
                if (req_data.buffer2_len > 0) {
                    if (req_data.delay > 0) {
                        delayMicroseconds(req_data.delay);
                    }
                    ts = esp_timer_get_time();
                    deadline = ts + (520 * req_data.buffer2_len) + 26;
                    for (int j = 0; j < req_data.buffer2_len; j++) {
                        Serial.write((uint8_t)req_data.buffer2[j]);
                    }
                    if (total_bytes >= 24) {
                        am32_waitTxDone(req_data.buffer2_len);
                    }
                    else {
                        while (esp_timer_get_time() < deadline) {
                            // do nothing
                        }
                    }
                }
                am32_setPinMode(req_data.pin, false);

                // I have no idea why there's an echo of the TX in the RX buffer, but clear it out
                for (int j = 0; j < total_bytes; j++) {
                    if (Serial.available() > 0) {
                        Serial.read();
                    }
                }

                default_response = true;
            }
            break;
        case AM32_ACTION_TEST_START:
            {
                test_mode_started = true;
                am32_serial_ready = false;
                pinMatrixOutDetach(req_data.pin, false, false);
                pinMatrixInDetach(req_data.pin, false, false);
                Serial.end();
                pinMode(req_data.pin, OUTPUT);
                digitalWrite(req_data.pin, LOW);
                servos_deinitAll();
                if (servos_singleInit(req_data.pin)) {
                    default_response = true;
                }
                else {
                    AsyncResponseStream *response = request->beginResponseStream("text/plain");
                    response->write("error", 5);
                    request->send(response);
                }
            }
            break;
        case AM32_ACTION_TEST_SIGNAL:
            {
                last_test_time = millis();
                servos_singleWrite(req_data.pin, req_data.delay);
                default_response = true;
            }
            break;
        case AM32_ACTION_CHANGE_DSHOT_MODE:
            {
                for (int ch = 0 ; ch < GPIO_PIN_PWM_OUTPUTS_COUNT ; ++ch)
                {
                    int8_t pwm_pin = GPIO_PIN_PWM_OUTPUTS[ch];
                    if (pwm_pin == req_data.pin)
                    {
                        rx_config_pwm_t *chConfig = (rx_config_pwm_t *)config.GetPwmChannel(ch);
                        rx_config_pwm_t ncfg;
                        memcpy(&ncfg, chConfig, sizeof(rx_config_pwm_t));
                        auto newMode = req_data.delay == 0 ? somDShot : somDShot3D;
#ifdef BUILD_SHREW_SLOW_DSHOT
                        const auto oldMode = (eServoOutputMode)chConfig->val.mode;
                        if (oldMode >= somDshotSlow && oldMode <= somDshotSlow16_3D)
                            newMode = (eServoOutputMode)(somDshotSlow + ((oldMode - somDshotSlow) & ~1) + (req_data.delay != 0));
#endif
                        config.SetPwmChannel(ch, chConfig->val.failsafe, chConfig->val.inputChannel, chConfig->val.inverted, newMode, false);
                        ncfg.val.mode = newMode;
                        memcpy(chConfig, &ncfg, sizeof(rx_config_pwm_t));
                        config.Commit();
                        default_response = true;
                        break;
                    }
                }
            }
            break;
    }

    am32_freeStruct(&req_data);
    if (default_response) {
        AsyncResponseStream *response = request->beginResponseStream("text/plain");
        response->write("ok", 2);
        request->send(response);
    }
}

void am32_setupServer(AsyncWebServer* srv)
{
    srv->on("/am32.html", WebUpdateSendContent);
    srv->on("/am32.js", WebUpdateSendContent);
    srv->on("/ihex.js", WebUpdateSendContent);
    srv->on("/am32io", HTTP_POST, am32_handleIo);
}

void am32_setPinMode(int pin, bool isTx)
{
    if (isTx)
    {
        //digitalWrite(pin, HIGH);
        //pinMode(pin, OUTPUT);
        //digitalWrite(pin, HIGH);
        //pinMatrixInDetach(pin, true, false);
        pinMatrixOutAttach(pin, U0TXD_OUT_IDX, false, false);
    }
    else
    {
        pinMatrixOutDetach(pin, false, false);
        pinMode(pin, INPUT_PULLUP);
        pinMatrixInAttach(pin, U0RXD_IN_IDX, false);
    }
}

void am32_waitTxDone(size_t bytes)
{
    // One byte at 19200 baud is about 520us. Use a transfer-sized timeout so
    // large TCP bridge chunks finish before the pin is switched back to RX.
    uint32_t timeout_ms = ((bytes * 520U) + 26000U + 999U) / 1000U;
    uart_wait_tx_done(0, pdMS_TO_TICKS(timeout_ms));
}

void am32_freeStruct(am32_request_t* x) {
    if (x->buffer1_len > 0) {
        free(x->buffer1);
        x->buffer1_len = 0;
        x->buffer1 = NULL;
    }
    if (x->buffer2_len > 0) {
        free(x->buffer2);
        x->buffer2_len = 0;
        x->buffer2 = NULL;
    }
}

void am32_hexDecode(const char* str, uint8_t* outbuf, int len) {
    int slen = strlen(str);
    char tmp[3] = {0, 0, 0};
    for (int i = 0; i < len && i < slen / 2; i++)
    {
        int j = i * 2;
        tmp[0] = str[j];
        tmp[1] = str[j + 1];
        int k = strtol(tmp, NULL, 16);
        outbuf[i] = (uint8_t)k;
    }
}

/*
String am32_encodeHex(uint8_t* data, size_t length) {
    String result;
    result.reserve(length * 2);
    for (size_t i = 0; i < length; ++i) {
        char hex[3];
        sprintf(hex, "%02X", data[i]);
        result += hex;
    }
    return result;
}
*/

void am32_tick()
{
    if (test_mode_started && pin_num >= 0)
    {
        uint32_t now = millis();
        if ((now - last_test_time) >= 1000 && last_test_time != 0) {
            last_test_time = now;
            servos_singleWrite(pin_num, 0);
        }
    }
    
    #ifdef ENABLE_AM32_TCP_BRIDGE
    // AM32 TCP bridge requires the host PC to have some sort of driver that bridges a virtual serial port to a TCP pipe
    // start up a TCP server for the AM32 TCP bridge
    if (!wclient.connected() && WiFi.getMode() != WIFI_OFF)
    {
        if (wserver == NULL) {
            wserver = new WiFiServer(TCP_AM32_DEFAULT_PORT);
            wserver->begin();
        }

        uint32_t now = millis();
        if ((now - tcpBridgeLastAcceptTime) >= TCP_BRIDGE_ACCEPT_INTERVAL_MS) {
            tcpBridgeLastAcceptTime = now;
            wclient = wserver->available();
            if (wclient) {
                wclient.setNoDelay(true);
                wclient.setTimeout(TCP_SEND_TIMEOUT);
                am32_tcpBridge_reset(true);
            }
        }
    }

    if (wclient.connected()) // somebody connected
    {
        uint8_t bridgeBuffer[TCP_BRIDGE_BUFFER_SIZE];
        am32_tcpBridge_checkTimeout();

        if (am32_serial_ready && pin_num >= 0)
        {
            // pass data from serial port to host PC
            while (Serial.available() > 0) {
                size_t readSize = min((size_t)Serial.available(), sizeof(bridgeBuffer));
                size_t bytesRead = Serial.readBytes(bridgeBuffer, readSize);
                if (bytesRead == 0) {
                    break;
                }
#ifdef ENABLE_AM32_TCP_BRIDGE_IMMEDIATE_ECHO
                // com2tcp appears to wait for echoed bytes before sending the
                // next TCP fragment. Echo immediately so it can finish sending
                // a full AM32 payload while we keep buffering it for UART.
                size_t writeOffset = 0;
                if (tcpBridgeEchoBytesToDrop > 0) {
                    size_t drop = min(bytesRead, tcpBridgeEchoBytesToDrop);
                    tcpBridgeEchoBytesToDrop -= drop;
                    writeOffset = drop;
                }
                if (writeOffset < bytesRead) {
                    wclient.write(bridgeBuffer + writeOffset, bytesRead - writeOffset);
                }
#else
                wclient.write(bridgeBuffer, bytesRead);
#endif
            }
        }

        while (wclient.connected())
        {
            // data from host PC is available
            int available = wclient.available();
            if (available <= 0) {
                break;// nothing to do
            }

            // pass data from host PC to serial port
            size_t readSize = min((size_t)available, sizeof(bridgeBuffer));
            int bytesRead = wclient.read(bridgeBuffer, readSize);

            if (bytesRead <= 0) {
                break; // nothing to do
            }

#ifdef ENABLE_AM32_TCP_BRIDGE_IMMEDIATE_ECHO
            // The single-wire UART naturally echoes transmitted bytes back to
            // RX. Since we already gave com2tcp the immediate echo above, count
            // these bytes so the later physical echo is not forwarded twice.
            if (am32_serial_ready && pin_num >= 0) {
                wclient.write(bridgeBuffer, bytesRead);
                tcpBridgeEchoBytesToDrop += bytesRead;
            }
#endif

            for (int i = 0; i < bytesRead; i++)
            {
                am32_tcpBridge_handleByte(bridgeBuffer[i]);
            }
        }
    }
    #endif
}

void am32_servoDeinit()
{
    #if 0
    if (!servo_initialized)
    {
        servo_initializeEnable();
    }
    #endif
    servos_deinitAll();
}

#ifdef ENABLE_AM32_TCP_BRIDGE

/*
The AM32 bootloader has a very short timeout, 250 microseconds, so if a large packet for firmware gets chunked up, the bootloader sends a 0xC2 meaning "send_BAD_CRC_ACK"

The strategy is to decode the packets, figure out how long they need to be, and then always send in full packet chunks. This is done by a state machine that reads every byte

The state machine resets after 200ms of inactivity. Bytes that are not understood are never thrown away, they are still sent out the UART. CRC is not being enforced.
*/

static size_t am32_tcpBridge_commandLength(uint8_t cmd)
{
    switch (cmd) {
        case 0xFF: // SET_ADDRESS: cmd, 0, addr_hi, addr_lo, crc_lo, crc_hi
        case 0xFE: // SET_BUFFER: cmd, 0, x256, len, crc_lo, crc_hi
            return 6;
        case 0x00:
            // The AM32 init query starts with zero padding and is 21 bytes
            // long. Short RUN sequences are flushed by the idle timer.
            return 21;
        case 0x01: // PROG_FLASH
        case 0x02: // ERASE_FLASH
        case 0x03: // READ_FLASH / VERIFY
        case 0x04: // READ_EEPROM / VERIFY
        case 0x05: // PROG_EEPROM on some bootloader variants
        case 0x06: // READ_SRAM on some bootloader variants
        case 0x07: // READ_FLASH_ATM on some bootloader variants
        case 0xFD: // KEEP_ALIVE
            return 4;
        default:
            return 1;
    }
}

static void am32_tcpBridge_flush()
{
    if (tcpBridgePacketLength == 0) {
        return;
    }
    if (tcpBridgeDelayBeforePayload) {
        delayMicroseconds(TCP_BRIDGE_SET_BUFFER_DELAY_US);
    }
    am32_tcpBridge_write(tcpBridgePacketBuffer, tcpBridgePacketLength);
    tcpBridgePacketLength = 0;
    tcpBridgeDelayBeforePayload = false;
}

static void am32_tcpBridge_handleByte(uint8_t b)
{
    uint32_t now = millis();
    if (tcpBridgeLastByteTime != 0 && (now - tcpBridgeLastByteTime) >= TCP_BRIDGE_RESET_GAP_MS) {
        am32_tcpBridge_reset(true);
    }
    tcpBridgeLastByteTime = now;

    if (tcpBridgeState == AM32_TCP_BRIDGE_WAITING) {
        tcpBridgeState = AM32_TCP_BRIDGE_COMMAND;
        tcpBridgeExpectedLength = am32_tcpBridge_commandLength(b);
    }

    if (tcpBridgePacketLength >= sizeof(tcpBridgePacketBuffer)) {
        am32_tcpBridge_reset(true);
        tcpBridgeState = AM32_TCP_BRIDGE_COMMAND;
        tcpBridgeExpectedLength = am32_tcpBridge_commandLength(b);
        tcpBridgeLastByteTime = now;
    }

    tcpBridgePacketBuffer[tcpBridgePacketLength++] = b;

    if (tcpBridgePacketLength < tcpBridgeExpectedLength) {
        return;
    }

    if (tcpBridgeState == AM32_TCP_BRIDGE_COMMAND) {
        uint8_t cmd = tcpBridgePacketBuffer[0];
        size_t payloadLength = 0;
        if (cmd == 0xFE && tcpBridgePacketLength >= 4) {
            payloadLength = (tcpBridgePacketBuffer[2] == 0x01) ? 256U : tcpBridgePacketBuffer[3];
        }

        am32_tcpBridge_flush();

        if (cmd == 0xFE) {
            tcpBridgeState = AM32_TCP_BRIDGE_PAYLOAD;
            tcpBridgeExpectedLength = payloadLength + 2U; // payload plus CRC
            tcpBridgeDelayBeforePayload = true;
            tcpBridgeLastByteTime = now;
        }
        else {
            am32_tcpBridge_reset(false);
        }
        return;
    }

    am32_tcpBridge_reset(true);
}

static void am32_tcpBridge_checkTimeout()
{
    if (tcpBridgeLastByteTime != 0 && (millis() - tcpBridgeLastByteTime) >= TCP_BRIDGE_RESET_GAP_MS) {
        am32_tcpBridge_reset(true);
    }
}

static void am32_tcpBridge_reset(bool flush)
{
    if (flush) {
        am32_tcpBridge_flush();
    }
    tcpBridgeState = AM32_TCP_BRIDGE_WAITING;
    tcpBridgePacketLength = 0;
    tcpBridgeExpectedLength = 0;
    tcpBridgeLastByteTime = 0;
    tcpBridgeDelayBeforePayload = false;
}

static void am32_tcpBridge_write(const uint8_t *data, size_t len)
{
    if (!am32_serial_ready || pin_num < 0 || data == NULL || len == 0) {
        return;
    }

    am32_setPinMode(pin_num, true);
    Serial.write(data, len);

    if (len >= 24) {
        am32_waitTxDone(len);
    }
    else {
        uint64_t deadline = esp_timer_get_time() + (520ULL * len) + 26;
        while (esp_timer_get_time() < deadline) {
            // wait for short packets without the uart_wait_tx_done latency
        }
    }

    am32_setPinMode(pin_num, false);
}
#endif

#else

#if defined(PLATFORM_ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

#include <ESPAsyncWebServer.h>

void am32_setupServer(AsyncWebServer* srv)
{
    // do nothing
}

void am32_tick()
{
    // do nothing
}

#endif
