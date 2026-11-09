#pragma once

#if defined(BUILD_SHREW_RNTESTTX) && defined(TARGET_TX)
#include <stddef.h>
#include <stdint.h>

// Called after CRC generation and antenna selection, before LBT or Radio.TXnb().
// Returns true when the packet is suppressed and its TX completion is deferred.
bool shrewRnTestTxSuppressPacket(uint8_t *packet, size_t packetSize);
#endif
