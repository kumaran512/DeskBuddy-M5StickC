# DeskBuddy — Original M5StickC

A DeskBuddy firmware port for the **original M5StickC** with ESP32-PICO-D4 and 80×160 colour TFT. This repository is organised for development in **GitHub Codespaces** using PlatformIO.

> **Status:** Initial port scaffold. The firmware has not yet been verified by compiling in this repository or testing on physical hardware. Treat the first build as a bring-up step and report compiler output before assuming the port is fully working.

## Features intended by the current sketch

- Animated robot eyes and mood expressions
- Built-in M5StickC buttons for interaction
- Wi-Fi configuration access point and web portal
- NTP clock, current weather and forecast screens
- Preferences-based storage of Wi-Fi and weather settings

## Hardware target

- M5StickC **original model** (ESP32-PICO-D4)
- Built-in 80×160 ST7735S-family TFT
- Built-in buttons A and B

This is not the M5StickC Plus or Plus2 project target. Those models have different display dimensions and/or hardware.

## Open in Codespaces

1. Create a new GitHub repository named `DeskBuddy-M5StickC`.
2. Upload the contents of this project (not the outer ZIP folder) to the repository's default branch.
3. On the repository page, choose **Code → Codespaces → Create codespace on main**.
4. Wait for the dev container to finish creating the Python virtual environment and installing PlatformIO.
5. In the Codespaces terminal, run:

   ```bash
   ./.venv/bin/pio run
   ```

6. If compilation succeeds, connect the physical M5StickC to your own computer by USB. Codespaces cannot directly access a USB device connected to your local computer in the normal cloud workflow, so perform the hardware upload locally with PlatformIO/Arduino IDE, or use an appropriate remote-device workflow.

## Local build

Install PlatformIO Core and run:

```bash
pio run
```

To upload from a computer with the M5StickC attached:

```bash
pio run -t upload
pio device monitor -b 115200
```

## Configuration

The sketch starts the `DeskBuddy-Setup` access point when no Wi-Fi credentials are saved. The current default AP password is `12345678`; connect to it and open `http://192.168.4.1` to enter Wi-Fi and OpenWeatherMap settings.

**Security note:** change the setup AP password before sharing or deploying the firmware. The configuration portal is intended for local setup and should not be exposed to untrusted networks. Weather requests currently use plain HTTP; HTTPS with certificate validation should be added before treating this as a hardened build.

Do not commit real Wi-Fi passwords, API keys, or other secrets. The current sketch is designed to collect them through its configuration portal and store them in ESP32 Preferences.

## Development workflow

- Make one change at a time and build with `./.venv/bin/pio run`.
- Keep compiler logs in the issue or pull request when asking for help.
- Test display initialisation and buttons first, then Wi-Fi/configuration, then time and weather.
- Track known issues and planned work in GitHub Issues.

## Planned milestones

- [ ] First clean PlatformIO compile in Codespaces
- [ ] Verify TFT rotation and eye layout on original M5StickC
- [ ] Verify buttons, screen navigation and mood controls
- [ ] Verify configuration portal and Preferences persistence
- [ ] Verify NTP and weather pages
- [ ] Improve HTTPS, portal security and reconnection behaviour
- [ ] Measure battery life and optimise Wi-Fi duty cycle
