// SPDX-License-Identifier: GPL-3.0-only
//
// Native unit and regression tests for the hardware-independent reader logic in
// components/pn532_i2c/hce_protocol.h. No ESPHome, no PN532, no radio: these
// tests cannot replace the hardware checklist in docs/TESTING.md.
//
// All card numbers and UIDs below are synthetic test values.
#include "../components/pn532_i2c/hce_protocol.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hce = esphome::pn532_i2c::hce;
using Bytes = std::vector<uint8_t>;

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond) \
  do { \
    ++g_checks; \
    if (!(cond)) { \
      ++g_failures; \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
  } while (0)

static Bytes apdu_response(const std::string &data, uint8_t sw1 = 0x90, uint8_t sw2 = 0x00) {
  Bytes bytes(data.begin(), data.end());
  bytes.push_back(sw1);
  bytes.push_back(sw2);
  return bytes;
}

static std::span<const uint8_t> sp(const Bytes &b) { return std::span<const uint8_t>(b.data(), b.size()); }

// ---------------------------------------------------------------------------
// Fake ISO-DEP target: answers APDUs like the SVN Android HCE service
// (SvnMemberCardApduService.kt) or like a foreign card / phone.
// ---------------------------------------------------------------------------
struct FakeIsoDep {
  enum class Kind { SVN_HCE, FOREIGN_APP, SILENT, SELECT_WITH_FCI, GET_TRANSPORT_FAIL, CUSTOM_GET };
  Kind kind{Kind::SVN_HCE};
  std::string card_number{"04-A1-B2-C3-D4-E5-F6"};
  Bytes custom_get{};
  std::vector<Bytes> sent{};
  Bytes last_response{};

  bool exchange(std::span<const uint8_t> apdu, std::span<const uint8_t> &response) {
    this->sent.emplace_back(apdu.begin(), apdu.end());
    const bool is_select = apdu.size() >= 4 && apdu[0] == 0x00 && apdu[1] == 0xA4;
    const bool is_get = apdu.size() >= 2 && apdu[0] == 0x80 && apdu[1] == 0xCA;
    switch (this->kind) {
      case Kind::SILENT:
        return false;  // no ISO-DEP answer at all, e.g. expired HCE window / phone moved away
      case Kind::FOREIGN_APP:
        this->last_response = is_select ? Bytes{0x6A, 0x82} : Bytes{0x6D, 0x00};
        break;
      case Kind::SELECT_WITH_FCI:
        this->last_response = is_select ? Bytes{0x6F, 0x00, 0x90, 0x00} : apdu_response(this->card_number);
        break;
      case Kind::GET_TRANSPORT_FAIL:
        if (is_get)
          return false;
        this->last_response = Bytes{0x90, 0x00};
        break;
      case Kind::CUSTOM_GET:
        this->last_response = is_select ? Bytes{0x90, 0x00} : this->custom_get;
        break;
      case Kind::SVN_HCE:
      default: {
        const bool aid_ok = is_select && apdu.size() >= 14 &&
                            std::equal(hce::SVN_AID.begin(), hce::SVN_AID.end(), apdu.begin() + 5);
        if (is_select)
          this->last_response = aid_ok ? Bytes{0x90, 0x00} : Bytes{0x6A, 0x82};
        else if (is_get)
          this->last_response = apdu_response(this->card_number);
        else
          this->last_response = Bytes{0x6D, 0x00};
        break;
      }
    }
    response = sp(this->last_response);
    return true;
  }

  auto fn() {
    return [this](std::span<const uint8_t> apdu, std::span<const uint8_t> &response) {
      return this->exchange(apdu, response);
    };
  }
};

// ---------------------------------------------------------------------------
// Model of PN532I2C::loop() for READ mode, composed from exactly the functions
// the firmware uses (poll_outcome, parse_in_list_passive_target, TagPresence,
// read_new_target, read_hce_presentation). Events are what Adonno's on_tag /
// on_tag_removed automations would receive.
// ---------------------------------------------------------------------------
struct Field {
  // What the PN532 sees in this poll.
  bool read_ok{true};
  Bytes frame{};          // InListPassiveTarget data after the response code
  FakeIsoDep *card{nullptr};  // ISO-DEP target behaviour for APDUs
};

static Bytes target_frame(uint8_t sel_res, const Bytes &uid) {
  Bytes f{0x01, 0x01, 0x00, 0x44, sel_res, static_cast<uint8_t>(uid.size())};
  f.insert(f.end(), uid.begin(), uid.end());
  return f;
}
static Bytes no_target_frame() { return Bytes{0x00}; }

static std::string hex_uid(const Bytes &uid) {
  std::string s;
  char buf[4];
  for (size_t i = 0; i < uid.size(); i++) {
    std::snprintf(buf, sizeof(buf), i ? "-%02X" : "%02X", uid[i]);
    s += buf;
  }
  return s;
}

struct SimReader {
  hce::TagPresence<Bytes> presence{};
  hce::RepeatFilter filter{};
  std::vector<std::string> events{};  // "tag:hass:<number>", "tag:uid:<uid>", "removed:<uid>"
  int warnings{0};
  int apdu_sessions{0};

  void poll(const Field &field, uint32_t now_ms) {
    const hce::Target target = hce::parse_in_list_passive_target(sp(field.frame));
    switch (hce::poll_outcome(field.read_ok, target.status)) {
      case hce::PollOutcome::TARGET:
        break;
      case hce::PollOutcome::ABSENT:
        if (this->presence.removal_pending())
          this->events.push_back("removed:" + hex_uid(this->presence.uid()));
        this->presence.reset();
        return;
      case hce::PollOutcome::ANOMALY:
        this->warnings++;
        return;
      case hce::PollOutcome::INVALID:
      default:
        return;
    }
    if (this->presence.is_current(target.uid))
      return;
    this->presence.set_current(target.uid);
    const Bytes uid(target.uid.begin(), target.uid.end());
    auto tag = hce::read_new_target(
        target, this->presence,
        [&]() -> std::optional<std::string> {
          this->apdu_sessions++;
          if (field.card == nullptr)
            return std::nullopt;
          std::string number;
          if (hce::read_hce_presentation(field.card->fn(), this->filter, now_ms, number) !=
              hce::HceOutcome::ACCEPTED)
            return std::nullopt;
          return "hass:" + number;  // synthetic NDEF https://www.home-assistant.io/tag/<number>
        },
        [&]() -> std::optional<std::string> { return "uid:" + hex_uid(uid); });
    if (tag)
      this->events.push_back("tag:" + *tag);
  }
};

// ---------------------------------------------------------------------------

static void test_apdu_constants() {
  // Must stay byte-identical with the Android HCE service and docs/HCE-PROTOCOL.md.
  const Bytes select{0x00, 0xA4, 0x04, 0x00, 0x09, 0xF0, 0x53, 0x56, 0x4E, 0x4D, 0x43, 0x41, 0x52, 0x44, 0x00};
  const Bytes get{0x80, 0xCA, 0x00, 0x00, 0x00};
  CHECK(Bytes(hce::SELECT_APDU.begin(), hce::SELECT_APDU.end()) == select);
  CHECK(Bytes(hce::GET_CARD_APDU.begin(), hce::GET_CARD_APDU.end()) == get);
  CHECK(std::string(hce::SVN_AID.begin() + 1, hce::SVN_AID.end()) == "SVNMCARD");
  CHECK(hce::SELECT_APDU[4] == hce::SVN_AID.size());
  CHECK(hce::ha_tag_uri("04-AA-BB-CC") == "https://www.home-assistant.io/tag/04-AA-BB-CC");
}

static void test_select_status() {
  CHECK(hce::select_succeeded(sp(Bytes{0x90, 0x00})));
  CHECK(!hce::select_succeeded(sp(Bytes{0x6A, 0x82})));  // file/app not found
  CHECK(!hce::select_succeeded(sp(Bytes{0x69, 0x85})));
  CHECK(!hce::select_succeeded(sp(Bytes{0xAA, 0x90, 0x00})));  // FCI data is not accepted
  CHECK(!hce::select_succeeded(sp(Bytes{0x90})));
  CHECK(!hce::select_succeeded(sp(Bytes{})));
}

static void test_card_number_parser() {
  std::string num;
  CHECK(hce::parse_card_number(sp(apdu_response("04-AA-BB-CC-DD-EE-FF")), num));
  CHECK(num == "04-AA-BB-CC-DD-EE-FF");
  CHECK(hce::parse_card_number(sp(apdu_response("04-aa-bb-cc-dd-ee-ff")), num));
  CHECK(num == "04-AA-BB-CC-DD-EE-FF");
  CHECK(hce::parse_card_number(sp(apdu_response("04-AA-BB-CC")), num));
  CHECK(num == "04-AA-BB-CC");
  CHECK(hce::parse_card_number(sp(apdu_response("01-23-45-67-89-AB-CD-EF-01-23")), num));
  CHECK(num == "01-23-45-67-89-AB-CD-EF-01-23");

  CHECK(!hce::parse_card_number(sp(apdu_response("04-AA-BB")), num));
  CHECK(num.empty());
  CHECK(!hce::parse_card_number(sp(apdu_response("04-AA-BB-CC-DD")), num));
  CHECK(!hce::parse_card_number(sp(apdu_response("04-AA-BB-CC-DD-EE-FG")), num));
  CHECK(!hce::parse_card_number(sp(apdu_response("04/AA/BB/CC/DD/EE/FF")), num));
  CHECK(!hce::parse_card_number(sp(apdu_response("04-AA-BB-CC-DD-EE-FF/../")), num));
  CHECK(!hce::parse_card_number(sp(apdu_response("04-AA-BB-CC ")), num));
  CHECK(!hce::parse_card_number(sp(apdu_response("04AABBCCDDEEFF")), num));
  CHECK(!hce::parse_card_number(sp(apdu_response("https://x.y/z")), num));
  CHECK(!hce::parse_card_number(sp(Bytes{0x69, 0x85}), num));
  CHECK(!hce::parse_card_number(sp(Bytes{0x90, 0x00}), num));
  CHECK(!hce::parse_card_number(sp(Bytes{}), num));
  CHECK(!hce::parse_card_number(sp(apdu_response("04-AA-BB-CC-DD-EE-FF", 0x90, 0x01)), num));
  CHECK(!hce::parse_card_number(sp(apdu_response("04-AA-BB-CC-DD-EE-FF", 0x62, 0x83)), num));
  Bytes non_ascii = apdu_response("04-AA-BB-CC");
  non_ascii[0] = 0xB0;
  CHECK(!hce::parse_card_number(sp(non_ascii), num));
  CHECK(num.empty());
}

static void test_read_card_number_flow() {
  std::string num;
  {
    FakeIsoDep phone;  // SVN Android HCE, presentation active
    CHECK(hce::read_card_number(phone.fn(), num) == hce::CardReadResult::OK);
    CHECK(num == "04-A1-B2-C3-D4-E5-F6");
    CHECK(phone.sent.size() == 2);
    CHECK(phone.sent.size() == 2 && phone.sent[0] == Bytes(hce::SELECT_APDU.begin(), hce::SELECT_APDU.end()));
    CHECK(phone.sent.size() == 2 && phone.sent[1] == Bytes(hce::GET_CARD_APDU.begin(), hce::GET_CARD_APDU.end()));
  }
  {
    FakeIsoDep foreign;  // bank card / other app: 6A 82 on SELECT
    foreign.kind = FakeIsoDep::Kind::FOREIGN_APP;
    CHECK(hce::read_card_number(foreign.fn(), num) == hce::CardReadResult::SELECT_FAILED);
    CHECK(num.empty());
    CHECK(foreign.sent.size() == 1);  // GET never sent to a foreign target
  }
  {
    FakeIsoDep silent;  // HCE window expired / transport error
    silent.kind = FakeIsoDep::Kind::SILENT;
    CHECK(hce::read_card_number(silent.fn(), num) == hce::CardReadResult::SELECT_FAILED);
    CHECK(silent.sent.size() == 1);
  }
  {
    FakeIsoDep fci;
    fci.kind = FakeIsoDep::Kind::SELECT_WITH_FCI;
    CHECK(hce::read_card_number(fci.fn(), num) == hce::CardReadResult::SELECT_FAILED);
  }
  {
    FakeIsoDep get_fail;
    get_fail.kind = FakeIsoDep::Kind::GET_TRANSPORT_FAIL;
    CHECK(hce::read_card_number(get_fail.fn(), num) == hce::CardReadResult::GET_FAILED);
    CHECK(num.empty());
  }
  const std::vector<Bytes> bad_get_answers{
      Bytes{0x6D, 0x00},                              // INS not supported
      Bytes{0x90, 0x00},                              // success but no number
      apdu_response("04-A1-B2-C3-D4-E5-F6", 0x6A, 0x82),  // number with error status
      apdu_response("0123456789"),                    // other format
      apdu_response("04-A1-B2-C3-D4-E5-F6-"),         // trailing separator
      Bytes{0x04, 0xA1, 0xB2, 0xC3, 0x90, 0x00},      // raw binary UID
  };
  for (const auto &answer : bad_get_answers) {
    FakeIsoDep odd;
    odd.kind = FakeIsoDep::Kind::CUSTOM_GET;
    odd.custom_get = answer;
    CHECK(hce::read_card_number(odd.fn(), num) == hce::CardReadResult::INVALID_NUMBER);
    CHECK(num.empty());
  }
}

static void test_repeat_filter() {
  hce::RepeatFilter f;
  std::string num;
  FakeIsoDep phone;
  CHECK(hce::read_hce_presentation(phone.fn(), f, 1000, num) == hce::HceOutcome::ACCEPTED);
  CHECK(num == "04-A1-B2-C3-D4-E5-F6");
  CHECK(hce::read_hce_presentation(phone.fn(), f, 1001, num) == hce::HceOutcome::REPEAT_SUPPRESSED);
  CHECK(num.empty());
  CHECK(hce::read_hce_presentation(phone.fn(), f, 3999, num) == hce::HceOutcome::REPEAT_SUPPRESSED);
  // suppressed attempts did not extend the window
  CHECK(hce::read_hce_presentation(phone.fn(), f, 4000, num) == hce::HceOutcome::ACCEPTED);

  FakeIsoDep other;
  other.card_number = "04-11-22-33-44-55-66";
  CHECK(hce::read_hce_presentation(other.fn(), f, 4001, num) == hce::HceOutcome::ACCEPTED);
  CHECK(num == "04-11-22-33-44-55-66");
  // after another number the first is a new presentation again
  CHECK(hce::read_hce_presentation(phone.fn(), f, 4002, num) == hce::HceOutcome::ACCEPTED);

  // millis() wrap-around (~49.7 days)
  hce::RepeatFilter w;
  w.accept("04-AA-BB-CC", 0xFFFFFF00u);
  CHECK(w.is_repeat("04-AA-BB-CC", 0x00000100u));   // 512 ms later
  CHECK(!w.is_repeat("04-AA-BB-CC", 0x00000C00u));  // > 3 s later
  hce::RepeatFilter fresh;
  CHECK(!fresh.is_repeat("", 0));
  CHECK(!fresh.is_repeat("04-AA-BB-CC", 0));

  // rejected presentations never arm the filter
  hce::RepeatFilter g;
  FakeIsoDep foreign;
  foreign.kind = FakeIsoDep::Kind::FOREIGN_APP;
  CHECK(hce::read_hce_presentation(foreign.fn(), g, 10, num) == hce::HceOutcome::SELECT_FAILED);
  CHECK(hce::read_hce_presentation(phone.fn(), g, 11, num) == hce::HceOutcome::ACCEPTED);
}

static void test_target_parser() {
  using S = hce::TargetStatus;
  CHECK(hce::parse_in_list_passive_target(sp(Bytes{})).status == S::EMPTY_FRAME);
  CHECK(hce::parse_in_list_passive_target(sp(Bytes{0x00})).status == S::NO_SINGLE_TARGET);
  CHECK(hce::parse_in_list_passive_target(sp(Bytes{0x02, 0x01})).status == S::NO_SINGLE_TARGET);
  CHECK(hce::parse_in_list_passive_target(sp(Bytes{0x01, 0x01, 0x00, 0x44, 0x00})).status == S::TOO_SHORT);
  CHECK(hce::parse_in_list_passive_target(sp(Bytes{0x01, 0x01, 0x00, 0x44, 0x00, 0x00})).status == S::INVALID_UID);
  CHECK(hce::parse_in_list_passive_target(sp(Bytes{0x01, 0x01, 0x00, 0x44, 0x00, 0x07, 1, 2, 3})).status ==
        S::INVALID_UID);
  Bytes too_long = target_frame(0x00, Bytes(11, 0xAB));
  CHECK(hce::parse_in_list_passive_target(sp(too_long)).status == S::INVALID_UID);

  for (size_t len : {4u, 7u, 10u}) {
    Bytes uid;
    for (size_t i = 0; i < len; i++)
      uid.push_back(static_cast<uint8_t>(0x10 + i));
    Bytes frame = target_frame(0x08, uid);
    frame.push_back(0xEE);  // trailing ATS bytes are ignored
    const auto t = hce::parse_in_list_passive_target(sp(frame));
    CHECK(t.status == S::OK);
    CHECK(t.sel_res == 0x08);
    CHECK(Bytes(t.uid.begin(), t.uid.end()) == uid);
  }

  using O = hce::PollOutcome;
  CHECK(hce::poll_outcome(false, S::OK) == O::ABSENT);
  CHECK(hce::poll_outcome(true, S::OK) == O::TARGET);
  CHECK(hce::poll_outcome(true, S::NO_SINGLE_TARGET) == O::ABSENT);
  CHECK(hce::poll_outcome(true, S::EMPTY_FRAME) == O::ANOMALY);
  CHECK(hce::poll_outcome(true, S::TOO_SHORT) == O::ANOMALY);
  CHECK(hce::poll_outcome(true, S::INVALID_UID) == O::INVALID);
}

static void test_iso_dep_classification() {
  CHECK(hce::is_iso_dep(0x20));   // ISO 14443-4 (Android HCE, DESFire, bank cards)
  CHECK(hce::is_iso_dep(0x28));   // dual interface (Classic + ISO-DEP): HCE path, as in 2026.9.1 port
  CHECK(hce::is_iso_dep(0x60));
  CHECK(!hce::is_iso_dep(0x00));  // MIFARE Ultralight / NTAG
  CHECK(!hce::is_iso_dep(0x08));  // MIFARE Classic 1K (SVN member cards)
  CHECK(!hce::is_iso_dep(0x18));  // MIFARE Classic 4K
  CHECK(!hce::is_iso_dep(0x01));  // TNP3xxx
}

static void test_scenario_physical_card_repeat_and_removal() {
  SimReader r;
  const Bytes classic_uid{0xDE, 0xAD, 0xBE, 0xEF};
  const Field card{true, target_frame(0x08, classic_uid), nullptr};
  const Field empty{true, no_target_frame(), nullptr};
  // card held for three polls: exactly one event
  r.poll(card, 0);
  r.poll(card, 1000);
  r.poll(card, 2000);
  CHECK(r.events == std::vector<std::string>{"tag:uid:DE-AD-BE-EF"});
  CHECK(r.apdu_sessions == 0);  // never treated as ISO-DEP
  // removed: exactly one removal
  r.poll(empty, 3000);
  r.poll(empty, 4000);
  CHECK(r.events.size() == 2 && r.events[1] == "removed:DE-AD-BE-EF");
  // presented again: reported again
  r.poll(card, 5000);
  CHECK(r.events.size() == 3 && r.events[2] == "tag:uid:DE-AD-BE-EF");
  // a read failure also counts as removal
  r.poll(Field{false, {}, nullptr}, 6000);
  CHECK(r.events.size() == 4 && r.events[3] == "removed:DE-AD-BE-EF");
}

static void test_scenario_ultralight_and_card_switch() {
  SimReader r;
  const Bytes ul{0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  const Bytes classic{0x0A, 0x0B, 0x0C, 0x0D};
  r.poll(Field{true, target_frame(0x00, ul), nullptr}, 0);
  r.poll(Field{true, target_frame(0x08, classic), nullptr}, 1000);  // swapped without gap
  CHECK((r.events == std::vector<std::string>{"tag:uid:04-11-22-33-44-55-66", "tag:uid:0A-0B-0C-0D"}));
  r.poll(Field{true, no_target_frame(), nullptr}, 2000);
  CHECK(r.events.size() == 3 && r.events[2] == "removed:0A-0B-0C-0D");
}

static void test_scenario_android_hce() {
  SimReader r;
  FakeIsoDep phone;
  // Android HCE: new random 4-byte UID (08 xx xx xx) on every RF activation.
  r.poll(Field{true, target_frame(0x20, Bytes{0x08, 0x01, 0x02, 0x03}), &phone}, 0);
  CHECK(r.events == std::vector<std::string>{"tag:hass:04-A1-B2-C3-D4-E5-F6"});
  // The random UID is never reported as a tag.
  for (const auto &e : r.events)
    CHECK(e.find("uid:") == std::string::npos);
  // re-activation 1 s later with a different random UID: suppressed duplicate
  r.poll(Field{true, target_frame(0x20, Bytes{0x08, 0x09, 0x0A, 0x0B}), &phone}, 1000);
  CHECK(r.events.size() == 1);
  // phone removed after a suppressed read: that target was not reported -> no removal event
  r.poll(Field{true, no_target_frame(), nullptr}, 1500);
  CHECK(r.events.size() == 1);
  // presented again after the 3 s window: reported again
  r.poll(Field{true, target_frame(0x20, Bytes{0x08, 0x0C, 0x0D, 0x0E}), &phone}, 4000);
  CHECK(r.events.size() == 2 && r.events[1] == "tag:hass:04-A1-B2-C3-D4-E5-F6");
  // removed right after a reported read: removal with the (internal) phone UID
  r.poll(Field{true, no_target_frame(), nullptr}, 4500);
  CHECK(r.events.size() == 3 && r.events[2] == "removed:08-0C-0D-0E");
}

static void test_scenario_hce_first_select_fails_then_succeeds() {
  // Observed on Reader 1 (2026-10-08): first SELECT failed, retry ~0.9 s later succeeded.
  SimReader r;
  FakeIsoDep not_ready;
  not_ready.kind = FakeIsoDep::Kind::SILENT;
  FakeIsoDep phone;
  r.poll(Field{true, target_frame(0x20, Bytes{0x08, 0x21, 0x22, 0x23}), &not_ready}, 0);
  CHECK(r.events.empty());
  r.poll(Field{true, target_frame(0x20, Bytes{0x08, 0x31, 0x32, 0x33}), &phone}, 900);
  CHECK(r.events == std::vector<std::string>{"tag:hass:04-A1-B2-C3-D4-E5-F6"});
}

static void test_scenario_foreign_iso_dep_never_reported() {
  SimReader r;
  FakeIsoDep bank;  // static-UID ISO-DEP card without the SVN app
  bank.kind = FakeIsoDep::Kind::FOREIGN_APP;
  const Field bank_field{true, target_frame(0x20, Bytes{0x12, 0x34, 0x56, 0x78}), &bank};
  r.poll(bank_field, 0);
  r.poll(bank_field, 1000);
  r.poll(bank_field, 2000);
  CHECK(r.events.empty());
  CHECK(r.apdu_sessions == 1);  // same static UID is not re-queried every poll
  r.poll(Field{true, no_target_frame(), nullptr}, 3000);
  CHECK(r.events.empty());  // no orphan removal for an unreported target

  // Foreign phone (random UID each activation, other HCE app) and dual-interface card.
  FakeIsoDep foreign_phone;
  foreign_phone.kind = FakeIsoDep::Kind::FOREIGN_APP;
  for (uint8_t i = 0; i < 5; i++)
    r.poll(Field{true, target_frame(0x20, Bytes{0x08, i, 0x55, 0x66}), &foreign_phone}, 4000 + i * 1000);
  FakeIsoDep dual;
  dual.kind = FakeIsoDep::Kind::FOREIGN_APP;
  r.poll(Field{true, target_frame(0x28, Bytes{0xCA, 0xFE, 0xBA, 0xBE}), &dual}, 10000);
  // expired SVN app presentation (phone does not answer ISO-DEP any more)
  FakeIsoDep expired;
  expired.kind = FakeIsoDep::Kind::SILENT;
  r.poll(Field{true, target_frame(0x20, Bytes{0x08, 0x77, 0x88, 0x99}), &expired}, 11000);
  // malicious / broken answer
  FakeIsoDep evil;
  evil.kind = FakeIsoDep::Kind::CUSTOM_GET;
  evil.custom_get = apdu_response("04-AA-BB-CC-DD-EE-FF/../../x");
  r.poll(Field{true, target_frame(0x20, Bytes{0x08, 0x44, 0x45, 0x46}), &evil}, 12000);
  r.poll(Field{true, no_target_frame(), nullptr}, 13000);
  CHECK(r.events.empty());
}

static void test_scenario_hce_held_in_field() {
  // Characterisation of unchanged 2026.9.1 behaviour: RF is switched off after every
  // poll, so a phone held in the field is re-activated (new random UID) on each poll.
  // The 3 s window allows one report per 3 s while the SVN app keeps presenting.
  SimReader r;
  FakeIsoDep phone;
  for (uint32_t t = 0; t <= 9000; t += 1000)
    r.poll(Field{true, target_frame(0x20, Bytes{0x08, static_cast<uint8_t>(t / 1000), 0x00, 0x01}), &phone}, t);
  CHECK(r.events.size() == 4);  // t = 0, 3000, 6000, 9000
}

static void test_scenario_transport_anomalies() {
  SimReader r;
  const Bytes uid{0xDE, 0xAD, 0xBE, 0xEF};
  const Field card{true, target_frame(0x08, uid), nullptr};
  r.poll(card, 0);
  // empty frame and truncated frames while the card stays in the field
  r.poll(Field{true, Bytes{}, nullptr}, 1000);
  r.poll(Field{true, Bytes{0x01, 0x01, 0x00}, nullptr}, 2000);
  r.poll(Field{true, Bytes{0x01, 0x01, 0x00, 0x44, 0x08, 0x04, 0xDE}, nullptr}, 3000);  // invalid uid
  r.poll(card, 4000);
  CHECK(r.events == std::vector<std::string>{"tag:uid:DE-AD-BE-EF"});  // no removal, no duplicate
  CHECK(r.warnings == 2);
}

int main() {
  test_apdu_constants();
  test_select_status();
  test_card_number_parser();
  test_read_card_number_flow();
  test_repeat_filter();
  test_target_parser();
  test_iso_dep_classification();
  test_scenario_physical_card_repeat_and_removal();
  test_scenario_ultralight_and_card_switch();
  test_scenario_android_hce();
  test_scenario_hce_first_select_fails_then_succeeds();
  test_scenario_foreign_iso_dep_never_reported();
  test_scenario_hce_held_in_field();
  test_scenario_transport_anomalies();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d of %d checks FAILED\n", g_failures, g_checks);
    return 1;
  }
  std::printf("Reader logic tests passed: %d checks\n", g_checks);
  return 0;
}
