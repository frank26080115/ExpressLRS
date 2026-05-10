#pragma once

#if defined(RADIO_BLUEPAD32) && defined(PLATFORM_ESP32)

#include "targets.h"
#include "SX12xxDriverCommon.h"

#define RADIO_SNR_SCALE 4

class Bluepad32Driver : public SX12xxDriverCommon
{
public:
    static Bluepad32Driver *instance;

    Bluepad32Driver();
    bool Begin(uint32_t minimumFrequency, uint32_t maximumFrequency);
    void End();
    void Poll();
    void SetTxIdleMode();
    void Config(uint8_t bw, uint8_t sf, uint8_t cr, uint32_t freq,
                uint8_t PreambleLength, bool InvertIQ, uint8_t PayloadLength,
                SX12XX_Radio_Number_t radioNumber = SX12XX_Radio_All);
    void SetFrequencyReg(uint32_t freq, SX12XX_Radio_Number_t radioNumber, bool doRx = false, uint32_t rxTime = 0);
    void SetOutputPower(int8_t power, bool isSubGHz = true);
    void startCWTest(uint32_t freq, SX12XX_Radio_Number_t radioNumber);

    bool GetFrequencyErrorbool(SX12XX_Radio_Number_t radioNumber);
    bool FrequencyErrorAvailable() const;

    void TXnb(uint8_t *data, bool sendGeminiBuffer, uint8_t *dataGemini, SX12XX_Radio_Number_t radioNumber);
    void RXnb();

    uint32_t GetIrqStatus(SX12XX_Radio_Number_t radioNumber);
    void ClearIrqStatus(SX12XX_Radio_Number_t radioNumber);

    void StartRssiInst(SX12XX_Radio_Number_t radioNumber);
    int8_t GetRssiInst(SX12XX_Radio_Number_t radioNumber);
    void GetLastPacketStats();
    void CheckForSecondPacket();
};

#endif
