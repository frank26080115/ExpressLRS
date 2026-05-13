#include "WebBackend.h"

#include "common.h"
#include "helpers.h"
#if defined(TARGET_RX)
#include "CustomMixer.h"
#include "devServoOutput.h"
#include "../../src/rx-serial/devSerialIO.h"
#include "AM32.h"
#endif
#if defined(TARGET_TX)
#include "handset.h"
#endif
#if defined(BUILD_BLUEPAD32) && defined(PLATFORM_ESP32)
#include <Arduino.h>
#include "bluepad.h"
#endif

bool webbe_installed = false;
bool webbe_ws_started = false;

#ifdef BUILD_WEB_BACKEND_WEBSOCKET
static AsyncWebSocket* ws;
static AsyncWebSocketClient* wsLastClient = NULL;

static constexpr uint8_t WS_CHANNEL_PACKET_HEADER = '>';
static constexpr uint8_t WS_CHANNEL_PACKET_FOOTER = '#';
static constexpr uint8_t WS_ACK = '!';
static constexpr uint32_t WS_ACK_INTERVAL_MS = 100;
static constexpr size_t WS_CHANNEL_PACKET_LEN = CRSF_NUM_CHANNELS * 2U;
static uint8_t wsChannelPacket[WS_CHANNEL_PACKET_LEN];
static size_t wsChannelPacketLen = 0;
static bool wsChannelPacketActive = false;
static uint32_t wsLastPacketTime = 0;
static uint32_t wsLastAckTime = 0;
static uint32_t wsPendingAckClientId = 0;
static bool wsPendingAck = false;

#if defined(BUILD_BLUEPAD32) && defined(PLATFORM_ESP32)
static constexpr uint32_t BLUEPAD_WS_MIN_SEND_HEAP = 18000;
#endif

static inline void resetWsChannelPacket()
{
    wsChannelPacketLen = 0;
    wsChannelPacketActive = false;
}

static bool parseWsChannelPacket()
{
    if (wsChannelPacketLen != WS_CHANNEL_PACKET_LEN)
    {
        return false;
    }

    #if defined(TARGET_TX) && defined(BUILD_BLUEPAD32) && defined(PLATFORM_ESP32)
    if (bluepad_has_recent_channel_data()) {
        return true;
    }
    #endif


    for (uint8_t ch = 0; ch < CRSF_NUM_CHANNELS; ++ch)
    {
        ChannelData[ch] = (uint16_t)wsChannelPacket[ch * 2] | ((uint16_t)wsChannelPacket[(ch * 2) + 1] << 8);
    }

    return true;
}

void onWsEvent(AsyncWebSocket *server,
               AsyncWebSocketClient *client,
               AwsEventType type,
               void *arg,
               uint8_t *data,
               size_t len)
{
    // assume single client!

    if (type == WS_EVT_CONNECT)
    {
        resetWsChannelPacket();
        webbe_ws_started = true;
        if (client != nullptr)
        {
            #if defined(BUILD_BLUEPAD32) && defined(PLATFORM_ESP32)
            client->setCloseClientOnQueueFull(true);
            #else
            client->setCloseClientOnQueueFull(false);
            #endif
            wsLastClient = client;
        }
    }
    else if (type == WS_EVT_DATA)
    {
        uint32_t now = millis();

        // if we stop mid-stream for a long time, reset the state machine
        if ((now - wsLastPacketTime) >= 500 && wsLastPacketTime != 0) {
            resetWsChannelPacket();
        }

        for (size_t i = 0; i < len; ++i) // for every byte in this current payload
        {
            const uint8_t ch = data[i];

            // state machine is waiting for header
            if (!wsChannelPacketActive && ch == WS_CHANNEL_PACKET_HEADER)
            {
                wsChannelPacketLen = 0;
                wsChannelPacketActive = true;
                // go to next state, check next byte
                continue;
            }

            if (!wsChannelPacketActive)
            {
                // do not populate temporary buffer
                continue;
            }

            // end of packet expected
            if (wsChannelPacketLen == WS_CHANNEL_PACKET_LEN)
            {
                // is end of packet, validated (and copied)
                if (ch == WS_CHANNEL_PACKET_FOOTER && parseWsChannelPacket())
                {
                    wsLastPacketTime = now;
                    #if defined(TARGET_RX)
                    custommixer_mix();
                    // signal to other services
                    servoNewChannelsAvailable();
                    crsfRCFrameAvailable();
                    #endif
                    #if defined(TARGET_TX)
                    handset->FakeDataReceived();
                    #endif
                    // Defer the ack to the main Wi-Fi loop on ESP8266/ESP8285.
                    // Sending inline from the websocket receive callback can re-enter
                    // the network stack while it is still processing the inbound frame.
                    wsPendingAckClientId = client->id();
                    wsPendingAck = true;
                }
                resetWsChannelPacket(); // restart state machine
                continue;
            }

            if (wsChannelPacketLen >= WS_CHANNEL_PACKET_LEN) // error, stream did not have a terminator
            {
                resetWsChannelPacket(); // restart state machine
                continue;
            }

            wsChannelPacket[wsChannelPacketLen++] = ch;
        }
    }
    else if (type == WS_EVT_DISCONNECT)
    {
        (void)server;
        resetWsChannelPacket();
        if (wsLastClient == client)
        {
            wsLastClient = NULL;
        }
        if (wsPendingAck && client != nullptr && wsPendingAckClientId == client->id())
        {
            wsPendingAck = false;
        }
        #if defined(TARGET_RX)
        servosFailsafe(true);
        #endif
    }
    else if (type == WS_EVT_ERROR)
    {
        // WebSocket error event.
    }
    else if (type == WS_EVT_PONG)
    {
        // WebSocket pong frame received from the client.
    }
}
#endif

