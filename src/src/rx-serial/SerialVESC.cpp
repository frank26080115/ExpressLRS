#include "SerialVESC.h"
#include "common.h"
#include "OTA.h"
#include "crc.h"
#include "device.h"
#include "config.h"
#include "CustomMixer.h"
#include "CustomCodeHooks.h"
#include "vesc_buffer.h"
#if defined(PLATFORM_ESP32)
#include <WiFi.h>
#elif defined(PLATFORM_ESP8266)
#include <ESP8266WiFi.h>
#endif
#include <math.h>

constexpr int32_t DUTY_RANGE_SNAP_TO_MAX = 99500; // 99.5%
constexpr int32_t POSITION_RANGE_SNAP_TO = 360000000; // 360 deg
constexpr int32_t POSITION_RANGE_SNAP_MIN = POSITION_RANGE_SNAP_TO - 500000;
constexpr int32_t POSITION_RANGE_SNAP_MAX = POSITION_RANGE_SNAP_TO + 500000;
// 1073741823 (0x3FFFFFFF) is the practical technical limit here because larger
// decoded values can overflow the signed int32 math used by i32map() in
// bidirectional mode. We intentionally cap at a cleaner 1000000000 so the
// limit is easier to read and reason about.
constexpr int32_t VESC_RANGE_MAX = 1000000000;
static int32_t i32map(int32_t x, int32_t in_min, int32_t in_max, int32_t out_min, int32_t out_max);
static int32_t decode_ufloat16(uint16_t v);
static int32_t xy_to_vesc_pos_offset(int16_t x, int16_t y, bool invert);
static int16_t xy_magnitude(int16_t x, int16_t y);

static Crc2Byte* vesc_crc = NULL; // we only need one instance even if two serial ports are used

static int8_t first_rx_pin = -1;

static SerialVESC* main_vesc_instance = NULL;

bool vesc_tcp_bridge_started = false;

#if defined(ENABLE_VESC_TCP_BRIDGE)
static WiFiServer* wserver = NULL;
static WiFiClient wclient;
static constexpr uint32_t TCP_VESC_DEFAULT_PORT = 65102;
static constexpr uint32_t TCP_SEND_TIMEOUT = 0;
static constexpr size_t TCP_BRIDGE_BUFFER_SIZE = 256;
#endif

static uint8_t telem_cfg = 0; // // bit 0 = enable power telemetry, bit 1 = enable RPM telemetry, bit 2 = enable TCP bridge
static void vesc_sendTelemetry(vesc_telem_t* data);

void SerialVESC::begin(uint8_t idx, int8_t pin, int8_t pin_rx)
{
    #ifdef BUILD_VESC_UART

    DBGLN("SerialVESC::begin %u %d", idx, pin);

    this->idx = idx;

    if (pin < 0) {
        // TX pin is configured as main TX output
        pin = GPIO_PIN_RCSIGNAL_TX;
    }
    if (pin < 0) {
        for (uint8_t ch = 0; ch < GPIO_PIN_PWM_OUTPUTS_COUNT; ch++)
        {
            if (config.GetPwmChannel(ch)->val.mode == somSerial
                && U0TXD_GPIO_NUM == GPIO_PIN_PWM_OUTPUTS[ch]
            ) {
                // TX pin is configured via PWM config
                pin = GPIO_PIN_PWM_OUTPUTS[ch];
                break;
            }
        }
    }
    if (pin_rx < 0) {
        // if we are lucky, RX pin is actually configured, so why not use it
        pin_rx = GPIO_PIN_RCSIGNAL_RX;
    }
    if (pin < 0) {
        for (uint8_t ch = 0; ch < GPIO_PIN_PWM_OUTPUTS_COUNT; ch++)
        {
            if (config.GetPwmChannel(ch)->val.mode == somSerial
                && U0RXD_GPIO_NUM == GPIO_PIN_PWM_OUTPUTS[ch]
            ) {
                // RX pin is configured as serial via PWM config
                pin = GPIO_PIN_PWM_OUTPUTS[ch];
                break;
            }
        }
    }

    this->pin = pin;
    this->pin_rx = pin_rx;
    // if no VESC class has registered a RX pin, then this is the first, and thus the main VESC
    if (first_rx_pin < 0) {
        first_rx_pin = pin_rx;
        this->is_main_vesc = true;
        main_vesc_instance = this;
    }

    // cache the configuration, depending on which serial port we are
    const uint32_t* cfg_int_ptr = config.GetVescCfg();
    int copy_idx = (this->idx == 0) ? 0 : 3;
    memcpy((void*)this->cfg, (void*)&(cfg_int_ptr[copy_idx]), sizeof(vesc_cfg_t) * 3);
    for (int i = 0; i < 3; i++) {
        this->range[i] = decode_ufloat16(this->cfg[i].range);
    }

    if (vesc_crc == NULL) {
        // we only need one instance even if two serial ports are used
        vesc_crc = new Crc2Byte();
        vesc_crc->init(16, 0x1021);
    }

    telem_cfg = config.GetVescCfgExtras();

    this->configed = true;

    resetReceiveState();

    #endif
}

