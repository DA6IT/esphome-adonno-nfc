# ESPHome Adonno NFC — Android HCE / ISO-DEP for PN532 TagReader

[![Build and test](https://github.com/DA6IT/esphome-adonno-nfc/actions/workflows/ci.yaml/badge.svg)](https://github.com/DA6IT/esphome-adonno-nfc/actions/workflows/ci.yaml)
[![ESPHome](https://img.shields.io/badge/ESPHome-2026.9.1-blue)](https://esphome.io/)
[![GPLv3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)

**Use an Adonno TagReader to read an Android phone's NFC Host Card Emulation (HCE) card number.** This community ESPHome external component adds **ISO-DEP / ISO 14443-4 APDU** support to the **PN532 I²C reader**, while preserving the familiar physical NFC tag workflow in **Home Assistant**.

> **Early hardware-tested prototype — not a security-certified access-control system.** Validated on one ESP8266/PN532 Adonno reader with ESPHome 2026.9.1. Keep a known-good firmware backup and recovery plan.

## The problem it solves

The stock [Adonno TagReader](https://github.com/adonno/tagreader) detects Android phones as NFC targets, but their **randomized UID is not an application card number**. A compatible Android HCE app instead exposes its identifier through an **APDU conversation**.

This component adds that missing reader-side support:

- **Physical MIFARE Ultralight tags:** existing UID/NDEF behavior retained.
- **Compatible Android HCE phones:** select a fixed application AID, request and validate the card number.
- **Home Assistant:** publish a successful card number through the **unchanged Adonno tag-scanned workflow**.
- **Unsupported ISO-DEP / failed APDU exchange:** do not forward a random phone UID as a successful tag.

There is **no Adonno firmware fork** and no second competing PN532 scanner.

## Quick install — existing ESPHome Adonno reader

Keep your **existing Adonno package, Wi-Fi, API encryption and OTA configuration**. Add this at the **top level** of the ESPHome YAML:

~~~yaml
external_components:
  - source: github://DA6IT/esphome-adonno-nfc@f89b5ef7f21067ecf64ed1b2a77bb322b8541df4
    components: [pn532_i2c]
~~~

If an `external_components:` section already exists, add this entry to its list rather than defining the key a second time. The source is pinned to the **hardware-tested implementation**, so future `main` commits cannot silently change your firmware.

**Before flashing:** save the original configuration and firmware, compile first, and have a realistic USB/serial recovery plan if OTA stops working.

**[Step-by-step installation and rollback](docs/INSTALL-TEST.md)**



## Android app compatibility — important

This is a **reader component only**, **not** a ready-to-install Android app or a universal NFC wallet reader. Your Android app must implement `HostApduService` using the protocol below.

| Command from reader | Expected phone response |
| --- | --- |
| ISO 7816-4 SELECT AID `F053564E4D43415244` | `90 00` |
| GET card number `80 CA 00 00 00` | UTF-8 identifier followed by `90 00` |

Supported identifiers are **4, 7 or 10 two-digit hexadecimal byte groups**, separated by hyphens, e.g. `04-AA-BB-CC-DD-EE-FF` (fabricated example). Lowercase hex is normalized to uppercase; invalid responses or status words are rejected.

**The AID, APDUs and accepted number format are fixed in the current source**, not configurable in ESPHome YAML. Other HCE apps must implement this same protocol or modify and retest the reader source. A 60-second presentation window, if used, is enforced **by the phone app**, not by the reader firmware.

**[Full Android HCE / APDU protocol](docs/HCE-PROTOCOL.md)**

## Compatibility and verified results

| Function / environment | Status |
| --- | --- |
| ESPHome **2026.9.1** · ESP8266/D1 mini · PN532 I²C | **Compiled and tested on a real Adonno reader** |
| Original Adonno package, Wi-Fi, Home Assistant API, LED and buzzer | **Working in live test** |
| Physical MIFARE Ultralight UID read | **Working in live test** |
| Android HCE SELECT + GET card number | **Working in live test** |
| Correct card number visible in Home Assistant | **Confirmed** |
| Re-presenting Android phone after its 60-second app window | **No visible reader response** |
| Node-RED downstream behavior, duplicate-event stress testing | **Not yet fully verified** |
| Other ESPHome versions, ESP32, PN532 SPI, iPhone HCE | **Not tested** |

The initial field test also showed one failed SELECT before a successful retry and PN532 operation-time warnings (127–188 ms). Longer-term reliability and full physical tag read/write regression tests remain open.

**[Test details and remaining checks](docs/TESTING.md)**

## How it works

Only ESPHome's `pn532_i2c` component is overridden. ESPHome's PN532 core and the original Adonno package remain in place:

1. Recognize ISO-DEP targets using the PN532 SAK data.
2. Send PN532 `InDataExchange` APDUs to select the application and request a card number.
3. Validate the response and create an **in-memory** Home Assistant NDEF URI: `https://www.home-assistant.io/tag/<CARD_NUMBER>`.
4. Let Adonno's existing `on_tag` automation forward the identifier as a Home Assistant tag.

Nothing is written to the phone. Physical non-ISO-DEP tags continue through the normal PN532 read path. The custom scan loop is **version-specific to ESPHome 2026.9.1**; review before upgrading.

## Security and limitations

- A **static card number can be copied and replayed**. It is not cryptographic proof of identity. Do not use it alone to authorize physical access, payments or other sensitive actions.
- Diagnostic logs may show a phone's randomized UID, but the **published Home Assistant tag ID** comes from the validated APDU response.
- The extension currently targets **ESP8266 + PN532 I²C** only. Other boards and iPhone support need separate testing.
- Start with a **spare test reader**, not a production installation.

## Contribute and report issues

Bug reports, compatible HCE implementations, reproducible tests and pull requests are welcome via **[GitHub Issues](https://github.com/DA6IT/esphome-adonno-nfc/issues)**. Read [CONTRIBUTING.md](CONTRIBUTING.md) and [SECURITY.md](SECURITY.md).

**Please never post API keys, Wi-Fi credentials, real card numbers, private IP addresses or unredacted logs.**

## Credits and license

Independent community project; **not officially affiliated with Adonno or ESPHome**. Based on the [Adonno TagReader](https://github.com/adonno/tagreader) and [ESPHome](https://esphome.io/).

The modified C++ components are GPL-3.0; the included upstream Python binding retains its MIT license. See [LICENSE](LICENSE), [LICENSES/MIT.txt](LICENSES/MIT.txt) and [upstream attribution](docs/UPSTREAM.md).
