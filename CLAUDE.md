# CLAUDE.md — Drone Transceiver Project

## Rules (non-negotiable)
- **Arduino IDE only** — all code is `.ino` files. No PlatformIO, no `platformio.ini`, no `src/main.cpp`.
- **No comments in code** — no `//` lines, no `/* */` blocks, ever. Not a single one.
- **No co-author in commits** — never add `Co-Authored-By:` or any AI attribution to commit messages.
- Arduino sketch folder name must match the `.ino` filename (Arduino IDE requirement).

---

## Project Summary
Building a custom LoRa + ESP-NOW RC transceiver system to replace/augment the FlySky FS-CT6B 2.4GHz link. The goal is dual-protocol: ESP-NOW (fast, close range) with automatic fallback to LoRa 433MHz (long range).

---

## Hardware

### Transmitter (inside/replacing FS-CT6B RF section)
- **MCU**: ESP32 DevKit v1 (Wroom-32, 30-pin)
- **RF module**: LoRa02 433MHz (SX1278-based, AI-Thinker)
- **Input**: Directly wired to FS-CT6B gimbal potentiometers + switches
- **Channels**: 4 analog (gimbals) + 6 digital switches = 10 channels total (6 original + 4 new AUX)

### Receiver (on drone)
- **MCU**: ESP32-C3 Super Mini
- **RF module**: LoRa02 433MHz (SX1278-based, AI-Thinker)
- **Output**: SBUS only (100000 baud, 8E2, inverted)

---

## Transmitter Approach (CONFIRMED)
Open the FS-CT6B and wire gimbals/switches DIRECTLY to the ESP32 (bypass the original STM8 MCU and 2.4GHz RF entirely). This reduces noise and allows adding 4 new AUX channels. See `documentation/transmitter_v1/notes.md` for wiring details.

---

## Protocol Priority
1. **ESP-NOW** (primary) — ~200–400m LOS, ~1–2ms latency
2. **LoRa 433MHz** (fallback) — 1–2km LOS, ~50–150ms latency
- Auto-switch: if no ESP-NOW ACK for 200ms → switch to LoRa
- Auto-recover: periodically beacon on ESP-NOW; switch back if receiver responds

---

## Channel Mapping

| Channel | Signal       | Source           |
|---------|-------------|------------------|
| CH1     | Aileron/Roll | Gimbal R X-axis  |
| CH2     | Elevator/Pitch | Gimbal R Y-axis |
| CH3     | Throttle    | Gimbal L Y-axis  |
| CH4     | Rudder/Yaw  | Gimbal L X-axis  |
| CH5     | AUX1 (SWA) | FS-CT6B Switch A |
| CH6     | AUX2 (SWB) | FS-CT6B Switch B |
| CH7     | AUX3       | New Switch 1     |
| CH8     | AUX4       | New Switch 2     |
| CH9     | AUX5       | New Switch 3     |
| CH10    | AUX6       | New Switch 4     |

---

## Output Format
- **SBUS**: 100000 baud, 8E2, inverted UART
- Channel range: 172–1811 (maps from 1000–2000 µs)
- 16-channel frame (CH1–CH10 used, CH11–CH16 set to mid 992)

---

## Code Structure
```
code/
  transmitter/
    v1/     ← current working version
    v2/     ← future iterations
  receiver/
    v1/
    v2/
```
- Only final/working code goes into version folders
- Document problems/failures in documentation/ before bumping version

---

## Documentation Convention
- `documentation/transmitter_v1/notes.md` — problems, lessons learned, changes
- `documentation/receiver_v1/notes.md` — same for receiver
- Bump to v2 folder when a fundamental issue requires a redesign

---

## Folder Rules
- `CAD/` — user manages
- `BOM.csv` — user fills Price column; Claude fills all other fields (transceiver parts only, NO drone frame/motors/stack/ESC)
- `electronics/circuitsurl.txt` — user adds CircuitLab URLs
- `reference_data/` — datasheets + pinouts flat (no subfolders)
- `photos_videos/images/` and `photos_videos/videos/` — user manages

---

## Important Constraints
- **DO NOT** add drone frame, motors, ESC, or flight controller to BOM or docs
- SBUS output only on receiver (no PWM, no PPM output)
- Keep library dependencies minimal: RadioLib (LoRa), built-in ESP-NOW, built-in UART
- Target IDE: PlatformIO

---

## Voltage Warning
FS-CT6B runs on 6V (4×AA). Gimbal pots may output 0–5V. Use voltage divider (10K/10K) on ADC inputs if needed. ESP32 ADC max is 3.3V. Verify before connecting.
