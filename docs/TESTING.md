# Tests, compatibility and remaining work

## Verified on real Adonno hardware — 2026-10-08

A single **ESP8266 + PN532 I²C** Adonno TagReader successfully ran the modified ESPHome **2026.9.1** firmware while preserving the original Adonno package.

**Observed successes:**

- Device boot, Wi-Fi, ESPHome native API, PN532 I²C connection, LED and buzzer.
- **Physical MIFARE Ultralight** scan through existing UID behavior. A missing-NDEF warning is expected for a UID-only tag; the reader still recognized it and played the success indication.
- **Android HCE** ISO-DEP/APDU exchange: first SELECT attempt failed, a retry succeeded and returned a validated member-format card number. A synthetic in-memory Home Assistant NDEF URI was recognized by original Adonno automation.
- **Home Assistant UI displayed the intended stable card number**, rather than using the randomized Android UID.
- Presenting the Android phone after the app's **60-second presentation window** yielded no visible/audible reader response.

**Important qualifications:** The screenshot shows the Home Assistant tag and its recent scan timestamp; it does not independently verify every downstream automation. The expired-window observation did not include a separate HA event trace. The application itself controls its 60-second window.

**Warnings observed:** A transient failed AID SELECT, and PN532 component-operation warnings (127 ms / 188 ms) during initial Android scanning. These warrant longer-term tests.

## Automated tests

[GitHub Actions CI](../.github/workflows/ci.yaml) runs on Linux and includes:

- Native C++ APDU parser tests with positive/negative responses.
- Validation and compile of an ESP8266/Adonno example using ESPHome **2026.9.1**.
- Validation and compile of the same component loaded from the **pinned public GitHub source**.

The CI has **no hardware flashing, OTA, deployment or credentials**. Passing CI does not establish real NFC behavior.

## Not fully tested yet

- Complete Home Assistant event-by-event and external Node-RED downstream processing.
- Repeat scans, rapid phone removal/re-presentation, duplicate-event prevention and extended runtime stability.
- Explicit HA event monitoring for an inactive/expired Android HCE app.
- Unsupported ISO-DEP cards, malformed responses and transport faults on actual hardware.
- Physical tag **NDEF read/write/clean** regression behavior, beyond tested Ultralight UID reading.
- Actual ESP8266 free heap, board flash characteristics and recovery after a bad OTA.
- ESP32, PN532 SPI, iPhone HCE, or other ESPHome versions.

## Minimum regression checklist for contributors

1. Compile using the pinned ESPHome release and check the image size.
2. Scan a physical MIFARE Ultralight tag and confirm correct UID-based Home Assistant behavior.
3. Present an Android HCE phone with the matching AID and APDU protocol.
4. Confirm the **stable APDU number**, not the phone UID, reaches Home Assistant.
5. Expire/disable HCE and test that no unauthorized tag is generated.
6. Repeat scans with removal between attempts; check for duplicates.
7. Confirm LED, buzzer, OTA, NDEF and any downstream integrations.
8. Log version/build conditions and **redact sensitive details** before posting.

See [HCE protocol](HCE-PROTOCOL.md) and [Security](../SECURITY.md). A fixed card number is **not** cryptographically secure access authentication.
