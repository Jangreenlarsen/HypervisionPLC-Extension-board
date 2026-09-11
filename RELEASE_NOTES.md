# Release Notes

---

## v0.3.0 — 2026-09-11 — Seriel provisioning-CLI (AP-mode droppet fra designet)

WiFi/PLC-IP-opsætning sker fremover udelukkende via en seriel CLI over USB — en tidligere planlagt WiFi AP-mode + webside er droppet (se EXPANSION_BOARD_DESIGN.md §3.4 for den fulde begrundelse: ingen trådløs angrebsflade, simplere firmware, CLI'en forbliver tilgængelig permanent i stedet for kun ved fabriksnyt board). Kommando-parsing/validering (`lib/provisioning_cli/`) er implementeret og fuldt testet — 33/33 unit-tests bestået, plus de 23 fra v0.2.0 (56/56 samlet). Selve NVS-skrivningen og WiFi-forbindelsesforsøget (`src/provisioning.cpp`) er endnu ikke bygget — det kræver fysisk hardware (Fase 3).

**Næste skridt**: Fase 1 (Hardware-bring-up) — anskaf en ESP32 gateway-MCU + én UART-expander-chip (MAX14830 eller SC16IS752) og verificér SPI-kommunikation samt én RS485-kanal mod en kendt Modbus RTU-slave, jf. designdokumentets §9. Kræver fysisk hardware og kan ikke udføres af Claude alene. Når hardwaren er i hånden, kan `src/provisioning.cpp` bygges oven på `lib/provisioning_cli/` (kalder ind i den ved hver seriel linje) og `lib/modbus_pdu/` genbruges direkte af `modbus_channel.cpp`.

---

## v0.2.0 — 2026-09-11 — Modbus RTU PDU-kerne

Første kodeleverance: PlatformIO-projektet er sat op (`esp32dev`-target + `native`-testmiljø), og den hardware-uafhængige Modbus RTU-protokolkerne (`lib/modbus_pdu/`) er implementeret og fuldt testet — CRC16, RTU-frame-opbygning, svar-længde-prædiktion og svar-parsing (inkl. exception-håndtering) for alle 7 function codes boardet skal understøtte (FC01-06/16). 23/23 unit-tests bestået (`pio test -e native`), bygger også rent til ESP32-target. Ikke kørt mod fysisk hardware endnu.

**Næste skridt**: Fase 1 (Hardware-bring-up) — anskaf en ESP32 gateway-MCU + én UART-expander-chip (MAX14830 eller SC16IS752) og verificér SPI-kommunikation samt én RS485-kanal mod en kendt Modbus RTU-slave, jf. designdokumentets §9. Kræver fysisk hardware og kan ikke udføres af Claude alene. Når SPI-driveren (`uart_expander.cpp`) og kanal-tasken (`modbus_channel.cpp`) findes, kan de genbruge `lib/modbus_pdu/` direkte.

---

## v0.1.0 — 2026-09-11 — Projektinitialisering

Projektstruktur og dokumentation oprettet. Design-dokumentet ([EXPANSION_BOARD_DESIGN.md](EXPANSION_BOARD_DESIGN.md)) er læst og lagt til grund for `ARCHITECTURE.md` og `FEATURES.md`. Ingen firmware-kode skrevet endnu.

**Næste skridt**: Fase 1 (Hardware-bring-up) — anskaf en ESP32 gateway-MCU + én UART-expander-chip (MAX14830 eller SC16IS752) og verificér SPI-kommunikation samt én RS485-kanal mod en kendt Modbus RTU-slave, jf. designdokumentets §9. Kræver fysisk hardware og kan ikke udføres af Claude alene.
