# Contributing

Thanks for supporting Android HCE / ISO-DEP for the Adonno TagReader.

## Opening an issue

Search [existing issues](https://github.com/DA6IT/esphome-adonno-nfc/issues) first. A helpful bug report includes the reader hardware (ESP8266 / PN532 I²C), ESPHome version, HCE app protocol, whether a physical MIFARE card still works, and **redacted** logs with expected and actual behavior.

**Never publish API keys, Wi-Fi details, private IPs, true card/member numbers, full unredacted logs or secret-containing YAML.** Use fictional values in code examples and screenshots.

## Pull requests

- Explain the problem and the tested platform.
- Make small, reviewable changes; keep Adonno's original automation and physical-tag behavior.
- Never forward randomized Android HCE UIDs as member identifiers.
- Add positive and negative tests for APDU parsing and errors where relevant.
- Pass the Linux CI: parser tests and ESPHome 2026.9.1 configuration/compile checks. CI never deploys.
- If reusing ESPHome C++ runtime code, preserve GPLv3 notices and original attribution. See [upstream licensing](docs/UPSTREAM.md).
- Report **actual hardware test results** separately from compile-only results.

## Compatibility philosophy

Only ESPHome **2026.9.1 / ESP8266 / PN532 I²C** is currently verified. The app AID, commands and card format are fixed in the current implementation; expanded support needs compatibility testing.

Please read [SECURITY.md](SECURITY.md) before reporting vulnerabilities.
