#pragma once

/*
 * Bluepad32 configuration shim for building the upstream ESP-IDF-oriented
 * sources inside ExpressLRS / PlatformIO Arduino.
 *
 * Upstream Bluepad32 normally receives these CONFIG_* symbols from ESP-IDF's
 * generated sdkconfig.h.  The files in this vendored tree include this shim
 * instead, so keep all Bluepad32-specific defaults here.
 *
 * Goal for this integration:
 *   - ESP32-C3 / ESP32-class RX hardware.
 *   - A PlayStation controller such as DualShock 4 or DualSense is the input.
 *   - ExpressLRS remains in charge of serial/WiFi/config UX.
 *   - Bluepad32 provides Bluetooth HID controller discovery and reports.
 */

/* Keep the framework-generated target/platform symbols when they exist.
 * This provides CONFIG_IDF_TARGET_ESP32C3, CONFIG_BT_ENABLED, UART defaults,
 * FreeRTOS settings, and similar ESP-IDF / Arduino core details.
 */
#if defined(__has_include)
#if __has_include(<sdkconfig.h>)
#include <sdkconfig.h>
#endif
#endif

/* -------------------------------------------------------------------------- */
/* Target Selection                                                            */
/* -------------------------------------------------------------------------- */

/* ESP32 original supports Bluetooth Classic + BLE.  ESP32-C3/S3 support BLE
 * only.  The real sdkconfig should define exactly one CONFIG_IDF_TARGET_* item;
 * these fallbacks are only here for Arduino builds that do not expose them.
 */
#if !defined(CONFIG_IDF_TARGET_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32C3) && \
    !defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(CONFIG_TARGET_POSIX) && \
    !defined(CONFIG_TARGET_PICO_W)
#if defined(ARDUINO_ESP32C3_DEV) || defined(CONFIG_IDF_TARGET_ESP32C3_BETA)
#define CONFIG_IDF_TARGET_ESP32C3 1
#elif defined(ARDUINO_ESP32S3_DEV)
#define CONFIG_IDF_TARGET_ESP32S3 1
#else
/* Conservative fallback for generic ESP32 Arduino boards. */
#define CONFIG_IDF_TARGET_ESP32 1
#endif
#endif

/* Non-ESP test/demo targets.  Leave disabled for ExpressLRS firmware. */
/* #define CONFIG_TARGET_POSIX 1 */  /* Linux host build with BT dongle. */
/* #define CONFIG_TARGET_PICO_W 1 */ /* Raspberry Pi Pico W build. */

/* -------------------------------------------------------------------------- */
/* Bluepad32 Platform                                                          */
/* -------------------------------------------------------------------------- */

/* Custom platform lets ExpressLRS provide the glue layer instead of compiling
 * the upstream Unijoysticle, NINA, AirLift, or MightyMiggy product behavior.
 */
#undef CONFIG_BLUEPAD32_PLATFORM_UNIJOYSTICLE
#undef CONFIG_BLUEPAD32_PLATFORM_NINA
#undef CONFIG_BLUEPAD32_PLATFORM_AIRLIFT
#undef CONFIG_BLUEPAD32_PLATFORM_MIGHTYMIGGY
#undef CONFIG_BLUEPAD32_PLATFORM_MAKEFILE
#ifndef CONFIG_BLUEPAD32_PLATFORM_CUSTOM
#define CONFIG_BLUEPAD32_PLATFORM_CUSTOM 1
#endif

/* Alternative upstream platforms, intentionally disabled for ExpressLRS. */
/* #define CONFIG_BLUEPAD32_PLATFORM_UNIJOYSTICLE 1 */ /* Retro joystick adapter. */
/* #define CONFIG_BLUEPAD32_PLATFORM_NINA 1 */         /* Arduino NINA coprocessor. */
/* #define CONFIG_BLUEPAD32_PLATFORM_AIRLIFT 1 */      /* Adafruit AirLift coprocessor. */
/* #define CONFIG_BLUEPAD32_PLATFORM_MIGHTYMIGGY 1 */  /* MightyMiggy device. */
/* #define CONFIG_BLUEPAD32_PLATFORM_MAKEFILE 1 */     /* Legacy makefile-selected platform. */

