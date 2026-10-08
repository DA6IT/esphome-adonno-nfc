// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace esphome::pn532_i2c::hce {

// Permitted UID-style card numbers: 4, 7, or 10 groups of two hex digits.
inline bool parse_card_number(const std::vector<uint8_t> &apdu, std::string &number) {
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

inline bool select_succeeded(const std::vector<uint8_t> &apdu) {
  return apdu.size() == 2 && apdu[0] == 0x90 && apdu[1] == 0x00;
}

}  // namespace esphome::pn532_i2c::hce
