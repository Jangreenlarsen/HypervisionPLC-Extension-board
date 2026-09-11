# Changelog

Nyeste øverst. Format: `## [version build NNNN] — YYYY-MM-DD — beskrivelse`

---

## [0.2.0 build 0002] — 2026-09-11 — Modbus RTU PDU-kerne

Første stykke firmware-logik: hardware-uafhængig kerne for Modbus RTU-framing, testbar uden fysisk board (`pio test -e native`). PlatformIO-projektet oprettet i samme omgang, da intet kunne bygges/testes uden det.

**Filer tilføjet:**
- `platformio.ini` — build-config: `esp32dev` (target-build) + `native` (host-side unit-tests, ingen ESP32-toolchain nødvendig)
- `lib/modbus_pdu/modbus_pdu.h`, `lib/modbus_pdu/modbus_pdu.cpp` — `mb_error_code_t` (matcher PLC-repoets enum 1:1 + ny `MB_CHANNEL_UNREACHABLE`), CRC16, `mb_pdu_build_rtu_request()`, `mb_pdu_expected_response_frame_len()`, `mb_pdu_response_frame_complete()`, `mb_pdu_parse_rtu_response()` — dækker FC01/02/03/04/05/06/16 jf. designdokumentets §4.1. Lagt i `lib/` (ikke `src/`) bevidst, så PlatformIOs Library Dependency Finder kun linker den ind hvor den faktisk bruges — forhindrer at fremtidige FreeRTOS/SPI-afhængige `src/`-filer trækkes ind i den hardware-uafhængige `native`-testbuild.
- `src/main.cpp` — minimal firmware-stub (tomt `setup()`/`loop()`), så `pio run -e esp32dev` kan verificere target-kompilering før den rigtige applikationslogik findes
- `test/test_modbus_pdu/test_modbus_pdu.cpp` — 23 unit-tests: CRC16 (inkl. det klassiske Modbus-spec-eksempel), frame-building, svar-længde-prædiktion for alle 7 function codes (inkl. afvisning af ukendte FC'er og spec-grænse-brud), delvis-frame-detektion, exception-parsing, CRC/slave-validering, FC16 round-trip. Alle CRC-testvektorer krydsverificeret med en uafhængig Python-implementering af samme algoritme, ikke kun mod egen kode.
- `.gitignore` — udelukker `.pio/` (build-cache/downloadede toolchains, ~24MB, regenereres af `pio run`)

**Miljø:** MinGW-w64 (gcc/g++ 16.1.0) installeret via chocolatey, så `pio test -e native` kan køre på denne maskine — der var ingen host-C++-compiler i forvejen (kun ESP32-cross-compileren, som PlatformIO selv henter, men den kan ikke køre testbinaries lokalt).

**Verificeret:** `pio test -e native` → 23/23 bestået. `pio run -e esp32dev` → bygger rent (ingen advarsler med `-Wall -Wextra`). Ikke testet på fysisk hardware endnu (afventer Fase 1, kræver fysisk board).

## [0.1.0 build 0001] — 2026-09-11 — Projektinitialisering

**Filer tilføjet:**
- `CLAUDE.md` — projektdefinition og systemregler, udfyldt fra `_ProjectTemplate` for HypervisionPLC Extension board
- `version.json` — versionskilde (0.1.0 build 0001)
- `ARCHITECTURE.md` — lagdelt arkitekturbeskrivelse (netværks-/protokollag → kanal-eksekveringslag → hardware-abstraktionslag)
- `FEATURES.md` — feature-backlog seedet fra designdokumentets implementeringsfaser
- `BUGS.md` — bug-register (tomt)
- `CHANGELOG.md` — denne fil
- `RELEASE_NOTES.md` — release-noter
- `README.md` — projektoversigt
- `EXPANSION_BOARD_DESIGN.md` — fuldt design-dokument for expansion-boardets hardware, firmware og protokoller (allerede tilstede ved projektstart)
- `reference-plc-source/` — statiske kode-referencer fra Hypervision PLC-repoet, brugt som implementeringsskabeloner (allerede tilstede ved projektstart)
