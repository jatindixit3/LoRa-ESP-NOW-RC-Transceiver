# Transmitter v7 — Notes

## Changes from v6
- Added per-channel RC min: CH1=1000, CH2=1000, CH3=987, CH4=1000
- adcToRC now uses rcMinDefault[ch] instead of RC_MIN so CH3 bottom = 987 µs
- Expo normalization updated to handle asymmetric ranges (lower half range = 0 for CH3 is safe)