uint32_t SerialVESC::sendRCFrame(bool frameAvailable, bool frameMissed, uint32_t *channelData)
{
    #ifdef BUILD_VESC_UART
    if (!this->configed) {
        return DURATION_IMMEDIATELY;
    }

    // no data from transmitter, send nothing, the VESC must be configured with the right failsafe behavior
    if (!frameAvailable) {
        return DURATION_IMMEDIATELY;
    }

    for (int i = 0; i < 3; i++)
    {
        const vesc_cfg_t* pcfg = &(this->cfg[i]);

        // check if it is actually configured to do anything
        if (pcfg->channel_x == 0 || pcfg->cmd == 0) {
            continue;
        }

        vesc_i32_packet_t packet;
        packet.start_byte = 0x02;
        packet.stop_byte = 0x03;
        packet.payload_length = 5;
        packet.command_byte = pcfg->cmd;

        int32_t val = 0;
        int32_t range = this->range[i];

        // for duty cycle, limit the duty to a max of 100%, and also account for the loss of precision from user settings
        if (pcfg->cmd == COMM_SET_DUTY) {
            if (range >= DUTY_RANGE_SNAP_TO_MAX || range == 0) {
                range = 100000;
            }
        }
        else if (pcfg->cmd == COMM_SET_POS) {
            // for position, because we can't represent 360 exactly, if it's close enough, snap to 360
            if (range >= POSITION_RANGE_SNAP_MIN && range <= POSITION_RANGE_SNAP_MAX) {
                range = POSITION_RANGE_SNAP_TO;
            }
        }

        if (pcfg->cmd != COMM_SET_POS || pcfg->channel_y == 0)
        {
            int32_t cval = ChannelDataMixed[pcfg->channel_x - 1];
            // we first check for specific inputs so that we are 100% certain that we are sending out a true zero, for safety reasons
            if (pcfg->cmd != COMM_SET_POS && pcfg->bidirectional && cval >= CRSF_CHANNEL_VALUE_MID - 1 && cval <= CRSF_CHANNEL_VALUE_MID + 1) {
                val = 0;
            }
            else if (pcfg->cmd != COMM_SET_POS && !pcfg->bidirectional && cval <= CRSF_CHANNEL_VALUE_MIN + 1) {
                val = 0;
            }
            // then consider the other deadzones
            else if (pcfg->cmd != COMM_SET_POS && cval >= CRSF_CHANNEL_VALUE_MAX - 1) {
                val = range;
            }
            else if (pcfg->cmd != COMM_SET_POS && pcfg->bidirectional && cval <= CRSF_CHANNEL_VALUE_MIN + 1) {
                val = -range;
            }
            else {
                // actually do calculations
                val = i32map(cval, CRSF_CHANNEL_VALUE_MIN, CRSF_CHANNEL_VALUE_MAX, pcfg->bidirectional ? -range : 0, range);
            }
        }
        else
        {
            int16_t x = ChannelDataMixed[pcfg->channel_x - 1] - CRSF_CHANNEL_VALUE_MID;
            int16_t y = ChannelDataMixed[pcfg->channel_y - 1] - CRSF_CHANNEL_VALUE_MID;
            if (xy_magnitude(x, y) <= ((CRSF_CHANNEL_VALUE_MAX - CRSF_CHANNEL_VALUE_MIN) / 4)) { // must exceed deadzone to actually be considered valid
                continue; // send nothing
            }
            val = xy_to_vesc_pos_offset(x, y, pcfg->bidirectional != 0);
        }

        packet.value = __builtin_bswap32((uint32_t)val);
        packet.crc = __builtin_bswap16(vesc_crc->calc(&(packet.command_byte), 5, 0));
        _outputPort->write((const uint8_t *)&packet, sizeof(vesc_i32_packet_t));
    }

    #ifdef ENABLE_VESC_TELEMETRY
    if (is_main_vesc && (telem_cfg & (VESC_TELEM_CFG_POWER | VESC_TELEM_CFG_RPM | VESC_TELEM_CFG_TEMPERATURE)) != 0U)
    {
        // see if it is time to send telemetry
        uint32_t now = millis();
        if ((now - last_req_time) >= 100) {
            last_req_time = now;
            vesc_cmd_packet_t telem_cmd;
            telem_cmd.start_byte = 0x02;
            telem_cmd.stop_byte = 0x03;
            telem_cmd.payload_length = 1;
            telem_cmd.command_byte = COMM_GET_VALUES;
            telem_cmd.crc = __builtin_bswap16(vesc_crc->calc(&(telem_cmd.command_byte), 1, 0));
            _outputPort->write((const uint8_t *)&telem_cmd, sizeof(vesc_cmd_packet_t));
        }
    }
    #endif

    #endif

    return DURATION_IMMEDIATELY;
}

