# Transmitter v4 — Notes

## Changes from v3
- WiFi AP config portal added (SSID: Jatins Drone Transmitter, pass: JatinDixit)
- Calibration, channel reversal, trim, expo all configurable via browser
- All settings saved to EEPROM, persist across power cycles

---

## Power System (Confirmed Working)

### Supply
- 2x 9V batteries wired in parallel
- Parallel wiring keeps voltage at 9V while doubling current capacity (~1A combined)
- Estimated runtime: ~4 hours

### Regulation
- L7805 voltage regulator takes 9V in, outputs 5V
- 5V feeds directly into ESP32 DevKit v1 VIN pin (onboard LDO converts to 3.3V internally)
- LoRa02 VCC and gimbal pots powered from ESP32 3.3V output pin

### Thermal
- Heatsink attached to L7805

### Assembly
- Hot glue applied on all important joints for short circuit prevention and mechanical strength
- ESP32 and all components housed inside the FS-CT6B shell with the original shell closed

### WiFi Config Portal
- SSID: Jatins Drone Transmitter
- Password: JatinDixit
- Connect within ~10 seconds of powering on — the AP is always running but some devices may drop and reconnect once before stabilising
- If connection drops, simply reconnect to the same network and navigate to 192.168.4.1
