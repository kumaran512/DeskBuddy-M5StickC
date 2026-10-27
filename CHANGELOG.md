# Changelog

## v1.0.0 — First Stable Release

### Included features and fixes

- Robot Eyes with existing expressions, randomized expression changes, natural blinking, and IMU-based eye movement.
- Clock with large 12-hour time and AM/PM display, manual local-time setup, NTP synchronization, saved time-zone settings, and UTC-based BM8563 RTC handling when the RTC is available and valid.
- Wi-Fi setup portal at `Kumaran Buddy` and five Button B diagnostic views for connection status, addressing, signal strength, access-point details, and diagnostics.
- Debounced button navigation, long-press restart, and the AXP192 power-off action.
- Nonblocking periodic status LED patterns and framebuffer-backed display rendering.
- Fixed the local-time zone being reset by NTP setup, the restart action being bypassed in portal mode, and the portal's blocking post-save reboot wait.
- Added bounds checks for Wi-Fi credential lengths and made eye blink scheduling safe across `millis()` rollover.
- Removed weather/forecast screens, ESCLABS startup branding, and automatic eight-second primary-screen switching.

### Verification

- PlatformIO source compilation: successful with `pio run` during release preparation.
- Physical-device testing: not performed as part of this release preparation. Button feel, display layout, RTC retention, Wi-Fi behavior, IMU response, LEDs, and power-button wake behavior still require an original M5StickC.
