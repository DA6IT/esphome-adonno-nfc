# Tests, compatibility and remaining work

## Status per version

| Version | Automated (CI) | Real hardware |
| --- | --- | --- |
| ESPHome **2026.10.0** port (`7b19ffeea58cb037f0c99961cce434f883e3335d`) | Native logic tests, ESP8266 compile (minimal + adopted-reader structure + pinned remote source) | **Not tested yet.** Planned on the test reader before any other reader is updated. |
| ESPHome **2026.9.1** port (`f89b5ef`) | Restore build compiled in CI on every change | Tested 2026-10-08, see below |

## Verified on real Adonno hardware — 2026-10-08 (ESPHome 2026.9.1, `f89b5ef`)

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

[GitHub Actions CI](../.github/workflows/ci.yaml) runs on Linux, with **no hardware, flashing, OTA, deployment or credentials** (Wi-Fi/API values are throw-away dummies generated per run):

1. **Reader logic tests** (`tests/hce_protocol_test.cpp`, C++20, `-Werror`, ASan/UBSan). They exercise the same functions `PN532I2C::loop()` uses (`hce_protocol.h`):
   - APDU bytes: SELECT `00 A4 04 00 09 F0 53 56 4E 4D 43 41 52 44 00`, GET `80 CA 00 00 00`.
   - SELECT/GET flow against a fake ISO-DEP target: SVN HCE phone, foreign app (`6A 82`, GET never sent), silent/expired phone, SELECT with FCI data, GET transport failure, six malformed GET answers.
   - Card number format (4/7/10 groups, case normalisation, separators, status words, non-ASCII).
   - 3 s repeat window incl. boundary, other card numbers and `millis()` wrap-around.
   - InListPassiveTarget parsing: empty/no/two targets, truncated header, UID length 0/11/truncated.
   - Scenarios: physical Classic card held/removed/re-presented, Ultralight→Classic swap, Android HCE with changing random UIDs, first SELECT failing then succeeding, foreign ISO-DEP cards/phones/dual-interface `0x28`/expired app/malicious answer (**never an event, never a removal**), phone held in the field, transport anomalies (no duplicate, no false removal).
   - The test suite was checked by mutation: 10 deliberate logic faults (e.g. ISO-DEP detection off, removal without report, empty frame as removal, `<` → `<=` in the window, lenient SELECT) are all detected.
   - Limit: the tests drive the decision functions through a model of `loop()`; the thin ESPHome glue itself (`exchange_apdu_` framing, NDEF record creation, trigger dispatch, CLEAN/FORMAT/WRITE) is only compile-checked and needs the hardware checklist.
2. **ESPHome 2026.10.0** validate + compile for ESP8266 D1 mini: minimal example, an *adopted reader* structure (encrypted API, Wi-Fi from secrets) and the component loaded from the **pinned public GitHub commit** (`examples/tagreader-remote-ci.yaml`).
3. **ESPHome 2026.9.1** restore build (`f89b5ef` + Adonno master) and the version guard (current component must refuse 2026.9.1 with a clear message).

What CI cannot show: real RF timing, PN532 behaviour, phone/card interoperability, LED/buzzer, Home Assistant and Node-RED processing, heap at runtime, OTA.

## Known behaviour worth watching (unchanged from the 2026.9.1 port)

- **Phone held in the field:** RF is switched off after every poll, so Android re-activates with a new random UID on each poll (default every 1 s). While the app keeps presenting, the same card number can be reported again every ~3 s. Apps with a one-shot presentation stop after the first successful read.
- **Removal of an HCE phone** fires `on_tag_removed` only if the last poll before removal produced a report; after a suppressed repeat no removal event follows.
- **Dual-interface cards** (SAK `0x28`) are handled as ISO-DEP: without the SVN app they produce no event at all, even though upstream would read them as MIFARE Classic.

## Hardware acceptance checklist for the 2026.10 port (test reader)

Record results with redacted logs. Each item is **open** until performed on hardware.

1. Boot log: ESPHome `2026.10.0`, `Adonno HCE extension: 2.0.0 (ESPHome 2026.10)`, PN532 `0x24`, Wi-Fi/API connected, no boot loop, Safe Mode counter reset.
2. Physical **member card (MIFARE Classic 1K)**: one HA tag scan with the **same ID as with the 2026.9.1 firmware**, green LED + success tone; held for 5 s → no second event; removed → one `esphome.tag_removed`; re-presented → new scan. (Log shows `Mifare classic`; tag type now comes from SAK, see UPSTREAM.md.)
3. Physical **MIFARE Ultralight** card: same as 2.
4. **Android HCE** (SVN app, presentation active): HA shows the stable card number (not the phone UID); count events for a short tap and for holding the phone 10 s.
5. **Wrong ISO-DEP** (bank card, other HCE app, SVN app with expired window): no HA tag event, no LED/tone, no removal event.
6. Alternate card → phone → card quickly: one event each, no mix-up.
7. Home Assistant events/automations, and Node-RED if available, process the scans as before.
8. Wi-Fi/API reconnect after reader restart, LED/buzzer switches from HA, `rfidreader_tag_ok/ko` services, ESPHome log free of new warnings beyond the known PN532 timing notices.
9. Rollback rehearsal is optional; if anything regresses, restore 2026.9.1 per [INSTALL-TEST.md](INSTALL-TEST.md).

See [HCE protocol](HCE-PROTOCOL.md) and [Security](../SECURITY.md). A fixed card number is **not** cryptographically secure access authentication.
