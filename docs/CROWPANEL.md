# CrowPanel Advance 5.0" HMI — Hardware Reference

> **Product:** Elecrow CrowPanel Advance 5.0" HMI ESP32 AI Display
> **Model:** ESP32-S3-WROOM-1-N16R8
> **Source:** [Elecrow Product Page](https://www.elecrow.com/crowpanel-advance-5-0-hmi-esp32-ai-display-800x480-ips-artificial-intelligent-touch-screen.html)

---

## Key Features

- ESP32-S3 dual-core LX7 @ 240 MHz with 512 KB SRAM, 8 MB PSRAM, 16 MB Flash
- 5.0" IPS capacitive touch display, 800×480, 178° viewing angle
- AI speech interaction via onboard microphone and speaker port
- WiFi 2.4 GHz (802.11 b/g/n) + Bluetooth 5.0 (BLE)
- Replaceable wireless module slot (Zigbee, LoRa, nRF24L01, Matter, Thread, WiFi HaLow, WiFi 6)
- RTC for persistent timekeeping across power cycles
- Compatible with Arduino IDE, ESP-IDF, PlatformIO, and LVGL
- Built-in LVGL demo interface — plug and play
- SquareLine Studio compatible for visual UI design

---

## Core Specifications

### Processor & Memory

| Parameter       | Value                                        |
|-----------------|----------------------------------------------|
| Main Chip       | ESP32-S3-WROOM-1-N16R8                       |
| CPU             | Xtensa 32-bit LX7 dual-core, up to 240 MHz  |
| SRAM            | 512 KB                                       |
| PSRAM           | 8 MB                                         |
| Flash           | 16 MB                                        |
| ROM             | 384 KB                                       |
| Dev Languages   | C / C++                                      |
| Dev Environment | ESP-IDF, Arduino IDE, PlatformIO, LVGL       |

### Display

| Parameter       | Value                    |
|-----------------|--------------------------|
| Size            | 5.0 inch                 |
| Driver IC       | ST7262                   |
| Resolution      | 800 × 480               |
| Panel Type      | IPS                      |
| Touch           | Capacitive, single-touch |
| Viewing Angle   | 178°                     |
| Brightness      | 400 cd/m² (typical)      |
| Color Depth     | 16-bit                   |
| Active Area     | 108 mm × 65 mm          |

### Wireless (Onboard)

| Parameter       | Value                            |
|-----------------|----------------------------------|
| WiFi            | 2.4 GHz, 802.11 a/b/g/n         |
| Bluetooth       | BLE 5.0                          |

### Optional Wireless (Replaceable Module Slot)

Zigbee, LoRa, nRF24L01, Matter, Thread, WiFi HaLow, WiFi 6

### Physical

| Parameter              | Value                               |
|------------------------|-------------------------------------|
| Dimensions             | 136.4 × 84.7 × 15.4 mm            |
| Operating Temperature  | −20 °C to +70 °C                   |
| Storage Temperature    | −30 °C to +80 °C                   |
| Power Input            | 5 V / 2 A (USB or UART terminal)   |
| Mounting               | Back hanging, fixed hole            |

---

## Interfaces & Functions

| Interface   | Description                                                              | Connector   |
|-------------|--------------------------------------------------------------------------|-------------|
| SPK         | Audio output to speaker (onboard power amplifier)                        | PH2.0-2P    |
| UART0-OUT   | Serial communication to external logic/printing modules                  | HY2.0-4P    |
| UART1-OUT   | Serial communication to external logic/printing modules                  | HY2.0-4P    |
| UART0-IN    | Serial input                                                             | XH2.54-4P   |
| I2C-OUT     | I2C bus for peripheral devices                                           | HY2.0-4P    |
| BAT         | Lithium battery connection (with charge management circuit)              | PH2.0-2P    |
| USB         | USB-C for programming and power                                          | —           |
| SD Card     | MicroSD slot (**cannot be used simultaneously with microphone**)         | —           |
| Microphone  | Onboard mic for voice interaction (**cannot be used simultaneously with SD**) | —     |

### Onboard Functions

- **RTC Clock** — Maintains time across power loss
- **Audio Amplifier** — Drives speaker from SPK port
- **Volume Control** — Software-adjustable
- **Battery Charge Management** — CHG LED indicates status
- **USB-to-UART** — Built-in converter for programming

### Buttons & LEDs

| Element       | Function                                        |
|---------------|-------------------------------------------------|
| RST Button    | Press to reset the device                       |
| BOOT Button   | Hold during reset to enter flash/burn mode      |
| PWR LED       | Power indicator                                 |
| CHG LED       | Lithium battery charge status / completion      |

---

## Wireless Module Details

The CrowPanel supports a replaceable wireless module in a dedicated slot. Four modules
are documented below.

### ESP32-H2 Module

**Chip:** ESP32-H2FH4 | **Flash:** 4 MB | **SRAM:** 320 KB | **ROM:** 128 KB | **LP Memory:** 4 KB

**Pins:**

| Pin     | Direction    | Function                          |
|---------|-------------|-----------------------------------|
| U1RXD   | Input       | Serial port 0 RX                  |
| U1TXD   | Output      | Serial port 0 TX                  |
| GPIO12  | I/O         | General purpose                   |
| GPIO13  | I/O         | General purpose                   |
| GPIO14  | I/O         | General purpose                   |
| 3V3     | Power       | 3.3 V supply                      |
| GND     | Power       | Ground                            |
| GPIO1   | I/O         | General purpose                   |
| GPIO0   | I/O         | General purpose                   |
| GPIO2   | I/O         | General purpose                   |
| GPIO22  | I/O         | General purpose                   |
| GPIO10  | I/O         | General purpose                   |
| GPIO11  | I/O         | General purpose                   |
| BOOT    | —           | Hold + RST to enter burn mode     |
| RST     | —           | Reset / re-run program            |

**BLE Radio:** 2402–2480 MHz, TX power −24.0 to +20.0 dBm

**Antenna:** Peak gain 4.33 dBi, avg efficiency 57.5%

---

### ESP32-C6 Module

**Chip:** ESP32-C6FH4 | **Flash:** 4 MB | **HP SRAM:** 512 KB | **LP SRAM:** 16 KB | **ROM:** 320 KB

**Pins:**

| Pin     | Direction    | Function                          |
|---------|-------------|-----------------------------------|
| U1RXD   | Input       | Serial port 0 RX                  |
| U1TXD   | Output      | Serial port 0 TX                  |
| GPIO0   | I/O         | General purpose                   |
| GPIO1   | I/O         | General purpose                   |
| GPIO2   | I/O         | General purpose                   |
| 3V3     | Power       | 3.3 V supply                      |
| GND     | Power       | Ground                            |
| GPIO23  | I/O         | General purpose                   |
| GPIO22  | I/O         | General purpose                   |
| GPIO21  | I/O         | General purpose                   |
| GPIO20  | I/O         | General purpose                   |
| GPIO19  | I/O         | General purpose                   |
| GPIO18  | I/O         | General purpose                   |
| BOOT    | —           | Hold + RST to enter burn mode     |
| RST     | —           | Reset / re-run program            |

**BLE Radio:** 2402–2480 MHz, TX power −15.0 to +20.0 dBm
**WiFi:** 2412–2484 MHz, 802.11 b/g/n/ax (WiFi 6)

**Antenna:** Peak gain 3.74 dBi, avg efficiency 55.9%

---

### nRF24L01 Module

**Chip:** nRF24L01+

**Specs:**
- 2.4 GHz ISM band
- Air data rates: 250 kbps, 1 Mbps, 2 Mbps
- TX current: 11.3 mA @ 0 dBm
- RX current: 13.5 mA @ 2 Mbps
- Power-down: 900 nA
- Supply: 1.9–3.6 V

**Pins:**

| Pin   | Direction | Function                             |
|-------|-----------|--------------------------------------|
| CE    | Input     | Module control (chip enable)         |
| SCK   | Output    | SPI clock                            |
| MISO  | Input     | SPI data (master in, slave out)      |
| MOSI  | Output    | SPI data (master out, slave in)      |
| 3V3   | Power     | 3.3 V supply                         |
| GND   | Power     | Ground                               |
| CSN   | Input     | SPI chip select (active low)         |
| IRQ   | Input     | Interrupt output (active low)        |

**Antenna:** Peak gain 3.74 dBi, avg efficiency 55.9%

---

## Resources

| Resource | Link |
|----------|------|
| Wiki / Tutorial | [CrowPanel Advance 5.0 Wiki](https://www.elecrow.com/pub/wiki/CrowPanel_Advance_5.0-HMI_ESP32_AI_Display.html) |
| GitHub Examples | [Elecrow-RD/CrowPanel-Advance-5](https://github.com/Elecrow-RD/CrowPanel-Advance-5-HMI-ESP32-S3-AI-Powered-IPS-Touch-Screen-800x480) |
| Forum | [Elecrow Display Forum](https://forum.elecrow.com/categories/display) |
| YouTube Tutorials | [CrowPanel Playlist](https://www.youtube.com/watch?v=W3LHGZRx7Jw&list=PLwh4PlcPx2Gfrtm7TmlARyF4ccTmIy-gK) |
| All ESP32 Displays | [Elecrow ESP32 HMI Displays](https://www.elecrow.com/display/esp-hmi-display/esp32-hmi-display.html) |
| SquareLine Studio | [squareline.io](https://squareline.io/) |
| SquareLine Vision (Beta) | [vision.squareline.io](https://vision.squareline.io/) — 40% off during beta |
| Tactility Project | [tactilityproject.org](https://tactilityproject.org/#/) |
