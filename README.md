# ESP32 W5500 Layer-2 Network Discovery

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-ESP32-E7352C.svg?logo=espressif)](https://www.espressif.com/)
[![Framework](https://img.shields.io/badge/Framework-Arduino-00979D.svg?logo=arduino)](https://www.arduino.cc/)
[![PlatformIO](https://img.shields.io/badge/Build-PlatformIO-FF7F00.svg?logo=platformio)](https://platformio.org/)
[![Language](https://img.shields.io/badge/Language-C%2B%2B-00599C.svg?logo=cplusplus)](https://isocpp.org/)
[![Ethernet](https://img.shields.io/badge/Ethernet-W5500-4CAF50.svg)](https://wiznet.io/products/ethernet-chips/w5500)
[![BLE](https://img.shields.io/badge/BLE-NimBLE--Arduino-6A0DAD.svg?logo=bluetooth)](https://github.com/h2zero/NimBLE-Arduino)
[![Protocol](https://img.shields.io/badge/Protocol-LLDP%20%7C%20CDP-1E88E5.svg)](https://en.wikipedia.org/wiki/Link_Layer_Discovery_Protocol)
[![Display](https://img.shields.io/badge/Display-SSD1306%20OLED-607D8B.svg)](https://github.com/olikraus/u8g2)
[![Last Commit](https://img.shields.io/github/last-commit/jenglue/esp32-w5500-l2discover.svg)](https://github.com/jenglue/esp32-w5500-l2discover/commits)
[![Repo Size](https://img.shields.io/github/repo-size/jenglue/esp32-w5500-l2discover.svg)](https://github.com/jenglue/esp32-w5500-l2discover)

這個專案旨在讓搭載 W5500 乙太網路模組的 ESP32 開發板能夠進行簡易的網路檢測。透過 W5500 的 MACRAW 模式被動監聽 Layer-2 網路封包，自動識別周遭網路設備（交換器/路由器）所廣播的 LLDP 與 CDP 訊框，進而取得交換器名稱、連接埠及 VLAN 等拓樸資訊。

This project enables an ESP32 paired with a W5500 Ethernet controller to passively capture Layer-2 Ethernet frames, parse LLDP and CDP discovery packets, and show the latest switch/port information on an SSD1306 OLED. The same data is exposed through BLE to a small Web Bluetooth dashboard.

All hardware values and runtime behavior documented here reflect the current source code. Verify the schematic for the exact board revision before wiring hardware; this documentation is not a replacement for board-level electrical validation.

## Features

- Passive LLDP and CDP discovery using W5500 MACRAW mode
- OLED pages for topology, IP status, packet count, and dashboard QR code
- BLE peripheral named `T-Lite-Sniffer`
- BLE characteristic payload: `SwitchName|PortID|VLAN|IP|Gateway`
- Background DHCP task with link-change handling
- Browser dashboard in [`src/webbt.html`](src/webbt.html)
- GitHub Pages dashboard: <https://jenglue.github.io/esp32-w5500-l2discover/>

## Current firmware pinout

The following table is the pin map currently compiled in [`src/main.cpp`](src/main.cpp):

| Function | GPIO | Notes |
| --- | ---: | --- |
| W5500 CS | 5 | Passed to `Ethernet.setCsPin()` |
| W5500 RST | 4 | Hardware reset, active-low pulse |
| W5500 SCLK | 18 | SPI clock |
| W5500 MISO | 23 | SPI controller input |
| W5500 MOSI | 19 | SPI controller output |
| OLED SDA | 21 | Hardware I2C data |
| OLED SCL | 22 | Hardware I2C clock |
| Boot button | 0 | `INPUT_PULLUP`; cycles OLED pages |

The current firmware does not configure a separate W5500 enable pin. If the physical board wiring differs from this table, treat that as a hardware and firmware change rather than silently changing the documentation.

## Hardware and software stack

- ESP32 target using the Arduino framework
- W5500 Ethernet controller in MACRAW mode
- SSD1306 128x64 OLED using hardware I2C
- PlatformIO environment `esp32dev`

The dependency constraints are declared in [`platformio.ini`](platformio.ini):

| Package | Constraint | Use |
| --- | --- | --- |
| [Ethernet3](https://github.com/sstaub/Ethernet3) | `^1.5.1` | W5500 Ethernet, PHY status, and low-level socket access |
| [U8g2](https://github.com/olikraus/u8g2) | `^2.35.9` | SSD1306 OLED rendering |
| [QRCode](https://github.com/ricmoo/QRCode) | `^0.0.1` | QR code generation for the dashboard URL |
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | `^1.4.1` | Declared BLE dependency |

`lib_ignore = Ethernet` excludes the standard Ethernet library so its symbols do not conflict with `Ethernet3`. The current firmware includes the ESP32 Arduino BLE headers (`BLEDevice.h`, `BLEServer.h`, and related headers); do not assume that the BLE implementation has already been migrated to the NimBLE API.

## Repository layout

- [`src/main.cpp`](src/main.cpp) - firmware entry point, hardware initialization, DHCP task, MACRAW handling, parsers, OLED UI, and BLE updates
- [`src/webbt.html`](src/webbt.html) - Web Bluetooth dashboard
- [`platformio.ini`](platformio.ini) - PlatformIO environment and dependency constraints
- [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) - reproducible development, upload, monitoring, and deployment workflow
- [`docs/PROTOCOL.md`](docs/PROTOCOL.md) - current MACRAW, LLDP/CDP, OLED, and BLE data contracts
- [`.github/workflows/pages.yml`](.github/workflows/pages.yml) - GitHub Pages deployment workflow

## Prerequisites

- [PlatformIO IDE for VS Code](https://docs.platformio.org/en/latest/integration/ide/pioide.html), or [PlatformIO Core CLI](https://docs.platformio.org/en/latest/core/index.html)
- A USB data cable and the serial driver required by the ESP32 board
- An Ethernet cable and a network segment that carries LLDP or CDP advertisements for discovery testing
- A Chromium-based browser with Web Bluetooth support for the dashboard

PlatformIO installs the declared library dependencies on the first build. No manual library copy is required.

## Build, upload, and monitor

Run these commands from the repository root:

```bash
# Compile only
pio run

# Upload to the esp32dev environment
pio run --target upload

# List serial ports before selecting a monitor port
pio device list

# Monitor the firmware at the baud rate used by Serial.begin()
pio device monitor --baud 115200
```

For a specific serial port, add `--port COM<number>` to the upload or monitor command. See [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) for Windows setup, expected serial messages, and troubleshooting.

## Runtime flow

1. `setup()` initializes the OLED, pulses W5500 reset on GPIO 4, starts SPI with the current pin map, configures CS on GPIO 5, and initializes one Ethernet socket with `Ethernet.init(1)`.
2. Ethernet starts with the firmware MAC and an initial `0.0.0.0` address. When the PHY link is up, DHCP runs on an ESP32 background task so the main loop can continue rendering the UI.
3. After DHCP finishes, socket 0 is reopened in W5500 MACRAW mode. The loop skips raw packet access while DHCP owns the controller.
4. The loop scans received frames for LLDP EtherType `0x88CC` and the CDP destination prefix `01:00:0C`, then parses the supported fields.
5. Discovery results update the OLED and the BLE characteristic. Link-down events clear the latest discovery and network state.

The parser intentionally handles only the fields needed by this project. See [`docs/PROTOCOL.md`](docs/PROTOCOL.md) for the exact current subset.

## OLED pages

The boot button cycles through four pages:

1. **Topology** - latest switch name, port, and VLAN; long values scroll once
2. **IP status** - DHCP address, gateway, PHY link state, and speed report
3. **Analytics** - received packet count
4. **Dashboard** - QR code for the hosted Web Bluetooth dashboard

The QR page currently encodes <https://jenglue.github.io/esp32-w5500-l2discover/> from the `dashboardURL` value in [`src/main.cpp`](src/main.cpp).

## BLE interface

| Property | Value |
| --- | --- |
| Device name | `T-Lite-Sniffer` |
| Service UUID | `4fafc201-1fb5-459e-8fcc-c5c9c331914b` |
| Characteristic UUID | `beb5483e-36e1-4688-b7f5-ea07361b26a8` |
| Characteristic properties | Read and notify |
| Payload | `SwitchName|PortID|VLAN|IP|Gateway` |

The dashboard splits the value on `|` and displays the five fields. Initial or unavailable values include `Searching...`, `Waiting LLDP/CDP...`, `N/A`, and `0.0.0.0`. The characteristic only sends a notification when the assembled payload changes. See [`docs/PROTOCOL.md`](docs/PROTOCOL.md) and the [Web Bluetooth API documentation](https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API).

## Web Bluetooth dashboard

### Hosted dashboard

Open <https://jenglue.github.io/esp32-w5500-l2discover/> in a supported browser, select **Connect Sensor**, and choose `T-Lite-Sniffer`. The Pages workflow copies [`src/webbt.html`](src/webbt.html) to `public/index.html` and deploys it when `develop` receives a push; it can also be started manually from GitHub Actions. See [`.github/workflows/pages.yml`](.github/workflows/pages.yml).

### Local dashboard

From the repository root, start a static server:

```bash
# Windows with Python installed
py -m http.server 8000

# Other environments may use
python3 -m http.server 8000
```

Then open <http://localhost:8000/src/webbt.html>. Web Bluetooth requires a secure context, and `localhost` is treated as secure for local development. Browser support varies; check the [MDN compatibility table](https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API#browser_compatibility) before troubleshooting the device.

## Troubleshooting

- **`pio` is not recognized:** use the PlatformIO IDE terminal, install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html), or restart the shell after installation.
- **No serial port appears:** run `pio device list`, check the USB data cable and board driver, and close other applications holding the port.
- **Upload cannot connect:** select the detected port explicitly with `--port COM<number>`; if the board requires manual bootloader entry, follow its board documentation.
- **OLED is blank:** verify the current firmware wiring, especially SDA=21 and SCL=22, and confirm the OLED is powered.
- **No Ethernet link:** verify the W5500 CS/RST/SPI connections from the pinout table, the Ethernet cable, and the switch port. The firmware only begins DHCP after `Ethernet.link()` reports a link.
- **No switch data:** discovery is passive. The connected network device must send LLDP or CDP frames, and the current parser reports only the latest matching device.
- **BLE device is missing:** confirm the board completed setup, enable Bluetooth on the browser host, and use a browser with Web Bluetooth support. The device name is exactly `T-Lite-Sniffer`.
- **Hosted page is stale:** GitHub Pages deploys from `develop`; inspect the [Actions workflow](https://github.com/jenglue/esp32-w5500-l2discover/actions/workflows/pages.yml) and its latest deployment before changing firmware.

## Current limitations

- Discovery is passive and depends on LLDP/CDP frames from nearby network equipment.
- The firmware keeps the latest observed switch, port, and VLAN only; it does not maintain a neighbor table.
- LLDP/CDP parsing covers only the fields documented in [`docs/PROTOCOL.md`](docs/PROTOCOL.md).
- DHCP and MACRAW share the W5500 controller sequentially; raw packet processing pauses while DHCP runs.
- The repository currently contains no executable test cases under `test/`.
- The pinout is based on current firmware definitions and has not been validated here against every physical board revision.

## References

- [Project repository](https://github.com/jenglue/esp32-w5500-l2discover)
- [Online dashboard](https://jenglue.github.io/esp32-w5500-l2discover/)
- [PlatformIO installation](https://docs.platformio.org/en/latest/core/installation/index.html) and [CLI documentation](https://docs.platformio.org/en/latest/core/index.html)
- [Arduino-ESP32 documentation](https://docs.espressif.com/projects/arduino-esp32/en/latest/)
- [Ethernet3](https://github.com/sstaub/Ethernet3)
- [U8g2](https://github.com/olikraus/u8g2)
- [QRCode](https://github.com/ricmoo/QRCode)
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino)
- [WIZnet](https://wiznet.io/) and [W5500 product information](https://wiznet.io/products/ethernet-chips/w5500)
- [IEEE 802.1AB LLDP standard](https://standards.ieee.org/ieee/802.1AB/4462)
- [Cisco CDP technical note](https://www.cisco.com/c/en/us/support/docs/network-management/discovery-protocol-cdp/118736-technote-cdp-00.html)
- [MDN Web Bluetooth API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API)
- [MIT License](LICENSE)

## License

This project is licensed under the [MIT License](LICENSE).
