// SPDX-License-Identifier: GPL-3.0-only
#include "../components/pn532_i2c/hce_protocol.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using esphome::pn532_i2c::hce::parse_card_number;
using esphome::pn532_i2c::hce::select_succeeded;

std::vector<uint8_t> response(const std::string &s) {
  auto bytes = std::vector<uint8_t>(s.begin(), s.end());
  bytes.push_back(0x90);
  bytes.push_back(0x00);
  return bytes;
}

int main() {
  assert(select_succeeded({0x90, 0x00}));
  assert(!select_succeeded({0x69, 0x85}));
  assert(!select_succeeded({0xAA, 0x90, 0x00}));
  std::string num;
  assert(parse_card_number(response("04-AA-BB-CC-DD-EE-FF"), num));
  assert(num == "04-AA-BB-CC-DD-EE-FF");
  assert(parse_card_number(response("04-aa-bb-cc-dd-ee-ff"), num));
  assert(num == "04-AA-BB-CC-DD-EE-FF");
  assert(parse_card_number(response("04-AA-BB-CC"), num));
  assert(!parse_card_number(response("04-AA-BB"), num));
  assert(!parse_card_number(response("04-AA-BB-CC-DD-EE-FG"), num));
  assert(!parse_card_number(response("04/AA/BB/CC/DD/EE/FF"), num));
  assert(!parse_card_number(response("04-AA-BB-CC-DD-EE-FF/../"), num));
  assert(!parse_card_number({0x69, 0x85}, num));
  assert(!parse_card_number({0x90, 0x00}, num));
  auto invalid = response("04-AA-BB-CC-DD-EE-FF");
  invalid.back() = 0x01;
  assert(!parse_card_number(invalid, num));
  assert(num.empty());
  std::cout << "APDU parser tests passed\n";
}
