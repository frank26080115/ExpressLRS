#include "OTA_Legacy.h"
#include "OTA.h"
#include "common.h"
#include "logging.h"
#include "options.h"
#include "config.h"
#include "CRSFRouter.h"
#include "stubborn_sender.h"
#include "stubborn_receiver.h"
#include "deferred.h"
#include "FHSS.h"
#include "hwTimer.h"
#include "handset.h"
#include "LBT.h"
#include <stdlib.h>
#ifdef TARGET_TX
#include "LQCALC.h"
#include "TXModuleEndpoint.h"
#endif
#if defined(PLATFORM_ESP32)
#include "esp_task_wdt.h"
#endif

bool ota_isLegacy = false; // this is a globally accessible indicator that we are in legacy mode
uint32_t ota_legacySyncHoldUntilMs = 0; // used to prevent hopping and rate changes to increase chances of hearing a sync packet

extern uint8_t ExpressLRS_nextAirRateIndex; // used to call SetRFLinkRate
extern uint32_t RFmodeLastCycled; // reset the scan timer
extern bool OtaIsFullRes;
extern StubbornSender DataDlSender;
extern uint8_t NextTelemetryType;
extern uint8_t telemetryBurstCount;
extern uint8_t telemetryBurstMax;
extern uint8_t geminiMode;
extern bool alreadyTLMresp;
extern StubbornReceiver DataUlReceiver;
extern uint32_t rfModeLastChangedMS;

// used to determine if we are getting legacy packets or new packets
static uint32_t consecutive_old_pkt_cnt = 0;
static uint32_t consecutive_new_pkt_cnt = 0;

extern Crc2Byte ota_crc;
uint16_t OtaCrcInitializer_v3;

bool ICACHE_RAM_ATTR ValidatePacketCrcFull_v3(OTA_Packet_v3_s * otaPktPtr);
bool ICACHE_RAM_ATTR ValidatePacketCrcStd_v3(OTA_Packet_v3_s * otaPktPtr);
void ICACHE_RAM_ATTR GeneratePacketCrcFull_v3(OTA_Packet_v3_s * const otaPktPtr);
void ICACHE_RAM_ATTR GeneratePacketCrcStd_v3(OTA_Packet_v3_s * const otaPktPtr);

extern bool ICACHE_RAM_ATTR ProcessRFPacket(SX12xxDriverCommon::rx_status const status, bool skip_crc);
extern void SetRFLinkRate(uint8_t index, bool bindMode);
extern void FHSSrandomiseFHSSsequence(const uint32_t seed);
extern void FHSSrandomiseFHSSsequenceBuild(const uint32_t seed, uint32_t freqCount, uint_fast8_t syncChannel, uint8_t *inSequence);
extern void ICACHE_RAM_ATTR GeneratePacketCrcFull(OTA_Packet_s * const otaPktPtr);
extern void ICACHE_RAM_ATTR GeneratePacketCrcStd(OTA_Packet_s * const otaPktPtr);
extern void ICACHE_RAM_ATTR LinkStatsToOta(OTA_LinkStats_s * const ls);

static uint8_t rateIdxXform(uint8_t x);
static bool mapSyncPacketV3ToV4(OTA_Packet_v3_s * const otaPktPtr);
static void rewriteDataUplinkHeaderV3ToV4(OTA_Packet_v3_s * const otaPktPtr);
#if defined(TARGET_RX)
static void LinkStatsToOta_v3(OTA_LinkStats_v3_s * const ls);
#endif

static constexpr uint32_t LEGACY_SYNC_HOLD_TIMEOUT_MS = 2000U;
static constexpr uint8_t PACKET_TYPE_TLM_V3 = 0x03;
static constexpr uint8_t ELRS_TELEMETRY_TYPE_LINK_V3 = 0x01;
static constexpr uint8_t ELRS_TELEMETRY_TYPE_DATA_V3 = 0x02;

bool ota_isLegacySyncHoldActive()
{
    if (ota_legacySyncHoldUntilMs == 0)
    {
        return false;
    }

    if ((millis() - ota_legacySyncHoldUntilMs) < LEGACY_SYNC_HOLD_TIMEOUT_MS)
    {
        return true;
    }

    ota_legacySyncHoldUntilMs = 0;
    return false;
}

bool ICACHE_RAM_ATTR ProcessRFPacket_v3(SX12xxDriverCommon::rx_status const status)
{
    if (status != SX12xxDriverCommon::SX12XX_RX_OK)
    {
        return false;
    }

    OTA_Packet_v3_s * const otaPktPtr = (OTA_Packet_v3_s * const)Radio.RXdataBuffer;

    // these CRC validation functions will also
    if (OtaIsFullRes) {
        if (!ValidatePacketCrcFull_v3(otaPktPtr)) {
            DBGLN("CRC err v3 full");
            return false;
        }
    }
    else {
        if (!ValidatePacketCrcStd_v3(otaPktPtr)) {
            DBGLN("CRC err v3 std");
            return false;
        }
    }

    //DBGLN("CRC v3 good");

    #if defined(PLATFORM_ESP32)
    // this code actually sometimes crashes due to WDT if debug is enabled
    esp_task_wdt_reset();
    #endif

    consecutive_old_pkt_cnt++;
    consecutive_new_pkt_cnt = 0;
    if (consecutive_old_pkt_cnt >= 3U) { // got enough evidence to conclude we are hearing a V3 transmitter
        if (ota_isLegacy == false) {
            // time to switch over
            DBGLN("many legacy packets detected");
            ota_isLegacy = true;
            // reset the search scan timer
            RFmodeLastCycled = millis();
            // regenerate the hop table so we know what the actual sync channel is
            FHSSrandomiseFHSSsequence_v3(uidMacSeedGet_v3());
            // reinitialize the radio with the new config
            SetRFLinkRate(ExpressLRS_nextAirRateIndex, false);
            // Enter a temporary hold state: stay on sync channel to lock quickly before hopping starts.
            ota_legacySyncHoldUntilMs = millis();
            Radio.SetFrequencyReg(FHSSgetInitialFreq(), SX12XX_Radio_1, false);
#if defined(RADIO_LR1121)
            if (FHSSuseDualBand)
            {
                Radio.SetFrequencyReg(FHSSgetInitialGeminiFreq(), SX12XX_Radio_2, false);
            }
#endif
            Radio.RXnb();
            // now we should be listening for sync on the correct sync channel since the hop table has been changed
            return false;
        }
    }

    if (otaPktPtr->std.type == PACKET_TYPE_SYNC) {
        // Translate the sync packet explicitly so field packing changes between V3/V4
        // cannot corrupt switch mode / tlm ratio information.
        if (!mapSyncPacketV3ToV4(otaPktPtr))
        {
            return false;
        }

        // Sync found, leave the temporary hold state and allow normal hopping/scanning behaviour.
        if (ota_legacySyncHoldUntilMs != 0)
        {
            DBGLN("legacy sync acquired, resume normal hopping");
            ota_legacySyncHoldUntilMs = 0;
        }
    }
    else if (otaPktPtr->std.type == PACKET_TYPE_DATA)
    {
        // PACKET_TYPE_DATA on V3 is MSP uplink. Standard-mode bit layout is compatible,
        // but full-resolution bitfield order is different and must be translated.
        rewriteDataUplinkHeaderV3ToV4(otaPktPtr);
    }

    #if defined(PLATFORM_ESP32)
    // this code actually sometimes crashes due to WDT if debug is enabled
    esp_task_wdt_reset();
    #endif

    #if 0
    // this will correct the CRC with the latest version salt
    // this is inefficient, it's better to just skip CRC check in the later processing
    if (OtaIsFullRes) {
        GeneratePacketCrcFull((OTA_Packet_s*)otaPktPtr);
    }
    else {
        GeneratePacketCrcStd((OTA_Packet_s*)otaPktPtr);
    }
    #endif

    bool ret = ProcessRFPacket(status, true); // use original packet processing, we've already checked the CRC so skip checking CRC again
    return ret;
}

