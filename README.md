# Shrew Firmware of ExpressLRS

You have stumbled upon the Shrew development branch of ExpressLRS

This is a branch of firmware based on ExpressLRS V4, containing additional features that are not in the official ELRS releases. The additional features mainly target the robot combat hobby (instead of drones).

Summary:

 * Backwards Compatibility with V3
 * Configuration Cloning
 * AM32 Configurator
 * VESC Serial Protocol
 * KISS Telemetry
 * Custom Mixer
 * Activity Indicator LED
 * Unique Wi-Fi SSID
 * Randomized Wi-Fi Channel

### Backwards Compatibility with V3

I am writing this in April of 2026, and ExpressLRS release v4.0.0 was released in Feburary of 2026. ExpressRLS V4 is not compatible with V3, so a V3 transmitter cannot talk with v4 receivers, and a V4 receiver cannot talk with a V3 transmitter. If somebody wants, or is forced to have a V4 transmitter, they will need to update all of their receivers to V4.

This can be quite annoying. So to overcome this, all Shrew branch firmware is written with an additional backwards compatibility layer. It is able to seamlessly automatically work with V4 and V3 transmitters. Hence, there's no reason to avoid using Shrew branch firmware.

If you build the transmitter firmware using the Shrew branch, it will become a V4 transmitter, with an additional option to transmit in a legacy V3 mode, in case you still have receivers that are not updated, or need to loan out your transmitter to somebody else.

### Configuration Cloning

With the Shrew branch of ELRS firmware, you can now download a copy of the currently running firmware that has all of your current configurations embedded in the firmware's metadata. When this firmware is uploaded or flashed from serial port, the metadata is extracted and applied automatically.

This can save some time if you are preparing multiple receivers for the same purpose. You can just configure one of them, and apply the same configuration to other receivers without having to fill in all the form fields. This also facilitates manufacturing of receivers with more intricate built-in default settings.

### AM32 Configurator

This feature is only available on more modern receivers built using ESP32 microcontrollers. (Any of the RadioMaster XR series, or any receiver with the LR1121 radio, or any receiver using diversity)

There is a web configurator built into the Shrew branch ELRS firmware. If you have one or more AM32 firmware ESCs, you can configure any one of them through the ELRS Wi-Fi web UI.

### VESC Serial Protocol

For big robots or big motors, the VESC serial protocol is implemented in the Shrew branch of ELRS firmware.

This feature is very experimental as I personally do not actually have large motors to play with. This feature is here because I was requested by a few BattleBot teams. If you want to help test this, that'd be great!

### KISS Telemetry

Telemetry data from ESCs outputting KISS telemetry protocol now shows up in EdgeTX radios.

### Custom Mixer

To lower the cost for people, there is a custom arcade-tank-drive mixer in the Shrew branch ELRS firmware. This allows for the usage of cheap transmitters that don't have a screen or a way to do mixing internally.

The mixer features a very customizable parametric curve, including options for:

 * deadzone and anti-deadzone
 * scaling (aka gain, sensitivity)
 * offset (aka trim)
 * exponential curve (aka ease)

It also lets you pick two additional channels to apply curves to

It also features a way to dictate a safety switch that can force a fail-safe state if a switch on the transmitter is in a certain position.

### Activity Indicator LED

The LED on receivers running Shrew branch firmware will now either blink or change colours when there is activity from the transmitter (like a stick that is actually moving).

This can be extremely useful for identifying which receiver is linked to which transmitter, especially when a robot can be using two isolated radio systems.

### Unique Wi-Fi SSID

Original ELRS firmware always used a SSID like `ExpressLRS RX` or `ExpressLRS TX`. With Shrew branch firmware, the SSIDs used are like `ELRS-RX-XXXX` or `ELRS-TX-XXXX` where the `XXXX` is a unique hexadecimal number, unique to each unit (based on its Wi-Fi MAC address). This can help users connect to the correct device when multiple devices within an area are in Wi-Fi mode.

### Randomized Wi-Fi Channel

There is a high probability that the original ELRS firmware only used the default Wi-Fi channel (1), which can cause congestion related problems, such as having difficulty seeing the SSID when scanning, or signal problems in general.

My branch of the firmware adds a randomization of used Wi-Fi channel, every time the AP starts, it randomly chooses between channel 1, 6, and 11.

# Original ExpressRLS Readme

