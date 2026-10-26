# DeskBuddy for the Original M5StickC

PlatformIO firmware for the **original M5StickC** (ESP32-PICO-D4, 4 MB flash, 80×160 TFT). M5StickC Plus and Plus 2 are not supported by this target.

Features include animated eyes and expressions, button controls, a Wi-Fi setup portal, local time, current weather, and a three-day forecast. Firmware compilation is verified with PlatformIO; physical-device behavior still needs testing on an original M5StickC.

## Build

Install [PlatformIO Core](https://platformio.org/install/cli) or use the included Codespaces dev container, then run:

```sh
pio run
```

In Codespaces, use `./.venv/bin/pio run`. The build targets the `m5stick-c` board and produces:

```text
.pio/build/m5stick-c/firmware.bin
```

GitHub Actions builds pushes and pull requests and saves the binary as the `deskbuddy-m5stickc-original` workflow artifact.

## Flash

Connect an original M5StickC over USB and run:

```sh
pio run -t upload
pio device monitor -b 115200
```

If PlatformIO does not find the serial port, specify it with `upload_port` (for example `/dev/ttyUSB0` or `COM3`) in a local PlatformIO configuration. The monitor runs at 115200 baud. Codespaces generally cannot access USB devices connected to your own computer; flash locally in that case.

## Wi-Fi and weather setup

On first boot, or after a failed Wi-Fi connection, the device opens a setup network:

- SSID: `DeskBuddy-Setup`
- Password: `12345678`
- Portal: `http://192.168.4.1`

You can also hold Button A during startup, or hold it for three seconds during normal operation, to open the portal. Enter the Wi-Fi SSID and password, an OpenWeatherMap API key, city, country code, and a supported IANA timezone or UTC offset. Saving restarts the device. Leaving a password or API key blank keeps its previously saved value.

Settings are stored in ESP32 Preferences. They are not committed to the repository, but they are stored on the device's flash without application-level encryption. The setup access point uses a fixed password, so only use the portal in a trusted physical setting. Weather requests currently use HTTP, which exposes the API key and request data on the network; use a trusted Wi-Fi network.

## Controls and screens

- Short Button A: move to the next main screen (eyes, clock, weather).
- Short Button B: cycle expressions on the eye screen; on the clock screen, open clock settings. On detail screens, return to the parent screen.
- Hold Button B for about 0.8–3 seconds: open clock details or the weather forecast.
- Hold Button B for three seconds: show “Restarting...” and restart. This takes priority over the detail action.
- Hold Button A for three seconds: open Wi-Fi setup.
- Main screens rotate automatically every eight seconds.

### Clock and time zone

On the clock screen, short-press Button B to open clock settings. Button A selects the year, month, day, hour, minute, second, time zone, Save, or Cancel item. Button B increments the selected date/time field; hold it briefly to repeat. For Time Zone, Button B opens common zone choices. The final choices open custom text entry or return to settings. In text entry, A cycles candidate characters and B adds the character; DEL, DONE, and CANCEL are at the end of the candidate list. Save the time with the Save item, or choose Cancel to discard the draft.

Supported IANA presets include `Asia/Kolkata`, `Europe/London`, `Europe/Paris`, `Europe/Berlin`, `America/New_York`, `America/Chicago`, `America/Denver`, `America/Los_Angeles`, `Asia/Tokyo`, `Asia/Singapore`, `Australia/Sydney`, and `UTC`. Fixed offsets use `UTC+HH:MM` or `UTC-HH:MM` (range −14:00 to +14:00). The ESP32 image has no general IANA zoneinfo database, so other IANA names are rejected; preset DST behavior is encoded with POSIX rules. Time is stored in UTC in the onboard BM8563 RTC, and changing the time zone does not change that UTC value.

The original M5StickC library exposes its BM8563 RTC through `M5.Rtc` and supports SH200Q/MPU6886 sensors through `M5.Imu`; the firmware checks whether the RTC responds and rejects its voltage-low flag. If the RTC is unavailable, manually set time runs only until power is lost or reset, unless NTP later synchronizes it. NVS stores the time-zone setting, not a source of continuously advancing time. The onboard red LED is active-low on `M5_LED`/GPIO 10: slow blink means disconnected, fast blink means connecting, double pulse means setup AP mode, and no light means connected with no alert. Important and critical alert patterns take priority.

Weather refreshes every ten minutes when Wi-Fi and an API key are available.

When the IMU initializes, it captures the startup orientation as neutral and uses the library's accelerometer/gyroscope AHRS estimate to move both eyes smoothly. If IMU detection fails, the existing idle eye saccades remain active.

## Troubleshooting

- **Build cannot download platform or libraries:** retry with network access and check the PlatformIO registry status; versions are pinned in `platformio.ini`.
- **Upload fails or no serial port appears:** use a known data-capable USB cable, close other serial monitors, install the USB serial driver if needed, and set `upload_port` explicitly.
- **Portal does not appear:** hold Button A during power-on, connect to `DeskBuddy-Setup`, and browse directly to `192.168.4.1`.
- **Weather is blank:** verify Wi-Fi credentials, API key, city and country code, and that the OpenWeatherMap key is active. The clock can take time to synchronize after joining Wi-Fi.
- **Display or button layout looks wrong:** confirm the device is the original M5StickC, not a Plus model. Rotation and eye placement have not yet been checked on physical hardware.

## Development

Build with `pio run`. The CI workflow provides the same target build and publishes its binary as an artifact. Test display initialization, buttons, portal persistence, clock, weather, and forecast on physical original M5StickC hardware before relying on them.
