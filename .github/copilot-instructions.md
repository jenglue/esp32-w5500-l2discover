# Copilot Instructions for ESP32 W5500 Layer-2 Discovery Project

You are an expert embedded systems developer specializing in ESP32 and W5500 Ethernet controllers.

## Hardware Context
- **Target:** ESP32 paired with a W5500 controller and SSD1306 OLED.
- **Ethernet Chip:** W5500 (SPI Interface).
- **Display:** Built-in SSD1306 OLED (128x64) via I2C.
- **Button:** Built-in Boot button on GPIO 0.

## Current Firmware Pin Mapping

These values match the definitions currently compiled in `src/main.cpp`. They are the documentation baseline until the physical board revision is verified.

- **W5500 SPI:** SCLK=18, MISO=23, MOSI=19, CS=5.
- **W5500 Control:** RST=4.
- **OLED I2C:** SDA=21, SCL=22.
- **Button:** GPIO 0 with `INPUT_PULLUP`.

The current firmware does not configure a separate W5500 enable pin. Do not add GPIO 12 power-control instructions unless the hardware and firmware are changed together.

## Coding Standards
1. **Library Usage:** - Use `Ethernet3.h` (by sstaub) for W5500 features used by this project, including MACRAW mode and PHY status.
   - Use `U8g2lib.h` for OLED display (Hardware I2C preferred).
   - Use `QRCode.h` for generating QR codes on the OLED.
   - The current firmware uses the ESP32 Arduino BLE headers (`BLEDevice.h`, `BLEServer.h`, and related headers). Do not describe it as a NimBLE API migration unless the code is migrated explicitly.

2. **Initialization Sequence:** - Initialize the OLED and button, then pulse the W5500 hardware reset on `GPIO 4`.
   - Start SPI with SCLK=18, MISO=23, and MOSI=19.
   - Call `Ethernet.setCsPin(5)` and `Ethernet.init(1)` before Ethernet setup.
   - Initialize Ethernet with the firmware MAC and `0.0.0.0`, run DHCP in the background when the link is up, and open socket 0 in MACRAW mode after DHCP completes.

3. **Performance:** - Use non-blocking code. Avoid `delay()` in the `loop()`.
   - Use `millis()`-based timers for button debounce, page transitions, marquee movement, and BLE synchronization.

4. **UI Design:** - Screen resolution is 128x64. Use scannable fonts (e.g., `u8g2_font_6x12_tf`).
   - For QR Codes, ensure a 2x2 pixel scale for better scannability by smartphones.

## Bluetooth Specifications
- **Device Name:** "T-Lite-Sniffer".
- **Service UUID:** "4fafc201-1fb5-459e-8fcc-c5c9c331914b".
- **Characteristic UUID:** "beb5483e-36e1-4688-b7f5-ea07361b26a8".
- **Data Format:** Five pipe-separated fields: `SwitchName|PortID|VLAN|IP|Gateway`.

## Documentation Source of Truth

- `src/main.cpp` is the source of truth for current pin definitions, initialization order, runtime behavior, and parser support.
- `platformio.ini` is the source of truth for dependency constraints and the PlatformIO environment.
- `src/webbt.html` is the source of truth for the dashboard's BLE connection and payload decoding.
- `docs/DEVELOPMENT.md` and `docs/PROTOCOL.md` contain the maintained development workflow and current data contracts.
- If physical board documentation disagrees with the current firmware, record a separate hardware/firmware change instead of silently changing these instructions.

## Git Workflow

### Conventional Commits
All commit messages **must** follow the [Conventional Commits](https://www.conventionalcommits.org/) specification:

```
<type>[optional scope]: <description>

[optional body]

[optional footer(s)]
```

**Allowed types:**
- `feat`: A new feature or capability.
- `fix`: A bug fix.
- `refactor`: Code change that neither fixes a bug nor adds a feature.
- `docs`: Documentation only changes.
- `test`: Adding or updating tests.
- `chore`: Build process, tooling, or dependency updates.
- `perf`: A code change that improves performance.
- `style`: Formatting, missing semicolons, etc. (no logic change).

**Examples:**
```
feat(ble): add BLE characteristic for port discovery results
fix(ethernet): correct W5500 PHY reset timing on GPIO 4
chore: update platformio.ini with Ethernet3 library version
```

### Commit Frequency
- **Commit early and often** — each logical unit of work (e.g., one feature, one fix, one refactor) should be its own commit.
- Never batch unrelated changes into a single commit.
- After completing each step of a multi-step task, commit before moving on.

### GitFlow Branch Strategy
For multi-step or non-trivial requirements, always create a dedicated feature branch before starting work:

| Branch pattern | Purpose |
|---|---|
| `main` | Stable, release-ready code only. |
| `develop` | Integration branch; merge feature branches here first. |
| `feature/<short-description>` | New features or enhancements (branch off `develop`). |
| `fix/<short-description>` | Bug fixes (branch off `develop` or `main` for hot-fixes). |
| `chore/<short-description>` | Tooling, deps, CI changes (branch off `develop`). |

**Workflow for a multi-step task:**
1. `git checkout develop && git pull` — sync with latest.
2. `git checkout -b feature/<short-description>` — create feature branch.
3. Implement each step, committing after every logical unit.
4. Open a Pull Request into `develop` when the feature is complete.
5. Delete the feature branch after merging.