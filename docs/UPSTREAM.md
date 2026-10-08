# Upstream and licensing

This repository is an independent modification of the ESPHome `pn532_i2c` component from release **2026.9.1**, based on these upstream files:

- [pn532_i2c/__init__.py](https://github.com/esphome/esphome/blob/2026.9.1/esphome/components/pn532_i2c/__init__.py) – unmodified from ESPHome 2026.9.1 (MIT).
- [pn532_i2c/pn532_i2c.h](https://github.com/esphome/esphome/blob/2026.9.1/esphome/components/pn532_i2c/pn532_i2c.h) – changed to override `loop()` and declare HCE helpers (GPL-3.0).
- [pn532_i2c/pn532_i2c.cpp](https://github.com/esphome/esphome/blob/2026.9.1/esphome/components/pn532_i2c/pn532_i2c.cpp) – changed to exchange ISO-DEP APDUs (GPL-3.0).
- The original ESPHome [pn532.cpp `loop()`](https://github.com/esphome/esphome/blob/2026.9.1/esphome/components/pn532/pn532.cpp) was adapted inside our `pn532_i2c.cpp` so events can be suppressed safely for unsupported ISO-DEP targets.

Upstream ESPHome copyright © 2019 ESPHome contributors. The license of the **C++ runtime** is GPLv3; Python and other ESPHome code use the MIT license. Our additional source code is GPL-3.0-only. The root [LICENSE](../LICENSE) contains the full GNU GPL version 3 terms. ESPHome's own dual-license explanation: https://github.com/esphome/esphome/blob/2026.9.1/LICENSE.

The [original Adonno project](https://github.com/adonno/tagreader) is included only via an ESPHome `packages` reference, not copied into this repository. Tested/referenced upstream revision: `a8bb6fc43e5a687503d1b43edb32855d0de7050a`.

## Design tradeoff

Overriding `pn532_i2c` instead of the entire `pn532` base leaves upstream MIFARE read/write logic unchanged. However, the overridden `loop()` mirrors ESPHome 2026.9.1's polling logic; changes in future ESPHome versions must be reviewed and regression tested before updating the supported version.