bool ICACHE_RAM_ATTR HandleSendTelemetryResponse_v3()
{
    #ifdef TARGET_RX
    // this function tries to mimic the old telemetry sending function, and in v4 it is replaced with HandleSendDataDl
    // this functionality doesn't work yet, TODO: fix me

    uint8_t modresult = OtaNonce % ExpressLRS_currTlmDenom;

    if ((connectionState == disconnected) || (ExpressLRS_currTlmDenom == 1) || (alreadyTLMresp == true) || (modresult != 0) || !teamraceHasModelMatch)
    {
        return false; // don't bother sending tlm if disconnected or TLM is off
    }

    // ESP requires word aligned buffer
    WORD_ALIGNED_ATTR OTA_Packet_v3_s otaPkt = {0};
    alreadyTLMresp = true;
    otaPkt.std.type = 0x03; // old PACKET_TYPE_TLM definition

    bool tlmQueued = false;
    //if (firmwareOptions.is_airport)
    //{
    //    tlmQueued = ((SerialAirPort *)serialIO)->isTlmQueued();
    //}
    //else
    {
        tlmQueued = DataDlSender.IsActive();
    }

    if (NextTelemetryType == PACKET_TYPE_LINKSTATS || !tlmQueued)
    {
        OTA_LinkStats_v3_s * ls;
        if (OtaIsFullRes)
        {
            otaPkt.full.tlm_dl.containsLinkStats = 1;
            ls = &otaPkt.full.tlm_dl.ul_link_stats.stats;
            // Include some advanced telemetry in the extra space
            // Note the use of `ul_link_stats.payload` vs just `payload`
            otaPkt.full.tlm_dl.packageIndex = DataDlSender.GetCurrentPayload(
                otaPkt.full.tlm_dl.ul_link_stats.payload,
                sizeof(otaPkt.full.tlm_dl.ul_link_stats.payload));
        }
        else
        {
            otaPkt.std.tlm_dl.type = 0x01; // ELRS_TELEMETRY_TYPE_LINK
            ls = &otaPkt.std.tlm_dl.ul_link_stats.stats;
        }
        LinkStatsToOta_v3(ls);

        NextTelemetryType = PACKET_TYPE_DATA; // ELRS_TELEMETRY_TYPE_DATA
        // Start the count at 1 because the next will be DATA and doing +1 before checking
        // against Max below is for some reason 10 bytes more code
        telemetryBurstCount = 1;
    }
    else
    {
        if (telemetryBurstCount < telemetryBurstMax)
        {
            telemetryBurstCount++;
        }
        else
        {
            NextTelemetryType = PACKET_TYPE_LINKSTATS; // ELRS_TELEMETRY_TYPE_LINK;
        }

        if (DataDlSender.IsActive())
        {
            if (OtaIsFullRes)
            {
                otaPkt.full.tlm_dl.packageIndex = DataDlSender.GetCurrentPayload(
                    otaPkt.full.tlm_dl.payload,
                    sizeof(otaPkt.full.tlm_dl.payload));
            }
            else
            {
                otaPkt.std.tlm_dl.type = 0x02; // ELRS_TELEMETRY_TYPE_DATA
                otaPkt.std.tlm_dl.packageIndex = DataDlSender.GetCurrentPayload(
                    otaPkt.std.tlm_dl.payload,
                    sizeof(otaPkt.std.tlm_dl.payload));
            }
        }
    }

    if (OtaIsFullRes)
    {
        GeneratePacketCrcFull_v3(&otaPkt);
    }
    else
    {
        GeneratePacketCrcStd_v3(&otaPkt);
    }

    SX12XX_Radio_Number_t transmittingRadio;
    if (config.GetForceTlmOff())
    {
        transmittingRadio = SX12XX_Radio_NONE;
    }
    else
    {
        transmittingRadio = LbtChannelIsClear(SX12XX_Radio_All);   // weed out the radio(s) if channel in use
        if (isDualRadio() && !geminiMode && transmittingRadio == SX12XX_Radio_All) // If the receiver is in diversity mode, only send TLM on a single radio.
        {
            transmittingRadio = Radio.GetStrongestReceivingRadio(); // Pick the radio with best rf connection to the tx.
        }
    }

    Radio.TXnb((uint8_t*)&otaPkt, false, nullptr, transmittingRadio);

    if (transmittingRadio == SX12XX_Radio_NONE)
    {
        // No packet will be sent due to LBT / Telem forced off.
        // Defer TXdoneCallback() to prepare for TLM when the IRQ is normally triggered.
        deferExecutionMicros(ExpressLRS_currAirRate_RFperfParams->TOA, Radio.TXdoneCallback);
    }

    #endif

    return true;
}

void OtaUpdateCrcInitFromUid_v3()
{
    OtaCrcInitializer_v3 = (UID[4] << 8) | UID[5];
    OtaCrcInitializer_v3 ^= 3;
    DBGV("OTAv3 %u %u %u", UID[0], UID[1], UID[2]);
    DBGVLN(" %u %u %u %u", UID[3], UID[4], UID[5], OtaCrcInitializer_v3);
}

bool ICACHE_RAM_ATTR ValidatePacketCrcFull_v3(OTA_Packet_v3_s * otaPktPtr)
{
    uint16_t const calculatedCRC = ota_crc.calc((uint8_t*)otaPktPtr, OTA8_CRC_CALC_LEN_v3, OtaCrcInitializer_v3);
    if (otaPktPtr->full.crc == calculatedCRC) {
        return true; 
    }
    return false;
}