void SerialVESC::forwardMessage(const crsf_header_t *message) {
}

void SerialVESC::processBytes(uint8_t *bytes, uint16_t size) {
    if (vesc_crc == NULL || !this->is_main_vesc) {
        return;
    }

    uint32_t now = millis();

    if ((now - last_byte_time) >= 500) {
        // too long signal loss, start over
        receiveState = RECEIVE_WAIT_START;
    }

    #ifdef ENABLE_VESC_TCP_BRIDGE
    if (handleTcpBridge(bytes, size)) {
        last_packet_time = now;
        return;
    }
    #endif

    #ifdef ENABLE_VESC_TELEMETRY
    if (size <= 0) {
        return;
    }

    last_byte_time = now;

    for (uint16_t i = 0; i < size; i++)
    {
        const uint8_t byte = bytes[i];

        switch (receiveState)
        {
        case RECEIVE_WAIT_START:
            if (byte == 0x02) {
                receiveState = RECEIVE_LENGTH;
            }
            break;

        case RECEIVE_LENGTH:
            if (byte == 0) {
                resetReceiveState();
                break;
            }

            payloadLength = byte;
            payloadBytesReceived = 0;
            payloadBytesCached = 0;
            calculatedCrc = 0;
            receivedCrc = 0;
            receiveState = RECEIVE_PAYLOAD;
            break;

        case RECEIVE_PAYLOAD:
            if (payloadBytesCached < PAYLOAD_CACHE_SIZE) {
                payloadCache[payloadBytesCached++] = byte;
            }

            calculatedCrc = vesc_crc->calc(&bytes[i], 1, calculatedCrc);
            payloadBytesReceived++;

            if (payloadBytesReceived >= payloadLength) {
                receiveState = RECEIVE_CRC_HIGH;
            }
            break;

        case RECEIVE_CRC_HIGH:
            receivedCrc = ((uint16_t)byte) << 8;
            receiveState = RECEIVE_CRC_LOW;
            break;

        case RECEIVE_CRC_LOW:
            receivedCrc |= byte;
            if (receivedCrc != calculatedCrc) {
                resetReceiveState();
            }
            else {
                receiveState = RECEIVE_STOP;
            }
            break;

        case RECEIVE_STOP:
            if (byte == 0x03) {
                last_packet_time = last_byte_time;
                processPacketPayload(payloadCache, payloadLength, payloadBytesCached);
            }
            resetReceiveState();
            break;

        default:
            resetReceiveState();
            break;
        }
    }
    #endif
}

