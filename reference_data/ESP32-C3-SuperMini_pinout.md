# ESP32-C3 Super Mini — Pinout Reference

## Datasheet / TRM
- ESP32-C3 Technical Reference Manual: https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_en.pdf
- ESP32-C3 Datasheet: https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf

## Super Mini Board Pinout

```
         USB-C
    ┌────┤├────┐
5V  │ 1      2 │ GND
GND │ 3      4 │ 3.3V
3V3 │ 5      6 │ GPIO21 (TX0)
GPIO0 │ 7    8 │ GPIO20 (RX0)
GPIO1 │ 9   10 │ GND
GPIO2 │ 11  12 │ 3.3V
GPIO3 │ 13  14 │ GPIO19 (USB D+)
GPIO4 │ 15  16 │ GPIO18 (USB D-)
GPIO5 │ 17  18 │ GPIO10
GPIO6 │ 19  20 │ GPIO9
GPIO7 │ 21  22 │ GPIO8
    └──────────┘
```

## Key GPIO Functions

| GPIO | ADC       | SPI (FSPI) | UART  | Notes                        |
|------|-----------|------------|-------|------------------------------|
| 0    | ADC1_CH0  | —          | —     |                              |
| 1    | ADC1_CH1  | —          | —     |                              |
| 2    | ADC1_CH2  | —          | —     |                              |
| 3    | ADC1_CH3  | —          | —     |                              |
| 4    | ADC1_CH4  | FSPI_HD    | —     | Used as LoRa SCK in this project |
| 5    | ADC2_CH0  | FSPI_WP    | —     | Used as LoRa MISO            |
| 6    | —         | FSPI_CLK   | —     | Used as LoRa MOSI            |
| 7    | —         | FSPI_D     | —     | Used as LoRa CS/NSS          |
| 8    | —         | FSPI_Q     | —     | Used as LoRa RST; boot strapping pin |
| 9    | —         | FSPI_CS0   | —     | Used as LoRa DIO0; boot strapping pin |
| 10   | —         | —          | UART1_TX | Used as SBUS output (inverted) |
| 18   | —         | —          | —     | USB D- (avoid if using USB)  |
| 19   | —         | —          | —     | USB D+ (avoid if using USB)  |
| 20   | —         | —          | UART0_RX | Serial monitor RX          |
| 21   | —         | —          | UART0_TX | Serial monitor TX          |

## ADC Notes
- ADC1: GPIO0–GPIO5 (reliable, not affected by WiFi)
- ADC2: GPIO5 only (avoid when WiFi active — shared with WiFi PHY)
- Resolution: up to 12-bit (0–4095)
- Reference: internal 1.1V (with 11dB attenuation → 0–3.3V effective range)

## Power
- VCC: 3.3V regulated (onboard LDO from 5V USB)
- Max GPIO current: 40mA per pin, 300mA total
- Deep sleep current: ~5µA

## Important Boot Strapping Pins
- GPIO8: Must be HIGH at boot (internal pull-up; LOW = boot ROM mode)
- GPIO9: Must be HIGH at boot (internal pull-up; LOW = download mode)
- These can be used as outputs AFTER boot completes