bool ICACHE_RAM_ATTR ValidatePacketCrcStd_v3(OTA_Packet_v3_s * otaPktPtr)
{
    uint8_t preserveCrcHigh = otaPktPtr->std.crcHigh;
    uint16_t const inCRC = ((uint16_t)otaPktPtr->std.crcHigh << 8) + otaPktPtr->std.crcLow;
    bool crcValid = false;

    // For smHybrid the CRC only has packet type in byte 0.
    // For smWide standard RC packets, the FHSS slot is mixed into byte 0 before CRC.
    // During initial acquisition (before sync), OtaNonce is unknown and using only one
    // slot value causes many false negatives. To improve reliability, try all possible
    // slot values for this mode.
#if defined(TARGET_RX)
    if (otaPktPtr->std.type == PACKET_TYPE_RCDATA && OtaSwitchModeCurrent == smWideOr8ch)
    {
        uint8_t const fhssHopInterval = ExpressLRS_currAirRate_Modparams->FHSShopInterval;
        if (fhssHopInterval > 0)
        {
            // First try the expected value from current OtaNonce (fast path when already in sync).
            otaPktPtr->std.crcHigh = (OtaNonce % fhssHopInterval) + 1;
            uint16_t const calculatedCRC = ota_crc.calc((uint8_t*)otaPktPtr, OTA4_CRC_CALC_LEN_v3, OtaCrcInitializer_v3);
            crcValid = (inCRC == calculatedCRC);

            // If not matched, brute-force the slot contribution 1..FHSShopInterval.
            if (!crcValid)
            {
                for (uint8_t slot = 1; slot <= fhssHopInterval; ++slot)
                {
                    otaPktPtr->std.crcHigh = slot;
                    uint16_t const slotCrc = ota_crc.calc((uint8_t*)otaPktPtr, OTA4_CRC_CALC_LEN_v3, OtaCrcInitializer_v3);
                    if (inCRC == slotCrc)
                    {
                        crcValid = true;
                        DBGVLN("legacy std CRC matched via slot search: slot=%u", slot);
                        break;
                    }
                }
            }
        }
    }
    else
#endif
    {
        otaPktPtr->std.crcHigh = 0;
        uint16_t const calculatedCRC = ota_crc.calc((uint8_t*)otaPktPtr, OTA4_CRC_CALC_LEN_v3, OtaCrcInitializer_v3);
        crcValid = (inCRC == calculatedCRC);
    }

    otaPktPtr->std.crcHigh = preserveCrcHigh;
    return crcValid;
}

void ICACHE_RAM_ATTR GeneratePacketCrcFull_v3(OTA_Packet_v3_s * const otaPktPtr)
{
    otaPktPtr->full.crc = ota_crc.calc((uint8_t*)otaPktPtr, OTA8_CRC_CALC_LEN_v3, OtaCrcInitializer_v3);
}

void ICACHE_RAM_ATTR GeneratePacketCrcStd_v3(OTA_Packet_v3_s * const otaPktPtr)
{
    uint16_t crc = ota_crc.calc((uint8_t*)otaPktPtr, OTA4_CRC_CALC_LEN_v3, OtaCrcInitializer_v3);
    otaPktPtr->std.crcHigh = (crc >> 8);
    otaPktPtr->std.crcLow  = crc;
}

#if defined(RADIO_LR1121) || defined(RADIO_SX128X)
static expresslrs_RFrates_e ICACHE_RAM_ATTR rateEnumXform_2G4(uint8_t x)
{
    switch(x)
    {
        case RATE_v3_LORA_25HZ     : return RATE_LORA_2G4_25HZ;
        case RATE_v3_LORA_50HZ     : return RATE_LORA_2G4_50HZ;
        case RATE_v3_LORA_100HZ    : return RATE_LORA_2G4_100HZ;
        case RATE_v3_LORA_100HZ_8CH: return RATE_LORA_2G4_100HZ_8CH;
        case RATE_v3_LORA_150HZ    : return RATE_LORA_2G4_150HZ;
        case RATE_v3_LORA_200HZ    : return RATE_LORA_2G4_200HZ;
        case RATE_v3_LORA_250HZ    : return RATE_LORA_2G4_250HZ;
        case RATE_v3_LORA_333HZ_8CH: return RATE_LORA_2G4_333HZ_8CH;
        case RATE_v3_LORA_500HZ    : return RATE_LORA_2G4_500HZ;
        case RATE_v3_DVDA_250HZ    : return RATE_FLRC_2G4_250HZ_DVDA;
        case RATE_v3_DVDA_500HZ    : return RATE_FLRC_2G4_500HZ_DVDA;
        case RATE_v3_FLRC_500HZ    : return RATE_FLRC_2G4_500HZ;
        case RATE_v3_FLRC_1000HZ   : return RATE_FLRC_2G4_1000HZ;
        case RATE_v3_LORA_200HZ_8CH: return RATE_LORA_2G4_200HZ_8CH;
        case RATE_v3_FSK_2G4_DVDA_500HZ: return RATE_FSK_2G4_500HZ_DVDA;
        case RATE_v3_FSK_2G4_1000HZ: return RATE_FSK_2G4_1000HZ;
        default: return (expresslrs_RFrates_e)0xFF;
    }
}
#endif

#if defined(RADIO_SX127X) || defined(RADIO_LR1121)
static expresslrs_RFrates_e ICACHE_RAM_ATTR rateEnumXform_900(uint8_t x)
{
    switch(x)
    {
        case RATE_v3_LORA_25HZ     : return RATE_LORA_900_25HZ;
        case RATE_v3_LORA_50HZ     : return RATE_LORA_900_50HZ;
        case RATE_v3_LORA_100HZ    : return RATE_LORA_900_100HZ;
        case RATE_v3_LORA_100HZ_8CH: return RATE_LORA_900_100HZ_8CH;
        case RATE_v3_LORA_150HZ    : return RATE_LORA_900_150HZ;
        case RATE_v3_LORA_200HZ    : return RATE_LORA_900_200HZ;
        case RATE_v3_LORA_250HZ    : return RATE_LORA_900_250HZ;
        case RATE_v3_LORA_333HZ_8CH: return RATE_LORA_900_333HZ_8CH;
        case RATE_v3_LORA_500HZ    : return RATE_LORA_900_500HZ;
        case RATE_v3_DVDA_50HZ     : return RATE_LORA_900_50HZ_DVDA;
        case RATE_v3_LORA_200HZ_8CH: return RATE_LORA_900_200HZ_8CH;
        case RATE_v3_FSK_900_1000HZ_8CH: return RATE_FSK_900_1000HZ_8CH;
        default: return (expresslrs_RFrates_e)0xFF;
    }
}
#endif

#if defined(RADIO_SX127X)
const static uint8_t rateXformTbl[RATE_MAX] = {
    RATE_v3_LORA_200HZ,
    RATE_v3_LORA_100HZ_8CH,
    RATE_v3_LORA_100HZ,
    RATE_v3_LORA_50HZ,
    RATE_v3_LORA_25HZ,
    RATE_v3_DVDA_50HZ,
};

#endif