bool SerialVESC::processPacketPayload(const uint8_t *payload, uint8_t payloadLength, uint16_t cachedLength)
{
    (void)payload;
    (void)payloadLength;
    (void)cachedLength;
    #ifdef ENABLE_VESC_TELEMETRY
    COMM_PACKET_ID packetId;
    int32_t index = 0;

    if (payloadLength == 0 || cachedLength < payloadLength) {
        return false;
    }

    packetId = (COMM_PACKET_ID)payload[0];
    payload++; // Removes the packetId from the actual payload (payload)

    switch (packetId){
        case COMM_FW_VERSION: // Structure defined here: https://github.com/vedderb/bldc/blob/43c3bbaf91f5052a35b75c2ff17b5fe99fad94d1/commands.c#L164
            //fw_version.major = payload[index++];
            //fw_version.minor = payload[index++];
            return true;
        case COMM_GET_VALUES: // Structure defined here: https://github.com/vedderb/bldc/blob/43c3bbaf91f5052a35b75c2ff17b5fe99fad94d1/commands.c#L164
            telem_data.tempMosfet        = buffer_get_float16(payload, 10.0, &index);                // 2 bytes - mc_interface_temp_fet_filtered()
            telem_data.tempMotor         = buffer_get_float16(payload, 10.0, &index);                // 2 bytes - mc_interface_temp_motor_filtered()
            telem_data.avgMotorCurrent   = buffer_get_float32(payload, 100.0, &index);               // 4 bytes - mc_interface_read_reset_avg_motor_current()
            telem_data.avgInputCurrent   = buffer_get_float32(payload, 100.0, &index);               // 4 bytes - mc_interface_read_reset_avg_input_current()
            index += 4; // Skip 4 bytes - mc_interface_read_reset_avg_id()
            index += 4; // Skip 4 bytes - mc_interface_read_reset_avg_iq()
            telem_data.dutyCycleNow      = buffer_get_float16(payload, 1000.0, &index);              // 2 bytes - mc_interface_get_duty_cycle_now()
            telem_data.rpm               = buffer_get_float32(payload, 1.0, &index);                 // 4 bytes - mc_interface_get_rpm()
            telem_data.inpVoltage        = buffer_get_float16(payload, 10.0, &index);                // 2 bytes - GET_INPUT_VOLTAGE()
            telem_data.ampHours          = buffer_get_float32(payload, 10000.0, &index);             // 4 bytes - mc_interface_get_amp_hours(false)
            telem_data.ampHoursCharged   = buffer_get_float32(payload, 10000.0, &index);             // 4 bytes - mc_interface_get_amp_hours_charged(false)
            telem_data.wattHours         = buffer_get_float32(payload, 10000.0, &index);             // 4 bytes - mc_interface_get_watt_hours(false)
            telem_data.wattHoursCharged  = buffer_get_float32(payload, 10000.0, &index);             // 4 bytes - mc_interface_get_watt_hours_charged(false)
            telem_data.tachometer        = buffer_get_int32(payload, &index);                        // 4 bytes - mc_interface_get_tachometer_value(false)
            telem_data.tachometerAbs     = buffer_get_int32(payload, &index);                        // 4 bytes - mc_interface_get_tachometer_abs_value(false)
            telem_data.error             = payload[index++];                          // 1 byte  - mc_interface_get_fault()
            telem_data.pidPos            = buffer_get_float32(payload, 1000000.0, &index);           // 4 bytes - mc_interface_get_pid_pos_now()
            telem_data.id                = payload[index++];                                         // 1 byte  - app_get_configuration()->controller_id
            vesc_sendTelemetry(&telem_data);
            return true;

        break;
        default:
            return false;
        break;
    }
    #else
    return false;
    #endif
}

void SerialVESC::resetReceiveState()
{
    receiveState = RECEIVE_WAIT_START;
    payloadLength = 0;
    payloadBytesReceived = 0;
    payloadBytesCached = 0;
    calculatedCrc = 0;
    receivedCrc = 0;
}

