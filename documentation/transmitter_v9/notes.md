# Transmitter v9 — Notes

## Changes from v8
- EMA smoothing on ADC readings (alpha=0.3) — reduces stick noise across loop iterations
- Configurable packet rate 10–200 Hz via web portal slider, persisted to EEPROM (addr 115)
- esp_wifi_set_ps(WIFI_PS_NONE) — disables WiFi power saving, keeps ESP-NOW latency consistent
- esp_wifi_set_max_tx_power(80) — sets WiFi/ESP-NOW TX to 20dBm for maximum range
- smoothedADC[] pre-seeded from first boot read so EMA starts settled, not from zero
- packetInterval driven by controlHz instead of hardcoded 14ms
