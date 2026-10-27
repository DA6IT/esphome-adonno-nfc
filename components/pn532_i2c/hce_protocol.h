// SPDX-License-Identifier: GPL-3.0-only
//
// Hardware-independent reader logic of the Adonno HCE extension.
//
// Everything in this header is free of ESPHome and Arduino dependencies so that
// the decisions which control Home Assistant events (which tag is reported,
// when a removal fires, which APDUs are sent, which card numbers are accepted)
// can be unit tested natively, see tests/hce_protocol_test.cpp.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace esphome::pn532_i2c::hce {

/// Identifies this extension in the ESPHome config dump (and therefore the device log).
inline constexpr const char *EXTENSION_VERSION = "2.0.0 (ESPHome 2026.10)";

/// SVN member card application identifier: F0 followed by ASCII "SVNMCARD".
inline constexpr std::array<uint8_t, 9> SVN_AID{0xF0, 0x53, 0x56, 0x4E, 0x4D, 0x43, 0x41, 0x52, 0x44};

/// ISO 7816-4 SELECT by DF name (P1=04, P2=00), Lc=9, the AID, short Le=00.
inline constexpr std::array<uint8_t, 15> SELECT_APDU{0x00, 0xA4, 0x04, 0x00, 0x09, 0xF0, 0x53, 0x56,
                                                     0x4E, 0x4D, 0x43, 0x41, 0x52, 0x44, 0x00};

/// Proprietary GET DATA for the card number: CLA=80, INS=CA, P1=P2=00, short Le=00.
inline constexpr std::array<uint8_t, 5> GET_CARD_APDU{0x80, 0xCA, 0x00, 0x00, 0x00};

/// SEL_RES (SAK) bit 6 announces ISO/IEC 14443-4 (ISO-DEP) support.
inline constexpr uint8_t SEL_RES_ISO_DEP_BIT = 0x20;

/// Same card number from an HCE phone within this window is not reported again.
inline constexpr uint32_t REPEAT_SUPPRESS_MS = 3000;

/// Mirrors nfc::NFC_UID_MAX_LENGTH (triple size NFCID1).
inline constexpr size_t MAX_UID_LENGTH = 10;

/// Prefix of the synthetic NDEF URI that original Adonno recognises as a Home Assistant tag.
inline constexpr const char *HA_TAG_URI_PREFIX = "https://www.home-assistant.io/tag/";

/// Every target that announces ISO-DEP is handled by the HCE path, including
/// dual-interface targets that also set the MIFARE Classic bit (SAK 0x28).
/// This intentionally keeps the precedence of the ESPHome 2026.9.1 port: an
/// ISO-DEP target is never reported by its (possibly random) UID.
inline bool is_iso_dep(uint8_t sel_res) { return (sel_res & SEL_RES_ISO_DEP_BIT) != 0; }

/// SELECT must answer with exactly the status word 90 00 (no response data).
inline bool select_succeeded(std::span<const uint8_t> apdu) {
  return apdu.size() == 2 && apdu[0] == 0x90 && apdu[1] == 0x00;
}

/// Accepts only UID-style card numbers followed by status 90 00:
/// 4, 7 or 10 groups of two hex digits separated by '-'. Lower case is normalised.
inline bool parse_card_number(std::span<const uint8_t> apdu, std::string &number) {
  number.clear();
  if (apdu.size() < 2 || apdu[apdu.size() - 2] != 0x90 || apdu.back() != 0x00)
    return false;
  const size_t len = apdu.size() - 2;
  if (len != 11 && len != 20 && len != 29)
    return false;
  std::string validated;
  validated.reserve(len);
  for (size_t i = 0; i < len; i++) {
    char c = static_cast<char>(apdu[i]);
    if (i % 3 == 2) {
      if (c != '-')
        return false;
    } else {
      if (c >= 'a' && c <= 'f')
        c = static_cast<char>(c - 'a' + 'A');
      if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')))
        return false;
    }
    validated.push_back(c);
  }
  number = validated;
  return true;
}