bool SerialVESC::handleTcpBridge(uint8_t *bytes, uint16_t size)
{
    #ifdef ENABLE_VESC_TCP_BRIDGE
    if ((telem_cfg & VESC_CFG_TCP_BRIDGE) == 0U) {
        return false;
    }

    if (this->is_main_vesc && !wclient.connected())
    {
        if (wserver == NULL) {
            // server not started
            if (WiFi.getMode() == WIFI_OFF) {
                // can't start server without Wi-Fi
                return false;
            }
            // start the TCP server
            wserver = new WiFiServer(TCP_VESC_DEFAULT_PORT);
            wserver->begin();
            vesc_tcp_bridge_started = true;
        }
        // check for new tcp client
        wclient = wserver->available();
        if (wclient) {
            // new client connected
            wclient.setNoDelay(true);
            wclient.setTimeout(TCP_SEND_TIMEOUT);
        }
        else {
            return false;
        }
    }

    if (wclient.connected())
    {
        bool ret = false;
        if (size > 0) {
            // there is something to send out to TCP, so send it
            wclient.write(bytes, size);
            ret |= true;
        }
        uint8_t tcpBuffer[TCP_BRIDGE_BUFFER_SIZE];
        while (wclient.connected()) {
            // see if there's anything from the TCP client to send out the serial port
            int available = wclient.available();
            if (available <= 0) {
                break;
            }

            // send out a chunk
            size_t readSize = min((size_t)available, sizeof(tcpBuffer));
            int bytesRead = wclient.read(tcpBuffer, readSize);
            if (bytesRead <= 0) {
                break;
            }
            _outputPort->write(tcpBuffer, bytesRead);
        }
        return ret;
    }
    else {
        return false;
    }
    #else
    return false;
    #endif
}

static void vesc_sendTelemetry(vesc_telem_t* data)
{
    CRSF_MK_FRAME_T(crsf_sensor_battery_t) crsfbatt = { 0 };
    CRSF_MK_FRAME_T(crsf_sensor_rpm_t    ) crsfrpm  = { 0 };
    uint16_t voltage = 0;
    uint16_t current = 0;
    int32_t rpmValue = 0;
    uint32_t rpm = 0;

    if(customcodehooks_onvesctelemetry((void*)data))
    {
        return;
    }

    if (data->inpVoltage > 0.0f) {
        float scaledVoltage = data->inpVoltage * 10.0f;
        voltage = (scaledVoltage >= 65535.0f) ? 0xFFFFU : (uint16_t)scaledVoltage;
    }

    if ((telem_cfg & VESC_TELEM_CFG_POWER) != 0U && voltage != 0U) {
        crsfBatterySensorDetected = true;
    }

    if (data->avgInputCurrent > 0.0f) {
        float scaledCurrent = data->avgInputCurrent * 10.0f;
        current = (scaledCurrent >= 65535.0f) ? 0xFFFFU : (uint16_t)scaledCurrent;
    }

    if ((telem_cfg & VESC_TELEM_CFG_POWER) != 0U) {
        crsfbatt.p.voltage = htobe16(voltage);
        crsfbatt.p.current = htobe16(current);
        crsfRouter.SetHeaderAndCrc((crsf_header_t *)&crsfbatt, CRSF_FRAMETYPE_BATTERY_SENSOR, CRSF_FRAME_SIZE(sizeof(crsf_sensor_battery_t)));
        crsfRouter.deliverMessageTo(CRSF_ADDRESS_RADIO_TRANSMITTER, &crsfbatt.h);
    }

    // CRSF RPM stores a signed 24-bit value, so clamp to the two's-complement range [-2^23, 2^23 - 1].
    constexpr int32_t CRSF_RPM_MIN = -(1 << 23);
    constexpr int32_t CRSF_RPM_MAX = (1 << 23) - 1;
    if (data->rpm >= (float)CRSF_RPM_MAX) {
        rpmValue = CRSF_RPM_MAX;
    } else if (data->rpm <= (float)CRSF_RPM_MIN) {
        rpmValue = CRSF_RPM_MIN;
    } else {
        rpmValue = (int32_t)data->rpm;
    }

    rpm = htobe24((uint32_t)rpmValue & 0xFFFFFFU);

    if ((telem_cfg & VESC_TELEM_CFG_RPM) != 0U) {
        constexpr uint8_t CRSF_RPM_PAYLOAD_SIZE = 1 + 3;
        crsfrpm.p.source_id = 1; // mimic a EAM device
        crsfrpm.p.rpm0 = rpm;
        crsfRouter.SetHeaderAndCrc((crsf_header_t *)&crsfrpm, CRSF_FRAMETYPE_RPM, CRSF_FRAME_SIZE(CRSF_RPM_PAYLOAD_SIZE));
        crsfRouter.deliverMessageTo(CRSF_ADDRESS_RADIO_TRANSMITTER, &crsfrpm.h);
    }

    if ((telem_cfg & VESC_TELEM_CFG_TEMPERATURE) != 0U) {
        constexpr uint8_t CRSF_TEMP_PAYLOAD_SIZE_ONE_SENSOR = 1 + 2;
        constexpr uint8_t CRSF_TEMP_PAYLOAD_SIZE_TWO_SENSORS = 1 + 4;
        CRSF_MK_FRAME_T(crsf_sensor_temp_t) crsftemp = { 0 };
        crsftemp.p.source_id = 1; // mimic a EAM device
        crsftemp.p.temperature[0] = htobe16((int16_t)(data->tempMosfet * 10.0f));

        uint8_t payloadSize = CRSF_TEMP_PAYLOAD_SIZE_ONE_SENSOR;
        if (isfinite(data->tempMotor)) {
            crsftemp.p.temperature[1] = htobe16((int16_t)(data->tempMotor * 10.0f));
            payloadSize = CRSF_TEMP_PAYLOAD_SIZE_TWO_SENSORS;
        }

        crsfRouter.SetHeaderAndCrc((crsf_header_t *)&crsftemp, CRSF_FRAMETYPE_TEMP, CRSF_FRAME_SIZE(payloadSize));
        crsfRouter.deliverMessageTo(CRSF_ADDRESS_RADIO_TRANSMITTER, &crsftemp.h);
    }
}

