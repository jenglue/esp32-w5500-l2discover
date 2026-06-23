# ESP32 W5500 Layer-2 Network Discovery

Passive Layer-2 discovery firmware for an ESP32 + W5500 device. The project listens for LLDP and CDP frames, shows switch/port details on a small OLED, and exposes the latest discovery result over BLE for a simple Web Bluetooth dashboard.

## Features

- Passive LLDP and CDP discovery using W5500 MACRAW mode
- OLED status UI with topology, IP status, traffic, and QR-code pages
- BLE advertisement as `T-Lite-Sniffer`
- BLE characteristic payload format: `SwitchName|PortID`
- Background DHCP task so the UI stays responsive
- Simple browser dashboard in `src/webbt.html`

## Hardware and software stack

- ESP32-based target
- W5500 Ethernet controller
- SSD1306 128x64 OLED
- PlatformIO + Arduino framework
- Libraries:
  - `Ethernet3`
  - `U8g2`
  - `QRCode`
  - `NimBLE-Arduino`

## Repository layout

- `src/main.cpp` - firmware entry point
- `src/webbt.html` - Web Bluetooth viewer
- `platformio.ini` - PlatformIO environment and library dependencies

## Build and flash

1. Install [PlatformIO](https://platformio.org/).
2. Connect the board to your computer.
3. Build the firmware:

   ```bash
   pio run
   ```

4. Upload the firmware:

   ```bash
   pio run --target upload
   ```

5. Open the serial monitor:

   ```bash
   pio device monitor
   ```

## How it works

1. The firmware initializes the OLED, Ethernet controller, and BLE service.
2. DHCP runs in a background task.
3. The W5500 receives raw Ethernet frames and looks for:
   - LLDP (`0x88CC`)
   - CDP (`01:00:0C`)
4. When discovery data is found, the device updates:
   - OLED switch name, port, and VLAN display
   - BLE characteristic value for external clients

## OLED pages

The boot button cycles through four pages:

1. Topology
2. IP status
3. Analytics
4. QR / dashboard

## Web Bluetooth dashboard

Serve `src/webbt.html` from a local web server on `localhost`, then open it in a Web Bluetooth-capable browser and connect to the BLE device named `T-Lite-Sniffer`.

Example:

```bash
python3 -m http.server
```

Then browse to `http://localhost:8000/src/webbt.html`.

> Note: Web Bluetooth usually requires HTTPS or `localhost`, and works best in Chromium-based browsers.

## Current limitations

- The dashboard QR target is still a placeholder URL in firmware.
- Discovery depends on nearby network equipment sending LLDP or CDP frames.
- The project currently reports the latest observed device/port only.
