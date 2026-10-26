# DeskBuddy for the Original M5StickC

PlatformIO firmware for the **original M5StickC** (ESP32-PICO-D4, 4 MB flash, 80×160 TFT). M5StickC Plus and Plus 2 are not supported by this target.

Features include animated, motion-controlled eyes and expressions, manual and RTC-backed local time, a Wi-Fi setup portal, and status/alert LEDs. Firmware compilation is verified with PlatformIO; physical-device behavior still needs testing on an original M5StickC.

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

## Wi-Fi setup

On first boot, or after a failed Wi-Fi connection, the device opens a setup network:

- SSID: `Kumaran Buddy`
- Password: `12345678`
- Portal: `http://192.168.4.1`

The LCD displays the SSID, actual configured access-point password, and portal address for as long as setup mode is active. Hold Button A for three seconds during normal operation to open the portal. Enter the Wi-Fi SSID and password and a supported IANA timezone or UTC offset. Saving restarts the device. Leaving the password blank keeps its previously saved value.

Wi-Fi and timezone settings are stored in ESP32 Preferences. They are not committed to the repository, but are stored on the device's flash without application-level encryption. The setup access point uses a fixed password, so only use the portal in a trusted physical setting.

## Controls and screens

- Short press the AXP192 power button (the third physical button): toggle between Robot Eyes and Clock. In clock settings, it returns to Robot Eyes.
- Hold the AXP192 power button until its AXP192 long-press event (about 1.5 seconds): show “Powering off...” and invoke the AXP192 power-off API. This cuts system power except the RTC rail; the device remains off until the AXP192 power button is pressed again. Disconnect USB/battery to remove all power.
- Short Button B: open clock settings on Clock; select the next expression on Eyes.
- Hold Button B for about 0.8–3 seconds: select the next expression on Eyes.
- Hold Button B for three seconds: show “Restarting...” and restart.
- Hold Button A for three seconds: open Wi-Fi setup. Short Button A has no action.
- Robot Eyes and Clock remain selected until the AXP192 button is pressed. While Robot Eyes is active, expressions change every 15–35 seconds with natural blinking and motion-controlled eye positioning. A manual expression selection is held for at least 30 seconds before automatic choices resume.

The library defines two GPIO buttons, `BtnA` and `BtnB`; the third physical control is the AXP192 power button, exposed through its short/long press IRQ API. The third button owns the direct screen toggle and power-off gesture. Button B retains expression selection, clock settings, and restart.

### Clock and time zone

On the clock screen, short-press Button B to open clock settings. Button A selects the year, month, day, hour, minute, second, time zone, Save, or Cancel item. Button B increments the selected date/time field; hold it briefly to repeat. For Time Zone, Button B opens common zone choices. The final choices open custom text entry or return to settings. In text entry, A cycles candidate characters and B adds the character; DEL, DONE, and CANCEL are at the end of the candidate list. Save the time with the Save item, or choose Cancel to discard the draft.

Supported IANA presets include `Asia/Kolkata`, `Europe/London`, `Europe/Paris`, `Europe/Berlin`, `America/New_York`, `America/Chicago`, `America/Denver`, `America/Los_Angeles`, `Asia/Tokyo`, `Asia/Singapore`, `Australia/Sydney`, and `UTC`. Fixed offsets use `UTC+HH:MM` or `UTC-HH:MM` (range −14:00 to +14:00). The ESP32 image has no general IANA zoneinfo database, so other IANA names are rejected; preset DST behavior is encoded with POSIX rules. Time is stored in UTC in the onboard BM8563 RTC, and changing the time zone does not change that UTC value.

The original M5StickC library exposes its BM8563 RTC through `M5.Rtc` and supports SH200Q/MPU6886 sensors through `M5.Imu`; the firmware checks whether the RTC responds and rejects its voltage-low flag. If the RTC is unavailable, manually set time runs only until power is lost or reset, unless NTP later synchronizes it. NVS stores the time-zone setting, not a source of continuously advancing time. The onboard red LED is active-low on `M5_LED`/GPIO 10: slow blink means disconnected, fast blink means connecting, double pulse means setup AP mode, and no light means connected with no alert. Important and critical alert patterns take priority.

When the IMU initializes, it captures the startup orientation as neutral and uses the library's accelerometer/gyroscope AHRS estimate to move both eyes smoothly. If IMU detection fails, the existing idle eye saccades remain active.

## Troubleshooting

- **Build cannot download platform or libraries:** retry with network access and check the PlatformIO registry status; versions are pinned in `platformio.ini`.
- **Upload fails or no serial port appears:** use a known data-capable USB cable, close other serial monitors, install the USB serial driver if needed, and set `upload_port` explicitly.
- **Portal does not appear:** hold Button A for three seconds, connect to `Kumaran Buddy`, and browse directly to `192.168.4.1`.
- **Clock is not set:** set it in Clock settings, or connect Wi-Fi and allow NTP synchronization. Check the RTC voltage-low condition if time does not persist across power loss.
- **Display or button layout looks wrong:** confirm the device is the original M5StickC, not a Plus model. Rotation and eye placement have not yet been checked on physical hardware.

## Development

Build with `pio run`. The CI workflow provides the same target build and publishes its binary as an artifact. Test display initialization, buttons, portal persistence, clock/RTC behavior, IMU eye motion, and LED patterns on physical original M5StickC hardware before relying on them.