inline std::string ha_tag_uri(const std::string &number) { return std::string(HA_TAG_URI_PREFIX) + number; }

enum class CardReadResult : uint8_t {
  OK = 0,
  SELECT_FAILED,   ///< transport error, or the target does not know the SVN AID
  GET_FAILED,      ///< transport error on GET after a successful SELECT
  INVALID_NUMBER,  ///< GET answered, but not with a valid card number and 90 00
};

/// Runs SELECT(AID) followed by GET(card number) over `exchange`.
///
/// `exchange(std::span<const uint8_t> apdu, std::span<const uint8_t> &response) -> bool`
/// sends one command APDU and on success points `response` at the response APDU
/// (data + SW1 SW2). The response only has to stay valid until the next call.
/// On anything but CardReadResult::OK `number` is empty.
template<typename Exchange> CardReadResult read_card_number(Exchange &&exchange, std::string &number) {
  number.clear();
  std::span<const uint8_t> response{};
  if (!exchange(std::span<const uint8_t>(SELECT_APDU), response) || !select_succeeded(response))
    return CardReadResult::SELECT_FAILED;
  response = {};
  if (!exchange(std::span<const uint8_t>(GET_CARD_APDU), response))
    return CardReadResult::GET_FAILED;
  if (!parse_card_number(response, number))
    return CardReadResult::INVALID_NUMBER;
  return CardReadResult::OK;
}

/// Suppresses the same HCE card number within REPEAT_SUPPRESS_MS of the last
/// accepted report. Android HCE presents a new random UID on every RF
/// activation, so the UID cannot be used to detect a repeated presentation.
/// Only accepted reports restart the window; suppressed attempts do not.
class RepeatFilter {
 public:
  bool is_repeat(const std::string &number, uint32_t now_ms) const {
    // unsigned subtraction stays correct across the 49.7-day millis() wrap
    return this->has_last_ && number == this->last_ && now_ms - this->last_ms_ < REPEAT_SUPPRESS_MS;
  }
  void accept(const std::string &number, uint32_t now_ms) {
    this->last_ = number;
    this->last_ms_ = now_ms;
    this->has_last_ = true;
  }

 protected:
  std::string last_{};
  uint32_t last_ms_{0};
  bool has_last_{false};
};

enum class TargetStatus : uint8_t {
  OK = 0,
  EMPTY_FRAME,       ///< response without any data byte: transport anomaly, not a removal
  NO_SINGLE_TARGET,  ///< no target (or more than one) in the field: the current tag is gone
  TOO_SHORT,         ///< target data shorter than the fixed type A header
  INVALID_UID,       ///< NFCID length 0, above 10 or truncated
};

struct Target {
  TargetStatus status{TargetStatus::EMPTY_FRAME};
  uint8_t sel_res{0};
  std::span<const uint8_t> uid{};
};

/// Parses InListPassiveTarget output for 106 kbps type A
/// (NbTg, Tg, SENS_RES[2], SEL_RES, NFCIDLength, NFCID1...; UM0701-02, 7.3.5).
/// The returned uid points into `read`.
inline Target parse_in_list_passive_target(std::span<const uint8_t> read) {
  Target target{};
  if (read.empty()) {
    target.status = TargetStatus::EMPTY_FRAME;
    return target;
  }
  if (read[0] != 1) {
    target.status = TargetStatus::NO_SINGLE_TARGET;
    return target;
  }
  if (read.size() < 6) {
    target.status = TargetStatus::TOO_SHORT;
    return target;
  }
  const uint8_t uid_length = read[5];
  if (uid_length == 0 || uid_length > MAX_UID_LENGTH || read.size() < 6U + uid_length) {
    target.status = TargetStatus::INVALID_UID;
    return target;
  }
  target.status = TargetStatus::OK;
  target.sel_res = read[4];
  target.uid = read.subspan(6, uid_length);
  return target;
}

enum class PollOutcome : uint8_t {
  TARGET = 0,  ///< exactly one valid target: continue with the presence check
  ABSENT,      ///< read failed or no single target: fire pending removal, forget the tag, RF off
  ANOMALY,     ///< empty or truncated frame: warning, RF off, presence unchanged
  INVALID,     ///< invalid UID data: RF off, presence unchanged
};

