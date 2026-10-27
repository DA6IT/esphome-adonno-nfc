# Upstream, licensing and port notes

This repository is an independent modification of the ESPHome `pn532_i2c` component. The current source is derived from ESPHome release **2026.10.0**; the previous, hardware-tested port for **2026.9.1** remains available at commit [`f89b5ef7f21067ecf64ed1b2a77bb322b8541df4`](https://github.com/DA6IT/esphome-adonno-nfc/tree/f89b5ef7f21067ecf64ed1b2a77bb322b8541df4).

Upstream files:

- [pn532_i2c/__init__.py](https://github.com/esphome/esphome/blob/2026.10.0/esphome/components/pn532_i2c/__init__.py) – MIT; extended with an ESPHome version guard (refuses < 2026.10.0, warns outside 2026.10.x).
- [pn532_i2c/pn532_i2c.h](https://github.com/esphome/esphome/blob/2026.10.0/esphome/components/pn532_i2c/pn532_i2c.h) – changed to override `loop()` and declare HCE helpers (GPL-3.0).
- [pn532_i2c/pn532_i2c.cpp](https://github.com/esphome/esphome/blob/2026.10.0/esphome/components/pn532_i2c/pn532_i2c.cpp) – the I²C transport (`is_read_ready` … `read_response_length_`) is **byte-for-byte** the 2026.10.0 code; `loop()` and the APDU exchange were added (GPL-3.0).
- The ESPHome [pn532.cpp `PN532::loop()`](https://github.com/esphome/esphome/blob/2026.10.0/esphome/components/pn532/pn532.cpp) of 2026.10.0 was adapted inside our `pn532_i2c.cpp` so unsupported ISO-DEP targets can be suppressed safely.
- `components/pn532_i2c/hce_protocol.h` is our own ESPHome-independent reader logic (GPL-3.0) and is unit tested natively.

Upstream ESPHome copyright © 2019 ESPHome contributors. The license of the **C++ runtime** is GPLv3; Python and other ESPHome code use the MIT license. Our additional source code is GPL-3.0-only. The root [LICENSE](../LICENSE) contains the full GNU GPL version 3 terms. ESPHome's own dual-license explanation: https://github.com/esphome/esphome/blob/2026.10.0/LICENSE.

The [original Adonno project](https://github.com/adonno/tagreader) is included only via an ESPHome `packages` reference, not copied into this repository. Referenced revisions:

| Revision | Use |
| --- | --- |
| `a8bb6fc43e5a687503d1b43edb32855d0de7050a` (master) | ESPHome 2026.9.1 builds. **Does not compile on ESPHome 2026.10.0.** |
| `23c2ebb880577a906c89f9fa90905bafcea4f446` | ESPHome 2026.10.x builds. Head of [adonno/tagreader#306](https://github.com/adonno/tagreader/pull/306): `a8bb6fc` plus exactly one changed line, `auto records = message->get_records();` → `auto &records = …` (see [adonno/tagreader#307](https://github.com/adonno/tagreader/issues/307)). Fetchable from the `adonno/tagreader` repository itself. Switch to the merge commit once #306 is merged. |

## ESPHome 2026.9.1 → 2026.10.0: what changed in the PN532 base

Compared for `pn532`, `pn532_i2c`, `pn532_spi`, `nfc` and `i2c` (the `i2c` component is unchanged).

| Area | 2026.9.1 | 2026.10.0 | Port |
| --- | --- | --- | --- |
| Transport virtuals | `write_data(const std::vector<uint8_t>&)`, `read_data(std::vector<uint8_t>&, uint8_t)`, `read_response(uint8_t, std::vector<uint8_t>&)` | `write_data(std::span<const uint8_t>)`, `read_data(PN532Frame&, size_t)`, `read_response(uint8_t, PN532Frame&)`; `PN532Frame` = `StaticVector<uint8_t, 263>` | Upstream 2026.10.0 I²C transport taken over unchanged (incl. length guard, RDY bit mask, checked `read_bytes_raw`). |
| Tag type detection | `guess_tag_type(uid.size())` inside `read_tag_` & co. | `tag_type_from_sel_res(SEL_RES)`; `read_tag_/clean_tag_/format_tag_/write_tag_` take `tag_type` | Conventional tags use the upstream SEL_RES mapping (Classic `0x08`, Ultralight/NTAG `0x00`). ISO-DEP is decided first, see deltas. |
| Trigger / sensor lists | `std::vector` members, always present | `StaticVector` sized by `PN532_ON_TAG_TRIGGER_COUNT`, `PN532_ON_TAG_REMOVED_TRIGGER_COUNT`, `PN532_BINARY_SENSOR_COUNT`; members and `register_*` only exist when the define is set | All uses wrapped in the same `#ifdef`s. |
| Pending write message | raw `NdefMessage *` + `delete` | `std::unique_ptr<NdefMessage>`, `write_tag_(…, message.get())`, `.reset()` | Adopted. |
| InDataExchange | each caller built and parsed frames | `in_data_exchange_(command, response)` checks the status byte, strips it and feeds the watchdog | APDUs now use the base helper instead of an own copy. |
| Read readiness | `optional<uint32_t> rd_start_time_` | `rd_started_` flag + loop-start timestamp | Inherited (base code). |
| Invalid target data | early `return` without RF off | NFCID length 0 rejected, RF switched off | Adopted. |
| NDEF API | `get_records()` → `std::vector<std::shared_ptr<NdefRecord>>` | `get_records() const` → `StaticVector<std::unique_ptr<NdefRecord>, 4>` (not copyable); `NfcTag` getters `const` | Our logging binds by reference. Affects the **Adonno package** (see table above). |
| `on_tag` argument | `NfcTag` (copy) | `const NfcTag &` | No change needed. |
| `pn532.is_writing` | dedicated condition class | `register_apply_condition("is_writing()")` | Base code, works unchanged with Adonno. |

## Intentional deltas to ESPHome 2026.10.0 `PN532::loop()`

These keep the behaviour of the 2026.9.1 port and are covered by `tests/hce_protocol_test.cpp`:

1. **ISO-DEP first.** Any target with SAK bit `0x20` – including dual-interface `0x28` that upstream would treat as MIFARE Classic – is read only via `SELECT AID F053564E4D43415244` + `GET 80 CA 00 00 00`. If that fails, **no** `on_tag` fires; a (random) ISO-DEP UID is never reported.
2. **Removal only for reported tags.** `on_tag_removed` fires only for a target that produced an `on_tag` event (upstream fires for any previous UID).
3. **Empty frame is not a removal.** A response without data bytes is logged as a warning and the current tag is kept; upstream would fire a removal and re-report a tag that is still present.
4. **HCE repeat window.** The same HCE card number is not reported again within 3 s of the last accepted report (Android presents a new random UID on every RF activation).
5. **Truncated frame** sets the component warning status (as in the 2026.9.1 port; upstream only switches RF off).
6. **No removal after CLEAN/FORMAT/WRITE.** A tag handled by a write task is not "reported", so no `on_tag_removed` follows (as in the 2026.9.1 port; upstream fires one).

Consequences of adopting the upstream SAK-based tag type (not deltas, but behaviour changes against the 2026.9.1 build):

- A MIFARE Classic with **7-byte UID** (SAK `0x08`) is now read via the Classic path instead of the Ultralight path, and TNP3xxx (`0x01`) as Classic. Without an NDEF record the Home Assistant ID stays the UID; with a Home Assistant NDEF record the ID would now come from that record. SVN member cards (Classic 1K, 4-byte UID) and Ultralight cards keep their previous path.
- A target that keeps the **same UID** after a failed SELECT is not queried again until it leaves the field (unchanged; Android normally presents a new random UID per activation).

## Design tradeoff

Overriding `pn532_i2c` instead of the entire `pn532` base leaves upstream MIFARE read/write logic unchanged. However, the overridden `loop()` mirrors one specific ESPHome release. **Every ESPHome update must be diffed against this table and regression tested on hardware before claiming compatibility.** The component refuses ESPHome releases older than 2026.10.0 and logs a warning on releases outside 2026.10.x.
