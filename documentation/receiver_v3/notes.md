# Receiver v3 — Notes

## Changes from v2
- Ported from XIAO ESP32-S3 Sense to XIAO ESP32-C6
- SPI bus changed from HSPI to default SPI (C6 has single SPI2 peripheral)
- Pin mapping updated for C6: SCK=19, MISO=20, MOSI=21, CS=3, RST=0, DIO0=1
- SBUS TX moved from GPIO43 to GPIO2
- LoRa SF changed from 9 to 7 (matches transmitter v10)
- Boot default CH3 set to 987

## Wiring (XIAO ESP32-C6)
| Function | Board Pin | GPIO |
|----------|-----------|------|
| LoRa SCK | D8 | 19 |
| LoRa MISO | D9 | 20 |
| LoRa MOSI | D10 | 21 |
| LoRa CS | D3 | 3 |
| LoRa RST | D0 | 0 |
| LoRa DIO0 | D1 | 1 |
| SBUS TX | D2 | 2 |

## Requirement
Arduino ESP32 core 3.0+ required — core 2.0.17 does not support ESP32-C6.
