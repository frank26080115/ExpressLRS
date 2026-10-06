# Shrew slow DShot

`BUILD_SHREW_SLOW_DSHOT` adds six output selections in the web and Lua menus:
DShot4, DShot4-3D, DShot8, DShot8-3D, DShot16 and DShot16-3D. The base enum
names are `somDshotSlow` and `somDshotSlow3D`; 8/16 variants select the other
RMT rates. Existing output enum values 0..15 are unchanged. The current Shrew
Zero and ForXR2 environments already enable the build definition.

The 3D options select reversible throttle mapping. All slow modes use ordinary
non-inverted DShot, the normal XOR checksum, and no telemetry requests.

| Mode | ESC sample rate | Bit rate | Zero high | One high | Bit period | Added low pause |
|---|---:|---:|---:|---:|---:|---:|
| DShot4 | 19,607.843 Hz | 3,921.569 bit/s | 76.5 us | 178.5 us | 255 us | 255 us |
| DShot8 | 39,215.686 Hz | 7,843.137 bit/s | 38.25 us | 89.25 us | 127.5 us | 127.5 us |
| DShot16 | 78,431.373 Hz | 15,686.275 bit/s | 19.15 us | 44.65 us | 63.75 us | 63.75 us |

At a 50 ns RMT tick, the high pulses approximate 1.5 sample intervals for zero
and 3.5 intervals for one. This centers the margin for accepting one/two or
three/four initial high samples, with the fifth sample low. All three modes have an exact five-sample bit period.

Each packet contains 16 bits followed by five sample intervals of low pause.
The last bit's low tail gives additional idle detection margin. Data plus pause
occupies 4.335 ms, 2.1675 ms, or 1.0838 ms. For C3 round robin, each pin's maximum
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

These timings deliberately target the sample rates specified above. The earlier
AM32 `SPECIAL_BUILD_SLOW_DSHOT` implementation programs exact 20/40/80 kHz.
That receiver must be retimed to 19,607.843/39,215.686/78,431.373 Hz to match this
sender. Merely changing the sender's mode cannot correct a mismatched receiver
clock. Interrupt jitter and missed samples still need hardware validation.
