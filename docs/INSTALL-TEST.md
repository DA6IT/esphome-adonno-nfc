# Install, update and restore — Adonno ESP8266 / PN532 I²C

Step-by-step guide for an **existing ESPHome Adonno TagReader** with an **ESP8266 (D1 mini) + PN532 over I²C**. Other hardware is not validated.

## Pick the matching version

The component replaces part of ESPHome's PN532 code, so its pin **must match your ESPHome release**:

| ESPHome (Device Builder / CLI) | `external_components` pin | Adonno `packages` reference | Status |
| --- | --- | --- | --- |
| **2026.10.x** | `7b19ffeea58cb037f0c99961cce434f883e3335d` | `github://adonno/tagreader/tagreader.yaml@23c2ebb880577a906c89f9fa90905bafcea4f446` | Compile + unit tests in CI. **Hardware test pending.** |
| **2026.9.1** | `f89b5ef7f21067ecf64ed1b2a77bb322b8541df4` | `github://adonno/tagreader/tagreader.yaml` (master `a8bb6fc…`) | **Hardware tested** 2026-10-08 (known-good restore state) |
| anything else | – | – | Not supported. 2026.10 component refuses older ESPHome; newer releases log a warning and must be re-tested. |

Why the Adonno reference changes for 2026.10: Adonno master does not compile on ESPHome 2026.10.0 (`auto records = message->get_records();`, [adonno/tagreader#307](https://github.com/adonno/tagreader/issues/307)). `23c2ebb` is master plus exactly that one-line fix ([PR #306](https://github.com/adonno/tagreader/pull/306)). Once #306 is merged, the plain master reference works again. See [UPSTREAM.md](UPSTREAM.md).

## Before changing anything

1. In **ESPHome Device Builder → Edit**, back up the working reader YAML privately. Keep any referenced `!secret` entries available; **do not publish credentials or real card IDs**.
2. **While Device Builder still runs the old ESPHome release** (e.g. 2026.9.1), download a **known-good firmware image**: **Install → Manual download / Download firmware binary**. After the ESPHome add-on was upgraded to 2026.10 it can no longer compile the 2026.9.1 pin. If that already happened, build the restore image with the CLI instead (see *Build a 2026.9.1 restore image* below).
3. A newly compiled image is a restore candidate, **not a byte-for-byte dump** of what is installed. For a full backup use an ESP8266 USB/serial flash read-out.
4. Prepare a **USB/serial recovery path** in case the reader becomes unreachable by OTA. ESPHome Safe Mode may help, but recovery is not guaranteed.
5. Check an existing physical tag and the reader's Home Assistant connection before making changes.

**Update a spare/test reader first.** Only after the full checklist in [TESTING.md](TESTING.md) passed there, decide about other readers.

## Update 2026.9.1 → 2026.10.x (test reader)

Keep the existing `wifi:`, `api:`, `ota:`, `esphome:` and other settings. Change only the two references:

~~~yaml
packages:
  adonno.tag_reader: github://adonno/tagreader/tagreader.yaml@23c2ebb880577a906c89f9fa90905bafcea4f446

external_components:
  - source: github://DA6IT/esphome-adonno-nfc@7b19ffeea58cb037f0c99961cce434f883e3335d
    components: [pn532_i2c]
~~~

`external_components:` is a **top-level** key (not under `wifi:`). If it exists already, change the entry instead of adding a second one. **Do not create a second `pn532_i2c:` scanner.** Do not use `@main` on unattended devices.

1. **Save** the YAML and resolve validation errors. A message `This component requires at least ESPHome version 2026.10.0` means Device Builder still runs an older ESPHome – use the 2026.9.1 pin there.
2. **Install → Manual download** to compile a candidate **without flashing**. Note the reported RAM/flash usage (CI reference: RAM ≈ 44 %, flash ≈ 43–47 % of the D1 mini budget).
3. Install **only on the test reader** via **Install → Wirelessly**.
4. In the boot log check: ESPHome version `2026.10.0`, Wi-Fi/API connected, PN532 at `0x24`, and the line **`Adonno HCE extension: 2.0.0 (ESPHome 2026.10)`** (proves the ported component is running), LED/buzzer.
5. Run the hardware checklist in [TESTING.md](TESTING.md).

A green `esphome compile` does not prove NFC compatibility on your reader.

## Restore the known-good 2026.9.1 state

Use this on any regression (no reader reaction, wrong/duplicate HA events, boot loops, API not connecting).

**If OTA still works:**

1. Restore the saved 2026.9.1 YAML (Adonno master reference, component pin `f89b5ef7f21067ecf64ed1b2a77bb322b8541df4`).
2. Upload the saved known-good `.bin` over the network, for example with the ESPHome CLI of the *same* release the image was built with:
   ~~~bash
   esphome upload reader.yaml --device <reader-ip> --file firmware-2026.9.1.bin
   ~~~
   (Device Builder can also install a manually chosen binary through the device's OTA/web update path, depending on version.)
3. Verify the boot log shows ESPHome `2026.9.1` and `[C][pn532_i2c:128]: Address: 0x24` (line 128 is the fingerprint of the 2026.9.1 port), then scan a physical card.

**If the reader no longer connects:** USB/serial recovery with the saved image (e.g. ESPHome Web / esptool), then step 3.

### Build a 2026.9.1 restore image after the add-on was upgraded

~~~bash
python3 -m venv esphome-2026.9.1 && . esphome-2026.9.1/bin/activate
pip install esphome==2026.9.1
esphome compile reader.yaml          # YAML with the 2026.9.1 pin and Adonno master
# image: .esphome/build/<name>/.pioenvs/<name>/firmware.bin
~~~

CI compiles exactly this restore combination on every change (`examples/tagreader-restore-2026.9.1-ci.yaml`).

Do **not** experiment on a reader that is needed for day-to-day operation. Review the [test checklist](TESTING.md) and [security warning](../SECURITY.md) before wider deployment.
