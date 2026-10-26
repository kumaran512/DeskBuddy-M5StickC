# Development notes

## First bring-up order

1. Build the project in Codespaces and save the full compiler log if it fails.
2. Verify display init and orientation on the original M5StickC.
3. Verify BtnA setup hold, BtnB settings/mood/restart gestures, and the AXP192 third-button screen toggle/power-off gestures.
4. Verify the setup access point and saving settings.
5. Verify NTP and local clock settings.
6. Only then optimise animation frame rate and battery usage.

## Important distinction

Codespaces is a cloud development environment. It can compile the firmware, but a USB-connected M5StickC attached to your own computer is not normally visible to the cloud container. Upload from your local computer or configure a suitable remote flashing method.
