# Transmitter v10 — Notes

## Changes from v8
- Removed entire web UI (no WiFi AP, no WebServer, no DNSServer, no EEPROM)
- WiFi STA mode only — no AP broadcast, cleaner radio environment
- esp_wifi_set_max_tx_power(84) — hardware max 21dBm for ESP-NOW range
- esp_wifi_set_ps(WIFI_PS_NONE) — WiFi power saving fully disabled
- No packet rate limit — loop runs as fast as possible
- No smoothing/filtering on ADC
- No expo, trim, or reversal
- calMin/calMax hardcoded (200/3895) — calMid auto-read at boot
- Startup packet: CH1=1500, CH2=1500, CH3=987, CH4=1500
- AUX (CH5-10) startup: live switch reads
- BLE Gamepad retained for sim use