/* -------------------------------------------------------------------------- */
/* Controller Capacity                                                         */
/* -------------------------------------------------------------------------- */

/* Maximum simultaneously connected controllers.  ExpressLRS consumes a single
 * gamepad as the control input, so keep the live controller capacity to one to
 * leave heap for WiFi and the web server.
 */
#ifndef CONFIG_BLUEPAD32_MAX_DEVICES
#define CONFIG_BLUEPAD32_MAX_DEVICES 1
#endif

/* Maximum Bluetooth allowlist entries stored by Bluepad32.  Keep this at least
 * as large as CONFIG_BLUEPAD32_MAX_DEVICES.
 */
#ifndef CONFIG_BLUEPAD32_MAX_ALLOWLIST
#define CONFIG_BLUEPAD32_MAX_ALLOWLIST 4
#endif

/* -------------------------------------------------------------------------- */
/* Pairing / Bluetooth Behavior                                                */
/* -------------------------------------------------------------------------- */

/* Enable Bluetooth in the ESP-IDF controller.  Arduino-ESP32 usually defines
 * this already; the fallback keeps BTstack code paths enabled.
 */
#ifndef CONFIG_BT_ENABLED
#define CONFIG_BT_ENABLED 1
#endif

/* Original ESP32 can run dual-mode BT/BLE.  ESP32-C3/S3 are BLE-only, so leave
 * BTDM/BR_EDR mode symbols to the real sdkconfig when available.
 */
/* #define CONFIG_BTDM_CTRL_MODE_BTDM 1 */       /* ESP32 dual-mode Classic + BLE. */
/* #define CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY 1 */ /* ESP32 Classic-only mode. */

/* Bluepad32 property default: accept BLE-capable controllers by default.  This
 * is needed for DualShock 4 / DualSense on ESP32-C3 where Classic is absent.
 */
#ifndef CONFIG_BLUEPAD32_ENABLE_BLE_BY_DEFAULT
#define CONFIG_BLUEPAD32_ENABLE_BLE_BY_DEFAULT 1
#endif

/* GAP security level 2 helps Nintendo Switch Pro and is generally fine for
 * DualShock 4 / DualSense.  Upstream notes DualShock 3 may need this disabled;
 * DS3 also needs Classic, so it is not the main ESP32-C3 target anyway.
 */
#ifndef CONFIG_BLUEPAD32_GAP_SECURITY
#define CONFIG_BLUEPAD32_GAP_SECURITY 1
#endif

/* Virtual devices expose extra mouse/touchpad devices for controllers with
 * touchpads.  Leave off by default so a DualSense/DualShock touchpad does not
 * appear as a separate input source until ExpressLRS explicitly maps it.
 */
#undef CONFIG_BLUEPAD32_ENABLE_VIRTUAL_DEVICE_BY_DEFAULT
/* #define CONFIG_BLUEPAD32_ENABLE_VIRTUAL_DEVICE_BY_DEFAULT 1 */

/* -------------------------------------------------------------------------- */
/* Console / Debug Commands                                                    */
/* -------------------------------------------------------------------------- */

/* Bluepad32's own USB/UART REPL conflicts philosophically with ExpressLRS
 * owning serial, WiFi, and user configuration.  Keep it disabled.
 *
 * Do not define these to 0: several upstream files use #ifdef, where a
 * zero-valued definition would still enable the code.
 */
#undef CONFIG_BLUEPAD32_USB_CONSOLE_ENABLE
#undef CONFIG_BLUEPAD32_CONSOLE_NVS_COMMAND_ENABLE
/* #define CONFIG_BLUEPAD32_USB_CONSOLE_ENABLE 1 */       /* Bluepad32 REPL. */
/* #define CONFIG_BLUEPAD32_CONSOLE_NVS_COMMAND_ENABLE 1 */ /* nvs_* REPL commands. */

/* Console history and UART console settings are only relevant when the
 * Bluepad32 REPL / BTstack stdin is enabled.
 */