void webbe_tick()
{
    // this function only runs if wifiStarted is true
    // it will repeat as fast as the device schedule engine allows

    uint32_t now = millis();

    #if defined(TARGET_RX) && defined(PLATFORM_ESP32)
    am32_tick();
    #endif

    #if defined(TARGET_RX) && defined(BUILD_WEB_BACKEND_WEBSOCKET)
    if (wsPendingAck)
    {
        if ((now - wsLastAckTime) >= WS_ACK_INTERVAL_MS) // rate limit the ack
        {
            if (ws->availableForWrite(wsPendingAckClientId))
            {
                ws->text(wsPendingAckClientId, &WS_ACK, 1U);
                wsLastAckTime = now;
                wsPendingAck = false;
            }
        }
    }

    if (webbe_ws_started) {
        servosUpdate(now); // transitioning to Wi-Fi mode would have disabled the servos device scheduler, so we call the update function here
        // no need to call handleSerialIO() as it is called in the main application loop
        // the serial IO device scheduler is still running, and will handle the new data if crsfRCFrameAvailable is called
    }
    
    if ((now - wsLastPacketTime) >= 2000 && wsLastPacketTime != 0) {
        // I understand this might be redundant as servosUpdate itself has an internal timeout
        servosFailsafe(!webbe_ws_started);
    }
    #else
    UNUSED(now);
    UNUSED(wsLastAckTime);
    #endif
}

void webbe_install(AsyncWebServer* srv)
{
    if (webbe_installed) {
        // do not repeat
        return;
    }

    #if defined(TARGET_RX) && defined(PLATFORM_ESP32)
    am32_setupServer(srv);
    #endif

    #if defined(BUILD_BLUEPAD32) && defined(PLATFORM_ESP32)
    bluepad_setupServer(srv);
    #endif

    #ifdef BUILD_WEB_BACKEND_WEBSOCKET
    ws = new AsyncWebSocket("/ws");
    ws->onEvent(onWsEvent);
    srv->addHandler(ws);
    #if defined(TARGET_RX)
    servosFailsafe(true); // this line may be redundant as the servos device scheduler's event handler would've done it too
    #endif
    #endif

    webbe_installed = true; // do not repeat
}

void webbe_sendStr(const char* s)
{
    #ifdef BUILD_WEB_BACKEND_WEBSOCKET
    if (s == nullptr || ws == nullptr)
    {
        return;
    }

    #if defined(BUILD_BLUEPAD32) && defined(PLATFORM_ESP32)
    if (ESP.getFreeHeap() < BLUEPAD_WS_MIN_SEND_HEAP)
    {
        return;
    }

    if (wsLastClient != NULL && wsLastClient->status() == WS_CONNECTED && !wsLastClient->queueIsFull())
    {
        const uint32_t clientId = wsLastClient->id();
        if (ws->availableForWrite(clientId))
        {
            wsLastClient->text(s);
        }
    }
    return;
    #endif

    if (ws->count() > 0)
    {
        if (ws->availableForWriteAll())
        {
            ws->textAll(s);
        }
        return;
    }

    if (wsLastClient != NULL && wsLastClient->status() == WS_CONNECTED && !wsLastClient->queueIsFull())
    {
        wsLastClient->text(s);
    }
    #else
    UNUSED(s);
    #endif
}

uint8_t webbe_getRandomWifiChannel()
{
    /*
    to combat Wi-Fi channel congestion, Wi-Fi channel is randomly selected every time
    */
    uint32_t rawRand =
    #if defined(PLATFORM_ESP32)
        esp_random()
    #elif defined(PLATFORM_ESP8266)
        ESP.random()
    #else
        rand() // this should not be used, unsupported platform
    #endif
        ;
    const uint32_t microsNow = micros();
    const uint32_t millisNow = millis();
    const uint32_t bootRand = rawRand ^ microsNow ^ millisNow;
    static const uint8_t channels[] = {1, 6, 11};
    const uint8_t channelIndex = bootRand % (sizeof(channels) / sizeof(channels[0]));
    const uint8_t channel = channels[channelIndex];
    #if defined(BUILD_BLUEPAD32) && defined(ENABLE_BLUEPAD32_DEBUG)
    printf("WiFi channel RNG: raw=%08x micros=%u millis=%u mixed=%08x index=%u channel=%u\n",
           rawRand,
           microsNow,
           millisNow,
           bootRand,
           channelIndex,
           channel);
    #endif
    return channel;
}
