# Transmitter v1 — Notes & Wiring Guide

## Approach: Direct Internal Wiring (FS-CT6B → ESP32)

The original FS-CT6B STM8 MCU and 2.4 GHz RF module are bypassed entirely. The ESP32 reads the gimbal potentiometers and switches directly.

---

## Why Direct Internal Wiring?

- Eliminates the original RF receiver from the chain (less noise, fewer failure points)
- Allows adding 4 new AUX switches beyond the 6 original channels
- Lower latency — no AFHDS2A decode step
- The 2.4 GHz STM8 board is simply left disconnected (or removed to save space)

---

## FS-CT6B Internal Connections

### Power
The FS-CT6B uses 4×AA batteries = ~6V. The internal regulator provides 3.3V to the STM8. You can power the ESP32 from the regulated 3.3V rail (check current capability — ESP32 peaks ~500mA, ensure the regulator can handle it) or add a small 3.3V LDO directly from the battery pack.

> **Recommended**: Add a dedicated AMS1117-3.3 LDO from the battery pack to power the ESP32 + LoRa02. Do not rely on the original regulator.

### Gimbal Potentiometers
Each gimbal has an X-axis and Y-axis 10K pot. Each pot has 3 wires:
- **VCC** (one end) — connect to 3.3V (NOT the battery 6V rail)
- **GND** (other end) — connect to GND
- **WIPER** (middle) — analog signal 0–3.3V → goes to ESP32 ADC

> **IMPORTANT**: If gimbal pots are powered from 5V or 6V, the wiper will exceed ESP32 ADC max (3.3V). Use a voltage divider (10K + 10K) on the wiper line.
> If you power the pots from 3.3V (recommended), no divider needed.

| Gimbal Axis    | ESP32 GPIO | ADC Channel |
|----------------|-----------|-------------|
| Right X (Roll/Aileron)   | GPIO34    | ADC1_CH6    |
| Right Y (Pitch/Elevator) | GPIO35    | ADC1_CH7    |
| Left Y (Throttle)        | GPIO32    | ADC1_CH4    |
| Left X (Yaw/Rudder)      | GPIO33    | ADC1_CH5    |

### Original Switches (CH5, CH6)
The FS-CT6B has two 2-position switches (SWA, SWB). They typically output 0V (GND) or 3.3V.

| Switch | ESP32 GPIO | Pull |
|--------|-----------|------|
| SWA (CH5) | GPIO25 | INPUT_PULLDOWN |
| SWB (CH6) | GPIO26 | INPUT_PULLDOWN |

### New AUX Switches (CH7–CH10)
Mount 4 new switches in the FS-CT6B housing (drill new holes or use existing blanks).
Wire one terminal to GPIO, other terminal to GND. Enable INPUT_PULLUP.

| Switch | ESP32 GPIO | Pull |
|--------|-----------|------|
| AUX3 (CH7) | GPIO27 | INPUT_PULLUP |
| AUX4 (CH8) | GPIO14 | INPUT_PULLUP |
| AUX5 (CH9) | GPIO12 | INPUT_PULLUP |
| AUX6 (CH10)| GPIO13 | INPUT_PULLUP |

---

## LoRa02 Module Wiring (Transmitter)

| LoRa02 Pin | ESP32 GPIO | Notes                        |
|-----------|-----------|------------------------------|
| VCC       | 3.3V      |                              |
| GND       | GND       |                              |
| SCK       | GPIO18    | SPI Clock                    |
| MISO      | GPIO19    | SPI MISO                     |
| MOSI      | GPIO23    | SPI MOSI                     |
| NSS/CS    | GPIO5     | Chip select                  |
| RST       | GPIO4     | Reset                        |
| DIO0/IRQ  | GPIO2     | TX done / RX done interrupt  |

---

## Known Issues / v1 Problems

*(Fill in as issues are discovered during testing)*

- [ ] ESP32 ADC non-linearity at low end (0–100mV) may cause stick drift at extremes
- [ ] Gimbal calibration needed after wiring (map raw ADC range to 1000–2000 µs)

---

## v1 → v2 Trigger Conditions
- If ADC noise is too high → add hardware RC filter (100Ω + 10µF) on each ADC input
- If ESP32 doesn't fit inside FS-CT6B shell → redesign PCB layout or use ESP32-C3 Super Mini for TX too