#if defined(RADIO_LR1121)
const static uint8_t rateXformTbl[] = {
    RATE_v3_LORA_200HZ,
    RATE_v3_LORA_100HZ_8CH,
    RATE_v3_LORA_100HZ,
    RATE_v3_LORA_50HZ,
    RATE_v3_LORA_500HZ,
    RATE_v3_LORA_333HZ_8CH,
    RATE_v3_LORA_250HZ,
    RATE_v3_LORA_150HZ,
    RATE_v3_LORA_100HZ_8CH,
    RATE_v3_LORA_50HZ,
    RATE_v3_LORA_150HZ,
    RATE_v3_LORA_100HZ_8CH,
    RATE_v3_LORA_250HZ,
    RATE_v3_LORA_200HZ_8CH,
    RATE_v3_FSK_2G4_DVDA_500HZ,
    RATE_v3_FSK_900_1000HZ_8CH,
};
#endif

#if defined(RADIO_SX128X)
const uint8_t rateXformTbl[] = {
    RATE_v3_FLRC_1000HZ,
    RATE_v3_FLRC_500HZ,
    RATE_v3_DVDA_500HZ,
    RATE_v3_DVDA_250HZ,
    RATE_v3_LORA_500HZ,
    RATE_v3_LORA_333HZ_8CH,
    RATE_v3_LORA_250HZ,
    RATE_v3_LORA_150HZ,
    RATE_v3_LORA_100HZ_8CH,
    RATE_v3_LORA_50HZ,
};
#endif

static uint8_t ICACHE_RAM_ATTR rateIdxXform(uint8_t x)
{
    // Legacy rateIndex is translated through a target-specific lookup table first,
    // then converted to the active V4 enum.
    if (x >= (sizeof(rateXformTbl) / sizeof(rateXformTbl[0])))
    {
        DBGLN("legacy sync rate index out of bounds: %u", x);
        return 0xFF;
    }

    uint8_t const rateV3 = rateXformTbl[x];
    #if defined(RADIO_SX127X)
    return rateEnumXform_900(rateV3);
    #elif defined(RADIO_LR1121)
    uint8_t ret = rateEnumXform_2G4(rateV3);
    if (ret == 0xFF) {
        ret = rateEnumXform_900(rateV3);
    }
    return ret;
    #elif defined(RADIO_SX128X)
    return rateEnumXform_2G4(rateV3);
    #endif
}

static bool ICACHE_RAM_ATTR mapSyncPacketV3ToV4(OTA_Packet_v3_s * const otaPktPtr)
{
    OTA_Sync_v3_s const * const syncV3 = OtaIsFullRes ? &otaPktPtr->full.sync.sync : &otaPktPtr->std.sync;
    OTA_Sync_s syncV4 = {};
    uint8_t const transformedRateEnum = rateIdxXform(syncV3->rateIndex);
    if (transformedRateEnum == 0xFF)
    {
        DBGLN("legacy sync rate unsupported: %u", syncV3->rateIndex);
        return false;
    }
    DBGLN("legacy sync rate map v3=%u v4=%u tlm=%u sw=%u", syncV3->rateIndex, transformedRateEnum, syncV3->newTlmRatio, syncV3->switchEncMode);

    syncV4.fhssIndex = syncV3->fhssIndex;
    syncV4.nonce = syncV3->nonce;
    syncV4.rfRateEnum = transformedRateEnum;
    syncV4.switchEncMode = syncV3->switchEncMode;
    syncV4.newTlmRatio = syncV3->newTlmRatio;
    syncV4.geminiMode = 0; // V3 packet has no Gemini flag.
    syncV4.otaProtocol = 0; // Keep serial mode controlled by receiver configuration.
    syncV4.free = 0;
    syncV4.UID4 = syncV3->UID4;
    syncV4.UID5 = syncV3->UID5;

    // Write the converted sync structure into the same RX buffer, but through the V4 packet view.
    // ProcessRFPacket() is called later and interprets this memory as OTA_Packet_s.
    OTA_Packet_s * const otaPktV4Ptr = (OTA_Packet_s * const)otaPktPtr;
    if (OtaIsFullRes)
    {
        otaPktV4Ptr->full.sync.sync = syncV4;
    }
    else
    {
        otaPktV4Ptr->std.sync = syncV4;
    }
    return true;
}

static void ICACHE_RAM_ATTR rewriteDataUplinkHeaderV3ToV4(OTA_Packet_v3_s * const otaPktPtr)
{
    if (!OtaIsFullRes)
    {
        // Standard packet mode uses compatible bit placement:
        // V3 msp_ul { packageIndex:7, tlmFlag:1 } and V4 data_ul { packageIndex:7, stubbornAck:1 }.
        return;
    }

    // Full-res V3 uplink data layout:
    //   packetType:2, packageIndex:5, tlmFlag:1
    // Full-res V4 uplink data layout:
    //   packetType:2, stubbornAck:1, packageIndex:5
    // Repack byte 0 so ProcessRFPacket() sees correct V4 field positions.
    uint8_t const packetType = otaPktPtr->full.msp_ul.packetType;
    uint8_t const packageIndex = otaPktPtr->full.msp_ul.packageIndex;
    uint8_t const stubbornAck = otaPktPtr->full.msp_ul.tlmFlag;

    // Re-interpret as V4 packet view and rewrite only the first byte fields needed by V4 parser.
    OTA_Packet_s * const otaPktV4Ptr = (OTA_Packet_s * const)otaPktPtr;
    otaPktV4Ptr->full.data_ul.packetType = packetType;
    otaPktV4Ptr->full.data_ul.stubbornAck = stubbornAck;
    otaPktV4Ptr->full.data_ul.packageIndex = packageIndex;

    DBGVLN("legacy full uplink hdr remap pidx=%u ack=%u", packageIndex, stubbornAck);
}

#if defined(TARGET_RX)
static void ICACHE_RAM_ATTR LinkStatsToOta_v3(OTA_LinkStats_v3_s * const ls)
{
    ls->uplink_RSSI_1 = linkStats.uplink_RSSI_1;
    ls->uplink_RSSI_2 = linkStats.uplink_RSSI_2;
    ls->antenna = linkStats.active_antenna;
    ls->modelMatch = connectionHasModelMatch;
    ls->lq = linkStats.uplink_Link_quality;
    ls->mspConfirm = DataUlReceiver.GetCurrentConfirm() ? 1 : 0;
    ls->SNR = linkStats.uplink_SNR;
}
#endif

void ota_cntNewVersionPkts()
{
    consecutive_new_pkt_cnt++;
    consecutive_old_pkt_cnt = 0;
    if (consecutive_new_pkt_cnt >= 5) { // enough packets with correct CRC
        if (ota_isLegacy != false) {
            // switch over to "normal" mode
            ota_isLegacy = false;
            // reset the search scan timer
            RFmodeLastCycled = millis();
            // regenerate the hop table with the correct seed
            FHSSrandomiseFHSSsequence(OtaGetUidSeed());
            // reinitialize all radio parameters
            SetRFLinkRate(ExpressLRS_nextAirRateIndex, false);
            Radio.RXnb();
            // now when we hop we will be at the sync channel and actually get a sync packet
            DBGLN("ret to normal pkts");
        }
    }
}

void ota_resetPktVersionCounters()
{
    consecutive_old_pkt_cnt = 0;
    consecutive_new_pkt_cnt = 0;
}

