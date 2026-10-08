// SPDX-License-Identifier: GPL-3.0-only
// Derived from ESPHome 2026.9.1, with HCE/ISO-DEP additions.
// Original copyright (c) 2019 ESPHome contributors; see docs/UPSTREAM.md.
#include "pn532_i2c.h"
#include "hce_protocol.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

// Based on:
// - https://cdn-shop.adafruit.com/datasheets/PN532C106_Application+Note_v1.2.pdf
// - https://www.nxp.com/docs/en/nxp/application-notes/AN133910.pdf
// - https://www.nxp.com/docs/en/nxp/application-notes/153710.pdf

namespace esphome::pn532_i2c {

static const char *const TAG = "pn532_i2c";

bool PN532I2C::is_read_ready() {
  uint8_t ready;
  if (!this->read_bytes_raw(&ready, 1)) {
    return false;
  }
  return ready == 0x01;
}

bool PN532I2C::write_data(const std::vector<uint8_t> &data) {
  return this->write(data.data(), data.size()) == i2c::ERROR_OK;
}

bool PN532I2C::read_data(std::vector<uint8_t> &data, uint8_t len) {
  delay(1);

  if (this->read_ready_(true) != pn532::PN532ReadReady::READY) {
    return false;
  }

  data.resize(len + 1);
  this->read_bytes_raw(data.data(), len + 1);
  return true;
}

bool PN532I2C::read_response(uint8_t command, std::vector<uint8_t> &data) {
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

  data.erase(data.begin(), data.begin() + 6);  // Remove headers

  uint8_t checksum = 0;
  for (int i = 0; i < len + 1; i++) {
    uint8_t dat = data[i];
    checksum += dat;
  }
  checksum = ~checksum + 1;

  if (data[len + 1] != checksum) {
    ESP_LOGV(TAG, "read data invalid checksum! %02X != %02X", data[len], checksum);
    return false;
  }

  if (data[len + 2] != 0x00) {
    ESP_LOGV(TAG, "read data invalid postamble!");
    return false;
  }

  data.erase(data.begin(), data.begin() + 2);  // Remove TFI and command code
  data.erase(data.end() - 2, data.end());      // Remove checksum and postamble

  return true;
}

uint8_t PN532I2C::read_response_length_() {
  std::vector<uint8_t> data;
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
  LOG_I2C_DEVICE(this);
}

void PN532I2C::loop() {
  if (!this->requested_read_)
    return;

  auto ready = this->read_ready_(false);
  if (ready == WOULDBLOCK)
    return;

  bool success = false;
  std::vector<uint8_t> read;

  if (ready == READY) {
    success = this->read_response(PN532_COMMAND_INLISTPASSIVETARGET, read);
  } else {
    this->send_ack_();  // abort still running InListPassiveTarget
  }

  this->requested_read_ = false;

  if (!success) {
    // Something failed
    if (!this->current_uid_.empty() && this->tag_reported_) {
      auto tag = make_unique<nfc::NfcTag>(this->current_uid_);
      for (auto *trigger : this->triggers_ontagremoved_)
        trigger->process(tag);
    }
    this->current_uid_ = {};
    this->tag_reported_ = false;
    this->turn_off_rf_();
    return;
  }

  uint8_t num_targets = read[0];
  if (num_targets != 1) {
    // no tags found or too many
    if (!this->current_uid_.empty() && this->tag_reported_) {
      auto tag = make_unique<nfc::NfcTag>(this->current_uid_);
      for (auto *trigger : this->triggers_ontagremoved_)
        trigger->process(tag);
    }
    this->current_uid_ = {};
    this->tag_reported_ = false;
    this->turn_off_rf_();
    return;
  }

  if (read.size() < 6) {
    this->status_set_warning();
    this->turn_off_rf_();
    return;
  }
  uint8_t nfcid_length = read[5];
  if (nfcid_length > nfc::NFC_UID_MAX_LENGTH || read.size() < 6U + nfcid_length) {
    // oops, pn532 returned invalid data
    return;
  }
  nfc::NfcTagUid nfcid(read.begin() + 6, read.begin() + 6 + nfcid_length);

  bool report = true;
  for (auto *bin_sens : this->binary_sensors_) {
    if (bin_sens->process(nfcid)) {
      report = false;
    }
  }

  if (nfcid.size() == this->current_uid_.size()) {
    bool same_uid = true;
    for (size_t i = 0; i < nfcid.size(); i++)
      same_uid &= nfcid[i] == this->current_uid_[i];
    if (same_uid)
      return;
  }

  this->current_uid_ = nfcid;

  if (next_task_ == READ) {
    // SAK bit 0x20 signals ISO-DEP. Never send a random HCE UID as an event.
    auto tag = (read[4] & 0x20) ? this->read_hce_tag_(nfcid) : this->read_tag_(nfcid);
    if (!tag) {
      this->tag_reported_ = false;
      this->turn_off_rf_();
      return;
    }
    this->tag_reported_ = true;
    for (auto *trigger : this->triggers_ontag_)
      trigger->process(tag);

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
  } else if (next_task_ == CLEAN) {
    ESP_LOGD(TAG, "  Tag cleaning");
    if (!this->clean_tag_(nfcid)) {
      ESP_LOGE(TAG, "  Tag was not fully cleaned successfully");
    }
    ESP_LOGD(TAG, "  Tag cleaned!");
  } else if (next_task_ == FORMAT) {
    ESP_LOGD(TAG, "  Tag formatting");
    if (!this->format_tag_(nfcid)) {
      ESP_LOGE(TAG, "Error formatting tag as NDEF");
    }
    ESP_LOGD(TAG, "  Tag formatted!");
  } else if (next_task_ == WRITE) {
    if (this->next_task_message_to_write_ != nullptr) {
      ESP_LOGD(TAG, "  Tag writing");
      ESP_LOGD(TAG, "  Tag formatting");
      if (!this->format_tag_(nfcid)) {
        ESP_LOGE(TAG, "  Tag could not be formatted for writing");
      } else {
        ESP_LOGD(TAG, "  Writing NDEF data");
        if (!this->write_tag_(nfcid, this->next_task_message_to_write_)) {
          ESP_LOGE(TAG, "  Failed to write message to tag");
        }
        ESP_LOGD(TAG, "  Finished writing NDEF data");
        delete this->next_task_message_to_write_;
        this->next_task_message_to_write_ = nullptr;
        this->on_finished_write_callback_.call();
      }
    }
  }

  this->read_mode();

  this->turn_off_rf_();
}


bool PN532I2C::exchange_apdu_(const std::vector<uint8_t> &apdu, std::vector<uint8_t> &response) {
  // InDataExchange target number 1 from InListPassiveTarget.
  std::vector<uint8_t> command{pn532::PN532_COMMAND_INDATAEXCHANGE, 0x01};
  command.insert(command.end(), apdu.begin(), apdu.end());
  if (!this->write_command_(command))
    return false;
  std::vector<uint8_t> raw;
  if (!this->read_response(pn532::PN532_COMMAND_INDATAEXCHANGE, raw))
    return false;
  if (raw.size() < 3 || raw[0] != 0x00)  // PN532 status byte + ISO 7816 response
    return false;
  response.assign(raw.begin() + 1, raw.end());
  return true;
}

std::unique_ptr<nfc::NfcTag> PN532I2C::read_hce_tag_(nfc::NfcTagUid &uid) {
  // F0 53 56 4E 4D 43 41 52 44 = SVNMCARD; the last 00 is short Le.
  static const std::vector<uint8_t> select_apdu{
      0x00, 0xA4, 0x04, 0x00, 0x09, 0xF0, 0x53, 0x56, 0x4E, 0x4D, 0x43, 0x41, 0x52, 0x44, 0x00};
  static const std::vector<uint8_t> get_card_apdu{0x80, 0xCA, 0x00, 0x00, 0x00};
  std::vector<uint8_t> response;
  if (!this->exchange_apdu_(select_apdu, response) || !hce::select_succeeded(response)) {
    ESP_LOGD(TAG, "ISO-DEP target has no supported app AID (or SELECT failed)");
    return nullptr;
  }
  response.clear();
  if (!this->exchange_apdu_(get_card_apdu, response)) {
    ESP_LOGW(TAG, "ISO-DEP card number request failed");
    return nullptr;
  }
  std::string number;
  if (!hce::parse_card_number(response, number)) {
    ESP_LOGW(TAG, "ISO-DEP response missing valid card number and success status");
    return nullptr;
  }
  // Avoid duplicate check-ins if the HCE UID changes during one presentation.
  if (number == this->last_hce_card_ && millis() - this->last_hce_card_ms_ < 3000UL) {
    ESP_LOGD(TAG, "Suppressing repeated HCE presentation");
    return nullptr;
  }
  auto ndef = make_unique<nfc::NdefMessage>();
  if (!ndef->add_uri_record("https://www.home-assistant.io/tag/" + number))
    return nullptr;
  this->last_hce_card_ = number;
  this->last_hce_card_ms_ = millis();
  ESP_LOGD(TAG, "Valid HCE card number received");
  return make_unique<nfc::NfcTag>(uid, "ISO-DEP HCE", std::move(ndef));
}

}  // namespace esphome::pn532_i2c
