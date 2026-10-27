# DeskBuddy-M5StickC

DeskBuddy is a compact desk companion firmware for the **original M5StickC**, with animated robot eyes, a local-time clock, and Wi-Fi setup and diagnostics. This release targets the ESP32-PICO-D4 model with a 160×80 LCD. M5StickC Plus and Plus 2 are not supported by this target.

The source includes motion-controlled eye expressions and blinking, Robot Eyes and Clock screens, manual and NTP time setting with a BM8563 RTC when available, configurable time zones, a Wi-Fi setup portal and diagnostic screens, and status/alert LED patterns. Compilation can be checked with PlatformIO; physical-device behavior still needs testing on the target hardware.

## Hardware requirements

- Original M5StickC (ESP32-PICO-D4, AXP192 power-management chip, 160×80 LCD).
- USB data cable and a local serial port for flashing, if desired.
- A 2.4 GHz Wi-Fi network for NTP time synchronization. The setup portal can be used from a phone or computer.

M5StickC Plus and Plus 2 models are not supported by this firmware target.

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

## Flash firmware (optional)

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

The access-point password is fixed as `12345678` in the firmware. Use the portal in a trusted physical setting. The home Wi-Fi SSID/password and time zone are stored in ESP32 Preferences on device flash without application-level encryption; they are not included in this repository. The portal rejects SSIDs over 32 bytes and passwords over 63 bytes.

## Controls and screens

- Short press Button B on Robot Eyes or Clock: open the Wi-Fi diagnostics overlay; further short presses cycle through its five screens. Short press the AXP192 power button (the third physical button) to return to the previous primary screen. In clock settings, that same power-button press returns to Robot Eyes.
- Hold the AXP192 power button until its AXP192 long-press event (about 1.5 seconds): show “Powering off...” and invoke the AXP192 power-off API. This cuts system power except the RTC rail; the device remains off until the AXP192 power button is pressed again. Disconnect USB/battery to remove all power.
- Hold Button B for about 0.8–3 seconds: open clock settings from Clock; select the next expression on Eyes.
- While clock settings are open, Button A cycles fields and Button B applies/selects the current action. The Wi-Fi overlay is not active in clock settings.
- Hold Button B for three seconds: show “Restarting...” and restart.
- Hold Button A for three seconds: open Wi-Fi setup. Short Button A has no action.
- Robot Eyes and Clock remain selected until the AXP192 button is pressed. While Robot Eyes is active, expressions change every 15–35 seconds with natural blinking and motion-controlled eye positioning. A manual expression selection is held for at least 30 seconds before automatic choices resume.

The library defines two GPIO buttons, `BtnA` and `BtnB`; the third physical control is the AXP192 power button, exposed through its short/long press IRQ API. Button B has release-edge handling and an additional 180 ms debounce for the diagnostic cycle. Its long press still provides expression selection, clock settings, and restart.

### Clock and time zone

From the Clock screen, hold Button B for 0.8–3 seconds to open clock settings. Button A selects the year, month, day, hour, minute, second, time zone, Save, or Cancel item. Button B increments the selected date/time field; hold it briefly to repeat. For Time Zone, Button B opens common zone choices. The final choices open custom text entry or return to settings. In text entry, A cycles candidate characters and B adds the character; DEL, DONE, and CANCEL are at the end of the candidate list. Save the time with the Save item, or choose Cancel to discard the draft.

Supported IANA presets include `Asia/Kolkata`, `Europe/London`, `Europe/Paris`, `Europe/Berlin`, `America/New_York`, `America/Chicago`, `America/Denver`, `America/Los_Angeles`, `Asia/Tokyo`, `Asia/Singapore`, `Australia/Sydney`, and `UTC`. Fixed offsets use `UTC+HH:MM` or `UTC-HH:MM` (range −14:00 to +14:00). The default is UTC only when no valid zone is saved. The ESP32 image has no general IANA zoneinfo database, so other IANA names are rejected; preset DST behavior is encoded with POSIX rules. Time is stored in UTC in the onboard BM8563 RTC, and changing the time zone does not change that UTC value. NTP uses the selected zone through `configTzTime`; `configTime(0, 0, ...)` would reset the framework's `TZ` environment to UTC.

The original M5StickC library exposes its BM8563 RTC through `M5.Rtc` and supports the onboard IMU through `M5.Imu`; the firmware checks whether the RTC responds and rejects its voltage-low flag. RTC contents and system time are maintained as UTC, then converted using the saved local zone for display. If the RTC is unavailable or invalid, manually set software time lasts only until power is lost or reset unless NTP synchronizes it. NVS stores the time-zone setting, not a source of continuously advancing time. The onboard red LED is active-low on `M5_LED`/GPIO 10: slow blink means disconnected, fast blink means connecting, double pulse means setup AP mode, and no light means connected with no alert. Important and critical alert patterns take priority.

The power-button long press uses the installed AXP192 `PowerOff()` API. It cuts system power except the RTC supply; it is not a software deep-sleep mode or a physical battery disconnect. Physical button wake and shutdown behavior must be verified on the device.

When the IMU initializes, it captures the startup orientation as neutral and uses the library's accelerometer/gyroscope AHRS estimate to move both eyes smoothly. If IMU detection fails, the existing idle eye saccades remain active.

## Troubleshooting

- **Build cannot download platform or libraries:** retry with network access and check the PlatformIO registry status; versions are pinned in `platformio.ini`.
- **Upload fails or no serial port appears:** use a known data-capable USB cable, close other serial monitors, install the USB serial driver if needed, and set `upload_port` explicitly.
- **Portal does not appear:** hold Button A for three seconds, connect to `Kumaran Buddy`, and browse directly to `192.168.4.1`.
- **Wi-Fi diagnostics:** short-press Button B on Eyes or Clock to open the status, addressing, signal, AP, and diagnostics screens. Use the AXP192 power button to return to Eyes or Clock.
- **Clock is not set:** set it in Clock settings, or connect Wi-Fi and allow NTP synchronization. Check the RTC voltage-low condition if time does not persist across power loss.
- **Display or button layout looks wrong:** confirm the device is the original M5StickC, not a Plus model. Rotation, screen margins, and eye placement still need physical verification.

## Development

Build with `pio run`. The CI workflow provides the same target build and publishes its binary as an artifact. Test display initialization, buttons, portal persistence, clock/RTC behavior, IMU eye motion, and LED patterns on physical original M5StickC hardware before relying on them.
