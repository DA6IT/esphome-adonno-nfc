# Adonno NFC – Smartphone-Mitgliedskarten für ESPHome

Diese Open-Source-Erweiterung soll den [Adonno TagReader](https://github.com/adonno/tagreader) um die Erkennung digitaler NFC-Karten ergänzen. Vorhandene MIFARE-/NFC-Karten und die Home-Assistant-Anbindung sollen weiterhin funktionieren.

**Status: experimentell, noch nicht für den Betrieb freigegeben.** Der Reader wird durch die Installation neuer ESPHome-Firmware verändert. Bitte **noch nicht auf produktive Reader installieren**. Das Projekt wird zunächst für **ESP8266 + PN532 über I²C** entwickelt und mit **ESPHome 2026.9.1** getestet.

## Geplante Nutzung

1. Adonno bleibt die Grundlage; nur die ESPHome-PN532-I²C-Komponente wird gezielt ergänzt.
2. Eine Android-App mit NFC Host Card Emulation (HCE) stellt während eines kurzen Präsentationsfensters eine Kartenkennung bereit.
3. Der Reader liest sie über ISO-DEP/APDU und meldet sie bei Erfolg als Home-Assistant-Tag.
4. Physische NFC-Karten sollen unverändert erkannt werden.

**Wichtig:** Noch **nicht hardwarevalidiert**. Wechselnde Smartphone-UIDs eignen sich nicht als Mitgliedskennung. Eine statische Kartenkennung ist nicht kryptografisch gegen Kopieren oder Replay geschützt.

## Installation

Ein bereinigtes Beispiel steht unter [examples](examples/); technische Details und Testgrenzen unter [docs](docs/). Die Installation auf echten Geräten erfolgt erst nach abgesicherten Tests und einer Rückfallstrategie. Keine Zugangsdaten aus einer bestehenden Installation veröffentlichen.

## Kompatibilität

- Ziel: Adonno ESP8266 / PN532 I²C, ESPHome 2026.9.1
- Grundlage: [adonno/tagreader](https://github.com/adonno/tagreader)
- Android: ISO-DEP mit SELECT AID und einer Kartenkennungs-APDU
- iPhone: später, abhängig von Apples Berechtigungen

## Lizenz

GNU GPL v3. Herkunft und Änderungen übernommener ESPHome-Dateien werden in [docs/UPSTREAM.md](docs/UPSTREAM.md) dokumentiert. Unabhängiges Community-Projekt, nicht von Adonno oder ESPHome offiziell unterstützt.

Beiträge sind willkommen. Bitte keine WLAN-Daten, API-Schlüssel oder echte Mitglieds- und Kartennummern veröffentlichen.