uint32_t uidMacSeedGet_v3()
{
    const uint32_t macSeed = ((uint32_t)UID[2] << 24) + ((uint32_t)UID[3] << 16) +
                             ((uint32_t)UID[4] << 8) + (UID[5]^(OTA_VERSION_ID - 1));
    return macSeed;
}

void FHSSrandomiseFHSSsequence_v3(const uint32_t seed)
{
    sync_channel = (FHSSconfig->freq_count / 2) + 1;
    freq_spread = (FHSSconfig->freq_stop - FHSSconfig->freq_start) * FREQ_SPREAD_SCALE / (FHSSconfig->freq_count - 1);
    primaryBandCount = (FHSS_SEQUENCE_LEN / FHSSconfig->freq_count) * FHSSconfig->freq_count;
    FHSSrandomiseFHSSsequenceBuild(seed, FHSSconfig->freq_count, sync_channel, FHSSsequence);

#if defined(RADIO_LR1121)
    sync_channel_DualBand = (FHSSconfigDualBand->freq_count / 2) + 1;
    freq_spread_DualBand = (FHSSconfigDualBand->freq_stop - FHSSconfigDualBand->freq_start) * FREQ_SPREAD_SCALE / (FHSSconfigDualBand->freq_count - 1);
    secondaryBandCount = (FHSS_SEQUENCE_LEN / FHSSconfigDualBand->freq_count) * FHSSconfigDualBand->freq_count;
    FHSSusePrimaryFreqBand = false;
    FHSSrandomiseFHSSsequenceBuild(seed, FHSSconfigDualBand->freq_count, sync_channel_DualBand, FHSSsequence_DualBand);
    FHSSusePrimaryFreqBand = true;
#endif
}

#ifdef TARGET_TX

// Implementing backwards compatibility mode for transmitter, so that a V4 transmitter can optionally send v3 packets (and work with v3 receivers in general)

enum { stbIdle, stbRequested, stbBoosting };

extern bool NextPacketIsDataUl;
extern volatile uint8_t syncSpamCounter;
extern volatile uint8_t syncSpamCounterAfterRateChange;
extern uint32_t SyncPacketLastSent;
extern uint8_t syncTelemBoostState;
extern uint32_t LastTLMpacketRecv_Ms;
extern LQCALC<100> LqTQly;
extern volatile bool busyTransmitting;
extern uint8_t BindingSendCount;
extern StubbornReceiver DataDlReceiver;
extern StubbornSender DataUlSender;

extern expresslrs_tlm_ratio_e ICACHE_RAM_ATTR UpdateTlmRatioEffective();
extern void ICACHE_RAM_ATTR LinkStatsFromOta(OTA_LinkStats_s * const ls);

extern uint32_t ICACHE_RAM_ATTR Decimate11to10_Limit(uint32_t ch11bit);
extern uint32_t ICACHE_RAM_ATTR Decimate11to10_Div2(uint32_t ch11bit);
extern void ICACHE_RAM_ATTR PackUInt11ToChannels4x10(uint32_t const * const src, OTA_Channels_4x10 * const destChannels4x10, Decimate11to10_fn decimate);

static inline uint8_t ICACHE_RAM_ATTR HybridWideNonceToSwitchIndex_v3(uint8_t const nonce)
{
    return ((nonce & 0b111) + ((nonce >> 3) & 0b1)) % 8;
}

#if defined(DEBUG_RCVR_LINKSTATS)
static uint32_t legacyPacketCnt_v3;
#endif
static uint8_t Hybrid8NextSwitchIndex_v3;
static bool FullResIsHighAux_v3;

static uint8_t ICACHE_RAM_ATTR HybridWideSwitchToOta_v3(const uint32_t *channelData, uint8_t const switchIdx, bool const lowRes)
{
    uint16_t ch = channelData[switchIdx + 4];
    uint8_t const binCount = lowRes ? 64 : 128;
    ch = CRSF_to_N(ch, binCount);
    return lowRes ? (ch & 0b111111) : (ch & 0b1111111);
}

static void ICACHE_RAM_ATTR GenerateSyncPacketData_v3(OTA_Sync_v3_s * const syncPtr)
{
    const uint8_t switchEncMode = config.GetSwitchMode();
    const uint8_t index = syncSpamCounter ? config.GetRate() : ExpressLRS_currAirRate_Modparams->index;

    if (syncSpamCounter)
    {
        --syncSpamCounter;
    }

    if (syncSpamCounterAfterRateChange && index == ExpressLRS_currAirRate_Modparams->index)
    {
        --syncSpamCounterAfterRateChange;
        if (connectionState == connected)
        {
            syncSpamCounterAfterRateChange = 0;
        }
    }

    SyncPacketLastSent = millis();

    expresslrs_tlm_ratio_e const newTlmRatio = UpdateTlmRatioEffective();

    syncPtr->fhssIndex = FHSSgetCurrIndex();
    syncPtr->nonce = OtaNonce;
    syncPtr->rateIndex = index;
    syncPtr->newTlmRatio = newTlmRatio - TLM_RATIO_NO_TLM;
    syncPtr->switchEncMode = switchEncMode;
    syncPtr->UID3 = UID[3];
    syncPtr->UID4 = UID[4];
    syncPtr->UID5 = UID[5];

    if (!InBindingMode && config.GetModelMatch())
    {
        syncPtr->UID5 ^= (~crsfTransmitter.modelId) & MODELMATCH_MASK;
    }
}

static OTA_LinkStats_s ICACHE_RAM_ATTR translateLinkStatsV3ToV4(OTA_LinkStats_v3_s const * const lsV3)
{
    OTA_LinkStats_s lsV4 = {};
    lsV4.uplink_RSSI_1 = lsV3->uplink_RSSI_1;
    lsV4.antenna = lsV3->antenna;
    lsV4.uplink_RSSI_2 = lsV3->uplink_RSSI_2;
    lsV4.modelMatch = lsV3->modelMatch;
    lsV4.lq = lsV3->lq;
    lsV4.trueDiversityAvailable = 0;
    lsV4.SNR = lsV3->SNR;
    return lsV4;
}

static void ICACHE_RAM_ATTR PackChannelDataHybridCommon_v3(OTA_Packet4_v3_s * const ota4, const uint32_t *channelData)
{
    ota4->type = PACKET_TYPE_RCDATA;
#if defined(DEBUG_RCVR_LINKSTATS)
    // Incremental packet counter for verification on the RX side, 32 bits shoved into CH1-CH4
    ota4->dbg_linkstats.packetNum = legacyPacketCnt_v3++;
#else
    // CRSF input is 11bit and OTA will carry only 10bit. Discard the Extended Limits (E.Limits)
    // range and use the full 10bits to carry only 998us - 2012us
    PackUInt11ToChannels4x10(&channelData[0], (OTA_Channels_4x10*)&ota4->rc.ch, &Decimate11to10_Limit);
    ota4->rc.ch4 = CRSF_to_BIT(channelData[4]);
#endif /* !DEBUG_RCVR_LINKSTATS */
}

