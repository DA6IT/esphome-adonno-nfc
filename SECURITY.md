# Security policy

## Important limitation

This is an early reader-side NFC HCE/APDU prototype, **not a secure credential or authentication system**. The Android app returns a **static, replayable card number**. A matching number alone does not establish the presenter's identity or authorization. Do not use the number alone to authorize physical access or other sensitive operations.

## Compatibility

The documented implementation has been tested with **ESPHome 2026.9.1 / ESP8266 / PN532 I²C** only. No security or reliability warranty is implied for other versions, hardware or production deployments.

## Reporting a security concern

**Never include secrets, real card numbers or details of an actively vulnerable installation in a public issue.**

If GitHub offers **Private vulnerability reporting** for this repository (under **Security**), use it. Otherwise create a minimal [issue](https://github.com/DA6IT/esphome-adonno-nfc/issues) requesting a private contact route, without posting exploit data.

We welcome responsible reports, but cannot promise a response time.
