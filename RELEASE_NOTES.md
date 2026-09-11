# Release Notes

---

## v0.4.0 — 2026-09-11 — Første kørsel på fysisk hardware

Den serielle provisioning-CLI er nu koblet til rigtig `Serial`-I/O og et rigtigt `WiFi.begin()`-forbindelsesforsøg (`src/provisioning.cpp`) — og er compileret, uploadet og testet interaktivt på et fysisk ESP32-board (USB/CH340) for første gang. Et scriptet seriel-smoke-test fandt og bekræftede rettelsen af en rigtig bug: `help`-kommandoens svartekst blev afkortet midt i en sætning fordi buffer-konstanten var for lille (se BUGS.md) — noget den native testsuite ikke kunne fange, fordi den delte samme (forkerte) konstant med produktionskoden.

Firmwaren viser nu også sin egen version+build (boot-banner + ny `version`-kommando), automatisk hentet fra `version.json` ved build-tid (`extract_version.py`) — `version.json` forbliver den ene kilde, nu også for den kørende firmware, ikke kun dokumentationen.

**Endnu ikke testet:** en ægte forbindelse til et rigtigt WiFi-netværk (kun timeout-fejlstien er verificeret) — kræver rigtige netværks-credentials. NVS-persistering, management-API-token og firewall-seed mangler stadig (`config.cpp`, resten af Fase 3).

**Næste skridt**: byg `config.cpp` (NVS-persistering + schema-versionering, §3.5) så `connect` rent faktisk gemmer WiFi-config og udsteder et management-API-token, der overlever en genstart — færdiggør dermed Fase 3. Herefter Fase 1 (SPI + UART-expander-chip) når den hardware er i hånden.

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
