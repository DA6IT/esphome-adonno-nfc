// SPDX-License-Identifier: GPL-3.0-only
// Derived from ESPHome 2026.10.0, with HCE/ISO-DEP additions.
// Original copyright (c) 2019 ESPHome contributors; see docs/UPSTREAM.md.
#pragma once

#include "esphome/core/component.h"
#include "esphome/components/pn532/pn532.h"
#include "esphome/components/i2c/i2c.h"

#include "hce_protocol.h"

#include <memory>
#include <span>
#include <string>

namespace esphome::pn532_i2c {

class PN532I2C final : public pn532::PN532, public i2c::I2CDevice {
 public:
  void dump_config() override;
  /// Replaces PN532::loop() of ESPHome 2026.10.0 so ISO-DEP targets are read
  /// via APDU and never reported by their UID; see docs/UPSTREAM.md.
  void loop() override;

 protected:
  // PN532 I2C transport, unchanged from ESPHome 2026.10.0
  bool is_read_ready() override;
  bool write_data(std::span<const uint8_t> data) override;
  bool read_data(pn532::PN532Frame &data, size_t len) override;
  bool read_response(uint8_t command, pn532::PN532Frame &data) override;
  uint8_t read_response_length_();

  // HCE / ISO-DEP extension
  bool exchange_apdu_(std::span<const uint8_t> apdu, pn532::PN532Frame &response);
  std::unique_ptr<nfc::NfcTag> read_hce_tag_(nfc::NfcTagUid &uid);
  /// The current tag left the field (or the read failed): fire on_tag_removed if it was reported.
  void handle_tag_absent_();

  hce::TagPresence<nfc::NfcTagUid> presence_{};
  hce::RepeatFilter repeat_filter_{};
};

}  // namespace esphome::pn532_i2c
