//
// btstack_config.h for esp32 port
//
// Documentation: https://bluekitchen-gmbh.com/btstack/#how_to/
//

#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

// Port related features
#define HAVE_ASSERT
#define HAVE_BTSTACK_STDIN
#define HAVE_EMBEDDED_TIME_MS
#define HAVE_FREERTOS_INCLUDE_PREFIX
#define HAVE_FREERTOS_TASK_NOTIFICATIONS
#define HAVE_MALLOC

// HCI Controller to Host Flow Control
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL

// BTstack features that can be enabled
// #define ENABLE_PRINTF_HEXDUMP
// #define ENABLE_LOG_ERROR
// #define ENABLE_LOG_INFO

// Enable Classic/LE based on esp-idf sdkconfig
#include "bp32_config_shim.h" // #include "sdkconfig.h"
#ifdef CONFIG_IDF_TARGET_ESP32
// ESP32 as dual-mode Controller
#define ENABLE_CLASSIC
#define ENABLE_BLE
#else /* CONFIG_IDF_TARGET_ESP32 */
// ESP32-C3 and ESP32-S3 with LE-only Controller
#define ENABLE_BLE
#endif

// Classic configuration
#ifdef ENABLE_CLASSIC

// ExpressLRS only needs HID gamepads.  Keep Classic HID/L2CAP/SDP support, but
// leave audio, SCO, HFP, AVRCP/GOEP-oriented ERTM, and CTKD out of this build.
#define NVM_NUM_LINK_KEYS CONFIG_BLUEPAD32_MAX_ALLOWLIST

#endif

// LE configuration
#ifdef ENABLE_BLE

#define ENABLE_LE_CENTRAL
#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_SECURE_CONNECTIONS
// ESP32 supports ECDH HCI Commands; the micro-ecc third-party source is not
// vendored in this PlatformIO integration.

#define NVM_NUM_DEVICE_DB_ENTRIES CONFIG_BLUEPAD32_MAX_ALLOWLIST

#endif

// BTstack configuration. buffers, sizes, ...

#ifdef ENABLE_CLASSIC

// HID reports and BLE report maps are small.  Avoid the default BNEP/PAN-sized
// buffers so WiFi mode still has room for AsyncWebServer handlers.
#define HCI_ACL_PAYLOAD_SIZE (512 + 4 + 3)

#define HCI_HOST_ACL_PACKET_LEN HCI_ACL_PAYLOAD_SIZE
#define HCI_HOST_ACL_PACKET_NUM 8
#define HCI_HOST_SCO_PACKET_LEN 0
#define HCI_HOST_SCO_PACKET_NUM 0

#else

// ACL buffer large enough to allow for 512 byte Characteristic
#define HCI_ACL_PAYLOAD_SIZE (512 + 4 + 3)

#define HCI_HOST_ACL_PACKET_LEN HCI_ACL_PAYLOAD_SIZE
#define HCI_HOST_ACL_PACKET_NUM 8
#define HCI_HOST_SCO_PACKET_LEN 0
#define HCI_HOST_SCO_PACKET_NUM 0

#endif


#endif
