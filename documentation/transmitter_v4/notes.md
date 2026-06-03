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
- L7805 runs warm under LoRa TX load (9V → 5V at up to 500mA = up to 2W dissipation)
- Heatsink attached to L7805 — required, not optional
