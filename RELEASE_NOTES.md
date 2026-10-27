# DeskBuddy-M5StickC v1.0.0

First stable release for the original M5StickC (ESP32-PICO-D4, 160×80 LCD).

## Highlights

- Animated, motion-controlled Robot Eyes with existing expressions and natural blinking.
- Local-time Clock with large 12-hour digits and AM/PM, manual time setup, NTP synchronization, saved time zones, and UTC-based BM8563 RTC integration when available.
- Wi-Fi setup portal at `Kumaran Buddy` and five Button B network diagnostic screens.
- Button navigation, long-press restart, AXP192 power-off, and status/alert LED patterns.
- Framebuffer-backed rendering, with the clock updated when its displayed time changes.

The release source also removes weather/forecast screens, ESCLABS startup branding, and automatic eight-second screen switching. NTP now applies the saved time zone correctly. Portal save/restart handling and credential length validation were tightened during final review.

## Build and testing

- Build: `pio run` succeeded during release preparation for the `m5stick-c` PlatformIO environment.
- Hardware: no device was flashed or physically tested during this preparation. Screen layout, buttons, Wi-Fi portal, RTC retention, IMU, LEDs, and AXP192 shutdown/wake behavior require verification on an original M5StickC.

## Known limitations

- Without a valid RTC, software-set time is lost at reset or complete power loss unless NTP is available.
- Only the time-zone presets and fixed offsets documented in the README are supported; there is no full IANA zone database.
- The setup access point uses the firmware's fixed password `12345678`; home Wi-Fi credentials stored in device Preferences are not encrypted by the application.
- AXP192 power-off cuts system power except the RTC supply and is not a physical battery disconnect. Wake behavior should be confirmed on the target hardware.
