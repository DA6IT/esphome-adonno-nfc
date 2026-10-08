# Install and restore — Adonno ESP8266 / PN532 I²C

This is a step-by-step guide for an **existing ESPHome Adonno TagReader** with an **ESP8266 + PN532 over I²C**, using **ESPHome 2026.9.1**. Other hardware and ESPHome versions are not yet validated.

## Before changing anything

1. In **ESPHome Device Builder → Edit**, back up the working reader YAML privately. Keep any referenced `!secret` entries available; **do not publish credentials or real card IDs**.
2. Download a **known-good firmware image** compiled from the previous working configuration: **Install → Download firmware binary** (the label may vary by ESPHome version).
3. **Important:** a newly compiled image is a restore candidate, **not a byte-for-byte dump** of what is currently installed. For a complete backup of the device, use an appropriate ESP8266 USB/serial flash read-out with the actual board flash size.
4. Prepare a **USB/serial recovery path** in case the reader becomes unreachable by OTA. ESPHome Safe Mode may help, but recovery is not guaranteed.
5. Check an existing physical tag and the reader's Home Assistant connection before making changes.

**Update a spare reader first.** If you cannot recover the device over USB, understand and accept the possibility of an unrecoverable remote OTA failure until you regain physical access.

## Add the external component

Keep the original Adonno package, existing `wifi:`, `api:`, `ota:` and other settings. Add this block **at the top YAML level** (not nested beneath `wifi:`):

~~~yaml
external_components:
  - source: github://DA6IT/esphome-adonno-nfc@f89b5ef7f21067ecf64ed1b2a77bb322b8541df4
    components: [pn532_i2c]
~~~

If `external_components:` already exists, merge this entry into the existing list rather than declaring it twice. **Do not create a second `pn532_i2c:` scanner.**

For orientation, this is how the new block fits alongside the existing Adonno package (this is an **example**, not a complete replacement config):

~~~yaml
substitutions:
  name: my-adonno-reader

packages:
  adonno.tag_reader: github://adonno/tagreader/tagreader.yaml

# Existing Wi-Fi, API, OTA and esphome configuration remains unchanged.

external_components:
  - source: github://DA6IT/esphome-adonno-nfc@f89b5ef7f21067ecf64ed1b2a77bb322b8541df4
    components: [pn532_i2c]
~~~

The component is pinned to a specific tested Git commit. Do not replace it with `@main` on unattended devices.

## Build, install and check

1. **Save** the ESPHome YAML and resolve any validation errors.
2. Choose **Install → Download firmware binary** to compile a candidate **without flashing**.
3. Keep the previous firmware binary backed up.
4. When ready, install **only on the test reader** using the normal ESPHome **Install → Wirelessly / Over the network** option.
5. After reboot, check ESPHome logs for Wi-Fi/API connectivity, successful PN532 I²C initialization, healthy boot, and working LED/buzzer.
6. Scan a **physical MIFARE Ultralight** tag first. Its original UID path should still work.
7. Present a phone running a **compatible HCE app**, as specified in [HCE-PROTOCOL.md](HCE-PROTOCOL.md). Home Assistant should display the **APDU card number**, not a random phone UID.
8. Check the HCE app's inactive/expired presentation behavior, then verify any downstream workflow separately.

A success in `esphome compile` does not prove NFC compatibility on your particular reader.

## Restore if something goes wrong

If ESPHome OTA still works, restore your previous YAML and reflash the **known-good** firmware image through a compatible OTA method. If the reader no longer connects, use your prepared **USB/serial recovery method** and appropriate original firmware.

Do **not** experiment on a reader that is already needed for day-to-day access control. Review the [test checklist](TESTING.md) and [security warning](../SECURITY.md) before wider deployment.
