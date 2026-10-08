# Staged setup on a real ESP8266 Adonno reader

This is a guide for a **controlled lab test**, not production instructions. The active Adonno YAML, the existing API encryption key, Wi-Fi settings, substitutions, OTA settings, custom I2C settings and local automations must be preserved.

## Stage A – before changing anything

1. Check this is **Reader 1 / `tagreader-df6508`**, not the matching reader in the Naturbad.
2. In ESPHome Device Builder, save a copy of the complete working YAML and any necessary `!secret` values to an **offline, private location** (do not place secrets in public repos or chat). Keep the project package revision, ESPHome version and installed reader settings.
3. Obtain a separate **known-good restore image**: build/download the current firmware in OTA format from the working YAML, or use a previous verified firmware image. A newly built image is *not* a byte-for-byte device flash backup; upstream package changes can make a rebuild differ from the currently installed image.
4. For a full image of the exact installed state, connect the ESP8266 over USB/serial and use an appropriate ESP flash read method; first determine actual flash size, and treat the resulting image as secret-bearing local data. Confirm that the USB serial recovery path and restore tool are available **before OTA**.
5. Test the normal physical card before changing the reader (record only a fake or redacted UID in shared notes).
6. Do not flash before a separate explicit go/no-go review.

## Stage B – candidate YAML (compile only first)

Keep the entire pre-existing YAML intact. Append exactly this block (it is additive and does not create a second NFC scanner):

```yaml
external_components:
  - source: github://DA6IT/esphome-adonno-nfc@f89b5ef7f21067ecf64ed1b2a77bb322b8541df4
    components: [pn532_i2c]
```

If `external_components:` already exists, **merge into its existing list** instead of declaring the key again. **Do not modify the original `packages:` Adonno reference** or Wi-Fi/API/OTA values for this first test.

Use ESPHome's config validation, then **Install → Manual download** to compile/download a candidate binary without installing it. Do not choose "Wirelessly"/"Plug into this computer" until separately approved.

The GitHub CI validates the exact component reference against ESPHome 2026.9.1 in `examples/tagreader-remote-ci.yaml`.

## Stage C – only after approved firmware update

1. Power Reader 1 from a stable supply; verify HA/ESPHome are working.
2. Install the tested candidate on Reader 1 only.
3. Immediately verify LED, buzzer, OTA connection, original physical card / UID / NDEF and HA forwarding.
4. Open the Android SVN app member card, activate its presentation window, present the phone, and check that the **actual EasyVerein card number**, not the random HCE UID, is reported. Share *redacted* logs only.
5. Test inactive HCE, an unrelated ISO-DEP card, repeated presentations, card removal and a restart. Failure must not emit a random HCE UID.
6. If behavior regresses, stop and restore the known-good firmware through OTA if available, or USB/serial if OTA stops working. Do not use the Naturbad reader for testing.

All NFC card numbers in example documentation are fabricated. Static card numbers do **not** provide cryptographic clone/replay protection.
