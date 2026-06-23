# Transmitter v8 — Notes

## Changes from v7
- Added BLE Gamepad (ESP32-BLE-Gamepad library) for RC simulator use
- Device name: "Drone TX", appears as a joystick on Windows/Mac/Linux
- CH1 → X axis, CH2 → Y axis, CH3 → Z axis (throttle), CH4 → RZ axis
- CH5–CH10 → Buttons 1–6
- BLE runs alongside ESP-NOW + LoRa — all three active simultaneously
- Config portal header now shows BLE connection status (advertising / connected)

## Library Required
Install "ESP32-BLE-Gamepad" by lemmingDev via Arduino IDE Library Manager before compiling.

## Usage
Power on transmitter → open Bluetooth settings on PC → pair "Drone TX" → open sim → assign axes in sim controller settings.
