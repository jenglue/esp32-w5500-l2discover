# Protocol and Data Contracts

This document describes the subset of Ethernet discovery and BLE behavior implemented by the current firmware. It is an implementation reference, not a claim of complete LLDP or CDP support.

## Capture path

The W5500 uses socket 0 in MACRAW mode after the background DHCP task completes. The receive path in [`../src/main.cpp`](../src/main.cpp) is:

1. Read the W5500 received-size register.
2. Read up to 600 bytes from the socket buffer.
3. Treat the first two bytes as the MACRAW packet-length prefix.
4. Treat the remaining bytes as the Ethernet frame.
5. Scan the frame for the LLDP EtherType bytes or the CDP destination-MAC prefix.
6. Parse supported fields and update the latest discovery state.

DHCP and MACRAW do not use the W5500 socket at the same time. The firmware closes the MACRAW socket while DHCP runs and reopens it after the DHCP task reports completion.

The packet counter increments for every received MACRAW buffer that passes the minimum-size check, not only for frames that contain LLDP or CDP data.

## LLDP

The current parser recognizes the LLDP EtherType `0x88CC` while scanning a received Ethernet frame. It reads the LLDP payload as a sequence of two-byte Type-Length-Value headers:

```text
15-bit type | 9-bit length | value bytes
```

The supported TLVs are:

| TLV type | Current use |
| ---: | --- |
| 2 | Port ID. The first value byte is treated as the subtype; the remaining bytes become the displayed port value. |
| 4 | Port Description. The value replaces the displayed port value. |
| 5 | System Name. The value becomes the displayed switch name. |
| 127 | Organizationally specific TLV. When the OUI is `00:80:C2` and the subtype is `1`, the parser reads the following two bytes as the displayed VLAN ID. |
| 0 | End of LLDPDU. Stops the parser. |

The parser stores the latest values in the fixed-size buffers `swName`, `swPort`, and `swVlan`. It does not maintain a neighbor table, track TTL expiry, validate every mandatory TLV, or decode every optional/organizational TLV.

Reference: [IEEE 802.1AB](https://standards.ieee.org/ieee/802.1AB/4462) and [LLDP overview](https://en.wikipedia.org/wiki/Link_Layer_Discovery_Protocol).

## CDP

The current frame scanner detects the CDP destination prefix `01:00:0C`. The CDP parser assumes the Ethernet, length, LLC/SNAP, and CDP header occupy the first 26 bytes of the buffer passed to `parseCDP()`; it then reads CDP TLVs as:

```text
16-bit type | 16-bit length | value bytes
```

The supported CDP TLVs are:

| TLV type | Current use |
| ---: | --- |
| `0x0001` | Device ID. The value becomes the displayed switch name. |
| `0x0003` | Port ID. The value becomes the displayed port value. |
| `0x000A` | Native VLAN. The first two value bytes become the displayed VLAN ID. |

The parser stops when a TLV length is less than four bytes. It does not decode all CDP TLVs, validate the complete CDP header, or maintain historical neighbors.

References: [Cisco CDP technical note](https://www.cisco.com/c/en/us/support/docs/network-management/discovery-protocol-cdp/118736-technote-cdp-00.html) and [CDP overview](https://en.wikipedia.org/wiki/Cisco_Discovery_Protocol).

## Discovery state

The firmware keeps one current result:

| Field | Firmware storage | Initial or cleared value |
| --- | --- | --- |
| Switch name | `swName` | `Searching...` |
| Port | `swPort` | `Waiting LLDP/CDP...` |
| VLAN | `swVlan` | `N/A` |
| Packet count | `packetCount` | `0` |
| IP address | `Ethernet.localIP()` | `0.0.0.0` until DHCP provides an address |
| Gateway | `Ethernet.gatewayIP()` | `0.0.0.0` until DHCP provides a gateway |

A link-down event clears the discovery state and Ethernet address configuration. A later link-up event starts DHCP again.

## BLE GATT contract

The firmware advertises a BLE peripheral with the following identifiers:

| Property | Value |
| --- | --- |
| Device name | `T-Lite-Sniffer` |
| Service UUID | `4fafc201-1fb5-459e-8fcc-c5c9c331914b` |
| Characteristic UUID | `beb5483e-36e1-4688-b7f5-ea07361b26a8` |
| Characteristic properties | Read and notify |
| Encoding | UTF-8 text |
| Field separator | `|` |

The characteristic value has exactly five fields in this order:

```text
SwitchName|PortID|VLAN|IP|Gateway
```

For example:

```text
Access-SW-01|Gi1/0/24|20|192.0.2.42|192.0.2.1
```

The current implementation may publish values containing the startup strings shown in the discovery-state table. While DHCP is running, the IP and gateway fields are reported as `0.0.0.0`; after DHCP completes they come from `Ethernet.localIP()` and `Ethernet.gatewayIP()`.

`updateBLE()` assembles the string and calls `notify()` only when it differs from the last published value. The Web Bluetooth page in [`../src/webbt.html`](../src/webbt.html) decodes the notification, splits on `|`, and maps indexes 0 through 4 to switch name, port, VLAN, IP address, and gateway.

Reference: [MDN Web Bluetooth API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API).

## OLED and QR behavior

The OLED is a 128x64 SSD1306 display driven by U8g2. The boot button on GPIO 0 cycles through four pages:

1. Topology: switch name, port, and VLAN.
2. IP status: address, gateway, PHY link, and speed report.
3. Analytics: received packet count.
4. Dashboard: a version-3 QR code rendered at 2x scale.

The QR code encodes the hosted dashboard URL currently stored in `dashboardURL`:

```text
https://jenglue.github.io/esp32-w5500-l2discover/
```

The QR page is a convenience for opening the Web Bluetooth dashboard; BLE still requires a compatible browser and a secure context. The hosted page is deployed by [`../.github/workflows/pages.yml`](../.github/workflows/pages.yml).

## Compatibility and limitations

- LLDP and CDP support is limited to the fields listed above.
- The firmware is a passive listener; it does not transmit discovery frames.
- Results represent the latest observed matching frame only.
- Network devices may suppress LLDP/CDP on an access port or use a different discovery protocol.
- The BLE payload is positional text rather than a versioned JSON schema. Changes to field order or separators require coordinated firmware and dashboard changes.
- The current firmware uses the ESP32 Arduino BLE headers. The declared NimBLE-Arduino dependency does not by itself mean this data contract uses the NimBLE API.