![Banner](https://github.com/ExpressLRS/ExpressLRS-Hardware/blob/master/img/banner.png?raw=true)

<center>

[![Release](https://img.shields.io/github/v/release/ExpressLRS/ExpressLRS?style=flat-square)](https://github.com/ExpressLRS/ExpressLRS/releases)
[![Build Status](https://img.shields.io/github/actions/workflow/status/ExpressLRS/ExpressLRS/build.yml?logo=github&style=flat-square)](https://github.com/ExpressLRS/ExpressLRS/actions)
[![License](https://img.shields.io/github/license/ExpressLRS/ExpressLRS?style=flat-square)](https://github.com/ExpressLRS/ExpressLRS/blob/master/LICENSE)
[![Stars](https://img.shields.io/github/stars/ExpressLRS/ExpressLRS?style=flat-square)](https://github.com/ExpressLRS/ExpressLRS/stargazers)
[![Chat](https://img.shields.io/discord/596350022191415318?color=%235865F2&logo=discord&logoColor=%23FFFFFF&style=flat-square)](https://discord.gg/expresslrs)

</center>

**ExpressLRS** is developed and maintained by **ExpressLRS LLC** and its passionate open source community, working together to advance reliable, high-performance radio control technology.

## Support ExpressLRS
You can support ExpressLRS by contributing code, testing new features, sharing your ideas, or helping others get started. We are exceptionally grateful for those who donate their time to our passion.

If you don't have time to lend a hand in that way but still want to have an impact, consider donating. Donations are used for infrastructure costs and to buy test equipment needed to further the project and make it securely accessible. ExpressLRS accepts donations through Open Collective, which provides recognition of donors and transparency on how that support is utilized.

[![Open Collective backers](https://img.shields.io/opencollective/backers/expresslrs?label=Open%20Collective%20backers&style=flat-square)](https://opencollective.com/expresslrs)

We appreciate all forms of contribution and hope you will join us on Discord!

## Website
For general information on the project please refer to our guides on the [website](https://www.expresslrs.org/), and our [FAQ](https://www.expresslrs.org/faq/)

## About

ExpressLRS is an open source Radio Link for Radio Control applications. Designed to be the best FPV Racing link, it is based on the fantastic Semtech **SX127x**/**SX1280** LoRa hardware combined with an Espressif or STM32 Processor. Using LoRa modulation as well as reduced packet size it achieves best in class range and latency. It achieves this using a highly optimized over-the-air packet structure, giving simultaneous range and latency advantages. It supports both 900 MHz and 2.4 GHz links, each with their own benefits. 900 MHz supports a maximum of 200 Hz packet rate, with higher penetration. 2.4 GHz supports a blistering fast 1000 Hz on [EdgeTX](http://edgetx.org/). With hundreds of different hardware targets from a wide range of hardware manufacturers, the choice of hardware is constantly growing, with different hardware suited to different requirements.

## Configurator
To configure your ExpressLRS hardware, the ExpressLRS Configurator can be used, which is found here:

https://github.com/ExpressLRS/ExpressLRS-Configurator/releases/

## Community
We have both a [Discord Server](https://discord.gg/expresslrs) and [Facebook Group](https://www.facebook.com/groups/636441730280366), which have great support for new users and constant ongoing development discussion

## Features

ExpressLRS has the following features:

- Up to 1000 Hz Packet Rate
- Telemetry (Betaflight Lua Compatibility)
- Wifi Updates
- Bluetooth or WiFi Sim Joystick
- Oled & TFT Displays
- 2.4 GHz, 900 MHz, and Dual-Band RC Link
- SMD Antenna - allows for easier installation into micros
- Supported receiver protocols: CRSF, SBUS, SUMD, HoTT Telemetry, MAVLink, and PWM
- VTX and VRX Frequency adjustments from the Lua, including SmartAudio and Tramp support
- Bind Phrases - no need for button binding

with many more features on the way!

## Supported Hardware

ExpressLRS currently supports hardware from a wide range of manufacturers. In principle, the targets listed in the [ExpressLRS Configurator](https://github.com/ExpressLRS/ExpressLRS-Configurator/releases/) are tested and supported hardware.

See [Hardware Selection](https://www.expresslrs.org/hardware/hardware-selection/) for guidance. We do not manufacture any of our hardware, so we can only provide limited support for faulty hardware.

## Developers

If you are a developer and would like to contribute to the project, feel free to join the [discord](https://discord.gg/expresslrs) and chat about bugs and issues. You can also look for issues at the [GitHub Issue Tracker](https://github.com/ExpressLRS/ExpressLRS/issues). The best thing to do is to submit a Pull Request to the GitHub Repository.

![](https://github.com/ExpressLRS/ExpressLRS-Hardware/blob/master/img/community.png?raw=true)
