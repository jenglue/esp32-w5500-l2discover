# Development Guide

This guide describes the current development and release workflow for the ESP32 W5500 Layer-2 Discovery project. Commands assume that they are run from the repository root.

## Prerequisites

- [PlatformIO IDE for VS Code](https://docs.platformio.org/en/latest/integration/ide/pioide.html), or [PlatformIO Core CLI](https://docs.platformio.org/en/latest/core/index.html)
- A USB data cable and the serial driver required by the ESP32 board
- An Ethernet cable and a network segment where a switch or router sends LLDP or CDP advertisements
- A Chromium-based browser with Web Bluetooth support for the dashboard

PlatformIO reads the environment and dependency constraints from [`../platformio.ini`](../platformio.ini) and installs missing packages during the first build. The current environment is `esp32dev` with the Arduino framework.

## Get the source

```bash
git clone https://github.com/jenglue/esp32-w5500-l2discover.git
cd esp32-w5500-l2discover
```

The repository's default branch may change over time. Check out the branch you intend to build before running PlatformIO commands.

## Build and upload

Compile the firmware without touching the board:

```bash
pio run
```

Upload the default environment:

```bash
pio run --target upload
```

List connected serial devices when PlatformIO cannot select a port automatically:

```bash
pio device list
```

Then select a port explicitly, for example:

```bash
pio run --target upload --upload-port COM7
```

The project uses the `esp32dev` environment from [`../platformio.ini`](../platformio.ini). To be explicit, use:

```bash
pio run --environment esp32dev
pio run --environment esp32dev --target upload
```

## Serial monitor

The firmware calls `Serial.begin(115200)`. Start the monitor with:

```bash
pio device monitor --baud 115200
```

For a specific port:

```bash
pio device monitor --port COM7 --baud 115200
```

Typical startup messages include OLED initialization, W5500 reset and SPI setup, BLE initialization, and DHCP status. Useful prefixes are `[OLED]`, `[SPI]`, `[ETH]`, `[DHCP]`, `[BLE]`, `[PKT]`, `[LLDP]`, and `[CDP]`.

## Local dashboard

The dashboard is a static HTML file. Serve the repository root so that the browser can load [`../src/webbt.html`](../src/webbt.html):

```bash
# Windows with Python installed
py -m http.server 8000

# Other environments may use
python3 -m http.server 8000
```

Open <http://localhost:8000/src/webbt.html> in a browser with Web Bluetooth support. The browser must allow the page to use Bluetooth; `localhost` is treated as a secure context for local development. The page filters for the BLE device named `T-Lite-Sniffer`.

The hosted dashboard is <https://jenglue.github.io/esp32-w5500-l2discover/>. It is the same page as [`../src/webbt.html`](../src/webbt.html), deployed as `public/index.html` by [`../.github/workflows/pages.yml`](../.github/workflows/pages.yml).

## GitHub Pages deployment

The Pages workflow:

1. Runs on pushes to the `develop` branch or through `workflow_dispatch`.
2. Checks out the repository.
3. Copies `src/webbt.html` to `public/index.html`.
4. Uploads the artifact and deploys it with GitHub Pages.

Verify a deployment from the [repository Actions page](https://github.com/jenglue/esp32-w5500-l2discover/actions/workflows/pages.yml). A successful workflow does not change the firmware; the board's QR code points to the hosted dashboard URL already compiled into [`../src/main.cpp`](../src/main.cpp).

## Firmware code map

The firmware implementation is currently concentrated in [`../src/main.cpp`](../src/main.cpp):

| Area | Entry point | Responsibility |
| --- | --- | --- |
| Hardware and services | `setup()` | OLED, button, W5500 reset/SPI, Ethernet, and BLE initialization |
| Main control loop | `loop()` | Button debounce, page timing, link changes, DHCP handoff, packet reception, and UI rendering |
| DHCP | `dhcpTask()` and `startDhcpTask()` | Runs DHCP on an ESP32 background task and reports the resulting address |
| MACRAW handoff | `openMacrawSocket()` and `closeMacrawSocket()` | Closes or opens socket 0 around DHCP and configures MACRAW mode |
| Link state | `handleEthernetLink()` | Clears state on link-down and starts DHCP on link-up |
| BLE output | `initBLE()` and `updateBLE()` | Creates the GATT service and publishes the five-field discovery payload |
| OLED UI | `drawUI()` and `drawQRCode()` | Renders the four pages and the hosted dashboard QR code |
| Protocol parsing | `parseLLDP()` and `parseCDP()` | Extracts the supported switch, port, and VLAN fields |

The detailed packet and BLE contracts are in [`PROTOCOL.md`](PROTOCOL.md).

## Runtime sequence

1. `setup()` initializes the OLED and button.
2. The W5500 reset pulse runs on GPIO 4.
3. SPI starts with the current firmware pin map, Ethernet CS is set to GPIO 5, and one W5500 socket is configured with `Ethernet.init(1)`.
4. Ethernet is initialized with the firmware MAC and an initial `0.0.0.0` address.
5. If the PHY link is up, DHCP runs on Core 0. The MACRAW socket is closed while DHCP uses the W5500.
6. When DHCP completes, socket 0 is opened in MACRAW mode and the main loop resumes raw frame processing.
7. Matching LLDP or CDP frames update the latest discovery state, OLED pages, and the BLE characteristic.

The code uses delays during the startup reset sequence, but the main loop uses timer-based state transitions and does not call `delay()`.

## Troubleshooting

### `pio` is not recognized

Use the PlatformIO IDE terminal, install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html), or restart the shell after changing the PATH.

### No serial device is listed

Run `pio device list`, use a USB data cable, check the board's serial driver, and close any terminal or IDE that already owns the port.

### Upload cannot connect

Select the port explicitly with `--upload-port`. If the board requires manual bootloader entry, use its hardware boot procedure. A failed upload does not indicate a W5500 or Ethernet problem.

### OLED is blank

Check power and the current firmware wiring: SDA=GPIO 21 and SCL=GPIO 22. The display is initialized as an SSD1306 128x64 hardware-I2C device.

### No Ethernet link or DHCP address

Check the Ethernet cable, switch port, W5500 power, CS=GPIO 5, RST=GPIO 4, and SPI pins. DHCP starts only after `Ethernet.link()` reports a link. The IP page shows `DHCP pending...` until the background task finishes.

### No LLDP or CDP result

The firmware is a passive listener. Confirm that the connected switch or router sends the relevant discovery protocol on that port. The parser only supports the fields listed in [`PROTOCOL.md`](PROTOCOL.md) and keeps the latest matching result.

### BLE dashboard cannot find the device

Confirm that setup completed, Bluetooth is enabled on the host, and the browser supports Web Bluetooth. The exact advertised name is `T-Lite-Sniffer`. For browser security and compatibility details, see the [MDN Web Bluetooth documentation](https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API).

### Pages shows an old dashboard

Check the latest run of the [Pages workflow](https://github.com/jenglue/esp32-w5500-l2discover/actions/workflows/pages.yml). The workflow deploys from `develop`, not from every branch.

## Tests and validation

The `test/` directory currently contains only `.gitkeep`; there are no executable PlatformIO tests yet. The minimum validation for a documentation or firmware change is:

```bash
git diff --check
pio run
```

Use `pio test` only when test cases have been added to the project.

## Related documentation

- [`../README.md`](../README.md) - project overview and quick start
- [`PROTOCOL.md`](PROTOCOL.md) - current MACRAW, LLDP/CDP, OLED, and BLE contracts
- [`../platformio.ini`](../platformio.ini) - environment and dependency constraints
- [`../src/main.cpp`](../src/main.cpp) - firmware source of truth
- [`../src/webbt.html`](../src/webbt.html) - dashboard source
- [`../.github/workflows/pages.yml`](../.github/workflows/pages.yml) - deployment source
