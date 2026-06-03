# Transmitter v2 — Notes

## Changes from v1
- ESP-NOW init failure now falls back to LoRa immediately instead of silently failing
- Serial monitor prints all channel values and active protocol every 500ms
- Receiver MAC set to XIAO ESP32-S3: 98:3D:AE:60:15:88

## Known Issues
- LoRa running SF9 — ~50ms air time, noticeably sluggish as fallback

## v2 → v3 Trigger
- Dropped LoRa to SF7 for faster fallback (~13ms air time, ~1km range)
