# Copilot Instructions for LilyGO T-ETH-Lite (W5500) Project

You are an expert embedded systems developer specializing in ESP32 and W5500 Ethernet controllers.

## Hardware Context
- **Board:** LilyGO T-ETH-Lite (ESP32-PICO-D4 based).
- **Ethernet Chip:** W5500 (SPI Interface).
- **Display:** Built-in SSD1306 OLED (128x64) via I2C.
- **Button:** Built-in Boot button on GPIO 0.

## Critical Pin Mapping (DO NOT CHANGE)
- **W5500 SPI:** MOSI=23, MISO=19, SCLK=18, CS=2.
- **W5500 Control:** RST=4, **EN=12 (Must be HIGH to power the chip)**.
- **OLED I2C:** SDA=13, SCL=15.

## Coding Standards
1. **Library Usage:** - Use `Ethernet3.h` (by sstaub) for advanced W5500 features like MACRAW mode and PHY status.
   - Use `U8g2lib.h` for OLED display (Hardware I2C preferred).
   - Use `QRCode.h` for generating QR codes on the OLED.
   - Use `NimBLE-Arduino` or standard `BLEDevice.h` for Bluetooth LE functionality.

2. **Initialization Sequence:** - Always set `GPIO 12` to `HIGH` first to power the W5500.
   - Perform a hardware reset on `GPIO 4` before calling `Ethernet.init()`.

3. **Performance:** - Use non-blocking code. Avoid `delay()` in the `loop()`.
   - Implement `millis()` based timers for UI refreshing (target 10-20 FPS).

4. **UI Design:** - Screen resolution is 128x64. Use scannable fonts (e.g., `u8g2_font_6x12_tf`).
   - For QR Codes, ensure a 2x2 pixel scale for better scannability by smartphones.

## Bluetooth Specifications
- **Device Name:** "T-Lite-Sniffer".
- **Service UUID:** "4fafc201-1fb5-459e-8fcc-c5c9c331914b".
- **Characteristic UUID:** "beb5483e-36e1-4688-b7f5-ea07361b26a8".
- **Data Format:** Pipe-separated strings (e.g., "SwitchName|PortID").

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