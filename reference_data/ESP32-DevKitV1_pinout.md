# ESP32 DevKit v1 (Wroom-32) — Pinout Reference

## Datasheet
- ESP32 Datasheet: https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf
- ESP32 Technical Reference Manual: https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf
- ESP32-C3 Technical Reference Manual v1.4: https://documentation.espressif.com/esp32-c3_technical_reference_manual_en.pdf

## 30-Pin DevKit Pinout

```
              USB
         ┌────┤├────┐
    3V3  │ 1      30 │ GND
     EN  │ 2      29 │ GPIO23 (MOSI)
  GPIO36 │ 3      28 │ GPIO22
  GPIO39 │ 4      27 │ GPIO1  (TX0)
  GPIO34 │ 5      26 │ GPIO3  (RX0)
  GPIO35 │ 6      25 │ GPIO21
  GPIO32 │ 7      24 │ GND
  GPIO33 │ 8      23 │ GPIO19 (MISO)
  GPIO25 │ 9      22 │ GPIO18 (SCK)
  GPIO26 │ 10     21 │ GPIO5  (CS)
  GPIO27 │ 11     20 │ GPIO17
  GPIO14 │ 12     19 │ GPIO16
  GPIO12 │ 13     18 │ GPIO4
  GND    │ 14     17 │ GPIO0
  GPIO13 │ 15     16 │ GPIO2
    VIN  │ 16     15 │ GPIO15
         └──────────┘
```

## Pin Usage in This Project (Transmitter)

| GPIO | Function          | Notes                                      |
|------|-------------------|--------------------------------------------|
| 34   | CH1 ADC (Aileron) | Input-only, no internal pull-up/down       |
| 35   | CH2 ADC (Elevator)| Input-only, no internal pull-up/down       |
| 32   | CH3 ADC (Throttle)| ADC1_CH4                                   |
| 33   | CH4 ADC (Rudder)  | ADC1_CH5                                   |
| 25   | CH5 (SWA)         | INPUT_PULLDOWN                             |
| 26   | CH6 (SWB)         | INPUT_PULLDOWN                             |
| 27   | CH7 AUX           | INPUT_PULLUP (new switch, active LOW)      |
| 14   | CH8 AUX           | INPUT_PULLUP (new switch, active LOW)      |
| 12   | CH9 AUX           | INPUT_PULLUP — Boot strapping: LOW at boot = SDIO download mode. Ensure switch is open at power-on |
| 13   | CH10 AUX          | INPUT_PULLUP (new switch, active LOW)      |
| 18   | LoRa SCK          | SPI VSPI clock                             |
| 19   | LoRa MISO         | SPI VSPI MISO                              |
| 23   | LoRa MOSI         | SPI VSPI MOSI                              |
|  5   | LoRa CS/NSS       | SPI VSPI CS; boot strapping: must be HIGH at boot |
|  4   | LoRa RST          |                                            |
|  2   | LoRa DIO0         | Boot strapping: must be LOW or floating at boot |

## ADC Notes (ESP32)
- ADC1 (GPIO32–39): Safe to use while WiFi is active
- ADC2 (GPIO0, 2, 4, 12–15, 25–27): **CANNOT be used while WiFi is active**
- Since this project uses ESP-NOW (WiFi), **only ADC1 pins (32–39) are used for gimbals**
- ADC resolution: 12-bit (0–4095)
- Attenuation: use ADC_11db for 0–3.3V range

## Boot Strapping Pins — IMPORTANT
| GPIO | Requirement at Boot | Effect if wrong         |
|------|--------------------|-----------------------------|
| 0    | HIGH (floating OK) | LOW = download mode         |
| 2    | LOW or float       | Must not be HIGH at boot    |
| 5    | HIGH               | LOW = SPI boot mode changes |
| 12   | LOW                | HIGH = 1.8V flash (bad)     |
| 15   | HIGH               | LOW = silences boot log     |

> GPIO12 (CH9 AUX) is a boot strapping pin. The AUX switch on GPIO12 MUST be open (not pressed/activated) at power-on.

## Power
- VIN: 5V input (through onboard LDO to 3.3V)
- 3V3: 3.3V regulated output (max ~600mA depending on board)
- Max GPIO current: 40mA per pin
