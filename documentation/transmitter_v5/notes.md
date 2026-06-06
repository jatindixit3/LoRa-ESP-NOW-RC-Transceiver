# Transmitter v5 — Notes

## Changes from v4
- Replaced blocking lora.transmit() with non-blocking lora.startTransmit() + ISR flag
- Fixes WiFi AP dropping after 1-2 minutes when on LoRa fallback

## v5 → v6 Trigger
- Auto-calibration of calMid on every boot from current stick positions
- Per-channel RC mid values: CH1=1500, CH2=1500, CH3=987, CH4=1500