static void vesc_stopTcpBridge()
{
    #if defined(ENABLE_VESC_TCP_BRIDGE)
    wclient.stop();
    if (wserver != NULL) {
        delete wserver;
        wserver = NULL;
    }
    #endif
    vesc_tcp_bridge_started = false;
}

SerialVESC::~SerialVESC()
{
    DBGLN("SerialVESC::destruct");
    if (this->is_main_vesc) {
        vesc_stopTcpBridge();
        main_vesc_instance = NULL;
        first_rx_pin = -1;
    }
    crsfRouter.removeConnector(this);
}

static int32_t i32map(int32_t x, int32_t in_min, int32_t in_max, int32_t out_min, int32_t out_max)
{
    int32_t in_range = in_max - in_min;

    int32_t out_range = out_max - out_min;
    int32_t dx = x - in_min;

    // Split to avoid overflow: dx = q * in_range + r
    int32_t q = dx / in_range;
    int32_t r = dx % in_range;

    // Combine safely
    return out_min + q * out_range + (r * out_range) / in_range;
}

static int32_t decode_ufloat16(uint16_t v)
{
    const uint32_t exponent = (v >> 11) & 0x1F;
    const uint32_t mantissa = v & 0x7FF;

    if (mantissa == 0) {
        return 0;
    }

    const uint64_t decoded = (uint64_t)mantissa << exponent;
    if (decoded > VESC_RANGE_MAX) {
        return VESC_RANGE_MAX;
    }

    return (int32_t)decoded;
}

#if 0
static float xy_magnitude(int16_t x, int16_t y)
{
    return sqrtf((float)x * (float)x + (float)y * (float)y);
}
#else
static int16_t xy_magnitude(int16_t x, int16_t y)
{
    // we don't actually care about actual magnitude so don't waste float calculations
    return abs(x) + abs(y);
}
#endif

static int32_t xy_to_vesc_pos_offset(int16_t x, int16_t y, bool invert)
{
    #ifdef BUILD_VESC_UART

    if (x == 0 && y == 0) {
        return 0;
    }

    double angle_rad = atan2((double)y, (double)x);
    double angle_deg = angle_rad * (180.0 / M_PI);

    if (angle_deg < 0.0) {
        angle_deg += 360.0;
    }

    int32_t pos = (int32_t)(angle_deg * 1000000.0 + 0.5);

    if (invert) {
        pos = 360000000 - pos;
        if (pos == 360000000) {
            pos = 0;
        }
    }

    while (pos < 0) {
        pos += 360000000;
    }
    while (pos >= 360000000) {
        pos -= 360000000;
    }

    return pos;
    #else
    return 0;
    #endif
}