/// Maps the result of one InListPassiveTarget poll to the action PN532I2C::loop() takes.
/// Delta to ESPHome 2026.10.0: there an empty frame counts as "no target" and fires a
/// removal; here it is an ANOMALY, because a spurious removal would make a tag that is
/// still in the field report a second time (duplicate Home Assistant tag scan).
inline PollOutcome poll_outcome(bool read_ok, TargetStatus status) {
  if (!read_ok)
    return PollOutcome::ABSENT;
  switch (status) {
    case TargetStatus::OK:
      return PollOutcome::TARGET;
    case TargetStatus::NO_SINGLE_TARGET:
      return PollOutcome::ABSENT;
    case TargetStatus::EMPTY_FRAME:
    case TargetStatus::TOO_SHORT:
      return PollOutcome::ANOMALY;
    case TargetStatus::INVALID_UID:
    default:
      return PollOutcome::INVALID;
  }
}

enum class HceOutcome : uint8_t {
  ACCEPTED = 0,
  SELECT_FAILED,
  GET_FAILED,
  INVALID_NUMBER,
  REPEAT_SUPPRESSED,
};

/// Complete HCE presentation: SELECT + GET, validation and repeat suppression.
/// On ACCEPTED `number` holds the validated card number and the repeat window restarts.
template<typename Exchange>
HceOutcome read_hce_presentation(Exchange &&exchange, RepeatFilter &filter, uint32_t now_ms, std::string &number) {
  switch (read_card_number(exchange, number)) {
    case CardReadResult::OK:
      break;
    case CardReadResult::SELECT_FAILED:
      return HceOutcome::SELECT_FAILED;
    case CardReadResult::GET_FAILED:
      return HceOutcome::GET_FAILED;
    case CardReadResult::INVALID_NUMBER:
    default:
      return HceOutcome::INVALID_NUMBER;
  }
  if (filter.is_repeat(number, now_ms)) {
    number.clear();
    return HceOutcome::REPEAT_SUPPRESSED;
  }
  filter.accept(number, now_ms);
  return HceOutcome::ACCEPTED;
}

/// Tracks the tag currently in the field.
///
/// A removal is only signalled for a tag that was actually reported via on_tag.
/// Unsupported ISO-DEP targets (wrong app, expired HCE window, repeat-suppressed
/// phone) therefore never produce an orphan on_tag_removed event.
template<typename Uid> class TagPresence {
 public:
  bool is_current(std::span<const uint8_t> uid) const {
    return uid.size() == this->uid_.size() && std::equal(uid.begin(), uid.end(), this->uid_.begin());
  }
  /// A different tag entered the field; it is not reported yet.
  void set_current(std::span<const uint8_t> uid) {
    this->uid_ = Uid(uid.begin(), uid.end());
    this->reported_ = false;
  }
  void mark_reported() { this->reported_ = true; }
  bool removal_pending() const { return !this->uid_.empty() && this->reported_; }
  void reset() {
    this->uid_ = Uid();
    this->reported_ = false;
  }
  const Uid &uid() const { return this->uid_; }
  bool reported() const { return this->reported_; }

 protected:
  Uid uid_{};
  bool reported_{false};
};

/// READ task for a target that just entered the field. ISO-DEP targets are only
/// read via APDU (`read_iso_dep`), all others via the upstream reader
/// (`read_conventional`). Both return a nullable tag handle; the target counts as
/// reported (eligible for on_tag_removed) only if a tag was produced.
template<typename Uid, typename IsoDepReader, typename ConventionalReader>
auto read_new_target(const Target &target, TagPresence<Uid> &presence, IsoDepReader &&read_iso_dep,
                     ConventionalReader &&read_conventional) {
  auto tag = is_iso_dep(target.sel_res) ? read_iso_dep() : read_conventional();
  if (tag)
    presence.mark_reported();
  return tag;
}

}  // namespace esphome::pn532_i2c::hce