void ICACHE_RAM_ATTR GenerateChannelData8ch12ch_v3(OTA_Packet8_v3_s * const ota8, const uint32_t *channelData, bool const telemetryStatus, bool isHighAux)
{
    // All channel data is 10 bit apart from AUX1 which is 1 bit
    ota8->rc.packetType = PACKET_TYPE_RCDATA;
    ota8->rc.telemetryStatus = telemetryStatus;
    // uplinkPower has 8 items but only 3 bits, but 0 is 0 power which we never use, shift 1-8 -> 0-7
    ota8->rc.uplinkPower = constrain(linkStats.uplink_TX_Power, 1, 8) - 1;
    ota8->rc.isHighAux = isHighAux;
    ota8->rc.ch4 = CRSF_to_BIT(channelData[4]);
#if defined(DEBUG_RCVR_LINKSTATS)
    // Incremental packet counter for verification on the RX side, 32 bits shoved into CH1-CH4
    ota8->dbg_linkstats.packetNum = legacyPacketCnt_v3++;
#else
    // Sources:
    // 8ch always: low=0 high=5
    // 12ch isHighAux=false: low=0 high=5
    // 12ch isHighAux=true:  low=0 high=9
    // 16ch isHighAux=false: low=0 high=4
    // 16ch isHighAux=true:  low=8 high=12
    uint8_t chSrcLow;
    uint8_t chSrcHigh;
    if (OtaSwitchModeCurrent == smHybridOr16ch)
    {
        // 16ch mode
        if (isHighAux)
        {
            chSrcLow = 8;
            chSrcHigh = 12;
        }
        else
        {
            chSrcLow = 0;
            chSrcHigh = 4;
        }
    }
    else
    {
        chSrcLow = 0;
        chSrcHigh = isHighAux ? 9 : 5;
    }
    PackUInt11ToChannels4x10(&channelData[chSrcLow], (OTA_Channels_4x10*)&ota8->rc.chLow, &Decimate11to10_Div2);
    PackUInt11ToChannels4x10(&channelData[chSrcHigh], (OTA_Channels_4x10*)&ota8->rc.chHigh, &Decimate11to10_Div2);
#endif
}

void ICACHE_RAM_ATTR GenerateChannelDataHybridWide_v3(OTA_Packet_v3_s * const otaPktPtr, const uint32_t *channelData, bool const TelemetryStatus, uint8_t const tlmDenom)
{
    OTA_Packet4_v3_s * const ota4 = &otaPktPtr->std;
    PackChannelDataHybridCommon_v3(ota4, channelData);

    uint8_t telemBit = TelemetryStatus << 6;
    uint8_t nextSwitchIndex = HybridWideNonceToSwitchIndex_v3(OtaNonce);
    uint8_t value;
    // Using index 7 means the telemetry bit will always be sent in the packet
    // preceding the RX's telemetry slot for all tlmDenom >= 8
    // For more frequent telemetry rates, include the bit in every
    // packet and degrade the value to 6-bit
    // (technically we could squeeze 7-bits in for 2 channels with tlmDenom=4)
    if (nextSwitchIndex == 7)
    {
        value = telemBit | linkStats.uplink_TX_Power;
    }
    else
    {
        bool telemInEveryPacket = (tlmDenom > 1) && (tlmDenom < 8);
        value = HybridWideSwitchToOta_v3(channelData, nextSwitchIndex + 1, telemInEveryPacket);
        if (telemInEveryPacket)
            value |= telemBit;
    }

    ota4->rc.switches = value;
}

void ICACHE_RAM_ATTR GenerateChannelDataHybrid8_v3(OTA_Packet_v3_s * const otaPktPtr, const uint32_t *channelData, bool const TelemetryStatus, uint8_t const tlmDenom)
{
    (void)tlmDenom;

    OTA_Packet4_v3_s * const ota4 = &otaPktPtr->std;
    PackChannelDataHybridCommon_v3(ota4, channelData);

    // Actually send switchIndex - 1 in the packet, to shift down 1-7 (0b111) to 0-6 (0b110)
    // If the two high bits are 0b11, the receiver knows it is the last switch and can use
    // that bit to store data
    uint8_t bitclearedSwitchIndex = Hybrid8NextSwitchIndex_v3;
    uint8_t value;
    // AUX8 is High Resolution 16-pos (4-bit)
    if (bitclearedSwitchIndex == 6)
        value = CRSF_to_N(channelData[6 + 1 + 4], 16);
    else
        value = CRSF_to_SWITCH3b(channelData[bitclearedSwitchIndex + 1 + 4]);

    ota4->rc.switches =
        TelemetryStatus << 6 |
        // tell the receiver which switch index this is
        bitclearedSwitchIndex << 3 |
        // include the switch value
        value;

    // update the sent value
    Hybrid8NextSwitchIndex_v3 = (bitclearedSwitchIndex + 1) % 7;
}

void ICACHE_RAM_ATTR OtaPackChannelData_v3(OTA_Packet_v3_s * const otaPktPtr, const uint32_t *channelData, bool const TelemetryStatus, uint8_t const tlmDenom)
{
    if (OtaIsFullRes)
    {
        bool isHighAux = false;
        if (OtaSwitchModeCurrent != smWideOr8ch) {
            isHighAux = FullResIsHighAux_v3;
        }
        GenerateChannelData8ch12ch_v3(&otaPktPtr->full, channelData, TelemetryStatus, isHighAux);
        if (OtaSwitchModeCurrent != smWideOr8ch) {
            FullResIsHighAux_v3 = !FullResIsHighAux_v3;
        }
    }
    else
    {
        if (OtaSwitchModeCurrent == smWideOr8ch)
        {
            GenerateChannelDataHybridWide_v3(otaPktPtr, channelData, TelemetryStatus, tlmDenom);
        }
        else
        {
            GenerateChannelDataHybrid8_v3(otaPktPtr, channelData, TelemetryStatus, tlmDenom);
        }
    }
}

