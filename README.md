# LoRa + ESP-NOW Dual-Protocol RC Transceiver

A custom long-range RC transceiver built around the FlySky FS-CT6B transmitter hardware, replacing the original 2.4GHz link with a dual-protocol system:

- **ESP-NOW** (primary) — ultra-low latency (~1–2 ms), ~200–400 m line-of-sight
- **LoRa 433 MHz** (fallback) — long range (~1–2 km), automatic switchover when ESP-NOW drops

The system automatically uses ESP-NOW when the drone is close and seamlessly switches to LoRa when out of ESP-NOW range — then switches back when the drone returns.

---

## Features

- 10 RC channels (4 gimbals + 6 switches, 4 of which are new AUX additions)
- Dual-protocol with zero-intervention auto-switching
- SBUS output on the receiver (compatible with most modern flight controllers)
- External 433 MHz antenna for maximum range
- Compact receiver: ESP32-C3 Super Mini + LoRa02 module

---

## Hardware

| Side        | MCU                  | RF Module         |
|-------------|----------------------|-------------------|
| Transmitter | ESP32 DevKit v1      | LoRa02 433 MHz    |
| Receiver    | ESP32-C3 Super Mini  | LoRa02 433 MHz    |

The transmitter ESP32 is installed **inside** the FS-CT6B shell, wired directly to the gimbal potentiometers and switches (bypassing the original STM8 MCU and 2.4 GHz RF module).

---

## Repository Structure

```
CAD/                    — CAD files (enclosures, mounts)
code/
  transmitter/v1/       — Transmitter firmware
  receiver/v1/          — Receiver firmware
documentation/
  transmitter_v1/       — Notes, problems, changelog per version
  receiver_v1/
electronics/
  circuitsurl.txt       — Links to wiring diagrams (CircuitLab)
photos_videos/
  images/
  videos/
reference_data/         — Datasheets and pinouts (flat)
BOM.csv                 — Bill of Materials (transceiver parts only)
```

---

## Getting Started

See `documentation/transmitter_v1/notes.md` for wiring the FS-CT6B internals.
See `documentation/receiver_v1/notes.md` for receiver wiring and SBUS connection to FC.

Flash firmware using [PlatformIO](https://platformio.org/) (`platformio.ini` included in each version folder).

---

## License

MIT License — see `LICENSE`.
