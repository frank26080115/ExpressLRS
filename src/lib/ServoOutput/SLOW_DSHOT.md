# Shrew slow DShot

`BUILD_SHREW_SLOW_DSHOT` adds DShot4 and DShot4-3D output selections in the
web and Lua menus. Their enum names are `somDshotSlow` and `somDshotSlow3D`.
Both use DSHOT4 timing with the 20 kHz AM32 special build. Existing output
enum values 0..15 are unchanged. The current Shrew Zero and ForXR2 environments
already enable the build definition. DSHOT8 and DSHOT16 remain available in
the RMT implementation but are not exposed as output configuration options.

The 3D options select reversible throttle mapping. All slow modes use ordinary
non-inverted DShot, the normal XOR checksum, and no telemetry requests.

| Mode | ESC sample rate | Bit rate | Zero high | One high | Bit period | Added low pause |
|---|---:|---:|---:|---:|---:|---:|
| DShot4 | 20,000 Hz | 4,000 bit/s | 75 us | 175 us | 250 us | 250 us |
| DShot8 | 40,000 Hz | 8,000 bit/s | 37.5 us | 87.5 us | 125 us | 125 us |
| DShot16 | 80,000 Hz | 16,000 bit/s | 18.75 us | 43.75 us | 62.5 us | 62.5 us |

At a 50 ns RMT tick, the high pulses approximate 1.5 sample intervals for zero
and 3.5 intervals for one. This centers the margin for accepting one/two or
three/four initial high samples, with the fifth sample low. All three modes have an exact five-sample bit period.

Each packet contains 16 bits followed by five sample intervals of low pause.
The last bit's low tail gives additional idle detection margin. Data plus pause
occupies 4.25 ms, 2.125 ms, or 1.0625 ms. For C3 round robin, each pin's maximum
update rate is further divided by the number of active outputs.

Slow C3 updates queue one transmission rather than ten mandatory repeats. The
scheduler waits for the previously transmitted packet and pause before changing
GPIO, even when mixing speeds. Classic ESP32 slow outputs buffer the latest
update and poll for frame completion instead of cutting off an in-flight frame.
Hardware looping is disabled for slow modes; existing software keepalive behavior
can continue sending the latest value until no-pulse failsafe disables it.

The feature widens the PWM mode field to six bits, leaves each channel four bytes
long, and shifts stretch/narrow/failsafe fields by two bits. Feature bit 128 in
`channel.features` tells the web UI which layout to decode. Configuration version
13 migrates version-12 PWM fields; firmware flash-discriminator reset behavior
is unchanged. Firmware without this feature uses version 12 and will follow the
existing defaults/reset policy when reading version-13 settings. Back up settings
before downgrading. Firmware metadata must match its configuration version.

These timings match the current AM32 `SPECIAL_BUILD_SLOW_DSHOT` timer setup,
which programs exact 20/40/80 kHz. The original nominal 20 kHz control timer
runs at 19,607.843 Hz, but the special build overrides that timer. The receiver
synchronizes to the first rising edge of each packet, then samples consecutive
groups of five; it does not resynchronize on each bit. Matching the bit period
therefore prevents timing error from accumulating through the 16-bit packet.
Interrupt jitter and missed samples still need hardware validation.