void SetRFLinkRate_v3(uint8_t index)
{
  expresslrs_mod_settings_s *const ModParams = get_elrs_airRateConfig(index);
  expresslrs_rf_pref_params_s *const RFperf = get_elrs_RFperfParams(index);
  // Binding always uses invertIQ
  bool invertIQ = InBindingMode || (UID[5] & 0x01);
  OtaSwitchMode_e newSwitchMode = (OtaSwitchMode_e)config.GetSwitchMode();

  bool subGHz = FHSSconfig->freq_center < 1000000000;
#if defined(RADIO_LR1121)
  if (FHSSuseDualBand && subGHz)
  {
      subGHz = FHSSconfigDualBand->freq_center < 1000000000;
  }
#endif

  if ((ModParams == ExpressLRS_currAirRate_Modparams)
    && (RFperf == ExpressLRS_currAirRate_RFperfParams)
    && (subGHz || invertIQ == Radio.IQinverted)
    && (OtaSwitchModeCurrent == newSwitchMode)
    && (!InBindingMode))  // binding mode must always execute code below to set frequency
    return;

  DBGLN("set rate %u", index);
  uint32_t interval = ModParams->interval;
#if defined(DEBUG_FREQ_CORRECTION) && defined(RADIO_SX128X)
  interval = interval * 12 / 10; // increase the packet interval by 20% to allow adding packet header
#endif
  hwTimer::updateInterval(interval);

  FHSSusePrimaryFreqBand = !RadioBandMod::isB2G4(ModParams->radio_type);
  FHSSuseDualBand = RadioBandMod::isBDUAL(ModParams->radio_type);

  Radio.Config(ModParams->bw, ModParams->sf, ModParams->cr, FHSSgetInitialFreq(),
               ModParams->PreambleLen, invertIQ, ModParams->PayloadLength
#if defined(RADIO_SX128X)
               , uidMacSeedGet_v3(), OtaCrcInitializer_v3, ModParams->radio_type
#endif
#if defined(RADIO_LR1121)
               , ModParams->radio_type, (uint8_t)UID[5], (uint8_t)UID[4]
#endif
               );

#if defined(RADIO_LR1121)
  if (FHSSuseDualBand)
  {
    Radio.Config(ModParams->bw2, ModParams->sf2, ModParams->cr2, FHSSgetInitialGeminiFreq(),
                ModParams->PreambleLen2, invertIQ, ModParams->PayloadLength,
                ModParams->radio_type,
                (uint8_t)UID[5], (uint8_t)UID[4], SX12XX_Radio_2);
  }
#endif

  Radio.FuzzySNRThreshold = (RFperf->DynpowerSnrThreshUp == DYNPOWER_SNR_THRESH_NONE) ? 0 : (RFperf->DynpowerSnrThreshUp - RFperf->DynpowerSnrThreshDn);

  if ((isDualRadio() && config.GetAntennaMode() == TX_RADIO_MODE_GEMINI) || FHSSuseDualBand) // Gemini mode
  {
    Radio.SetFrequencyReg(FHSSgetInitialGeminiFreq(), SX12XX_Radio_2, false);
  }

  // InitialFreq has been set, so lets also reset the FHSS Idx and Nonce.
  FHSSsetCurrIndex(0);
  OtaNonce = 0;

  // OtaUpdateSerializers(newSwitchMode, ModParams->PayloadLength); // not called, we pick the serializers via if statements later instead of using pointers
  OtaSwitchModeCurrent = newSwitchMode;
  OtaIsFullRes = (ModParams->PayloadLength == OTA8_PACKET_SIZE);
  if (OtaIsFullRes) {
    ota_crc.init(16, ELRS_CRC16_POLY);
  }
  else {
    ota_crc.init(14, ELRS_CRC14_POLY);
  }

  DataUlSender.setMaxPackageIndex(ELRS_MSP_MAX_PACKAGES);
  DataDlReceiver.setMaxPackageIndex(OtaIsFullRes ? ELRS8_DATA_DL_MAX_PACKAGES : ELRS4_DATA_DL_MAX_PACKAGES);

  ExpressLRS_currAirRate_Modparams = ModParams;
  ExpressLRS_currAirRate_RFperfParams = RFperf;
  linkStats.rf_Mode = ModParams->enum_rate;

  handset->setPacketInterval(interval * ExpressLRS_currAirRate_Modparams->numOfSends);
  setConnectionState(disconnected);
  rfModeLastChangedMS = millis();
}

bool ICACHE_RAM_ATTR ProcessDownlinkPacket_v3(SX12xxDriverCommon::rx_status const status)
{
  if (status != SX12xxDriverCommon::SX12XX_RX_OK)
  {
    DBGLN("TLM HW CRC error");
    return false;
  }

  OTA_Packet_v3_s * const otaPktPtr = (OTA_Packet_v3_s * const)Radio.RXdataBuffer;
  if ((OtaIsFullRes && !ValidatePacketCrcFull_v3(otaPktPtr)) || (!OtaIsFullRes && !ValidatePacketCrcStd_v3(otaPktPtr)))
  {
    DBGLN("TLM v3 crc error");
    return false;
  }

  if (otaPktPtr->std.type != PACKET_TYPE_TLM_V3)
  {
    DBGLN("TLM type error %d", otaPktPtr->std.type);
    return false;
  }

  LastTLMpacketRecv_Ms = millis();
  LqTQly.add();

  Radio.GetLastPacketStats();
  linkStats.downlink_SNR = SNR_DESCALE(Radio.LastPacketSNRRaw);
  linkStats.downlink_RSSI_1 = Radio.LastPacketRSSI;
  linkStats.downlink_RSSI_2 = Radio.LastPacketRSSI2;

  if (OtaIsFullRes)
  {
    OTA_Packet8_v3_s * const ota8_v3 = &otaPktPtr->full;
    uint8_t const packageIndex = ota8_v3->tlm_dl.packageIndex & OTALEGACY_ELRS8_TELEMETRY_MAX_PACKAGES;
    uint8_t const *telemPtr;
    uint8_t dataLen;
    if (ota8_v3->tlm_dl.containsLinkStats)
    {
      OTA_LinkStats_s lsV4 = translateLinkStatsV3ToV4(&ota8_v3->tlm_dl.ul_link_stats.stats);
      LinkStatsFromOta(&lsV4);
      DataUlSender.ConfirmCurrentPayload(ota8_v3->tlm_dl.ul_link_stats.stats.mspConfirm != 0);
      telemPtr = ota8_v3->tlm_dl.ul_link_stats.payload;
      dataLen = sizeof(ota8_v3->tlm_dl.ul_link_stats.payload);
    }
    else
    {
      if (firmwareOptions.is_airport)
      {
        return true;
      }
      telemPtr = ota8_v3->tlm_dl.payload;
      dataLen = sizeof(ota8_v3->tlm_dl.payload);
    }
    DataDlReceiver.ReceiveData(packageIndex, telemPtr, dataLen);
  }
  else
  {
    switch (otaPktPtr->std.tlm_dl.type)
    {
      case ELRS_TELEMETRY_TYPE_LINK_V3:
      {
        OTA_LinkStats_s lsV4 = translateLinkStatsV3ToV4(&otaPktPtr->std.tlm_dl.ul_link_stats.stats);
        LinkStatsFromOta(&lsV4);
        DataUlSender.ConfirmCurrentPayload(otaPktPtr->std.tlm_dl.ul_link_stats.stats.mspConfirm != 0);
        break;
      }

      case ELRS_TELEMETRY_TYPE_DATA_V3:
        if (firmwareOptions.is_airport)
        {
          return true;
        }
        DataDlReceiver.ReceiveData(
          otaPktPtr->std.tlm_dl.packageIndex & OTALEGACY_ELRS4_TELEMETRY_MAX_PACKAGES,
          otaPktPtr->std.tlm_dl.payload,
          sizeof(otaPktPtr->std.tlm_dl.payload));
        break;
    }
  }

  return true;
}

