# Receiver v2 — Notes

## Changes from v1
- Ported from ESP32-C3 Super Mini to XIAO ESP32-S3 Sense
- SPI bus changed from FSPI to HSPI — FSPI failed to init on S3
- SPI pins updated: SCK=GPIO7, MISO=GPIO8, MOSI=GPIO9, CS=GPIO1, RST=GPIO3, DIO0=GPIO4
- SBUS TX moved to GPIO43
- Transmitter MAC registered as peer: 10:06:1C:F6:7B:10
- delay(2000) on boot for USB CDC enumeration

## Known Issues
- LoRa running SF9 — sluggish fallback

## v2 → v3 Trigger
- Dropped LoRa to SF7 for faster fallback
