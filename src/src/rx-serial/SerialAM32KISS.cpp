#include "SerialAM32KISS.h"

#include "common.h"
#include "crc.h"
#include <string.h>
#include "CustomCodeHooks.h"

static GENERIC_CRC8* kiss_crc = NULL;
static constexpr uint32_t AM32KISS_TELEMETRY_INTERVAL_MS = 100;
static uint32_t last_telem_send_time = 0;

static uint16_t kiss_read_u16(uint8_t high, uint8_t low)
{
    return ((uint16_t)high << 8) | low;
}

static void am32kiss_sendTelemetry(const kiss_telem_pkt_t *data)
{
    const uint32_t now = millis();
    if (last_telem_send_time != 0 && (now - last_telem_send_time) < AM32KISS_TELEMETRY_INTERVAL_MS) {
        return;
    }
    last_telem_send_time = now;

    if (customcodehooks_onam32kisstelemetry((void*)data))
    {
        return;
    }

    const uint16_t voltage = kiss_read_u16(data->voltage_h, data->voltage_l);
    const uint16_t current = kiss_read_u16(data->current_h, data->current_l);
    const uint16_t consumption = kiss_read_u16(data->consumption_h, data->consumption_l);
    const uint16_t erpm = kiss_read_u16(data->erpm_h, data->erpm_l);

    CRSF_MK_FRAME_T(crsf_sensor_battery_t) crsfbatt = { 0 };
    crsfbatt.p.voltage = htobe16(voltage / 10U);
    crsfbatt.p.current = htobe16(current / 10U);
    crsfbatt.p.capacity = htobe24(consumption);
    crsfRouter.SetHeaderAndCrc((crsf_header_t *)&crsfbatt, CRSF_FRAMETYPE_BATTERY_SENSOR, CRSF_FRAME_SIZE(sizeof(crsf_sensor_battery_t)));
    crsfRouter.deliverMessageTo(CRSF_ADDRESS_RADIO_TRANSMITTER, &crsfbatt.h);
    crsfBatterySensorDetected = true;

    CRSF_MK_FRAME_T(crsf_sensor_rpm_t) crsfrpm = { 0 };
    crsfrpm.p.source_id = 0;
    crsfrpm.p.rpm0 = htobe24((uint32_t)erpm * 100U);
    crsfRouter.SetHeaderAndCrc((crsf_header_t *)&crsfrpm, CRSF_FRAMETYPE_RPM, CRSF_FRAME_SIZE(1 + 3));
    crsfRouter.deliverMessageTo(CRSF_ADDRESS_RADIO_TRANSMITTER, &crsfrpm.h);

    CRSF_MK_FRAME_T(crsf_sensor_temp_t) crsftemp = { 0 };
    crsftemp.p.source_id = 0;
    crsftemp.p.temperature[0] = htobe16((int16_t)data->temperature * 10);
    crsfRouter.SetHeaderAndCrc((crsf_header_t *)&crsftemp, CRSF_FRAMETYPE_TEMP, CRSF_FRAME_SIZE(1 + 2));
    crsfRouter.deliverMessageTo(CRSF_ADDRESS_RADIO_TRANSMITTER, &crsftemp.h);
}

void SerialAM32KISS::begin(uint8_t idx, int8_t pin_rx)
{
    DBGLN("SerialAM32KISS::begin %u %d", idx, pin_rx);

    if (kiss_crc == NULL) {
        kiss_crc = new GENERIC_CRC8(0x07);
    }
    this->idx = idx;
    this->pin_rx = pin_rx;
    this->configed = true;
    resetReceiveState();
}

uint32_t SerialAM32KISS::sendRCFrame(bool frameAvailable, bool frameMissed, uint32_t *channelData)
{
    (void)frameAvailable;
    (void)frameMissed;
    (void)channelData;
    return DURATION_IMMEDIATELY;
}

void SerialAM32KISS::forwardMessage(const crsf_header_t *message)
{
    (void)message;
}

void SerialAM32KISS::processBytes(uint8_t *bytes, uint16_t size)
{
    if (!this->configed || size == 0) {
        return;
    }

    const uint32_t now = millis();
    if (last_byte_time != 0 && (now - last_byte_time) >= RECEIVE_RESET_GAP_MS) {
        resetReceiveState();
    }
    last_byte_time = now;

    for (uint16_t i = 0; i < size; i++) {
        payloadCache[payloadBytesReceived++] = bytes[i];

        if (payloadBytesReceived == PAYLOAD_CACHE_SIZE) {
            if (processPacketPayload(payloadCache, PAYLOAD_CACHE_SIZE, payloadBytesReceived)) {
                last_packet_time = now;
            }
            resetReceiveState();
        }
    }
}

bool SerialAM32KISS::processPacketPayload(const uint8_t *message, uint8_t payloadLength, uint16_t cachedLength)
{
    if (payloadLength != sizeof(kiss_telem_pkt_t) || cachedLength < sizeof(kiss_telem_pkt_t)) {
        return false;
    }

    const kiss_telem_pkt_t *packet = (const kiss_telem_pkt_t *)message;
    if (packet->crc != kiss_crc->calc(message, sizeof(kiss_telem_pkt_t) - 1)) {
        return false;
    }

    memcpy(&data, packet, sizeof(data));
    am32kiss_sendTelemetry(&data);
    return true;
}

void SerialAM32KISS::resetReceiveState()
{
    payloadBytesReceived = 0;
}

SerialAM32KISS::~SerialAM32KISS()
{
    DBGLN("SerialAM32KISS::destruct");
    crsfRouter.removeConnector(this);
}
