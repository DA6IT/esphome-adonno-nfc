# Android NFC HCE for Adonno TagReader

**ESPHome + PN532 I²C · Android Host Card Emulation · ISO-DEP / APDU · Home Assistant**

[![Build and test](https://github.com/DA6IT/esphome-adonno-nfc/actions/workflows/ci.yaml/badge.svg)](https://github.com/DA6IT/esphome-adonno-nfc/actions/workflows/ci.yaml)
[![ESPHome 2026.9.1](https://img.shields.io/badge/ESPHome-2026.9.1-03A9F4)](https://esphome.io/)
[![License GPLv3](https://img.shields.io/badge/License-GPLv3-blue)](LICENSE)
[![Hardware tested](https://img.shields.io/badge/Hardware-tested%20prototype-orange)](docs/TESTING.md)

Make an existing **[Adonno NFC TagReader](https://github.com/adonno/tagreader)** recognize the **stable card number from a compatible Android HCE app**, instead of relying on the phone's changing NFC UID. This open-source **ESPHome external component** extends the **PN532 I²C reader** with ISO 14443-4 / ISO-DEP APDU support and forwards a valid identifier through the **existing Home Assistant tag workflow**.

**Good news:** physical MIFARE Ultralight cards still work, and you do **not** need to fork the original Adonno firmware, replace your Home Assistant automations or add a second PN532 scanner.

> **Status: hardware-tested prototype.** Successful physical-card and Android-HCE scans were verified on an ESP8266 Adonno reader with ESPHome 2026.9.1. A second matching reader boots and reconnects with the extension, but its NFC/Node-RED workflow has not yet been tested on-site. This is **not** a secure, production-certified access-control solution.

## Quick start: add one ESPHome block

Already using the [Adonno TagReader](https://github.com/adonno/tagreader) with **ESP8266 + PN532 over I²C**? Keep your existing configuration and add this **top-level** YAML block:

~~~yaml
external_components:
  - source: github://DA6IT/esphome-adonno-nfc@f89b5ef7f21067ecf64ed1b2a77bb322b8541df4
    components: [pn532_i2c]
~~~

**Do not nest `external_components` under `wifi`.** If the key exists already, add this source to the existing list. Keep the original Adonno `packages`, API encryption, Wi-Fi, OTA and reader settings intact.

**Before updating:** back up the original configuration and obtain a working restore image. Compile the new firmware first and test it on a spare reader. A failed OTA installation can require USB/serial recovery. The Git commit above is intentionally pinned to the **hardware-tested source**.

**→ [Full installation, checks and rollback guide](docs/INSTALL-TEST.md)**

## Why this extension exists

A phone running **Android NFC Host Card Emulation (HCE)** is not the same as a physical NFC tag. Android may assign it a *different UID each scan*. To obtain an app-defined card number reliably, the NFC reader must talk to the phone using **ISO-DEP / ISO 7816-4 APDU commands**.

This project adds that conversation and keeps the rest of Adonno's tag processing in place.

| | Original Adonno workflow | With this extension |
| --- | --- | --- |
| Physical MIFARE Ultralight | UID / NDEF | **Unchanged** |
| Compatible Android HCE app | Randomized phone UID may be seen | **Reads app-provided card number via APDUs** |
| Home Assistant integration | Existing tag-scanned events | **Same tag integration** |
| Unsupported ISO-DEP cards | Not an app-defined membership number | **Ignored rather than reporting a random UID** |
| Reader configuration | Adonno ESPHome package | **Same package + one external component** |

### How the NFC scan reaches Home Assistant

~~~mermaid
flowchart LR
    A["Android HCE app<br/>AID + APDU card number"] --> B["Adonno TagReader<br/>ESP8266 + PN532 I²C"]
    P["Physical NFC card<br/>UID / NDEF"] --> B
    B --> C["Original Adonno<br/>tag handling"]
    C --> D["Home Assistant<br/>tag_scanned"]
~~~

For a successful Android scan, the reader creates a **temporary, in-memory** Home Assistant NDEF URI with the validated card number. It does **not** write anything to the phone. Existing physical-card behavior remains available.

## Compatibility and real-world results

| Test / platform | Result |
| --- | --- |
| ESPHome **2026.9.1**, ESP8266, PN532 I²C | **Compiled and tested** |
| Existing Adonno LED, buzzer, Wi-Fi and HA API | **Working** |
| Physical MIFARE Ultralight card | **Working** |
| Android HCE SELECT + GET card number | **Working** |
| Correct card number displayed in Home Assistant | **Confirmed** |
| Android after app's 60-second presentation window | **No visible reader response** |
| Second matching ESP8266 reader | **Updated, boots and reconnects; NFC not yet tested there** |
| Node-RED end-to-end, repeated-scan stress tests | **Pending** |
| ESP32, PN532 SPI, other ESPHome versions, iPhone HCE | **Not verified** |

The initial Android hardware test included an unsuccessful first AID SELECT and successful subsequent attempt, plus ESPHome PN532 timing warnings. See [the test report](docs/TESTING.md) for honest limitations and regression checks.

## Using your own Android HCE app

**This repository is the reader extension, not an Android app.** It is **not** compatible with every phone, wallet, payment card or HCE implementation out of the box. Your Android `HostApduService` must support the **currently fixed application protocol**:

| Reader command | Phone reply |
| --- | --- |
| SELECT AID `F053564E4D43415244` | `90 00` |
| GET card number `80 CA 00 00 00` | UTF-8 card number + `90 00` |

Accepted IDs contain **4, 7 or 10 hexadecimal byte pairs separated by hyphens**, such as the *fictional* `04-AA-BB-CC-DD-EE-FF`. The AID, APDU commands and validation rules are currently fixed in the component source, **not configurable in YAML**.

If your Android app has a 60-second presentation mode, **the app** manages that timeout. The reader just accepts or rejects its APDU responses.

**→ [Android HCE / ISO-DEP / APDU integration specification](docs/HCE-PROTOCOL.md)**

## Frequently asked questions

**Will it work with any Android phone?**  
Only if an installed Android app implements the expected **HCE AID and APDU protocol** and NFC/HCE is available and active. Installing this reader component alone cannot make an arbitrary phone present a card number.

**Does this break physical NFC cards?**  
The tested MIFARE Ultralight UID path continued working, including Adonno's green LED and success tone. Other card types, NDEF writing/cleaning and long-term regressions still need testing.

**Does it require changes to Home Assistant or Node-RED?**  
The confirmed reader-side path uses the original Adonno **Home Assistant tag-scanned** integration. A specific downstream Node-RED integration must still be checked against its own matching rules.

**Can an iPhone read an Android HCE card?**  
An iOS app can use Core NFC ISO 7816 APDU reading where supported, but the app must explicitly implement it. This repository does not add that feature to an iPhone app. **Presenting an iPhone as an HCE card** is a different, Apple-entitlement-dependent capability and is not provided here.

**Can I change the AID or use a different card-number format?**  
Not through YAML today. You would need to adapt the source and rerun parser, firmware and hardware tests. Contributions to make this configurable are welcome.

**Is this safe enough to unlock doors?**  
Not by itself. The transmitted identifier is **static and replayable**; it is not cryptographic proof of authorization. Keep appropriate independent authentication and access controls.

## Documentation

- **[Install, compile and restore](docs/INSTALL-TEST.md)** — for existing ESPHome Adonno owners
- **[Android HCE / APDU protocol](docs/HCE-PROTOCOL.md)** — for developers implementing a compatible Android app
- **[Hardware tests and open checks](docs/TESTING.md)** — what is verified and what is not
- **[Contributing](CONTRIBUTING.md)** — bug reports and pull requests
- **[Security](SECURITY.md)** — limitations and responsible reporting
- **[Upstream licensing and changes](docs/UPSTREAM.md)** — ESPHome origins and credits

## Community and license

Found this while searching for **ESPHome PN532 Android HCE**, **Adonno TagReader smartphone NFC**, **Home Assistant NFC phone card** or **ISO-DEP APDU reader support**? That's exactly what this experimental integration is for. Please **[open an issue](https://github.com/DA6IT/esphome-adonno-nfc/issues)** with hardware details and *sanitized* logs, or propose improvements via a pull request.

**Do not post** real member/card numbers, API keys, Wi-Fi secrets, private IP addresses or unredacted production logs.

Independent community project, **not officially supported by Adonno or ESPHome**. The modified ESPHome C++ component is GPL-3.0; the upstream Python binding retains its MIT license. See [LICENSE](LICENSE), [LICENSES/MIT.txt](LICENSES/MIT.txt) and [docs/UPSTREAM.md](docs/UPSTREAM.md).