void ICACHE_RAM_ATTR SendRCdataToRF_v3()
{
  // Do not send a stale channels packet to the RX if one has not been received from the handset
  // *Do* send data if a packet has never been received from handset and the timer is running
  // this is the case when bench testing and TXing without a handset
  bool dontSendChannelData = false;
  uint32_t lastRcData = handset->GetRCdataLastRecv();
  if (lastRcData && (micros() - lastRcData > 1000000))
  {
    // The tx is in Mavlink mode and without a valid crsf or RC input.  Do not send stale or fake zero packet RC!
    // Only send sync and MSP packets.
    if (config.GetLinkMode() == TX_MAVLINK_MODE)
    {
      dontSendChannelData = true;
    }
    else
    {
      return;
    }
  }

  busyTransmitting = true;

  uint32_t const now = millis();
  // ESP requires word aligned buffer
  WORD_ALIGNED_ATTR OTA_Packet_v3_s otaPkt = {0};
  static uint8_t syncSlot;

  const bool isTlmDisarmed = config.GetTlm() == TLM_RATIO_DISARMED;
  uint32_t SyncInterval = (connectionState == connected && !isTlmDisarmed) ? ExpressLRS_currAirRate_RFperfParams->SyncPktIntervalConnected : ExpressLRS_currAirRate_RFperfParams->SyncPktIntervalDisconnected;
  bool skipSync = InBindingMode ||
    // TLM_RATIO_DISARMED keeps sending sync packets even when armed until the RX stops sending telemetry and the TLM=Off has taken effect
    (isTlmDisarmed && isArmed && (ExpressLRS_currTlmDenom == 1));

  uint8_t NonceFHSSresult = OtaNonce % ExpressLRS_currAirRate_Modparams->FHSShopInterval;

  // Sync spam only happens on slot 1 and 2 and can't be disabled
    if ((syncSpamCounter || (syncSpamCounterAfterRateChange && FHSSonSyncChannel())) && (NonceFHSSresult == 1 || NonceFHSSresult == 2))
  {
    otaPkt.std.type = PACKET_TYPE_SYNC;
    GenerateSyncPacketData_v3(OtaIsFullRes ? &otaPkt.full.sync.sync : &otaPkt.std.sync);
    syncSlot = 0; // reset the sync slot in case the new rate (after the syncspam) has a lower FHSShopInterval
  }
  // Regular sync rotates through 4x slots, twice on each slot, and telemetry pushes it to the next slot up
  // But only on the sync FHSS channel and with a timed delay between them
  else if ((!skipSync) && ((syncSlot / 2) <= NonceFHSSresult) && (now - SyncPacketLastSent > SyncInterval) && FHSSonSyncChannel())
  {
    otaPkt.std.type = PACKET_TYPE_SYNC;
    GenerateSyncPacketData_v3(OtaIsFullRes ? &otaPkt.full.sync.sync : &otaPkt.std.sync);
    syncSlot = (syncSlot + 1) % (ExpressLRS_currAirRate_Modparams->FHSShopInterval * 2);
  }
  else
  {
    if (firmwareOptions.is_airport)
    {
      // OtaPackAirportData(&otaPkt, &apInputBuffer); // DO NOT ATTEMPT, backward compatibility with Airport mode is not going to be implemented
    }
    else if ((NextPacketIsDataUl && DataUlSender.IsActive()) || dontSendChannelData)
    {
      otaPkt.std.type = PACKET_TYPE_DATA;
      if (OtaIsFullRes)
      {
        otaPkt.full.msp_ul.packageIndex = DataUlSender.GetCurrentPayload(
          otaPkt.full.msp_ul.payload,
          sizeof(otaPkt.full.msp_ul.payload));
        if (config.GetLinkMode() == TX_MAVLINK_MODE)
          otaPkt.full.msp_ul.tlmFlag = DataDlReceiver.GetCurrentConfirm();
      }
      else
      {
        otaPkt.std.msp_ul.packageIndex = DataUlSender.GetCurrentPayload(
          otaPkt.std.msp_ul.payload,
          sizeof(otaPkt.std.msp_ul.payload));
        if (config.GetLinkMode() == TX_MAVLINK_MODE)
          otaPkt.std.msp_ul.tlmFlag = DataDlReceiver.GetCurrentConfirm();
      }

      // send channel data next so the channel messages also get sent during msp transmissions
      NextPacketIsDataUl = false;
      // counter can be increased even for normal msp messages since it's reset if a real bind message should be sent
      BindingSendCount++;
      // If not in TlmBurst, request a sync packet soon to trigger higher download bandwidth for reply
      if (syncTelemBoostState == stbIdle)
        syncSpamCounter = 1;
      syncTelemBoostState = stbRequested;
    }
    else
    {
      // always enable msp after a channel package since the slot is only used if MspSender has data to send
      NextPacketIsDataUl = true;

      //injectBackpackPanTiltRollData(now); // unsupported in legacy mode
      OtaPackChannelData_v3(&otaPkt, ChannelData, DataDlReceiver.GetCurrentConfirm(), ExpressLRS_currTlmDenom);
    }
  }

  ///// Next, Calculate the CRC and put it into the buffer /////
  if (OtaIsFullRes) {
    GeneratePacketCrcFull_v3(&otaPkt);
  }
  else {
    GeneratePacketCrcStd_v3(&otaPkt);
  }

  SX12XX_Radio_Number_t transmittingRadio = Radio.GetStrongestReceivingRadio();

  if (isDualRadio())
  {
    switch (config.GetAntennaMode())
    {
    case TX_RADIO_MODE_GEMINI:
      transmittingRadio = SX12XX_Radio_All; // Gemini mode
      break;
    case TX_RADIO_MODE_ANT_1:
      transmittingRadio = SX12XX_Radio_1; // Single antenna tx and true diversity rx for tlm receiption.
      break;
    case TX_RADIO_MODE_ANT_2:
      transmittingRadio = SX12XX_Radio_2; // Single antenna tx and true diversity rx for tlm receiption.
      break;
    case TX_RADIO_MODE_SWITCH:
      if(OtaNonce%2==0)   transmittingRadio = SX12XX_Radio_1; // Single antenna tx and true diversity rx for tlm receiption.
      else   transmittingRadio = SX12XX_Radio_2; // Single antenna tx and true diversity rx for tlm receiption.
      break;
    default:
      break;
    }
  }

#if defined(Regulatory_Domain_EU_CE_2400)
  transmittingRadio = LbtChannelIsClear(transmittingRadio);   // weed out the radio(s) if channel in use

  if (transmittingRadio == SX12XX_Radio_NONE)
  {
    // No packet will be sent due to LBT.
    // Defer TXdoneCallback() to prepare for TLM when the IRQ is normally triggered.
    deferExecutionMicros(ExpressLRS_currAirRate_RFperfParams->TOA, Radio.TXdoneCallback);
  }
  else
#endif
  {
    Radio.TXnb((uint8_t*)&otaPkt, false, nullptr, transmittingRadio);
  }
}

#endif
