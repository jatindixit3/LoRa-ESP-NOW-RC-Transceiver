# Receiver v1 — Notes & Wiring Guide

## Hardware: ESP32-C3 Super Mini + LoRa02 433MHz

---

## LoRa02 Module Wiring (Receiver)

| LoRa02 Pin | ESP32-C3 GPIO | Notes                        |
|-----------|--------------|------------------------------|
| VCC       | 3.3V         |                              |
| GND       | GND          |                              |
| SCK       | GPIO4        | SPI Clock                    |
| MISO      | GPIO5        | SPI MISO                     |
| MOSI      | GPIO6        | SPI MOSI                     |
| NSS/CS    | GPIO7        | Chip select                  |
| RST       | GPIO8        | Reset                        |
| DIO0/IRQ  | GPIO9        | RX done interrupt            |

---

## SBUS Output Wiring

| Connection | ESP32-C3 GPIO | Notes                                      |
|-----------|--------------|---------------------------------------------|
| SBUS TX   | GPIO10       | UART1 TX — signal is hardware-inverted      |

Connect GPIO10 → SBUS input on flight controller via a signal wire.

> **Note**: SBUS is an inverted 3.3V UART signal. Most modern FCs (Betaflight, iNav, ArduPilot) have a dedicated SBUS/inverted UART port — just wire it directly. No external inverter needed; inversion is done in software on the ESP32-C3.

### SBUS Protocol Parameters
- Baud rate: 100000
- Format: 8E2 (8 data bits, Even parity, 2 stop bits)
- Signal: Inverted (logic 0 = high voltage, logic 1 = low voltage)
- Frame rate: ~14ms (71Hz)
- 16 channels × 11 bits per frame

---

## Power Input

The receiver can be powered from:
- The drone's 5V BEC output (through a 3.3V LDO) — recommended
- Or directly from a 3.3V BEC if available

Do not connect 5V directly to the ESP32-C3 3.3V pin — use a voltage regulator.

| Source  | Connection |
|---------|-----------|
| 5V BEC  | Via AMS1117-3.3 LDO → ESP32-C3 3.3V pin |
| GND     | Common GND with FC |

---

## Known Issues / v1 Problems

*(Fill in as issues are discovered during testing)*

- [ ] ESP32-C3 ADC is not used (receive-only), but WiFi/BLE init may cause brief spikes — monitor power supply stability
- [ ] First ESP-NOW packet may be dropped on cold start (~200ms WiFi init time)

---

## v1 → v2 Trigger Conditions
- If SBUS timing jitter > 1ms → switch to hardware UART with DMA
- If LoRa range insufficient → increase spreading factor (SF) at cost of latency
- If ESP-NOW range too short → use directional antenna or increase TX power
