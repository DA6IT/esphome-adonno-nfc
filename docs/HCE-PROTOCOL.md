# Android Host Card Emulation (HCE) / ISO-DEP APDU protocol

The reader supports a **specific Android HCE protocol**. It is **not** a universal NFC payment, wallet or NFC Forum Type 4 NDEF reader, and this repository does **not** include a ready-made Android app.

## APDU command sequence

1. Detect ISO 14443-4 / ISO-DEP using PN532 `InListPassiveTarget`.
2. SELECT-by-name application AID: **`F053564E4D43415244`**.
3. Expect a response consisting exactly of **`90 00`**.
4. Send the card-number request: **`80 CA 00 00 00`**.
5. Expect **UTF-8 card number + `90 00`**.

Example of the exchange (with a **fabricated** identifier):

~~~text
Reader -> phone: 00 A4 04 00 09 F0 53 56 4E 4D 43 41 52 44 00
Phone  -> reader: 90 00

Reader -> phone: 80 CA 00 00 00
Phone  -> reader: 04-AA-BB-CC-DD-EE-FF (as UTF-8) + 90 00
~~~

The reader rejects missing/invalid data and any status other than `90 00`. The AID and APDUs are **currently hardcoded**, not YAML settings.

## Card number format

Accepted: exactly **4, 7, or 10 pairs** of hexadecimal characters, separated by ASCII hyphens (`-`). Examples: `01-AB-CD-EF` and `04-AA-BB-CC-DD-EE-FF`. Uppercase and lowercase hex input are allowed; the reader normalizes to uppercase.

Unaccepted: strings containing spaces, slashes, URI prefixes, other punctuation, unsupported lengths or nonhexadecimal characters.

## Android app requirements

Implement an Android `HostApduService` that registers this AID, returns `90 00` for the matching SELECT, and returns the active card number plus `90 00` for the GET request.

An app can return a failure status such as `69 85` if card presentation is not active. **The app controls its own presentation window** (e.g. 60 seconds); the reader does not independently enforce that duration.

If your HCE app uses another AID, APDU or identifier format, adapt and retest the reader code before expecting interoperability.

## Home Assistant integration

A successful response produces an **in-memory** NDEF URI of this form:

~~~text
https://www.home-assistant.io/tag/04-AA-BB-CC-DD-EE-FF
~~~

The original Adonno `on_tag` logic recognizes the URI and emits the card number through Home Assistant's existing tag-scanned functionality. **No NDEF data is written to the phone.**

An Android phone's UID may change from scan to scan and is **not** a stable member identifier. Unsupported or unsuccessful ISO-DEP exchanges must be ignored rather than turning the phone UID into an accepted tag.

## Security notes

A **static APDU card number is copyable/replayable** and cannot, by itself, prove that a person is authorized. Use independent authorization and adequate security controls for entry/access decisions. See [SECURITY.md](../SECURITY.md).
