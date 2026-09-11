# Changelog

Nyeste øverst. Format: `## [version build NNNN] — YYYY-MM-DD — beskrivelse`

---

## [0.4.0 build 0004] — 2026-09-11 — Seriel CLI koblet til rigtig hardware (første kørsel på fysisk board)

**Version/build i selve firmwaren (Jan bemærkede at firmwaren ikke viste sit eget versionsnummer noget sted):** `version.json` var hidtil kun læst af dokumentation/commit-beskeder — firmwaren selv anede ikke sin egen version. `extract_version.py` (PlatformIO `extra_scripts`, kun `esp32dev`-target'et) injicerer nu `version.json`s `version`+`build` som `FW_VERSION`/`FW_BUILD`-compile-time-defines ved hver build, så `version.json` forbliver den ENESTE kilde (CLAUDE.md regel 1) også for den kørende firmware. Vises i boot-banneret og via en ny `version`-kommando. `native`-miljøet (unit-tests) får dem bevidst IKKE injiceret — testen verificerer fallback-teksten, ikke et hardkodet versionsnummer der ellers skulle opdateres ved hver bump.

**Filer tilføjet:**
- `extract_version.py` — PlatformIO pre-build-script, se ovenfor.
- `src/provisioning.cpp/.h` — læser linjer fra `Serial` (lokal ekko, backspace-håndtering, CR/LF-tolerant), kalder `lib/provisioning_cli/`, og udfører et RIGTIGT `WiFi.begin()`-forsøg ved `connect` (DHCP eller statisk IP via `WiFi.config()`, 30 sek. timeout, jf. §3.4.1's fejlhåndtering). `factory-reset confirm` genstarter boardet (NVS-rydning følger med `config.cpp`, ikke implementeret endnu). Boot-banner viser `FW_VERSION`/`FW_BUILD`.

**Filer ændret:**
- `src/main.cpp` — kalder nu `provisioning_begin()`/`provisioning_poll()` i stedet for at være en tom stub.
- `lib/provisioning_cli/provisioning_cli.h` — `MB_PROV_MSG_MAX_LEN` hævet fra 96 til 192 bytes (se BUGS.md — den gamle værdi afkortede `help`-kommandoens svartekst midt i en sætning).
- `lib/provisioning_cli/provisioning_cli.cpp` — `help`-teksten nævnte ikke sig selv som kommando; tilføjet. Ny `version`-kommando (samme `FW_VERSION`/`FW_BUILD`-defines, fallback-tekst når de ikke er sat).
- `lib/provisioning_cli/provisioning_cli.h` — `PROV_ACTION_VERSION` tilføjet til resultat-enum'en.
- `test/test_provisioning_cli/test_provisioning_cli.cpp` — `test_help_action` tjekker nu at ALLE kommandoer faktisk er til stede i `help`-output, ikke kun at strengen er ikke-tom (den svage version fangede ikke afkortnings-bugen ovenfor).

**Verificeret PÅ FYSISK HARDWARE** (ESP32-board tilsluttet via USB/CH340, COM6) — første gang firmwaren kører på rigtig hardware:
- Compileret (`pio run -e esp32dev`) og uploadet (`pio run -t upload --upload-port COM6`).
- Selv-testet med et scriptet pyserial-baseret smoke-test (sender kommandoer, læser svar, tjekker indhold programmatisk — ikke kun "kompilerer") mod boardet: `help`, `show`, `wifi ssid/pass`, `plc ip`, `connect` (rigtigt `WiFi.begin()`-forsøg, timede korrekt ud efter 30 sek. mod et ikke-eksisterende testnetværk), og en eksplicit kontrol af at password ALDRIG optræder i klartekst i noget seriel-output.
- Denne test fandt selve `MB_PROV_MSG_MAX_LEN`-bugen (BUGS.md) — `pio test -e native` fangede den IKKE, fordi test-koden brugte samme (for lille) buffer-konstant som produktionskoden. Lektion: en native-testsuite der deler en konstant med koden den tester, kan ikke opdage at konstanten selv er forkert — kun test mod den faktiske output-grænse (her: rigtig seriel-linje-bredde) afslørede det.
- `pio test -e native` → 56/56 stadig bestået efter rettelsen.

**Stadig IKKE verificeret på hardware:** en ægte WiFi-forbindelse til et rigtigt netværk (kun timeout-stien er testet, med opdigtede credentials) — kræver Jans faktiske WiFi-oplysninger, som Claude ikke har og ikke bør gætte på.

## [0.3.0 build 0003] — 2026-09-11 — Seriel provisioning-CLI + design-revision (droppet AP-mode)

**Designændring (EXPANSION_BOARD_DESIGN.md, alle referencer til §3.4 opdateret):** WiFi/PLC-IP-provisionering sker nu UDELUKKENDE via en seriel CLI over USB, ikke en midlertidig WiFi AP-mode + webside som tidligere designet. Begrundelse dokumenteret i §3.4: seriel/USB er fysisk uafhængig af WiFi (løser "hønen og ægget" uden mode-switching), kræver fysisk USB-adgang (ingen trådløs angrebsflade for et fabriksnyt board, jf. §8), og CLI'en er — modsat den droppede AP-mode-side — permanent tilgængelig, ikke kun ved fabriksnyt board (indbygget recovery-vej ved WiFi-password-skift). §3.4.1 (tidligere AP-mode-sidens felt-spec) er erstattet af CLI-kommandospecifikationen. `http_server.cpp`/§4.2's auth-afsnit forenklet: ALLE HTTP-endpoints kræver nu token uden undtagelse, da provisionering ikke længere er en del af HTTP-laget.

**Filer tilføjet:**
- `lib/provisioning_cli/provisioning_cli.h`, `lib/provisioning_cli/provisioning_cli.cpp` — kommando-tokenizer (understøtter citerede værdier med mellemrum, fx SSID'er), felt-validering (SSID 1-32 tegn, WPA2-password 8-63 tegn, IPv4-dotted-quad, dhcp/static-mode), og tilstandsopbygning for kommandoerne `wifi ssid/pass/open/mode/ip/mask/gw`, `plc ip`, `show`, `connect`, `factory-reset confirm`, `help`. 100% hardware-uafhængig (ingen Arduino/FreeRTOS-afhængighed) — samme `lib/`-mønster som `modbus_pdu`.
- `test/test_provisioning_cli/test_provisioning_cli.cpp` — 33 unit-tests: validator-grænser (SSID/password/IPv4), alle kommandoer inkl. citerede SSID'er med mellemrum, versalfølsomheds-regler (kommando-ord ikke versalfølsomme, værdier er), `connect`-readiness (dhcp/static/åbent netværk, manglende felter navngivet ét ad gangen), `factory-reset confirm`-kravet, og eksplicit verifikation af at password ALDRIG optræder i klartekst i noget `out_message` (hverken ved `wifi pass` eller `show`).

**Verificeret:** `pio test -e native` → 56/56 bestået (23 modbus_pdu + 33 provisioning_cli). `pio run -e esp32dev` → bygger fortsat rent. Ikke testet på fysisk hardware endnu — selve NVS-skrivning/WiFi-forbindelse (`src/provisioning.cpp`) er ikke implementeret, kun kommando-parsingen der skal kalde ind i den.

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
