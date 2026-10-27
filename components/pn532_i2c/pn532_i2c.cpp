// SPDX-License-Identifier: GPL-3.0-only
// Derived from ESPHome 2026.10.0, with HCE/ISO-DEP additions.
// Original copyright (c) 2019 ESPHome contributors; see docs/UPSTREAM.md.
//
// The I2C transport (is_read_ready .. read_response_length_) is byte-for-byte
// the ESPHome 2026.10.0 code. loop() is ESPHome 2026.10.0 PN532::loop() with the
// HCE deltas documented in docs/UPSTREAM.md.
#include "pn532_i2c.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

#include <algorithm>
#include <array>

// Based on:
// - https://cdn-shop.adafruit.com/datasheets/PN532C106_Application+Note_v1.2.pdf
// - https://www.nxp.com/docs/en/nxp/application-notes/AN133910.pdf
// - https://www.nxp.com/docs/en/nxp/application-notes/153710.pdf

namespace esphome::pn532_i2c {

static const char *const TAG = "pn532_i2c";

// hce_protocol.h is ESPHome-independent; keep its copies of upstream constants in sync.
static_assert(hce::SEL_RES_ISO_DEP_BIT == pn532::SEL_RES_ISO_DEP, "ISO-DEP SAK bit differs from ESPHome");
static_assert(hce::MAX_UID_LENGTH == nfc::NFC_UID_MAX_LENGTH, "UID length differs from ESPHome");
static constexpr size_t MAX_COMMAND_APDU_SIZE = std::max(hce::SELECT_APDU.size(), hce::GET_CARD_APDU.size());

bool PN532I2C::is_read_ready() {
  uint8_t status;
  if (!this->read_bytes_raw(&status, 1)) {
    return false;
  }
  // only bit 0 (RDY) of the status byte is defined (UM0701-02, 6.2.4)
  return status & 0x01;
}

bool PN532I2C::write_data(const std::span<const uint8_t> data) {
  return this->write(data.data(), data.size()) == i2c::ERROR_OK;
}

bool PN532I2C::read_data(pn532::PN532Frame &data, size_t len) {
  if (len + 1 > pn532::PN532_FRAME_MAX_SIZE) {
    return false;
  }
  delay(1);

  if (this->read_ready_(true) != pn532::PN532ReadReady::READY) {
    return false;
  }

  // the PN532 prefixes every frame with a status byte
  data.resize(len + 1);
  return this->read_bytes_raw(data.data(), len + 1);
}

bool PN532I2C::read_response(uint8_t command, pn532::PN532Frame &data) {
  ESP_LOGV(TAG, "Reading response");
  uint8_t len = this->read_response_length_();
  if (len == 0) {
    return false;
  }

  ESP_LOGV(TAG, "Reading response of length %d", len);
  if (!this->read_data(data, 6 + len + 2)) {
    ESP_LOGD(TAG, "No response data");
    return false;
  }

  if (data[1] != 0x00 || data[2] != 0x00 || data[3] != 0xFF) {
    // invalid packet
    ESP_LOGV(TAG, "read data invalid preamble!");
    return false;
  }

  bool valid_header = (static_cast<uint8_t>(data[4] + data[5]) == 0 &&  // LCS, len + lcs = 0
                       data[6] == 0xD5 &&                               // TFI - frame from PN532 to system controller
                       data[7] == command + 1);                         // Correct command response

  if (!valid_header) {
    ESP_LOGV(TAG, "read data invalid header!");
    return false;
  }

  // frame: status, preamble, start code (2), LEN, LCS, TFI, command response code, data, DCS, postamble
  constexpr size_t tfi_offset = 6;
  uint8_t checksum = 0;
  for (size_t i = 0; i < len + 1U; i++) {
    checksum += data[tfi_offset + i];
  }
  checksum = ~checksum + 1;

  if (data[tfi_offset + len + 1] != checksum) {
    ESP_LOGV(TAG, "read data invalid checksum! %02X != %02X", data[tfi_offset + len + 1], checksum);
    return false;
  }

  if (data[tfi_offset + len + 2] != 0x00) {
    ESP_LOGV(TAG, "read data invalid postamble!");
    return false;
  }

  // keep only the data bytes that follow the command response code
  std::copy(data.begin() + tfi_offset + 2, data.begin() + tfi_offset + len + 1, data.begin());
  data.resize(len - 1);

  return true;
}

uint8_t PN532I2C::read_response_length_() {
  pn532::PN532Frame data;
  if (!this->read_data(data, 6)) {
    return 0;
  }

  if (data[1] != 0x00 || data[2] != 0x00 || data[3] != 0xFF) {
    // invalid packet
    ESP_LOGV(TAG, "read data invalid preamble!");
    return 0;
  }

  bool valid_header = (static_cast<uint8_t>(data[4] + data[5]) == 0 &&  // LCS, len + lcs = 0
                       data[6] == 0xD5);                                // TFI - frame from PN532 to system controller

  if (!valid_header) {
    ESP_LOGV(TAG, "read data invalid header!");
    return 0;
  }

  this->send_nack_();

  // full length of message, including TFI
  uint8_t full_len = data[4];
  // length of data, excluding TFI
  uint8_t len = full_len - 1;
  if (full_len == 0)
    len = 0;
  return len;
}

void PN532I2C::dump_config() {
  PN532::dump_config();
  ESP_LOGCONFIG(TAG, "  Adonno HCE extension: %s", hce::EXTENSION_VERSION);
  LOG_I2C_DEVICE(this);
}

void PN532I2C::handle_tag_absent_() {
#ifdef PN532_ON_TAG_REMOVED_TRIGGER_COUNT
  if (this->presence_.removal_pending()) {
    auto tag = make_unique<nfc::NfcTag>(this->presence_.uid());
    for (auto *trigger : this->triggers_ontagremoved_)
      trigger->process(tag);
  }
#endif
  this->presence_.reset();
  this->turn_off_rf_();
}

void PN532I2C::loop() {
  if (!this->requested_read_)
    return;

  auto ready = this->read_ready_(false);
  if (ready == pn532::WOULDBLOCK)
    return;

  bool success = false;
  pn532::PN532Frame read;

  if (ready == pn532::READY) {
    success = this->read_response(pn532::PN532_COMMAND_INLISTPASSIVETARGET, read);
  } else {
    this->send_ack_();  // abort still running InListPassiveTarget
  }

  this->requested_read_ = false;

  const hce::Target target = hce::parse_in_list_passive_target(std::span<const uint8_t>(read.data(), read.size()));
  switch (hce::poll_outcome(success, target.status)) {
    case hce::PollOutcome::TARGET:
      break;
    case hce::PollOutcome::ABSENT:
      // read failed, no tags found or too many
      this->handle_tag_absent_();
      return;
    case hce::PollOutcome::ANOMALY:
      // Empty or truncated frame: not treated as a removal (see hce::poll_outcome).
      ESP_LOGW(TAG, "PN532 returned an incomplete tag response");
      this->status_set_warning();
      this->turn_off_rf_();
      return;
    case hce::PollOutcome::INVALID:
    default:
      // oops, pn532 returned invalid data
      this->turn_off_rf_();
      return;
  }

  nfc::NfcTagUid nfcid(target.uid.begin(), target.uid.end());

  bool report = true;
#ifdef PN532_BINARY_SENSOR_COUNT
  for (auto *bin_sens : this->binary_sensors_) {
    if (bin_sens->process(nfcid)) {
      report = false;
    }
  }
#endif

  if (this->presence_.is_current(target.uid))
    return;

  this->presence_.set_current(target.uid);
  const uint8_t tag_type = pn532::tag_type_from_sel_res(target.sel_res);

  if (this->next_task_ == READ) {
    // ISO-DEP (SAK bit 0x20) is read via APDU only. Never send a random HCE UID as an event.
    std::unique_ptr<nfc::NfcTag> tag = hce::read_new_target(
        target, this->presence_, [this, &nfcid]() { return this->read_hce_tag_(nfcid); },
        [this, &nfcid, tag_type]() { return this->read_tag_(nfcid, tag_type); });
    if (!tag) {
      this->turn_off_rf_();
      return;
    }
#ifdef PN532_ON_TAG_TRIGGER_COUNT
    for (auto *trigger : this->triggers_ontag_)
      trigger->process(tag);
#endif

    if (report) {
      char uid_buf[nfc::FORMAT_UID_BUFFER_SIZE];
      ESP_LOGD(TAG, "Found new tag '%s'", nfc::format_uid_to(uid_buf, nfcid));
      if (tag->has_ndef_message()) {
        const auto &message = tag->get_ndef_message();
        const auto &records = message->get_records();
        ESP_LOGD(TAG, "  NDEF formatted records:");
        for (const auto &record : records) {
          ESP_LOGD(TAG, "    %s - %s", record->get_type().c_str(), record->get_payload().c_str());
        }
      }
    }
  } else if (this->next_task_ == CLEAN) {
    ESP_LOGD(TAG, "  Tag cleaning");
    if (!this->clean_tag_(nfcid, tag_type)) {
      ESP_LOGE(TAG, "  Tag was not fully cleaned successfully");
    }
    ESP_LOGD(TAG, "  Tag cleaned!");
  } else if (this->next_task_ == FORMAT) {
    ESP_LOGD(TAG, "  Tag formatting");
    if (!this->format_tag_(nfcid, tag_type)) {
      ESP_LOGE(TAG, "Error formatting tag as NDEF");
    }
    ESP_LOGD(TAG, "  Tag formatted!");
  } else if (this->next_task_ == WRITE) {
    if (this->next_task_message_to_write_ != nullptr) {
      ESP_LOGD(TAG, "  Tag writing");
      ESP_LOGD(TAG, "  Tag formatting");
      if (!this->format_tag_(nfcid, tag_type)) {
        ESP_LOGE(TAG, "  Tag could not be formatted for writing");
      } else {
        ESP_LOGD(TAG, "  Writing NDEF data");
        if (!this->write_tag_(nfcid, tag_type, this->next_task_message_to_write_.get())) {
          ESP_LOGE(TAG, "  Failed to write message to tag");
        }
        ESP_LOGD(TAG, "  Finished writing NDEF data");
        this->next_task_message_to_write_.reset();
        this->on_finished_write_callback_.call();
      }
    }
  }

  this->read_mode();

  this->turn_off_rf_();
}

bool PN532I2C::exchange_apdu_(std::span<const uint8_t> apdu, pn532::PN532Frame &response) {
  // InDataExchange with target number 1, the target InListPassiveTarget activated.
  // Our command APDUs are fixed and short; a small array keeps a second 263-byte
  // PN532Frame off the ESP8266 stack.
  std::array<uint8_t, 2 + MAX_COMMAND_APDU_SIZE> command{};
  if (apdu.size() > MAX_COMMAND_APDU_SIZE)
    return false;
  command[0] = pn532::PN532_COMMAND_INDATAEXCHANGE;
  command[1] = 0x01;
  std::copy(apdu.begin(), apdu.end(), command.begin() + 2);
  // in_data_exchange_ rejects a non-zero PN532 status and strips the status byte,
  // so `response` is the ISO 7816 response APDU: data followed by SW1 SW2.
  return this->in_data_exchange_(std::span<const uint8_t>(command.data(), 2 + apdu.size()), response) &&
         response.size() >= 2;
}

std::unique_ptr<nfc::NfcTag> PN532I2C::read_hce_tag_(nfc::NfcTagUid &uid) {
  pn532::PN532Frame buffer;
  auto exchange = [this, &buffer](std::span<const uint8_t> apdu, std::span<const uint8_t> &response) {
    if (!this->exchange_apdu_(apdu, buffer))
      return false;
    response = std::span<const uint8_t>(buffer.data(), buffer.size());
    return true;
  };

  std::string number;
  switch (hce::read_hce_presentation(exchange, this->repeat_filter_, millis(), number)) {
    case hce::HceOutcome::ACCEPTED:
      break;
    case hce::HceOutcome::SELECT_FAILED:
      ESP_LOGD(TAG, "ISO-DEP target has no supported app AID (or SELECT failed)");
      return nullptr;
    case hce::HceOutcome::GET_FAILED:
      ESP_LOGW(TAG, "ISO-DEP card number request failed");
      return nullptr;
    case hce::HceOutcome::REPEAT_SUPPRESSED:
      // Avoid duplicate check-ins: the HCE UID changes on every RF activation.
      ESP_LOGD(TAG, "Suppressing repeated HCE presentation");
      return nullptr;
    case hce::HceOutcome::INVALID_NUMBER:
    default:
      ESP_LOGW(TAG, "ISO-DEP response missing valid card number and success status");
      return nullptr;
  }

  auto ndef = make_unique<nfc::NdefMessage>();
  if (!ndef->add_uri_record(hce::ha_tag_uri(number)))
    return nullptr;
  ESP_LOGD(TAG, "Valid HCE card number received");
  return make_unique<nfc::NfcTag>(uid, "ISO-DEP HCE", std::move(ndef));
}

}  // namespace esphome::pn532_i2c
