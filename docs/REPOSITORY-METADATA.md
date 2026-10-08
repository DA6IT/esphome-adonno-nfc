# Make this repository discoverable on GitHub

GitHub's **About** description and repository **Topics** are separate settings from source files. They affect GitHub's repository search filters and discovery. The GitHub connector used to maintain the code has **not** been granted an action to change these two settings directly; an authorized repo owner must enter them in the GitHub interface.

## Description

In [DA6IT/esphome-adonno-nfc](https://github.com/DA6IT/esphome-adonno-nfc), open the **About** gear icon, paste this **Description**, and save:

> ESPHome PN532 I2C extension for Adonno TagReader: Android NFC HCE, ISO-DEP/APDU card IDs and Home Assistant tag scans.

## Topics / search tags (not NFC tags)

In the **same About** dialog, add these GitHub Topics (one at a time):

`esphome` · `home-assistant` · `nfc` · `pn532` · `adonno` · `tagreader` · `android` · `android-hce` · `host-card-emulation` · `iso-dep` · `apdu` · `esp8266` · `rfid` · `nfc-reader` · `esphome-component`

Topics are part of GitHub's **repository metadata**. Listing them in a README does **not** automatically set them. After saving, GitHub should display them as clickable chips under **About**. If it still says **No topics**, the metadata have not been applied.

## Why these topics?

- **`esphome`, `adonno`, `tagreader`, `pn532`** — people who own the specific ESPHome hardware.
- **`nfc`, `android-hce`, `host-card-emulation`, `iso-dep`, `apdu`** — people looking for APDU support to read Android card emulation.
- **`home-assistant`, `esp8266`, `rfid`, `nfc-reader`, `esphome-component`, `android`** — adjacent discoverable projects and integrations.

## Versions and tags

**GitHub Topics are search tags; Git release tags like `v0.1.0` are something different.** No stable firmware release should be advertised yet: the core code has only early hardware validation. Installations should keep using the tested Git commit explicitly shown in [README.md](../README.md).

An optional pre-release Git tag can be considered after additional regression and end-to-end tests, but does not improve GitHub Topic discovery.
