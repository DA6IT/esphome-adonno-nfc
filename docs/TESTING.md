# Test and release checklist

## Current status

- Offline implementation only; hardware APDU communication has **not yet been verified**.
- Linux CI compiles the ESP8266 example without secrets and runs a standalone parser test.
- CI does **not** flash, install, update, deploy, or connect to a reader.
- Supported ESPHome version is pinned to **2026.9.1**. No compatibility promise for newer versions.

## Hardware validation (explicit approval required)

Before flashing `tagreader-df6508`:
1. Back up the redacted device YAML, firmware image (when obtainable), relevant ESPHome build inputs and exact version information; determine the ESP8266 flash size.
2. Prepare a **verified** way back to the working Adonno firmware, including physical serial recovery if OTA no longer works.
3. Check compile results and image size; allow enough flash and heap headroom for ESP8266.
4. Only after separate approval flash **Reader 1**. The matching reader in the Naturbad and the newer ESP32 reader stay untouched.
5. Verify MIFARE Ultralight (existing UID path), HA scanned tag, LED, buzzer, NDEF, write/clean modes and OTA.
6. Verify supported Android HCE returns the correct fabricated `04-AA-BB-CC-DD-EE-FF` tag, one event only, during the presentation window.
7. Verify unsupported ISO-DEP cards, Android without active window, invalid SW, malformatted numbers, broken connection, rapid re-presentations and reboots **never** emit a random HCE UID.
8. Confirm Node-RED and downstream system see exactly the previously expected Home Assistant scanned-tag identity. No backend/Node-RED changes in this project without permission.

## Security boundaries

The protocol currently transmits a **static, replayable card number** without cryptographic authentication. These CI tests do not prove physical compatibility, safety for entry control, or resistance to cloned credentials. No production deployment until those questions are deliberately accepted or addressed.

## Notes

- Detection uses ISO-DEP bit `0x20` in PN532's SAK; unsupported ISO-DEP cards are deliberately ignored, not treated as physical UID cards.
- Number format is 4/7/10 two-digit hexadecimal octets separated by hyphens. If valid member card numbers differ, define the canonical format before relaxing this validation.
- A three-second debounce is currently applied to repeated successful HCE presentations of the same number, in addition to ESPHome UID deduplication.
