# LoRa02 433MHz (AI-Thinker) — Pinout & Reference

## Datasheet
- AI-Thinker LoRa02 Product Page: https://docs.ai-thinker.com/en/lora/ra-02
- SX1278 Datasheet (Semtech): https://semtech.my.salesforce.com/sfc/p/#E0000000JelG/a/2R0000001OKs/Af0607Tj5cmF.0uFaRwovFSLkEiYkVJQfJmDqRiYhfQ
- RadioLib documentation: https://jgromes.github.io/RadioLib/

## Module Pinout (RA-02 / LoRa02)

```
    ┌──────────────────┐
    │  AI-Thinker      │
    │  Ra-02 / LoRa02  │
    │  433MHz / SX1278 │
ANT ├─ Antenna         │
    │                  │
GND ├─ GND             │
3V3 ├─ VCC (3.3V)      │
SCK ├─ SCK (SPI Clock) │
    │                  │
MISO├─ MISO            │
MOSI├─ MOSI            │
NSS ├─ NSS/CS          │
RST ├─ RST             │
DIO0├─ DIO0 (IRQ)      │
DIO1├─ DIO1            │
DIO2├─ DIO2            │
    └──────────────────┘
```

## Pin Descriptions

| Pin  | Direction | Description                                                  |
|------|-----------|--------------------------------------------------------------|
| GND  | —         | Ground                                                       |
| VCC  | IN        | 3.3V supply (do NOT connect to 5V — module is 3.3V only!)   |
| SCK  | IN        | SPI clock                                                    |
| MISO | OUT       | SPI MISO (Master In Slave Out)                               |
| MOSI | IN        | SPI MOSI (Master Out Slave In)                               |
| NSS  | IN        | SPI chip select (active LOW)                                 |
| RST  | IN        | Module reset (active LOW pulse)                              |
| DIO0 | OUT       | Multipurpose: TX done / RX done interrupt (configure in code)|
| DIO1 | OUT       | Multipurpose: RX timeout / FHSS change channel              |
| DIO2 | OUT       | Multipurpose: FHSS change channel                           |
| ANT  | —         | Antenna connection (50Ω coax / SMA)                         |

## Key Specifications (SX1278 @ 433MHz)

| Parameter         | Value                          |
|-------------------|--------------------------------|
| Frequency         | 410–525 MHz                    |
| Modulation        | LoRa (CSS) / FSK / OOK         |
| Max TX power      | +20 dBm                        |
| Sensitivity       | -148 dBm (SF12, BW125, CR4/8)  |
| Supply voltage    | 1.8–3.7V (3.3V nominal)        |
| TX current        | 120 mA @ +20 dBm               |
| RX current        | 9.9 mA                         |
| Sleep current     | 0.2 µA                         |
| SPI speed         | Max 10 MHz                     |
| Interface         | SPI (mode 0, CPOL=0, CPHA=0)   |

## LoRa Settings Used in This Project

| Parameter        | Value   | Notes                                              |
|------------------|---------|----------------------------------------------------|
| Frequency        | 433.0 MHz |                                                  |
| Bandwidth        | 125 kHz | Wider = faster but less range                     |
| Spreading Factor | 9       | SF9 balances range and latency (~50ms air time)   |
| Coding Rate      | 4/7     |                                                   |
| Sync Word        | 0x34    | Custom (must match TX and RX)                     |
| TX Power         | 17 dBm  | Below max to reduce current draw; increase if needed |
| Preamble         | 8 symbols |                                                 |

## Antenna
- 433MHz quarter-wave whip = 164mm (~16.4 cm)
- Alternatively: helical spring antenna with SMA connector
- For maximum range: use external antenna with SMA connector, not the chip antenna

## IMPORTANT: 3.3V ONLY
The LoRa02 / Ra-02 module runs at 3.3V. Connecting VCC to 5V will destroy the module.
All SPI lines must also be 3.3V logic. ESP32 and ESP32-C3 are 3.3V native — no level shifting needed.
