// SPDX-License-Identifier: GPL-3.0-only
// Derived from ESPHome 2026.9.1, with HCE/ISO-DEP additions.
// Original copyright (c) 2019 ESPHome contributors; see docs/UPSTREAM.md.
#pragma once

#include "esphome/core/component.h"
#include "esphome/components/pn532/pn532.h"
#include "esphome/components/i2c/i2c.h"

#include <vector>

namespace esphome::pn532_i2c {

class PN532I2C final : public pn532::PN532, public i2c::I2CDevice {
 public:
  void dump_config() override;
  void loop() override;

 protected:
  bool exchange_apdu_(const std::vector<uint8_t> &command, std::vector<uint8_t> &response);
  std::unique_ptr<nfc::NfcTag> read_hce_tag_(nfc::NfcTagUid &uid);
  bool tag_reported_{false};
  std::string last_hce_card_{};
  uint32_t last_hce_card_ms_{0};
  bool is_read_ready() override;
  bool write_data(const std::vector<uint8_t> &data) override;
  bool read_data(std::vector<uint8_t> &data, uint8_t len) override;
  bool read_response(uint8_t command, std::vector<uint8_t> &data) override;
  uint8_t read_response_length_();
};

}  // namespace esphome::pn532_i2c