#undef CONFIG_ESP_CONSOLE_UART
#undef CONFIG_ESP_UART_CUSTOM
/* #define CONFIG_CONSOLE_STORE_HISTORY 1 */       /* Store REPL history in FS. */
/* #define CONFIG_ESP_CONSOLE_UART 1 */            /* Enable ESP console UART. */
/* #define CONFIG_ESP_CONSOLE_UART_NUM 0 */        /* UART used for console. */
/* #define CONFIG_CONSOLE_UART_NUM 0 */            /* Older ESP-IDF alias. */
/* #define CONFIG_ESP_CONSOLE_UART_BAUDRATE 115200 */
/* #define CONFIG_ESP_UART_CUSTOM 1 */             /* Use custom console pins. */
/* #define CONFIG_ESP_CONSOLE_UART_TX_GPIO 1 */
/* #define CONFIG_ESP_CONSOLE_UART_RX_GPIO 3 */

/* Log level used by uni_log.h: 0 none, 1 error, 2 info, 3 debug.  Error keeps
 * useful bring-up failures without chatty Bluetooth packet logs.
 */
#ifndef CONFIG_BLUEPAD32_LOG_LEVEL
#if defined(ENABLE_BLUEPAD32_DEBUG)
#define CONFIG_BLUEPAD32_LOG_LEVEL 2
#else
#define CONFIG_BLUEPAD32_LOG_LEVEL 1
#endif
#endif

/* Symbolic Kconfig choices, kept disabled/documented for reference. */
/* #define CONFIG_BLUEPAD32_LOG_LEVEL_NONE 1 */
/* #define CONFIG_BLUEPAD32_LOG_LEVEL_ERROR 1 */
/* #define CONFIG_BLUEPAD32_LOG_LEVEL_INFO 1 */
/* #define CONFIG_BLUEPAD32_LOG_LEVEL_DEBUG 1 */

/* -------------------------------------------------------------------------- */
/* Arduino Runtime                                                             */
/* -------------------------------------------------------------------------- */

/* ExpressLRS already owns Arduino setup()/loop().  Keep Arduino autostart on
 * so Bluepad32 does not create its template ArduinoTask via arduino_bootstrap().
 */
#ifndef CONFIG_AUTOSTART_ARDUINO
#define CONFIG_AUTOSTART_ARDUINO 1
#endif

/* Only needed for ESP-IDF projects that add Arduino as an IDF component and
 * disable autostart.  Included here for completeness, disabled for ELRS.
 */
/* #define CONFIG_ENABLE_ARDUINO_DEPENDS 1 */

/* Used only if CONFIG_AUTOSTART_ARDUINO is disabled and Bluepad32 creates its
 * own Arduino task.  ExpressLRS does not use that path.
 */
/* #define CONFIG_ARDUINO_LOOP_STACK_SIZE 8192 */ /* Stack bytes for ArduinoTask. */
/* #define CONFIG_ARDUINO_RUNNING_CORE 1 */       /* Core to pin ArduinoTask to. */

/* FreeRTOS core mode is normally supplied by sdkconfig.  Define only if a build
 * lacks it and trips #if CONFIG_FREERTOS_UNICORE checks.
 */
#ifndef CONFIG_FREERTOS_UNICORE
#define CONFIG_FREERTOS_UNICORE 0
#endif

/* -------------------------------------------------------------------------- */
/* BTstack Feature Knobs                                                       */
/* -------------------------------------------------------------------------- */

/* BTstack audio is unrelated to controller input and pulls in I2S/demo-board
 * code.  Keep it disabled for an RX controlled by a PlayStation controller.
 */
#ifndef CONFIG_BTSTACK_AUDIO
#define CONFIG_BTSTACK_AUDIO 0
#endif

/* BTstack example-board audio config.  Disabled because ExpressLRS boards are
 * custom receiver hardware, not ESP-LyraT audio boards.
 */
/* #define CONFIG_ESP_LYRAT_V4_3_BOARD 1 */
/* #define CONFIG_ESP_CUSTOM_BOARD 1 */

/* -------------------------------------------------------------------------- */
/* Unijoysticle-Only Options                                                   */
/* -------------------------------------------------------------------------- */

/* Only meaningful when CONFIG_BLUEPAD32_PLATFORM_UNIJOYSTICLE is enabled. */
/* #define CONFIG_BLUEPAD32_UNIJOYSTICLE_ENABLE_SWAP_FOR_C64 1 */
