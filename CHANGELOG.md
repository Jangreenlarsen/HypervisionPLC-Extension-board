# Changelog

Nyeste øverst. Format: `## [version build NNNN] — YYYY-MM-DD — beskrivelse`

---

## [0.30.0 build 0047] — 2026-09-24 — PLC-styret OTA: identitetstjek, MD5, bekræftelse + automatisk rollback

**Baggrund:** Jan: "kan vi implementere OTA så det kan styre og upload fra PLC, hvis ja og med en plan som jeg kan tag med over til PLC team" / "vi har firewall access regl for at kun PLC ip kan nå PLC expansions boardet så der for tænker jeg at hvis vi kan styre det fra plc". v0.12.0's `POST /api/ota` virkede, men havde intet sikkerhedsnet for en fjernstyret opdatering: PLC'ens egen `.bin` (også en ESP32-firmware, `0xE9`) ville være accepteret, og en ny firmware der ikke kom på nettet igen krævede fysisk USB-adgang.

**`lib/ota_validation/`:**
- Firmware-identitets-scanner (`mb_fwid_scanner_*`) — streaming-søgning efter `HVEXT-FWID:hypervisionplc-extension-board:<version>;` på tværs af bid-grænser.
- `mb_ota_is_valid_md5_hex()` og `mb_ota_build_status_json()` (JSON-escaping af fejltekst; hellere tomt end halv JSON ved for lille buffer).

**Ny `src/ota_manager.cpp/.h`** (tværgående, delt af REST og CLI):
- Indlejret identitets-markør `g_mb_firmware_id` (`used` + volatile-læsning, så linkeren ikke fjerner den — verificeret i `firmware.bin`).
- Overstyrer Arduino-corens `verifyRollbackLater()` (bootloaderen har `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`): første opstart efter OTA (`ESP_OTA_IMG_PENDING_VERIFY`) godkendes IKKE automatisk. `ota_manager_confirm()` → `esp_ota_mark_app_valid_cancel_rollback()`; ellers ruller en deadline-task tilbage efter `kOtaConfirmTimeoutS` = 600 s (`esp_ota_mark_app_invalid_rollback_and_reboot()`). Crash/genstart inden bekræftelse → bootloaderen ruller selv tilbage.
- `last_update_rolled_back` udledt af `esp_ota_get_last_invalid_partition()`.

**`src/ota_handler.cpp`:**
- `POST /api/ota`: `400 ota_wrong_firmware` hvis markøren mangler; valgfri `X-Firmware-MD5` (`Update.setMD5()`); `411 length_required` uden Content-Length; `413 ota_too_large` før noget skrives; `409 ota_pending_confirm` mens den kørende firmware afventer bekræftelse (et upload ville overskrive rollback-målet); svaret har `new_version` + `md5_verified`; chunk-buffer nu `static` (ikke 2 KB på httpd-stakken).
- Nyt `POST /api/ota/confirm` (idempotent).
- `GET /api/ota/status` udvidet med `running_version`, `new_version`, `pending_confirm`, `confirm_remaining_s`, `last_update_rolled_back` (bagudkompatibelt — de gamle felter er uændrede).
- Syslog (facility SYSTEM) af upload start/succes/fejl med klient-IP, reboot-anmodninger, afventer-bekræftelse, bekræftelse og rollback (CLAUDE.md regel 11).

**CLI:** `ota confirm` (manuel bekræftelse over USB); `status` viser `ota.firmware_id`, `ota.pending_confirm`, `ota.last_rolled_back`; nyt `help ota`. **`src/main.cpp`:** `ota_manager_begin()` tidligt i `setup()`.

**Dokumentation:** ny **`PLC_OTA_INTEGRATION_PLAN.md`** — implementeringsplan til PLC-teamet (streaming-relay browser → PLC → board, async-kald, web-UI-forløb, fejltekster, acceptkriterier). `PLC_INTEGRATION_MANUAL.md` §4.7/§7, `ARCHITECTURE.md`, `CLAUDE.md` opdateret.

**Tests:** 11 nye i `test_ota_validation` (markør over alle bid-grænser 1-64, fremmed firmware, prefix-literal alene, grænser for versionslængde, MD5-format inkl. tom/whitespace, status-JSON inkl. escaping og for lille buffer) + `test_ota_confirm_action` og `help ota`/`help ota.pending_confirm` i `test_provisioning_cli`.

**Filer ændret:** `lib/ota_validation/*`, `src/ota_manager.cpp/.h` (ny), `src/ota_handler.cpp`, `src/main.cpp`, `src/provisioning.cpp`, `lib/provisioning_cli/*`, `test/test_ota_validation/*`, `test/test_provisioning_cli/*`, `PLC_OTA_INTEGRATION_PLAN.md` (ny), `PLC_INTEGRATION_MANUAL.md`, `ARCHITECTURE.md`, `CLAUDE.md`, `FEATURES.md`, `version.json`.

**Status:** 317/317 native-tests grønne. Bygger for esp32dev. **Live-verificeret over Ethernet** (curl, samme kald som PLC'en skal lave): OTA 0.29.1→0.30.0, afventer-bekræftelse, 409 under pending, confirm (idempotent), 400 ota_wrong_firmware (image med ødelagt markør), 413 (PLC'ens firmware), bad_md5/forkert MD5, 411, automatisk rollback ved deadline, øjeblikkelig rollback ved reboot under pending — detaljer i PLC_OTA_INTEGRATION_PLAN.md §9. Boardet kører nu den endelige, bekræftede v0.30.0-b0047. **Ikke live-verificeret:** CLI `ota confirm`/`status`-linjerne (COM8 optaget).

## [0.29.1 build 0046] — 2026-09-24 — fix: gemt konfiguration forsvandt efter genstart (plc ip m.m.)

**Baggrund:** Jan: "save fungere ikke som den skal plc ip bliver ikke save m.m." — se BUGS.md v0.29.1.

**`src/provisioning.cpp`:** `provisioning_begin()` indlæser nu ALTID CLI'ens arbejdskopi (`g_state`) fra den gemte konfiguration (`mb_config_to_provisioning_state()`), ikke kun når boardet er `provisioned` (= har haft en vellykket WiFi-`connect`). Tidligere startede et board sat op med `save` alene med en tom `g_state` — `show` viste "(ikke sat)", og næste `save`/`rest ...` overskrev hele NVS med de tomme felter. Den automatiske WiFi-genforbindelse er uændret betinget af `provisioned`.

**`lib/board_config/board_config.cpp`:** `mb_config_to_provisioning_state()` udleder nu `has_ip/has_mask/has_gw` af om feltet har indhold, i stedet for af `wifi_static_ip` (som antog at de kun persisteres ved en vellykket `connect` — ikke sandt, `save` persisterer dem også).

**Tests:** ny `test_saved_config_without_connect_survives_reboot` (`test/test_board_config/`) — apply → rigtig NVS-blob-serialisering → load → CLI-state for et board der aldrig har kørt `connect`, inkl. et delvist udfyldt static-sæt.

**Filer ændret:** `src/provisioning.cpp`, `lib/board_config/board_config.cpp`, `test/test_board_config/test_board_config.cpp`, `BUGS.md`, `version.json`.

**Status:** 305/305 native-tests grønne. Bygger for esp32dev. **Live-verificeret** på fysisk hardware (Ethernet-only board, aldrig WiFi-`connect`): `show` viste `plc.ip: 10.1.1.30` straks efter opstart (før rettelsen: "(ikke sat)"), og værdien overlevede `save` → `reboot` → `show`.

## [0.29.0 build 0045] — 2026-09-24 — Udførlig, sektionsopdelt `help` i den serielle CLI

**Baggrund:** Jan: "opret en help i cli som forklar alle cli commandos hvad de skal brugs" / "da det man se i cli ikke er det samme som de kommando man skal slå så skal vi bruge en help" / "i den help skal ting være opdelt i seksioner". Feltnavnene i `show` (fx `rest.auth_mode`) svarer ikke til de kommandoer man skriver (`rest auth ...`), og den gamle `help` var én lang, usorteret liste.

**`lib/provisioning_cli/provisioning_cli.cpp`:**
- `help` viser nu oversigten opdelt i sektioner (WiFi, Ethernet, Netværk og adgang, REST-API login, Logging og diagnose, Vis/gem/system, Hjælp), hver med henvisning til sit udførlige emne.
- Ny `help <emne>` — udførlig forklaring pr. emne (`kHelpTopics`: wifi, eth, hostname, plc, rest, token, syslog, debug, test, show, status, save, connect, reboot, factory-reset, version, board_mode, no, help) med faste undersektioner: Kommandoer / Vises i 'show' som / Træder i kraft / Eksempel.
- Ny `help <felt>` — et feltnavn fra `show`/`status` slås op direkte (`find_help_topic()`): delen før punktummet (`rest.auth_mode` → rest, `syslog.target2` → syslog) eller et alias (`firmware` → version, `mgmt.token` → token, `uptime_s` → status). `help show` giver en samlet felt → kommando-oversigt.
- Ukendt emne → `PROV_INVALID_VALUE` med listen over gyldige emner.
- Rettet undervejs: oversigtens `show`-linje sagde stadig "password maskeret" (forkert siden CLI'en viser alt i klartekst); `factory-reset`-beskrivelsen nævnte en "firewall" i stedet for det reelle (hele NVS inkl. kanal-opsætning); `rest` uden argumenter nævnte ikke `rest auth`.

**`lib/provisioning_cli/provisioning_cli.h`:** `MB_PROV_MSG_MAX_LEN` 2048 → 4096 (oversigt + emne-tekster overskred 2048).

**`src/provisioning.cpp`:** `message`-bufferen i `provisioning_poll()` er nu `static` — 4096 bytes på loopTask-stakken (8192) ville genintroducere BUGS.md v0.24.0's stack-overflow-klasse.

**Tests (`test/test_provisioning_cli/`):** 7 nye — sektioner i oversigten, fast margin (512 bytes) under bufferen for oversigt OG hvert emne (en test der deler konstanten med koden kan ellers ikke opdage at den er for lille, jf. v0.4.0-lektionen), `help rest`'s undersektioner, versal-uafhængighed, 16 `show`/`status`-felter slået op, ukendte emner, og at alle emner i `help help` findes og er uafkortede.

**Filer ændret:** `lib/provisioning_cli/provisioning_cli.cpp`, `lib/provisioning_cli/provisioning_cli.h`, `src/provisioning.cpp`, `test/test_provisioning_cli/test_provisioning_cli.cpp`, `FEATURES.md`, `CLAUDE.md` (testantal), `version.json`.

**Status:** 304/304 native-tests grønne. Bygger rent for esp32dev (ingen nye advarsler).

## [0.28.6 build 0044] — 2026-09-16 — Kanalnavnet er nu `mb_ch_A`/`mb_ch_B` (stort bogstav)

**Baggrund:** Jan: "ændre i debug output tekst 'mb_ch_a' til 'mb_ch_A' det samme for b til B".

**`src/modbus_channel.cpp`:** `modbus_channel_init_all()`s `task_name`-argument til `init_channel()` ændret fra `"mb_ch_a"`/`"mb_ch_b"` til `"mb_ch_A"`/`"mb_ch_B"` — rent kosmetisk, samme streng bruges både i alt debug-/syslog-output og som selve FreeRTOS-task-navnet, ingen kode andetsteds sammenligner disse strenge.

**Filer ændret:** `src/modbus_channel.cpp`, `FEATURES.md`.

**Status:** Ingen native-testbar logik ændret (rent `src/`-lag). Bygger rent for esp32dev. **Live-verificeret** på fysisk hardware (kanal A): alle linjer i en komplet transaktion viste konsekvent `mb_ch_A`.

## [0.28.5 build 0043] — 2026-09-16 — Debug-tidsstemplet gjort menneskelæseligt

**Baggrund:** Jan: "kan vi ikke gør den timestamp mere pæn at se på jeg tænker få dag timer sekundær millisekundær på [d:t:s:m]" — v0.28.4's rå `[millis]`-millisekund-tal var svært at læse på et øjekast.

**Afklaret via 1 spørgsmål:** Jans egen notation "[d:t:s:m]" har kun 4 felter (dag/timer/sekunder/millisekunder), uden minutter — afklaret om det var bevidst (sekunder tæller så 0-3599 inden for timen) eller en forglemmelse. Valgte det fulde, standard 5-felts urs-format MED minutter (`D:HH:MM:SS.mmm`).

**`src/modbus_channel.cpp`:** ny `format_uptime()` — omregner `millis()` til dage:timer:minutter:sekunder.millisekunder (fx `[0:00:19:51.352]`). Erstatter alle 5 steder der tidligere printede det rå `[%lu]`-tal (de tre delte hjælpere `debug_line()`/`debug_packet()`/`debug_decode()`, samt RX-byte-timing-loopet og `MB_NOT_ENABLED`-grenen).

**Filer ændret:** `src/modbus_channel.cpp`, `FEATURES.md`.

**Status:** 297/297 native-tests upåvirket. Bygger rent for esp32dev. **Live-verificeret** på fysisk hardware (kanal A, level 8): tidsstemplet viste nu `[0:00:00:01.192]`-stil gennem en komplet transaktion, korrekt voksende linje for linje (`01.192` → `01.206` → `01.220` → ... → `01.374`).

## [0.28.4 build 0042] — 2026-09-16 — Millisekund-tidsstempel på debug-/syslog-linjer

**Baggrund:** Jan: "kan vi få timestamp på debug".

**`src/modbus_channel.cpp`:** hver debug-/syslog-linje starter nu med `[millis]` (ms siden boot) — boardet har ingen RTC/NTP, så det er det eneste rigtige, altid-tilgængelige tidsstempel (samme grundlag alle "ventede Xms"-angivelser allerede bruger). Tilføjet til de tre delte hjælpere (`debug_line()`/`debug_packet()`/`debug_decode()`, v0.28.3) OG de to steder der bevidst omgår dem: pr.-byte RX-timing-loopet (level 4) og `MB_NOT_ENABLED`-grenen i `channel_task()` (som aldrig kalder `execute_transaction()`). Pr.-byte-tidsstemplerne er et reelt, KUMULERET absolut tidspunkt (`txn_start` + løbende summeret ventetid), ikke et fladt `millis()`-kald ved print-tidspunktet (som ville have vist samme, misvisende tidsstempel for alle bytes, da hele listen printes samlet efter modtagelsen er afsluttet) — dokumenteret unøjagtighed: kun byte[0] kan ramme `rx_wait_ms`s 255ms-saturering (efterfølgende bytes bruger `interchar_ms`, maks 20ms, og saturerer derfor aldrig), så en evt. fejl er en konstant forskydning, ikke en voksende.

**Filer ændret:** `src/modbus_channel.cpp`, `FEATURES.md`.

**Status:** 297/297 native-tests upåvirket (ingen native-testbar logik ændret). Bygger rent for esp32dev. **Live-verificeret** på fysisk hardware (kanal A, level 8): samtlige linjer fik et korrekt, monotont voksende `[millis]`-tidsstempel, og de kumulerede pr.-byte-tidsstempler stemte PRÆCIST overens med de allerede viste "ventede Xms"-deltaer (fx `[1254] byte[0] (ventede 63ms)` → `[1255] byte[1] (ventede 1ms)` → ... → `[1262] byte[6] (ventede 2ms)`, alle verificeret som `forrige ts + ventetid`).

## [0.28.3 build 0041] — 2026-09-16 — Ensrettet, kompakt debug-/syslog-linjeformat

**Baggrund:** Jan: "kan vi gøre det output mere lækket med en mere klar afgrænsning af de modtage data samt sende data, sådan pakkeren bliver mere let læselige" — pegede konkret på at `DEBUG RX: 09 03 02 00 00 59 85` manglede kanalnavnet, modsat resten af linjerne (`DEBUG mb_ch_a: ...`). Foreslog selv et nyt format med `<RX<`/`>TX>`-retningsmarkører og et kompakt `ID:/FC:/Addr:/CRC:/...`-feltformat for decode-linjen.

**Afklaret via 2 spørgsmål:** (1) skift `mb_pdu_decode()`s indhold til det kompakte feltformat (valgt, i stedet for at beholde de fulde engelske sætninger fra v0.28.2) — (2) brug Jans egne `>TX>`/`<RX<`-pile som retningsmarkør (valgt, i stedet for simplere "TX:"/"RX:").

**`src/modbus_channel.cpp`:** al debug-/syslog-udskrivning i `execute_transaction()` ensrettet til ÉN fast skabelon: `DEBUG <kanal> <retning> <label>: <indhold>`. Tre nye delte hjælpere: `debug_line()` (variadic, generisk enkelt-linje, sender BÅDE Serial bag `dbg>=min_level` OG syslog ubetinget — samme "syslog er en uafhængig kanal"-princip som hele v0.26.0), `debug_packet()` (hex-dump, nu MED kanalnavn — det var netop denne linje der manglede det), `debug_decode()` (afkoder den FULDE RTU-frame, ikke kun PDU'en — lægger `ID: <hex slave-adresse>` foran og `CRC: <hex hex>` bagved `mb_pdu_decode()`s indhold, plus `Status: <resultat>` for RX-linjen, da det endelige udfald er kendt på det tidspunkt). RX-byte-timing (level 4) forbliver bevidst en SÆRSKILT, ikke-delt kodesti — syslog skal stadig kun have ÉT samlet resumé pr. transaktion, ikke én pakke pr. byte (uændret fra v0.26.0's "flood ikke netværket"-hensyn).

**`lib/modbus_pdu/`:** `mb_pdu_decode()`s outputformat ændret fra fulde sætninger ("Read Holding Registers: addr=0 qty=1") til Jans kompakte feltformat ("FC: 03, Addr: 0, Qty: 1") — FC vises nu i HEX (matcher hex-dump-linjens bytes for nem krydsreference), Addr/Qty/Values forbliver decimal. FC05/06's tidligere " (ack)"-tekst-suffiks på svar er fjernet — request og svar er byte-identiske (et ægte ekko), og retningen fremgår nu i forvejen af kaldstedets `>TX>`/`<RX<`-præfiks.

**Filer ændret:** `lib/modbus_pdu/modbus_pdu.h/.cpp`, `src/modbus_channel.cpp`, `test/test_modbus_pdu/test_modbus_pdu.cpp`, `FEATURES.md`.

**Status:** 297/297 native-tests bestået (14 opdaterede). Bygger rent for esp32dev. **Live-verificeret** på fysisk hardware (kanal A, level 8): samtlige linjer i en komplet transaktion fulgte nu det ensrettede format uden undtagelse — `>TX> start`, `>TX> decode` (`ID: 09, FC: 03, Addr: 0, Qty: 1, CRC: 85 42`), `>TX> dir`, `>TX> packet`, `<RX< dir`, `<RX< byte[N]` (×7), `<RX< packet`, `<RX< decode` (`ID: 09, FC: 03, Values: [17942], CRC: EA 2B, Status: MB_OK`), `<RX< parse`, `<RX< result` — ingen linje mangler længere kanalnavn/retning.

## [0.28.2 build 0040] — 2026-09-16 — Menneskelæselig Modbus-PDU-decode i debug-/syslog-outputtet

**Baggrund:** Jan: "kan vi ikke få en modbus protocol frame pakke decode med i det debug output" — hidtil viste `debug modbus ...` kun rå hex (level 7/8) og et generisk slave/fc/pdu_len-resumé (level 1), aldrig selve INDHOLDET af en frame (adresse, quantity, faktiske register-/coil-værdier).

**`lib/modbus_pdu/`:** ny `mb_pdu_decode()` — hardware-uafhængig, native-testet (14 nye tests). Fortolker en PDU (request ELLER response) til læsbar tekst for alle 8 understøttede FC'er, fx `"Read Holding Registers: addr=0 qty=1"` / `"Read Holding Registers: [17942]"` / `"Write Multiple Coils: addr=0 qty=3 values=[1,0,1]"` / `"Exception: Illegal Data Address (0x02)"`. Værdilister afkortes ved 20 elementer.

**`src/modbus_channel.cpp`:** ny `log_pdu_decode()`-hjælper, kaldt fra `execute_transaction()` for BÅDE TX og RX — samme "altid til syslog, kun Serial bag `debug modbus level>=1`"-mønster som resten af filen. RX-decode kaldes KUN når CRC/slave-adresse allerede er bekræftet gyldig (`MB_PDU_RESULT_OK`/`_EXCEPTION`) — `out_pdu` er først reelt udfyldt på det tidspunkt.

**Live-observeret og rettet bug undervejs:** den første implementering af `append_register_list()`/`append_bit_list()` (interne hjælpere) null-terminerede ikke bufferen korrekt efter den afsluttende `]` (en rå `out[pos++]=']'`-tildeling overskrev den forrige terminator uden at sætte en ny) — gav trailing garbage-bytes i outputtet, og for lange værdilister en reel krasch (`SIGILL`) i native-testen, da `strcmp`/`strstr` læste langt forbi den tiltænkte streng. Fundet af testene selv (`test_decode_fc03_response` m.fl.) FØR nogen live-upload. Rettet med eksplicit `out[pos]='\0'` (samme konvention som `mb_status_build_json`, `lib/rest_status`).

**Filer ændret:** `lib/modbus_pdu/modbus_pdu.h/.cpp`, `src/modbus_channel.cpp`, `test/test_modbus_pdu/test_modbus_pdu.cpp`, `FEATURES.md`.

**Status:** 297/297 native-tests bestået (14 nye). Bygger rent for esp32dev. **Live-verificeret** på fysisk hardware (kanal A, slave 9): en rigtig FC03-læsning viste både `DEBUG mb_ch_a: >> Read Holding Registers: addr=0 qty=1` (TX-decode) og `DEBUG mb_ch_a: << Read Holding Registers: [17942]` (RX-decode, den FAKTISKE registerværdi) — bekræftet gentagne gange, inkl. under en anden slave-enheds periodiske baggrundspolling (register 256 → `[0]`).

## [0.28.1 build 0039] — 2026-09-16 — Kanal-fejl vises nu kun på konsollen når debug er slået til

**Baggrund:** Jan: "vi har i dag output ved fejl til console lave det om sådan vi ikke har det output men kun hvis vi bruger debug til at output til console".

**`src/modbus_channel.cpp`:** fjernede den ubetingede `MODBUS-FEJL kanal ...`-Serial-linje fra `channel_task()` — den fyrede hidtil for HVER fejlende transaktion på BEGGE kanaler, uanset debug-tilstand (kun undertrykt hvis en ANDEN kanal havde debug slået til, v0.25.1). Al Serial-fejlvisning sker fremover UDELUKKENDE via `debug modbus ...`s eksisterende `level>=1`-opsummeringslinjer i `execute_transaction()`, plus en ny, tilsvarende linje for `MB_NOT_ENABLED` (den ene fejltilstand der aldrig går igennem `execute_transaction()`, da kanalen slet ikke er aktiveret). v0.25.1's `any_channel_debug_active()`-hjælper er fjernet som overflødig (dens formål — undertrykke krydskanal-støj — er nu opnået strukturelt, ved slet ikke at printe noget uden om debug-systemet).

**syslog upåvirket:** syslog (v0.26.0) er en bevidst UAFHÆNGIG udgangskanal med sin egen, pr.-modtager-konfigurerede verbositet — den modtager fortsat alle fejl uanset om CLI-debug er slået til, denne ændring gælder KUN den serielle konsol.

**Filer ændret:** `src/modbus_channel.cpp`, `FEATURES.md`.

**Status:** 283/283 native-tests upåvirket (ingen native-testbar logik ændret — rent `src/`-lag), bygger rent for esp32dev. **Live-verificeret** på fysisk hardware: med debug FRA var konsollen fuldstændig stille i et 4-sekunders observationsvindue (ingen `MODBUS-FEJL`-linjer, selv under trafik der tidligere ville have logget); med `debug modbus a level 1` slået til viste konsollen straks de forventede `DEBUG mb_ch_a: ...`-linjer igen.

## [0.28.0 build 0038] — 2026-09-15 — `GET /api/capabilities` + dedikeret "unsupported function code"-fejlkode

**Baggrund:** Jan: "vi skal have lavet en udvidelse til test se efter i projekt folde efter en fil 'DESIGN_GUIDE_MODBUS_EXPANSION_FC_CAPABILITIES.md' hvor det er beskrivet" — et designforslag fra PLC-udviklingsteamet (allerede i repoets rod), skrevet efter PLC-siden fik et empirisk "Funktions-test"-panel og indså at der ingen live-forespørgelig capabilities-API fandtes. Implementeret i sin helhed (§1 anbefalede løsning + §2's supplerende fejlkode-oprydning, begge valgt via AskUserQuestion).

**`lib/modbus_pdu/`:** ny `MB_UNSUPPORTED_FUNCTION=10` i `mb_error_code_t` (adskilt fra `MB_INVALID_ADDRESS=7`, som nu KUN dækker "ugyldig adresse/quantity for en ELLERS kendt FC"). Ny `MB_PDU_SUPPORTED_FUNCTIONS[8]`/`MB_PDU_SUPPORTED_FUNCTION_COUNT` — ÉN kilde til "hvilke FC'er understøtter boardet", brugt af det nye endpoint. Modbus-spec'ens pr.-FC quantity-grænser navngivet (`MB_PDU_MAX_READ_BIT_QUANTITY` m.fl.) i stedet for spredte magic numbers. Ny meta-test (`test_supported_functions_array_matches_switch`) krydstjekker FC-listen mod selve switch-logikken for ALLE 256 mulige byte-værdier — forhindrer eksplicit den "to lag drifter fra hinanden"-fejlklasse designdokumentet advarer om (den ramte allerede PLC-siden én gang for FC15/16, se v0.27.1).

**`lib/diagnostic_modbus/`:** ny navngivet `MB_DIAG_MAX_READ_QUANTITY=2000` (erstatter et magic number i `mb_diag_parse_read_request()`), brugt af det nye endpoint.

**`lib/rest_status/`:** ny `mb_status_build_capabilities_json()` — bygger `GET /api/capabilities`s JSON (separate `modbus_tcp`/`rest_diagnostic`-lister, bevidst, jf. designdokumentets §1 begrundelse: de KAN divergere). 4 nye native-tests.

**`src/http_server.cpp`:** nyt, autentificeret `GET /api/capabilities`-endpoint (samme auth-regel som `/api/status`) — rent deklarativt, ingen bus-trafik/sideeffekter. Diagnostik-skrivnings-/læsnings-handleren giver nu en specifik besked ("Function code X ikke understøttet af dette board") for `MB_UNSUPPORTED_FUNCTION`, i stedet for den generiske "se error_code".

**`src/modbus_channel.cpp`:** `execute_transaction()` skelner nu `MB_PDU_UNSUPPORTED_FUNCTION` fra `MB_PDU_MALFORMED_REQUEST` (tidligere begge → `MB_INVALID_ADDRESS`).

**`src/modbus_tcp_server.cpp`:** ny `kIllegalFunction=0x01`-konstant — `gateway_exception_for()` mapper nu `MB_UNSUPPORTED_FUNCTION` til Modbus-STANDARDENS `0x01` "Illegal Function" (i stedet for det hidtil overbelastede `0x0A` "Gateway Path Unavailable", som fremover KUN dækker deaktiveret/util-gaengelig kanal) — matcher hvad en standard Modbus TCP-master allerede ved hvordan den skal fortolke.

**Dokumentation:** `PLC_INTEGRATION_MANUAL.md` §4.8 (nyt endpoint, fuldt skema), §5 (opdateret fejlkode-/exception-tabel), §6 (integrationsflow nævner nu capabilities-cache); `EXPANSION_BOARD_DESIGN.md` (endpoint-tabel); `DESIGN_GUIDE_MODBUS_EXPANSION_FC_CAPABILITIES.md`s egen status-linje markeret implementeret.

**Filer ændret:** `lib/modbus_pdu/modbus_pdu.h/.cpp`, `lib/diagnostic_modbus/diagnostic_modbus.h/.cpp`, `lib/rest_status/rest_status.h/.cpp`, `src/http_server.cpp`, `src/modbus_channel.cpp`, `src/modbus_tcp_server.cpp`, `test/test_modbus_pdu/`, `test/test_rest_status/`, `PLC_INTEGRATION_MANUAL.md`, `EXPANSION_BOARD_DESIGN.md`, `DESIGN_GUIDE_MODBUS_EXPANSION_FC_CAPABILITIES.md`, `FEATURES.md`.

**Status:** 283/283 native-tests bestået (12 nye), bygger rent for esp32dev. **Live-verificeret** på fysisk hardware:
1. `GET /api/capabilities` — svarede byte-for-byte som forventet: `{"api_version":1,"fw_version":"0.28.0","modbus_tcp":{"supported_function_codes":[1,2,3,4,5,6,15,16],"max_read_quantity":2000,"max_write_quantity":1968},"rest_diagnostic":{...,"max_write_quantity":32}}`.
2. Rå Modbus TCP-kald med en HELT ukendt FC (0x07, port 503) — gatewayen svarede nu korrekt med exception `0x01` "Illegal Function" (bekræftet via seriel log: `MODBUS-FEJL kanal mb_ch_b: slave=9 fc=7 -> MB_UNSUPPORTED_FUNCTION`), i stedet for det hidtidige `0x0A`.
3. **Fund undervejs:** `MB_UNSUPPORTED_FUNCTION`/`error_code:10` kan IKKE aktuelt nås via REST-diagnostikkens `/write`-endpoint — dens EGEN JSON-parser (`mb_diag_parse_write_request()`) afviser allerede en fc udenfor `{5,6,15,16}` med `400 Bad Request` FØR noget PDU overhovedet bygges/sendes til kanal-laget (samme mønster gælder `/read` og `{1,2,3,4}`). Koden i `src/http_server.cpp` der rapporterer `MB_UNSUPPORTED_FUNCTION` med en specifik besked er derfor pt. kun nåelig via Modbus TCP-data-planet (verificeret ovenfor) — men forbliver værdifuld som beskyttelse MOD netop den "to lag drifter fra hinanden"-fejlklasse designdokumentet advarer om, hvis REST-parserens egen FC-liste nogensinde løsnes uafhængigt af `lib/modbus_pdu`s.

## [0.27.1 build 0037] — 2026-09-15 — FC15's REST-kontrakt rettet til booleans

**Baggrund:** Jan bad om at krydstjekke v0.27.0's FC15/16-implementering mod PLC-udviklingsteamets egen spec for boardets Modbus TCP-data-plan og REST-diagnostik. Data-plan-delen (port 502/503, `lib/modbus_pdu`) stemte allerede fuldstændigt overens — `modbus_tcp_server.cpp` relayer PDU'en helt function-code-agnostisk, så tilføjelsen af `0x0F` til `mb_pdu_expected_response_frame_len()`s whitelist i v0.27.0 var alt der krævedes for at løse deres beskrevne problem ("MBX_WRITE_COILS fra ST Logic får en gateway-exception"). REST-diagnostik-delen (`POST /api/channels/{n}/write`) havde derimod en reel uoverensstemmelse: deres "Foreslået syntaks" for FC15 er `"values": [true, false, true, true]` (booleans), men v0.27.0's implementering genbrugte fejlagtigt FC16's tal-parser og forventede `[1, 0, 1]`.

**`lib/diagnostic_modbus/`:** ny `parse_bool_array_field()` — parser et JSON-array af `true`/`false`-literaler (ikke tal), brugt for FC15's `"values"`. Matcher IKKE kun PLC-teamets forslag, men også denne fils EGEN eksisterende konvention (FC05's `"value"` er allerede en bool, `parse_bool_field()`) — v0.27.0's tal-baserede udgave var inkonsistent med kodebasens egen etablerede stil. Et FC16-stil tal-array (`[1,0,1]`) for FC15 afvises nu bevidst med 400 Bad Request, i stedet for at blive stille fejlfortolket eller accepteret forkert.

**Bemærk (ikke rettet her, udenfor scope):** PLC-teamet fandt undervejs at PLC-repoets egen REST-test-panel-dispatcher (`api_handlers.cpp:5778`, `Modbus_server_slave_ESP32`-repoet) kun har en array-gren for `function_code==16` — vælges FC15 der i dag, sendes en forkert payload. Dette er en PLC-side-fejl i et andet repo, udenfor dette repos scope (CLAUDE.md's projektgrænse) — kræver koordinering med den, der vedligeholder det repo.

**Filer ændret:** `lib/diagnostic_modbus/diagnostic_modbus.h/.cpp`, `test/test_diagnostic_modbus/test_diagnostic_modbus.cpp`, `PLC_INTEGRATION_MANUAL.md`, `FEATURES.md`.

**Status:** 278/278 native-tests bestået (opdateret til boolsk syntaks, 1 ny afvisnings-test), bygger rent for esp32dev. **Live-verificeret** på fysisk hardware, alle 3 scenarier fra PLC-teamets spec:
1. REST med NY boolsk kontrakt (`values: [true,false,true,true]`) — accepteret, producerede TX-framen `09 0F 00 00 00 04 01 0D FE F5`, BYTE-IDENTISK med PLC-teamets eget eksempel-PDU (`0F 00 00 00 04 01 0D`, korrekt CRC tilføjet).
2. REST med GAMMEL tal-kontrakt (`values: [1,0,1]`) — korrekt AFVIST med 400 Bad Request.
3. Rå Modbus TCP data-plan (port 503, PLC-teamets eksakte MBAP+PDU-eksempel) — gatewayen forsøger nu FC15-transaktionen (ikke længere whitelist-blokeret) og svarer korrekt med exception `0x0B` ("Target Device Failed to Respond") da den fysiske testslave ikke svarede — IKKE `0x0A` ("Path Unavailable"), som PLC-teamet beskrev som den daværende (nu rettede) adfærd.

## [0.27.0 build 0036] — 2026-09-15 — FC15 (Write Multiple Coils) understøttet

**Baggrund:** Jan: "har vi support for FC 15 og 16" → svaret afslørede FC16 (Write Multiple Registers) allerede var understøttet, men FC15 (Write Multiple Coils) manglede helt — en ekstern Modbus TCP-master der sendte FC15 fik en misvisende `0x0A` "Gateway Path Unavailable"-exception i stedet for enten et rigtigt svar eller et korrekt `0x01` "Illegal Function". Jan: "ja tak vi skal have FC15 og FC16 support".

**`lib/modbus_pdu/`:** ny `case 0x0F` i `mb_pdu_expected_response_frame_len()` — samme mønster som FC16's `case 0x10`: `qty` 1-1968 (Modbus-spec-grænsen for FC15, forskellig fra FC16's 123), `byte_count` skal matche `ceil(qty/8)`, forventet svar-længde er adresse+fc+startadresse(2)+quantity(2)+CRC(2) (samme ekko-format som FC16).

**`lib/diagnostic_modbus/`:** `POST /api/channels/{n}/write` accepterer nu `{"function_code":15,...,"values":[1,0,1,...]}` — hver værdi skal være 0 eller 1 (afvist ellers, `mb_diag_parse_write_request()`). `mb_diag_build_write_pdu()` bit-pakker værdierne til request-PDU'en (`out_pdu[6+i/8] |= 1<<(i%8)`), samme `MB_DIAG_MAX_WRITE_VALUES`=32-loft som FC16 (diagnostisk brug, ikke høj-frekvent drift). Bekræftelses-JSON'en var allerede function-code-agnostisk, ingen ændring nødvendig der.

**Bevidst UDELADT:** ingen ny CLI-kommando — `test`-kommandoen (v0.24.0) forbliver KUN læsning (fc 1-4, samme lavere-risiko-begrundelse som hidtil), skrivning sker udelukkende via REST §4.2, samme arkitekturbeslutning som allerede gjaldt FC05/06/16.

**Dokumentation:** `PLC_INTEGRATION_MANUAL.md` §3.2 (understøttede function codes-liste) + §4.6 (write-endpoint-eksempler), `EXPANSION_BOARD_DESIGN.md` (REST-endpoint-tabellen), `CLAUDE.md` (projektstruktur-kommentaren for `lib/modbus_pdu/`).

**Filer ændret:** `lib/modbus_pdu/modbus_pdu.h/.cpp`, `lib/diagnostic_modbus/diagnostic_modbus.h/.cpp`, `src/http_server.cpp` (fejlbesked), `test/test_modbus_pdu/`, `test/test_diagnostic_modbus/`, `PLC_INTEGRATION_MANUAL.md`, `EXPANSION_BOARD_DESIGN.md`, `CLAUDE.md`, `FEATURES.md`.

**Status:** 278/278 native-tests bestået (12 nye), bygger rent for esp32dev. **Live-verificeret** på fysisk hardware (kanal B, `debug modbus b level 8`): en `POST /api/channels/2/write` med `{"function_code":15,"slave_id":9,"address":0,"values":[1,0,1]}` producerede den BYTE-PERFEKTE RTU-frame `09 0F 00 00 00 03 01 05 4E F2` (slave=9, FC=0x0F, adresse=0, qty=3, byte_count=1, data=0x05 — nøjagtigt bit-mønsteret 1,0,1 — korrekt CRC) — gatewayens frame-bygning/afsendelse er dermed bekræftet korrekt. Den fysiske testslave (adr. 9) svarede ikke (`MB_TIMEOUT`, REST 502) — enheden understøtter tilsyneladende ikke FC15 (almindeligt for feltdevices der kun implementerer et delmængde af function codes), hvilket IKKE er en gateway-fejl. Fuld ende-til-ende-bekræftelse (et rigtigt FC15-svar) kræver et testudstyr der reelt understøtter funktionen.

## [0.26.1 build 0035] — 2026-09-15 — `no syslog`/`no syslog all`

**Baggrund:** Jan: "har vi også no syslog som mulighed for at slette config for syslog".

**`lib/provisioning_cli/`:** ny `no syslog`/`no syslog all` (synonymer, samme mønster som v0.25.0's `no debug modbus`/`no debug all`) — fjerner ALLE konfigurerede syslog-modtagere i ét kald, i stedet for `syslog remove <tag>` én ad gangen. Persisteret (kræver `save`, ligesom `syslog add`/`remove`). `no`-kommandoen omstruktureret til at understøtte flere underkommandoer (`debug`/`syslog`) i stedet for kun `debug`.

**Filer ændret:** `lib/provisioning_cli/provisioning_cli.cpp`, `test/test_provisioning_cli/test_provisioning_cli.cpp`, `FEATURES.md`.

**Status:** 269/269 native-tests bestået (6 nye), bygger rent for esp32dev. Live-verifikation følger.

## [0.26.0 build 0034] — 2026-09-15 — Syslog-klient (RFC 3164, UDP) med op til 4 modtagere

**Baggrund:** Jan: "kan vi lave en syslog funktion som vi kan sætte et target på som modtager af syslog" → "en eller flere target" → "vi skal have lave en level 1-8 samt local0-7 for syslog og vi skal kunne sætte hvad for output der skal sendet så det er også et sp om at vi nu skal have instruduceret syslog output fra de forskellige operationer i expansions board".

**`lib/syslog_client/` (nyt modul):** hardware-uafhængig RFC 3164-pakkeformatering (`mb_syslog_build_packet()`), native-testet (9 nye tests). Genbruger v0.25.0's 1-8-verbositetsskala som severity-akse for ALLE syslog-beskeder: `severity = level - 1` (level 1→severity 0/Emergency...level 8→severity 7/Debug), bijektiv, ingen oversættelsestabel. Facility (`local0`-`local7`) er fast pr. delsystem i firmwaren (Modbus=local0, netværk=local1, REST=local2, system=local3), ikke brugerkonfigurerbart.

**`src/syslog_sender.cpp` (nyt):** ESP32 UDP-afsendelse (`WiFiUDP`, interface-agnostisk — virker over både WiFi og Ethernet). Mutex-beskyttet fælles `static` sende-buffer (flere FreeRTOS-tasks — begge kanal-tasks, REST-httpd — kan logge samtidig, modsat `provisioning_poll()`s `static`-brug i BUGS.md v0.24.0, som er sikker netop fordi DEN kun kører på én task). Kort mutex-timeout + en billig "ingen modtager vil have dette niveau"-hurtig-exit FØR noget bygges/sendes — "fire and forget", en util-tilgængelig syslog-server kan ALDRIG blokere/forsinke boardets egentlige drift.

**`lib/provisioning_cli/` + `lib/board_config/`:** ny `syslog add <ip> <port> <tag> <level 1-8>` / `syslog remove <tag>` (op til `MB_SYSLOG_MAX_TARGETS`=4 modtagere, genbrug af et eksisterende tag OPDATERER i stedet for at duplikere). **Persisteret i NVS** (Jan bekræftet — modsat v0.25.0's runtime-only debug-niveau, en syslog-modtager er driftskonfiguration). NVS-skema bumpet 6→7 (`syslog_targets[]`), fuld migrationskæde + frossen v6-struct, jf. §3.5. Vist i `show`. 15 nye native-tests (CLI-parsing + persistering + migration).

**Instrumentering (Jan: "introducere syslog output fra de forskellige operationer i expansions board"):** `src/modbus_channel.cpp`s `execute_transaction()` — samme 8 debug-niveauer som CLI'ens `debug modbus ...` (v0.25.0), men en UAFHÆNGIG udgangskanal (en syslog-modtager med højt `max_level` ser fuld detalje uanset CLI-debug-tilstand), samt `MB_NOT_ENABLED` (som CLI-debuggen aldrig selv rapporterer). `src/http_helpers.cpp`s `require_auth()` — 401-afvisninger (CLAUDE.md regel 11's "auth-afvisninger"). RX-byte-timing og hex-dumps sendes som ÉN samlet syslog-linje (ikke op til 256 enkelt-byte-pakker) — bevidst afvejning mod netværks-flooding.

**`src/main.cpp`/`src/provisioning.cpp`:** `syslog_sender_begin()` ved boot (efter `config_begin()`), `syslog_sender_refresh()` efter hvert `save`/`connect` der kan røre syslog-config — ændringer virker STRAKS, ingen reboot nødvendig.

**Filer ændret:** `lib/syslog_client/` (nyt), `src/syslog_sender.h/.cpp` (nyt), `lib/provisioning_cli/provisioning_cli.h/.cpp`, `lib/board_config/board_config.h/.cpp`, `src/modbus_channel.cpp`, `src/http_helpers.cpp`, `src/main.cpp`, `src/provisioning.cpp`, `test/test_syslog_client/` (nyt), `test/test_provisioning_cli/`, `test/test_board_config/`, `FEATURES.md`.

**Status:** 264/264 native-tests bestået (24 nye), bygger rent for esp32dev. **Live-verificeret** på fysisk hardware: opsatte en rigtig UDP-syslog-modtager (`syslog add 10.1.1.75 5140 devtest 8` + `save`), kørte en rigtig Modbus-transaktion (kanal B, slave 9) og et REST-401-forsøg — modtog alle 9 forventede pakker med korrekt PRI (facility*8+severity), rigtigt hostname/tag, og læselige beskeder (se BUGS.md-fri live-log). Verificerede desuden at en syslog-modtager overlever en RIGTIG `reboot` (NVS-skema 7 round-trip på ægte hardware, ikke kun i native-tests). Ingen stack- eller timing-relaterede problemer observeret.

## [0.25.1 build 0033] — 2026-09-15 — Debug-output undertrykker nu resten af konsol-støjen mens det er aktivt

**Baggrund:** Jan, efter at have brugt v0.25.0's `debug modbus`-feature: "hvis den er aktiv skal alt andet console output undertrykkes og ikke som nu hvor man få blandet alt muligt ind i debug output også". Konkret problem: `channel_task()`s generiske `MODBUS-FEJL kanal ...`-linje (`src/modbus_channel.cpp`) er upåvirket af debug-niveau og fyrer for HVER fejlende transaktion på BEGGE kanaler — så et forsøg på at kigge rent på kanal B's debug-output blev oversvømmet af kanal A's helt uafhængige, normale fejl-trafik (fx en anden Modbus TCP-master der periodisk poller en kanal uden noget tilsluttet).

**`src/modbus_channel.cpp`:** ny `any_channel_debug_active()`-hjælper — så snart MINDST ÉN kanal har debug slået til, undertrykkes den generiske `MODBUS-FEJL`-linje for BEGGE kanaler. Debug-outputtet (level ≥1) viser allerede slave/fc/resultat for den/de kanal(er) man rent faktisk debugger, så intet reelt går tabt DÉR — for en ikke-debugget kanal er det en bevidst, midlertidig afvejning under aktiv fejlsøgning.

**Filer ændret:** `src/modbus_channel.cpp`, `FEATURES.md`.

**Status:** 238/238 native-tests upåvirket (ingen native-testbar logik ændret — rent `src/`-lag), bygger rent for esp32dev. Live-verifikation følger.

## [0.25.0 build 0032] — 2026-09-15 — Leveled Modbus-debug-output i den serielle CLI

**Baggrund:** Jan: "lave en debug som outputer til console alt hvad der forgå på kanal A og B", med egen Cisco-inspireret syntaks: "debug modbus a-b-all level 1-8 for on mode og no debug modbus eller no debug all for off mode, lave level af debug med level 1-8 hvor level 8 er rå hex dump af driver på en kanal", fulgt op af "man skal kunne disable debug fra cli også".

**`src/modbus_channel.h`/`.cpp`:** ny `volatile uint8_t debug_level`-felt pr. kanal (`ChannelContext`, default 0/fra ved boot), nye `modbus_channel_set_debug_level()`/`modbus_channel_get_debug_level()`. `execute_transaction()` instrumenteret med leveled, ADDITIVE debug-output (ingen ændring af eksisterende timing/kontrolflow): level 1 = transaktions-start/slut-resumé (slave/fc/resultat/varighed), 2 = støj-dræning, 3 = DE/RE-retningsskift, 4 = RX-byte-timing, 5 = inter-frame-delay, 6 = rå `parse_result` før mapping til `mb_error_code_t`, 7 = TX rå hex-dump, 8 = RX rå hex-dump. Multiple-return-switch'en refaktoreret til én `final_result`-variabel + ét debug-print-punkt. **BUGS.md v0.24.0-lektionen anvendt bevidst:** `channel_task()` kører på en LILLE 4096-byte FreeRTOS-stack — al hex-dump-output skrives byte-for-byte direkte via `Serial.printf()` i en løkke (`debug_print_hex()`), ALDRIG samlet i en stor lokal buffer først.

**`lib/provisioning_cli/`:** ny `mb_debug_target_t`-enum (kA/kB/kAll) + scratch-felter `debug_target`/`debug_level` i `mb_provisioning_state_t` (IKKE en del af den persisterede config, samme mønster som v0.24.0's `test_channel_number`/`test_read`). Ny kommando `debug modbus <a|b|all> level <1-8>` samt `no debug modbus`/`no debug all` (begge synonymer for fuld deaktivering på begge kanaler) — begge udløser ny `PROV_ACTION_DEBUG_SET`. 14 nye native-tests.

**`src/provisioning.cpp`:** ny `PROV_ACTION_DEBUG_SET`-håndtering, kalder `modbus_channel_set_debug_level()` for den/de valgte kanal(er) — ren runtime-tilstand, rører intet i NVS. `status` viser nu `debug.channel_a`/`debug.channel_b` (live værdi, ikke `show` — det er ikke persisteret config).

**Bevidst IKKE persisteret** — nulstilles altid til FRA ved reboot (Jan bekræftet), så det aldrig utilsigtet efterlades kørende og fylder konsollen/påvirker performance i normal drift.

**Filer ændret:** `src/modbus_channel.h/.cpp`, `lib/provisioning_cli/provisioning_cli.h/.cpp`, `src/provisioning.cpp`, `test/test_provisioning_cli/test_provisioning_cli.cpp`, `FEATURES.md`, `BUGS.md`.

**Live-verificeret bug fundet OG rettet under selve verifikationen (se BUGS.md v0.25.0):** den første live-test (level 8 mod den rigtige slave på kanal B) afslørede at et `Serial.printf()` pr. RX-byte INDE i den timing-kritiske modtageløkke faktisk fik ellers gyldige, rettidige transaktioner til at fejle med `MB_TIMEOUT` (4 af 5 gentagne kald). Rettet ved at udskyde al print til EFTER løkken er færdig (registreres undervejs i en billig lokal array, ingen I/O midt i løkken) — 5/5 gentagne level-8-kald lykkedes efter rettelsen.

**Status:** 238/238 native-tests bestået (14 nye), bygger rent for esp32dev. **Live-verificeret** på fysisk hardware (kanal B mod slave adr. 9): level 1 (start/slut-resumé), level 8 (fuld TX+RX hex-dump, 5/5 gentagne kald efter rettelsen ovenfor), `no debug modbus`/`no debug all` (begge slår faktisk output fra), samt uafhængige debug-niveauer pr. kanal samtidig (kanal A level 2, kanal B level 8, på samme tid). Ingen stack-relaterede nedbrud observeret under nogen af testene.

## [0.24.1 build 0031] — 2026-09-15 — `test` viser nu en "CLI'en venter"-afklaringsbesked

**Baggrund:** Jan: "det se ud til at hvis en kanal ikke svar og skal timer ud så bliver der ikke udført noget aktivitet på den anden kanal i det tids rum hvor der er timer out". Undersøgt konkret via et REST-baseret parallelitetstest: sendte et kald til kanal A (ingen slave, timer ud) og SAMTIDIG (100ms senere) et kald til kanal B (rigtig, svarende slave) — kanal B svarede på 140ms, LÆNGE FØR kanal A's 762ms-timeout var færdig. Dette beviser at de to kanalers FreeRTOS-tasks (`mb_ch_a`/`mb_ch_b`, `mb_tcp_a`/`mb_tcp_b`) kører fuldstændig uafhængigt — INGEN reel blokering mellem kanalerne. Jan bekræftede at observationen kom fra `test`-kommandoen (v0.24.0) i den serielle CLI, som er BEVIDST synkron/blokerende (samme princip som `connect` ved WiFi) — det er CLI-terminalens egen ventetid, ikke boardets kanaler, der "blokerer".

**`src/provisioning.cpp`:** `PROV_ACTION_TEST_READ`-håndteringen printer nu `(CLI'en venter nu op til Xms paa svar/timeout - den ANDEN kanal koerer uforstyrret videre i baggrunden)` FØR selve ventetiden (henter kanalens faktiske `timeout_ms` via `modbus_channel_get_config()`). Ren afklaring — ingen adfærdsændring.

**Filer ændret:** `src/provisioning.cpp`.

**Status:** 225/225 native-tests upåvirket, bygger rent for esp32dev. Live-verifikation følger.

## [0.24.0 build 0030] — 2026-09-15 — `test <kanal> <slave_id> <fc> <adresse> <antal>` i den serielle CLI

**Baggrund:** Jan: "kan vi lave test fra cli" — hidtil krævede diagnostisk Modbus-testning enten curl/Postman (§4.2's `POST /api/channels/{n}/read`) eller en rigtig Modbus TCP-klient. Ingen måde at teste direkte fra den serielle CLI.

**`lib/provisioning_cli/`:** ny `test <kanal 1|2> <slave_id> <fc 1-4> <adresse> <antal>`-kommando. Ny `parse_uint_token()`-hjælper (heltalsparsing af CLI-tokens, adskilt fra `lib/channel_config`s JSON-feltparsing). Samme grænser som REST-udgaven (`mb_diag_parse_read_request()`): fc 1-4, slave_id 1-247, adresse 0-65535, antal 1-2000. **KUN læsning** — bevidst ingen `test write` (lavere risiko: en CLI-tastefejl kan ikke skrive forkert til et tilsluttet felt-device). Nye scratch-felter `test_channel_number`/`test_read` i `mb_provisioning_state_t` (IKKE en del af den persisterede config — bærer blot parametrene fra parseren til udførelsen). Tokenizer-bufferen hævet fra 4 til 6 tokens for at rumme kommandoens fem argumenter.

**`src/provisioning.cpp`:** ny `PROV_ACTION_TEST_READ`-håndtering — genbruger `lib/diagnostic_modbus`s eksisterende `mb_diag_build_read_pdu()`/`mb_diag_is_exception()`/`mb_diag_build_exception_json()`/`mb_diag_build_read_values_json()` (SAMME byggeklodser som `src/http_server.cpp`s REST-handler, ingen duplikeret formaterings-/PDU-logik), kalder `modbus_channel_submit()` direkte. Udløser en RIGTIG transaktion — tænder derfor kanalens aktivitets-LED (v0.23.1), praktisk til selv at kunne teste/bekræfte den fra CLI'en uden eksterne værktøjer.

**Filer ændret:** `lib/provisioning_cli/provisioning_cli.h/.cpp`, `src/provisioning.cpp`, `test/test_provisioning_cli/test_provisioning_cli.cpp`.

**Status:** 225/225 native-tests bestået (8 nye), bygger rent for esp32dev. Live-verifikation følger.

## [0.23.1 build 0029] — 2026-09-15 — Aktivitets-LED'erne (GPIO26/33) driver nu faktisk noget

**Baggrund:** Jan: "aktivitet LED for de to kanal hvordan opføre de sig" — GPIO26/33 har været reserveret til "valgfri diagnostik-LED" siden v0.13.0 (§2.2), men ingen kode nogensinde skrev til dem — rent elektrisk udefinerede.

**`src/modbus_channel.cpp`:** nye `kChannelALedPin = 26`/`kChannelBLedPin = 33`, nyt `led_pin`-felt i `ChannelContext`, initialiseret som `OUTPUT`/`LOW` i `init_channel()` (samme mønster som `dir_pin`). `channel_task()` tænder kanalens LED lige før `execute_transaction()`-kaldet og slukker den lige efter — dækker succes OG fejl/timeout ens. Bevidst placeret omkring det ENE kaldested i `channel_task()`, ikke inde i `execute_transaction()` selv (som har flere `return`-stier) — undgår risikoen for at LED'en utilsigtet blev efterladt tændt på en glemt return-vej. En deaktiveret kanal (`enabled:false`) blinker bevidst IKKE (ingen reel bus-aktivitet).

**Filer ændret:** `src/modbus_channel.cpp`, `GPIO_MAPPING.md`, `EXPANSION_BOARD_DESIGN.md`.

**Status:** 217/217 native-tests upåvirket (ren ESP32/Arduino-specifik GPIO-logik, ikke native-testbar), bygger rent for esp32dev. Live-verifikation følger — **den fysiske LED-blinken kan ikke bekræftes visuelt herfra**, kun at koden bygger/kører korrekt og at kanal B's rigtige Modbus-transaktioner (som udløser blinket) fortsat virker.

## [0.23.0 build 0028] — 2026-09-14 — `token regenerate` i den serielle CLI

**Baggrund:** Jan: "hvordan generare vi ny token" — management-API'ets Bearer-token blev hidtil kun genereret ÉN gang nogensinde (ved første vellykkede `connect`, `config_ensure_mgmt_token()`, som kun genererer hvis der IKKE allerede er ét). Eneste vej til et NYT token var `factory-reset confirm`, som også rydder WiFi/firewall/al anden config.

**`lib/provisioning_cli/`:** ny `PROV_ACTION_TOKEN_REGENERATE`, udløst af `token regenerate` — ingen `confirm` krævet (Jan bekræftet: ikke-destruktiv, rører KUN tokenet, langt mindre indgribende end `factory-reset`).

**`src/config.h/.cpp`:** ny `config_regenerate_mgmt_token()` — genererer og persisterer UBETINGET et nyt token (samme hardware-RNG/`mb_config_token_from_random_bytes()` som `config_ensure_mgmt_token()`), modsat den eksisterende funktion som kun genererer hvis der ikke allerede findes ét.

**`src/provisioning.cpp`:** viser det nye token i klartekst (samme tillidsmodel som resten af CLI'en) og en tydelig ADVARSEL om at det gamle token øjeblikkeligt holder op med at virke — husk at opdatere PLC'ens System-side.

**Filer ændret:** `lib/provisioning_cli/provisioning_cli.h/.cpp`, `src/config.h/.cpp`, `src/provisioning.cpp`, `test/test_provisioning_cli/test_provisioning_cli.cpp`.

**Status:** 217/217 native-tests bestået (3 nye), bygger rent for esp32dev. Live-verifikation følger.

## [0.22.1 build 0027] — 2026-09-14 — `rest.user`/`rest.pass` skjules i CLI'en medmindre auth_mode er `both`

**Baggrund:** Jan: "hvis vi køre rest auth token så skal rest user og rest pass i være i config kun hvis rest auth both" — `show`/`status` viste hidtil altid `rest.user`/`rest.pass`, uanset `rest.auth_mode`, hvilket er misvisende når de reelt ikke bruges (Basic Auth er helt afvist i `token`-mode uanset hvad der er konfigureret).

**`lib/provisioning_cli/`:** `mb_provisioning_format_status()` ("show") viser nu kun `rest.user`/`rest.pass` når `rest_auth_mode == MB_REST_AUTH_MODE_BOTH`. Bekræftet med Jan: reglen gælder EKSPLICIT kun `both` — skjules altså også i `basic`-mode, ikke kun `token`, selvom Basic Auth teknisk set er den eneste accepterede metode i det tilfælde.

**`src/provisioning.cpp`:** samme regel i `print_status()` ("status"), en separat kodesti (læser direkte fra `config_get()`, ikke via `show`-formatteren).

**Værdierne slettes IKKE fra NVS** ved et modeskift — kun visningen er betinget, så intet går tabt hvis man senere skifter tilbage til `both`.

**Filer ændret:** `lib/provisioning_cli/provisioning_cli.cpp`, `src/provisioning.cpp`, `test/test_provisioning_cli/test_provisioning_cli.cpp`.

**Status:** 214/214 native-tests bestået (3 nye), bygger rent for esp32dev. Live-verifikation følger.

## [0.22.0 build 0026] — 2026-09-14 — Konfigurerbart DHCP-hostname (`hostname <navn>`/`hostname auto`)

**Baggrund:** Jan: "vi skal lige have en hostname på kan jeg se da dhcp server bare har et espressif name nu" — firmwaren satte hidtil aldrig et eksplicit hostname; Arduino-WiFi-kernens default (`esp32-XXXXXX`) blev brugt uændret, og Ethernet fik slet intet hostname.

**`lib/provisioning_cli/`:** ny `hostname`/`has_hostname`-felt i `mb_provisioning_state_t`. Ny `hostname <navn>` (validering: RFC 1123-label, 1-32 tegn) / `hostname auto` (rydder override) kommando. Vist i `show` som `hostname: <custom>` eller `hostname: (auto-genereret, se 'status')` — den KONFIGUREREDE tilstand, ikke den faktiske streng (som afhænger af MAC'en, som denne hardware-uafhængige lib ikke kender).

**`lib/board_config/`:** ny `mb_config_build_hostname(has_hostname, hostname, mac, out, out_capacity)` — hardware-uafhængig/testbar, bygger enten den eksplicit satte streng eller et auto-genereret `hypervision-ext-XXXXXX` (XXXXXX = boardets sidste 3 MAC-bytes, samme unikke MAC som v0.20.0). Tager primitive parametre bevidst (ikke en `mb_board_config_t*`), så den kan bruges BÅDE med den persisterede config (boot) OG med den evt. usaved, in-progress CLI-state (`connect`).

**`src/eth_driver.h/.cpp`:** ny `hostname`-parameter til `eth_driver_begin()` — sat via `esp_netif_set_hostname()` FØR `esp_eth_start()`, så det indgår i DHCP Option 12 fra første forespørgsel.

**`src/provisioning.cpp`:** `attempt_connect()` kalder `WiFi.setHostname()` FØR `WiFi.mode(WIFI_STA)` (den eneste rækkefølge der reelt virker i Arduino-WiFi-kernen — et kald efter `mode()`/`begin()` har ingen effekt). Læses fra `g_state` (ikke `config_get()`), så et lige-sat, endnu ikke gemt `hostname ...` også gælder med det samme ved `connect`, ligesom SSID/password. Ny `print_hostname()` viser den FAKTISK anvendte streng i `status`/`show`.

**`src/main.cpp`:** beregner hostnamet én gang ved boot (persisteret config) og sender det til `eth_driver_begin()`.

**NVS-skema 5→6** (`lib/board_config/`): `hostname`, `has_hostname`. `mb_board_config_v5_t` frosset til migration. `migrate_v5_to_current()`: `has_hostname=false` (matcher hidtidig adfærd — auto-genereret).

**Filer ændret:** `lib/provisioning_cli/provisioning_cli.h/.cpp`, `lib/board_config/board_config.h/.cpp`, `src/eth_driver.h/.cpp`, `src/main.cpp`, `src/provisioning.cpp`, `test/test_provisioning_cli/test_provisioning_cli.cpp`, `test/test_board_config/test_board_config.cpp`.

**Status:** 211/211 native-tests bestået (16 nye), bygger rent for esp32dev. Live-verifikation følger.

## [0.21.0 build 0025] — 2026-09-14 — `wifi enable`/`wifi disable` i CLI'en + kritisk fix: REST/Modbus TCP startede kun via WiFi

**Baggrund:** Jan: "kan vi disable wifi også fra cli" — mirroring v0.20.0's `eth enable`/`eth disable`.

**`lib/provisioning_cli/`:** ny `wifi_enabled`-felt i `mb_provisioning_state_t` (default `true`). Nye underkommandoer `wifi enable`/`wifi disable` i den eksisterende `wifi`-kommando. Kræver `save` + `reboot` — bevidst SAMME mentale model som `eth disable` (Jan, bekræftet), IKKE live. Gælder KUN boot-tids-auto-genforbindelsen (`src/provisioning.cpp`) — en eksplicit `connect` virker stadig uanset flaget. **Lockout-advarsel** (Jan, bekræftet): `wifi disable` printer en ADVARSEL hvis `eth` allerede er deaktiveret på det tidspunkt (og omvendt, symmetrisk for `eth disable`) — blokerer IKKE, kun en tydelig besked (fysisk USB-adgang er stadig en udvej). Vist i `show` som `wifi.enabled`.

**Kritisk arkitekturfejl fundet OG rettet under implementeringen (se BUGS.md):** `http_server_begin()`/`modbus_tcp_server_begin()` blev udelukkende kaldt fra `attempt_connect()` — dvs. KUN udløst af en vellykket WiFi-forbindelse. Et rent Ethernet-board (WiFi deaktiveret) ville derfor ALDRIG få REST-API'et eller Modbus TCP-serverne startet, selvom Ethernet forbandt korrekt — hvilket reelt ville have gjort "wifi disable" ubrugelig for netop det formål Jan bad om det til. Rettet: begge kald flyttet til `src/main.cpp::setup()`, kaldt ubetinget (begge idempotente, kræver ikke at noget interface allerede har en IP).

**NVS-skema 4→5** (`lib/board_config/`): `wifi_enabled`. `mb_board_config_v4_t` frosset til migration. `migrate_v4_to_current()`: `wifi_enabled=true` (matcher hidtidig ubetinget adfærd).

**Filer ændret:** `lib/provisioning_cli/provisioning_cli.h/.cpp`, `lib/board_config/board_config.h/.cpp`, `src/main.cpp`, `src/provisioning.cpp`, `test/test_provisioning_cli/test_provisioning_cli.cpp`, `test/test_board_config/test_board_config.cpp`.

**Status:** 195/195 native-tests bestået (9 nye), bygger rent for esp32dev. Live-verifikation følger.

## [0.20.0 build 0024] — 2026-09-14 — Ethernet enable/disable/static-IP via CLI + tilfældig persisteret MAC

**Baggrund (del 1):** Jan: "har vi kommando til at enable/disable eterhnet samt ip config, modes m.m." — Ethernet startede hidtil altid ubetinget (ren DHCP), ingen CLI-styring.

**`lib/provisioning_cli/`:** nye felter i `mb_provisioning_state_t` (`eth_enabled`, `eth_static_ip`, `eth_ip/mask/gw`) og en ny `eth`-kommando: `eth enable`/`eth disable`, `eth mode dhcp|static`, `eth ip/mask/gw <a.b.c.d>` — mirroring `wifi ...`-mønsteret. `eth_enabled` defaulter til `true` (sat eksplicit i `mb_provisioning_state_init()`, IKKE zero-value'en). Vises i `show` (`eth.enabled`/`eth.mode`/`eth.ip`/osv.).

**`src/eth_driver.h/.cpp`:** ny signatur `eth_driver_begin(enabled, static_ip, ip, mask, gw, mac)`. `enabled=false` springer al SPI-/GPIO-/netif-opsætning over. Ny `apply_ip_config()` (kaldt ved hvert `ETHERNET_EVENT_CONNECTED`, ikke kun ved boot — mirroring `Modbus_API_Gateway`s fungerende mønster) — `esp_netif_dhcpc_stop()` + enten `esp_netif_set_ip_info()` (statisk) eller `esp_netif_dhcpc_start()` (DHCP).

**`src/main.cpp`:** læser `config_get().eth_*` og sender til `eth_driver_begin()` ved boot. Ændringer via `eth ...`+`save` kræver et `reboot` for at træde i kraft (ingen forsøg på live-genstart af den SPI-baserede driver).

**Baggrund (del 2):** Jan: "vi skal også have en random MAC adr brændt ind i NVS ved start" (fandt samme rodårsag som `Modbus_API_Gateway`s BUGS.md F6: W5500 uden MAC-tildeling kører med `00:00:00:00:00:00`).

**`lib/board_config/`:** ny `mb_config_mac_from_random_bytes()` — sætter unicast+lokalt-administreret-bits (IEEE 802) på 6 rå bytes. `src/config.cpp`: ny `config_ensure_eth_mac()` (mirroring `config_ensure_mgmt_token()`, `esp_fill_random()`), kaldt ved HVERT boot, FØR `eth_driver_begin()`. Bevidst en NVS-persisteret TILFÆLDIG MAC (ikke `esp_read_mac(ESP_MAC_ETH)`, søsterprojektets tilgang) — overlever en fysisk WROOM-modul-udskiftning ved reparation. Sat på chippen via `esp_eth_ioctl(ETH_CMD_S_MAC_ADDR)` (efter `esp_eth_driver_install()`, før `esp_netif_attach()`). Vist i `show`/`status` (Jan: "MAC skal så ved en show status i cli") som `eth.mac: AA:BB:CC:DD:EE:FF`, uafhængigt af eth_enabled/forbindelsesstatus.

**NVS-skema 3→4** (`lib/board_config/`): `eth_enabled`, `eth_static_ip`, `eth_ip/mask/gw`, `eth_mac[6]`, `has_eth_mac`. `mb_board_config_v3_t` frosset til migration. `migrate_v3_to_current()`: `eth_enabled=true` (matcher hidtidig ubetinget adfærd), `has_eth_mac=false` (genereres ved næste boot).

**Bug fundet og rettet undervejs (3. gang, se BUGS.md):** `MB_PROV_MSG_MAX_LEN` (1024) blev igen for lille — de nye `eth ...`-hjælpelinjer i `help` afkortede teksten stille (fanget af `test_help_action` FØR nogen hardware-upload). Hævet til 2048.

**Filer ændret:** `lib/provisioning_cli/provisioning_cli.h/.cpp`, `lib/board_config/board_config.h/.cpp`, `src/config.h/.cpp`, `src/eth_driver.h/.cpp`, `src/main.cpp`, `src/provisioning.cpp`, `test/test_provisioning_cli/test_provisioning_cli.cpp`, `test/test_board_config/test_board_config.cpp`.

**Status:** 186/186 native-tests bestået (14 nye), bygger rent for esp32dev. Live-verifikation følger.

## [0.19.1 build 0023] — 2026-09-14 — Fix: W5500 svarede aldrig over SPI (manglende command_bits/address_bits)

**Baggrund:** Jan monterede et fysisk W5500-modul (korrekt forbundet efter GPIO_MAPPING.md, link-LED lyste), men boardet kunne aldrig tale med chippen — boot-log viste konsekvent `E w5500.mac: w5500_send_command(132): send command timeout`. Fejlsøgning udelukkede: forkert GPIO/pin-kapabilitet, manglende pull-up på INT (tilføjet, ingen ændring), og SPI-clockhastighed (8→1 MHz testet, identisk fejl på nøjagtig samme millisekund).

**Root cause (fundet ved sammenligning med `Modbus_API_Gateway`, søsterprojekt med samme W5500-hardware i produktion):** `src/eth_driver.cpp`s `spi_device_interface_config_t` satte aldrig `.command_bits`/`.address_bits`. W5500'ens SPI-protokol kræver en 16-bit adresse-fase + 8-bit kontrol-fase FØR databytes (Wiznet-datasheet) — disse felter er en del af SPI-DEVICE-konfigurationen (sat ved `spi_bus_add_device()`), ikke noget den enkelte transaktion selv kan levere. Uden dem klokker SPI-hardwaren simpelthen aldrig header'en ud — chippen kan derfor aldrig tolke nogen kommando, uanset klokhastighed eller ellers korrekt wiring.

**`src/eth_driver.cpp`:** tilføjet `spi_devcfg.command_bits = 16`, `spi_devcfg.address_bits = 8`, `spi_devcfg.cs_ena_posttrans = 5` (matcher søsterprojektets fungerende konfiguration).

**Filer ændret:** `src/eth_driver.cpp`.

**Status:** 171/171 native-tests upåvirket (ren ESP32-specifik fil), bygger rent for esp32dev. **Live-bekræftet — boardets FØRSTE succesfulde Ethernet-forbindelse nogensinde:** link op, DHCP-IP `10.1.1.90` tildelt, `GET /api/status` og et REST-kald direkte mod Ethernet-IP'en begge besvaret korrekt (bekræfter reel dataoverførsel, ikke kun link+DHCP), samtidig med WiFi forbundet (dual-stack).

## [0.19.0 build 0022] — 2026-09-14 — `reboot`-kommando i den serielle CLI

**Baggrund:** Jan, under W5500-hardware-fejlsøgning (skulle genstarte boardet gentagne gange efter ledningsændringer): "vi har ikke en reboot kommando på board". REST-API'et har haft `POST /api/reboot` siden v0.12.0, men den serielle CLI havde intet tilsvarende — kun `factory-reset confirm`, som også rydder al gemt config, langt mere end nødvendigt til hurtig hardware-iteration.

**`lib/provisioning_cli/`:** ny `PROV_ACTION_REBOOT` — ingen "confirm" nødvendig (ikke-destruktiv, rydder intet i NVS, samme filosofi som REST-udgaven). Tilføjet til `help`-teksten.

**`src/provisioning.cpp`:** `PROV_ACTION_REBOOT`-håndtering kalder `ESP.restart()` efter en kort besked — samme mønster som den eksisterende `PROV_ACTION_FACTORY_RESET`-gren, minus `config_factory_reset()`-kaldet.

**Filer ændret:** `lib/provisioning_cli/provisioning_cli.h/.cpp`, `src/provisioning.cpp`, `test/test_provisioning_cli/test_provisioning_cli.cpp`.

**Status:** 171/171 native-tests bestået, bygger rent for esp32dev. Live-verifikation følger.

## [0.18.0 build 0021] — 2026-09-14 — Detaljeret W5500-Ethernet-diagnostik

**Baggrund:** Jan: "vi skal have noget diag på det w5500 så vi kan se det fungere eller om det er link fejl". `eth_driver_link_up()` (v0.13.0) rapporterede kun et binært op/nede — umuligt at skelne et reelt hardware-/wiring-problem (intet W5500-modul fundet på SPI-bussen) fra en ren netværks-sag (modulet virker fint, men kablet mangler eller switch-porten er nede).

**`src/eth_driver.h/.cpp`:** ny `eth_driver_status_t`-enum: `ETH_STATUS_NOT_DETECTED` (default — `esp_eth_driver_install()`, som reelt taler SPI til W5500-chippen og læser dens versions-register, er aldrig lykkedes), `ETH_STATUS_LINK_DOWN` (modul bekræftet fundet og driver kører, men PHY'en rapporterer intet link), `ETH_STATUS_WAITING_DHCP` (link oppe, venter på IP), `ETH_STATUS_CONNECTED` (link oppe + IP). Sat fra `ETHERNET_EVENT_START`/`ETHERNET_EVENT_CONNECTED`/`ETHERNET_EVENT_DISCONNECTED`/`ETHERNET_EVENT_STOP` og `got_ip_event_handler()`. Nye `eth_driver_status()`/`eth_driver_status_string()`.

**`src/provisioning.cpp`:** ny `eth_status_text()` — fire klare danske sætninger i stedet for det tidligere udifferentierede "link nede (intet kabel/modul...)". `print_ethernet_status()` bruger nu `eth_driver_status()` i stedet for kun `eth_driver_link_up()`.

**`lib/rest_status/`:** nyt `const char *eth_status` i `mb_status_data_t`, skrevet som `"status"` i `ethernet`-JSON-objektet (additivt felt, ingen `api_version`-bump). `src/http_server.cpp` overfører `eth_driver_status_string()`.

**Bug fundet og rettet UNDER live-boot-testen (se BUGS.md):** status blev oprindeligt sat i `ETHERNET_EVENT_START`-eventet, som fyrer så snart `esp_eth_start()` KALDES — ikke når hardwaren reelt er bekræftet til stede. Den faktiske SPI-samtale med W5500-chippen sker FØRST inde i `esp_eth_start()` selv. Resultat: boardet (uden noget fysisk W5500-modul) rapporterede fejlagtigt "modul fundet, link nede" i stedet for "intet modul fundet". Rettet: status sættes nu eksplicit EFTER `esp_eth_start()`s egen retur-kode i `eth_driver_begin()`, ikke i event-handleren.

**Filer ændret:** `src/eth_driver.h/.cpp`, `src/provisioning.cpp`, `lib/rest_status/rest_status.h/.cpp`, `src/http_server.cpp`, `test/test_rest_status/test_rest_status.cpp`.

**Status:** 170/170 native-tests bestået, bygger rent for esp32dev. **Live-verificeret på fysisk hardware for `not_detected`-stien** (boardet har stadig intet fysisk W5500-modul monteret) — CLI (`status`/`show`) og `GET /api/status` viser begge korrekt "intet W5500-modul fundet". De øvrige tre tilstande (`link_down`/`waiting_dhcp`/`connected`) er endnu ikke live-verificerbare — afventer fysisk modul-montering (v0.13.0).

## [0.17.0 build 0020] — 2026-09-14 — MODE_SEL (GPIO4) er nu en fabriks-input, ikke et PUT-bart felt

**Baggrund:** Jan rapporterede at `show status` altid viste `board_mode: rs485` uanset hvad han påtrykte GPIO4 udefra. Årsag: GPIO4 var siden v0.14.0 en OUTPUT drevet af firmwarens egen software-config, ikke en INPUT. Efter afklaring (Jan: "Paatrykte selv en spaending udefra", "ok det skal være et input til at sætte board ved framstilling") blev arkitekturen ændret til: `mode` bliver en ren, hardware-udlæst, READ-ONLY egenskab; MODE_SEL læses kun ved boot (ikke løbende).

**`src/modbus_channel.cpp`:** `apply_board_mode_sel()` (output-skrivning) fjernet. Ny `mb_channel_mode_t g_hardware_mode` + `read_board_mode_sel()` (`pinMode(4, INPUT_PULLUP)`, læst med `digitalRead()`). `modbus_channel_init_all()` læser nu `g_hardware_mode` ved boot og tvinger begge kanalers config til den — erstatter v0.14.0's "ADVARSEL: forskellig persisteret mode"-konsistenstjek (overflødigt nu). `apply_config_now()` tvinger `ctx.config.mode = g_hardware_mode` uanset hvad en reconfigure-request indeholdt. `modbus_channel_apply_config()`: v0.14.0's cross-channel mode-spejlings-blok fjernet (overflødig, samme grund); persisterer nu `ctx.config` (den FAKTISK anvendte, forcerede config) i stedet for den rå `new_config`-parameter.

**`lib/channel_config/channel_config.cpp/.h`:** `mb_channel_parse_config_json()` parser/validerer ikke længere `"mode"` — et tilstedeværende `mode`-felt i PUT-bodyen ignoreres stiltiende. `string_to_mode()`-hjælperen fjernet (ubrugt).

**`src/http_server.cpp`:** PUT-handleren bygger nu sit JSON-svar fra `modbus_channel_get_config()` (den faktisk anvendte config, EFTER hardware-force) i stedet for den rå, mode-løse parse-struct.

**`test/test_channel_config/test_channel_config.cpp`:** `test_parse_rejects_invalid_mode` fjernet (mode valideres ikke længere); ny `test_parse_ignores_absent_mode_field` (bekræfter at et request UDEN `mode`-felt overhovedet accepteres).

**Filer ændret:** `src/modbus_channel.cpp/.h`, `lib/channel_config/channel_config.cpp/.h`, `src/http_server.cpp`, `test/test_channel_config/test_channel_config.cpp`.

**Status:** 169/169 native-tests bestået, bygger rent for esp32dev. Ingen NVS-skema-ændring. Live-verifikation følger.

## [0.16.0 build 0019] — 2026-09-14 — Ethernet-status og board_mode i den serielle CLI

**Baggrund:** Jan spurgte hvordan han kunne se Ethernet-status og MODE_SEL/board_mode i CLI'en — begge var kun tilgængelige via REST (`GET /api/status`, v0.13.0/v0.15.0), ikke i den serielle `status`/`show`.

**`src/provisioning.cpp`:** ny `print_ethernet_status()` (samme mønster som den eksisterende `print_wifi_connection_status()`) — viser `eth.connection: link op/link nede` og, hvis op, `eth.ip`. Ny `print_board_mode()` — viser `board_mode: rs485/rs232` (læst fra kanal A's config, altid synkroniseret med kanal B siden v0.14.0). Begge kaldt fra `print_status()` (kommandoen `status`) OG `PROV_ACTION_SHOW`-handleren (kommandoen `show`), samme sted som WiFi-status allerede kaldes — så alle tre CLI-udskrifter forbliver konsistente indbyrdes.

**Filer ændret:** `src/provisioning.cpp`.

**Status:** 169/169 native-tests upåvirket, bygger rent for esp32dev. Live-verifikation følger.

## [0.15.0 build 0018] — 2026-09-14 — `GET /api/status` får et direkte `board_mode`-felt

**Baggrund:** Jan påpegede at siden MODE_SEL (v0.14.0) nu er én delt GPIO for hele boardet, burde REST-API'et også rapportere RS232/RS485-tilstanden direkte som en board-egenskab, i stedet for at en klient skal udlede den indirekte via en tilfældig kanals `mode`-felt.

**`lib/rest_status/`:** nyt `mb_channel_mode_t board_mode` i `mb_status_data_t`. `rest_status.h` inkluderer nu `board_config.h` for enum'en. `mb_status_build_json()` skriver `"board_mode":"rs485"`/`"rs232"` lige efter `"provisioned"`.

**`src/http_server.cpp`:** `status_handler()` udfylder feltet via `modbus_channel_get_config(ModbusChannelId::kA).mode` — kanal A's config bruges vilkårligt, da begge kanaler altid er synkroniseret efter v0.14.0.

**Bug fundet og rettet UNDER test:** det nye felt gjorde JSON-outputtet netop langt nok til at overskride `test_rest_status.cpp`s 256-byte test-buffere for de tungeste fixtures — `mb_status_build_json()` selv opførte sig korrekt (returnerede 0 ved den deraf følgende snprintf-truncation), men testen tjekkede ikke returværdien før den scannede indholdet, og fejlede derfor med en forvirrende "ubalancerede krøllede parenteser"-besked i stedet for en klar "buffer for lille". Rettet: alle test-buffere hævet til 512 bytes, og testen tjekker nu returværdien FØRST.

**Filer ændret:** `lib/rest_status/rest_status.h/.cpp`, `src/http_server.cpp`, `test/test_rest_status/test_rest_status.cpp`.

**Status:** 169/169 native-tests bestået, bygger rent for esp32dev. Live-verifikation følger.

## [0.14.0 build 0017] — 2026-09-14 — Hardware-revision: delt MODE_SEL, W5500 får en rigtig RST-pin

**Baggrund:** Jan bad om at samle MODE_SEL til én fælles pin for hele boardet i stedet for én pr. kanal, og bruge den frigjorte GPIO til at give W5500'en en rigtig, software-styret RST-pin (i stedet for kun at stole på modulets eget power-on-reset, jf. v0.13.0's beslutning).

**GPIO-ændring (§2.0.1):** MODE_SEL er nu ÉN delt GPIO4 for hele boardet — kanal A og B kan ALDRIG have forskellig RS232/RS485-mode. GPIO23 (kanal B's tidligere dedikerede MODE_SEL) er frigjort og bruges nu som W5500's RST-pin.

**`src/modbus_channel.cpp`:** `mode_sel_pin` fjernet fra `ChannelContext` (var pr.-kanal), erstattet af en ny fælles `apply_board_mode_sel()` der skriver GPIO4 én gang. `modbus_channel_apply_config()` spejler nu automatisk en `mode`-ændring på ÉN kanal til den ANDEN (kun `mode`, kanalens øvrige felter urørte) og persisterer BEGGE kanaler selv — kaldstedet (`http_server.cpp`) persisterer ikke længere separat. `modbus_channel_init_all()` fik et nyt boot-tids-konsistenstjek: findes to forskellige persisterede mode-værdier (fra en firmware fra FØR denne revision), vinder kanal A's værdi, og kanal B's persisterede config rettes.

**`src/eth_driver.cpp`:** `phy_config.reset_gpio_num` ændret fra `-1` til `kEthRstPin = 23`.

**API-konsekvens (dokumenteret i PLC_INTEGRATION_MANUAL.md):** `PUT /api/channels/{n}/config`s JSON-form er UÆNDRET (stadig `mode` som et felt pr. kanal, for bagudkompatibilitet) — men et kald der ændrer `mode` på ÉN kanal ændrer nu også hvad den ANDEN kanal efterfølgende rapporterer ved `GET`. Dette er en bevidst, dokumenteret sideeffekt af den fysiske hardware-begrænsning, ikke en fejl.

**Ingen NVS-skema-ændring** — `mb_channel_config_t`s form er uændret, kun HVORDAN/HVORNÅR `mode` skrives til de to kanalers eksisterende felter er ændret. 168 native-tests upåvirket.

**Filer ændret:** `src/modbus_channel.h/.cpp`, `src/eth_driver.cpp`, `src/http_server.cpp`, `EXPANSION_BOARD_DESIGN.md` §2.0.1, `PLC_INTEGRATION_MANUAL.md`.

**Live-verificeret PÅ FYSISK HARDWARE — inkl. den FØRSTE nogensinde vellykkede test af kanal B mod en rigtig slave:** Jan flyttede test-devicet (slave 9) fra kanal A til kanal B undervejs i testen. Mode-spejling bekræftet (PUT kanal 1→rs232 spejlede korrekt til kanal 2, overlevede en ægte hardware-genstart, tilbage til rs485 igen). Kanal B (port 503): 6/6 sammenhængende, korrekte FC03-transaktioner (register 0 = 0x4616, register 1 = 0x0000) — `GET /api/channels/2` bekræftede `total_requests:6, successful_requests:6, timeout_errors:0`. Kanal B var indtil nu KUN testet via framing/firewall-tests, aldrig mod en fysisk slave — det er nu gjort. W5500 RST-GPIO23-ændringen forstyrrede ikke kanal A/B's UART-drift.

## [0.13.0 build 0016] — 2026-09-14 — Valgfri W5500-Ethernet (dual-stack med WiFi)

**Baggrund:** Jan spurgte om W5500 kunne tilføjes uden at kompromittere kanal A/B's UART-pins (2026-09-13). Ingen konflikt fundet — GPIO-allokering aftalt og låst i EXPANSION_BOARD_DESIGN.md §2.0.1 samme dag, derefter implementeret.

**`src/eth_driver.cpp` (ny):** bringer W5500'en op som en RIGTIG lwIP-netværksinterface via ESP-IDF's native `esp_eth`-komponent (`esp_eth_mac_new_w5500`/`esp_eth_phy_new_w5500`) — bevidst IKKE Arduino's klassiske `Ethernet`-bibliotek, som har sin egen private TCP/IP-stack (EthernetServer/EthernetClient) og derfor IKKE ville virke sammen med den WiFiServer/WiFiClient-baserede kode `modbus_tcp_server.cpp`/`http_server.cpp` allerede bruger. Fordi esp_eth registrerer sig som en almindelig netif, virker ALT eksisterende netværkskode uændret, uanset om trafikken kommer ind på WiFi eller Ethernet — kører SIDELØBENDE (dual-stack), ikke et enten-eller. Ethernet har ingen egen provisionering — ren DHCP så snart kabel+link er til stede.

**GPIO-allokering (§2.0.1, aftalt 2026-09-13):** SCK=14, MOSI=13, CS=32, MISO=35, INT=39 (interrupt-drevet). Ingen dedikeret RST-GPIO — Jan bekræftede modulets eget power-on-reset er tilstrækkeligt. GPIO21/22 (I2C-default) og 26/33 (kanal-aktivitets-LED'er) bevidst IKKE brugt.

**`GET /api/status`** udvidet med et `"ethernet":{"connected":bool,"ip":"..."}`-objekt (samme form som `"wifi"`, minus `rssi_dbm`) — `lib/rest_status/` udvidet, JSON-bygningen omskrevet til at dele en fælles hjælpefunktion mellem wifi/ethernet-objekterne.

**Reel bug fundet og rettet UNDER live-boot-testen (se BUGS.md):** W5500-driverens interrupt-registrering (GPIO39) fejlede ved boot med `gpio_isr_handler_add(): GPIO isr service is not installed` — ESP-IDF's globale GPIO-ISR-service var aldrig eksplicit installeret. Boardet crashede IKKE, men interrupt-drevet Ethernet-drift ville reelt aldrig have virket. Rettet: `gpio_install_isr_service(0)` kaldes nu eksplicit FØR `esp_eth_driver_install()`.

**API-opdagelse undervejs:** den installerede ESP32 Arduino-core (3.20017, ESP-IDF 5.x-baseret) bundler en ÆLDRE `esp_eth`-header-variant end den generelle `framework-espidf`-pakke — `eth_w5500_config_t` tager en færdigoprettet `spi_device_handle_t` direkte (`spi_bus_add_device()` kaldt manuelt), ikke en `spi_host_device_t`+`spi_device_interface_config_t*`-kombination som den nyeste ESP-IDF-dokumentation ellers beskriver. Kode tilpasset den faktisk installerede API-form.

**Filer ændret:** `src/eth_driver.h/.cpp` (nye), `src/main.cpp`, `src/http_server.cpp`, `lib/rest_status/rest_status.h/.cpp`, `test/test_rest_status/test_rest_status.cpp`.

**Status:** 168/168 native-tests bestået, bygger rent for esp32dev (flash 847277/1310720 bytes, 64.6%). **Live-boot-testet UDEN fysisk W5500-modul** (Jan har endnu ikke monteret hardwaren) — driveren fejler bevidst stille, resten af boardet (WiFi/CLI/Modbus TCP/REST) upåvirket, `GET /api/status` rapporterer korrekt `"ethernet":{"connected":false}`. **Selve Ethernet-funktionaliteten (link/DHCP/dataoverførsel) er IKKE verificeret endnu** — kræver at Jan fysisk monterer et W5500-modul på de reserverede GPIO'er.

## [0.12.0 build 0015] — 2026-09-12 — OTA-firmwareopdatering (Fase 5 afsluttet)

**Baggrund:** sidste planlagte Fase 5-endpoint — `POST /api/ota`, `GET /api/ota/status`, `POST /api/reboot` (§4.2). Med denne feature er ALLE Jans oprindelige 6 punkter for management-API'et implementeret.

**`lib/ota_validation/` (ny, hardware-uafhængig):** `mb_ota_is_valid_firmware_magic()` — tjekker ESP32-firmware-imagets kendte magic byte (0xE9) i den FØRSTE modtagne chunk, før noget skrives til flash. 4 nye tests.

**`src/ota_handler.cpp` (ny):** `POST /api/ota` streamer den rå binære body (IKKE multipart, samme `--data-binary`-mønster som referenceimplementeringen i `reference-plc-source/src/ota_handler.cpp`) i 2KB-bidder direkte til den inaktive OTA-partition via Arduino-corets `Update`-bibliotek — ingen fuld-fil-buffering i RAM. Konkurrence-lås forhindrer to samtidige uploads. Et vellykket upload VERIFICERER firmwaren (`Update.end(true)`s indbyggede checksum-tjek) og sætter den som boot-partition — men boardet genstarter IKKE af sig selv. `POST /api/reboot` er det eksplicitte, separate skridt der rent faktisk aktiverer den (bevidst adskilt, jf. designdokumentets §4.2: en operatør skal aktivt vælge at genstarte, ikke overraskes af det). `GET /api/ota/status` rapporterer state/received/total/percent/error.

**Bevidst UDELADT** ift. referenceimplementeringen: GitHub-Releases-baseret auto-opdatering (TLS-klient, CA-bundling, baggrundstasks til DNS/download) — betydelig ekstra angrebsflade/kompleksitet som hverken vores design eller Fase 5's scope kræver.

**Refaktorering undervejs:** `require_auth()`/`send_json_error()` var duplikeret hvis de skulle bruges i to filer — udtrukket til nyt `src/http_helpers.h/.cpp`, delt mellem `http_server.cpp` og `ota_handler.cpp`, så selve AUTH-TJEKKET (sikkerhedskritisk) kun findes ét sted. `httpd_config_t`s `max_uri_handlers` hævet fra ESP-IDF's default (8) til 16 — de eksisterende 4 endpoints + denne features 3 nye ramte allerede 7, tæt på en stille fejlgrænse (`httpd_register_uri_handler()`s returværdi tjekkes ikke i koden, så en overskredet grænse ville have fejlet TAVST).

**Filer ændret:** `lib/ota_validation/ota_validation.h/.cpp` (nye), `test/test_ota_validation/` (nyt), `src/http_helpers.h/.cpp` (nye), `src/ota_handler.h/.cpp` (nye), `src/http_server.cpp`.

**Live-verificeret PÅ FYSISK HARDWARE:** (1) En ugyldig upload (forkert magic byte) afvist korrekt med `500`, boardet upåvirket, `GET /api/ota/status` viste `"failed"` med den rigtige fejlbesked. (2) Et rigtigt `.bin`-upload (816.832 bytes, den faktisk byggede v0.12.0-firmware — bevidst identisk med den kørende, for at teste selve OTA-mekanismen uden at risikere en reelt anderledes/fejlbehæftet image) gennemførtes: `{"ok":true,...,"bytes":816832}`, status gik til `"success"`/100%. Boardet blev IKKE genstartet af sig selv (uptime blev ved med at stige) — først efter et eksplicit `POST /api/reboot` genstartede det (bekræftet: uptime faldt til 20s). Efter genstart: WiFi-genforbindelse automatisk, REST/Modbus TCP-servere startet igen, `GET /api/channels` viste uændret persisteret kanal-config, og `plc_ip`-firewallet virkede stadig korrekt (afviste denne test-maskines forbindelse til port 502, som forventet). Hele OTA-kæden — upload, verifikation, den bevidste ikke-automatiske aktivering, og at al persisteret config overlever — er dermed bekræftet at virke ende-til-ende.

## [0.11.0 build 0014] — 2026-09-12 — Diagnostisk Modbus read/write REST-endpoints (Fase 5, fortsat)

**Baggrund:** fortsætter FEATURES.md's roadmap — `POST /api/channels/{n}/read` (FC01/02/03/04) og `POST /api/channels/{n}/write` (FC05/06/16), §4.2's ad-hoc diagnose-endpoints (curl/Postman, supplement til Modbus TCP-data-planet, §4.1, ikke en erstatning).

**`lib/diagnostic_modbus/` (ny, hardware-uafhængig):** `mb_diag_parse_read_request()`/`mb_diag_parse_write_request()` — atomisk JSON-parsing (samme "alle felter eller afvis"-princip som kanal-config); `mb_diag_build_read_pdu()`/`mb_diag_build_write_pdu()` — bygger den rå PDU (FC05: JSON-bool mappes til Modbus-spec'ens 0xFF00/0x0000-tråd-niveau; FC16: variabelt antal registre, maks `MB_DIAG_MAX_WRITE_VALUES`=32); `mb_diag_build_read_values_json()` — skriver DIREKTE ind i output-bufferen uden en mellemliggende kopi (et diagnostisk read kan i teorien indeholde op til ~2000 bit-værdier — en ekstra kopi af den størrelse ville belaste en ESP32 HTTP-worker-tasks stak unødigt); `mb_diag_is_exception()`/`mb_diag_build_exception_json()`. 21 nye unit-tests (fangede undervejs en fejl i selve TESTEN — en forkert forventet FC16-PDU-længde, ikke i produktionskoden).

**`src/http_server.cpp`:** ny `channel_read_write_post_handler()`, registreret på `POST /api/channels/*` (dispatcher internt mellem `/read`- og `/write`-suffiks). En Modbus-exception FRA slaven selv giver stadig HTTP 200 (REST-kaldet lykkedes — indholdet rapporterer trofast hvad slaven sagde); en kanal-niveau-fejl (timeout/deaktiveret/CRC, §4) giver 502 med `error_code` = `mb_error_code_t`-værdien. HTTP-serverens `stack_size` hævet fra ESP-IDF's default (4096) til 10240 bytes — handleren kan have en JSON-body, request-/response-PDU og et svar med ~2000 værdier i sine lokale buffere samtidig.

**Filer ændret:** `lib/diagnostic_modbus/diagnostic_modbus.h/.cpp` (nye), `test/test_diagnostic_modbus/` (nyt), `src/http_server.cpp`.

**Live-verificeret PÅ FYSISK HARDWARE:** `POST /api/channels/1/read` (FC03, slave 9) gav korrekte værdier, identiske med den tidligere manuelle registerscan. Mod en ikke-eksisterende slave (99): korrekt `502 channel_error` med `error_code:1` (MB_TIMEOUT). Ufuldstændig body: korrekt `400`. `POST /api/channels/1/write` afslørede at slave 9 tilsyneladende KUN understøtter læsning: FC06 timer ud (`502`), FC16 giver en RIGTIG Modbus-exception fra slaven selv ("Illegal Function", kode 1) — validerer BEGGE fejl-veje i koden (kanal-niveau-fejl vs. ægte slave-exception) på ægte hardware. Enhedens manglende skrive-understøttelse er en egenskab ved DEVICET, ikke en kodefejl.

**Status:** 163/163 native-tests bestået, bygger rent for esp32dev.

## [0.10.0 build 0013] — 2026-09-12 — UART-kanal-config REST-endpoints (Fase 5, fortsat)

**Baggrund:** Jan bad om at fortsætte med FEATURES.md's roadmap efter Fase 4's live-verifikation — næste punkt var kanal-config-endpointsene (§4.2), der hidtil kun eksisterede som hardkodede 9600-baud/RS485-værdier i `modbus_channel.cpp`.

**NVS-skema 3 (`lib/board_config/`):** ny persisteret `mb_channel_config_t channel[2]` (enabled, mode RS485/RS232, baudrate, parity, stop_bits, timeout_ms, inter_frame_delay_ms). `mb_board_config_v2_t` frosset som migrationskilde, migrationskæde v1→v2→v3 implementeret (nye felter får defaults der matcher v0.9.0's tidligere hardkodede adfærd — intet eksisterende board ændrer opførsel ved opgraderingen). 4 nye tests.

**`lib/channel_config/` (ny, hardware-uafhængig):** `mb_is_valid_baudrate()`; `mb_channel_build_json()` til `GET`-svaret (§4.2's eksempel-skema: mode/baudrate/parity/stop_bits/timeout/inter_frame_delay + status + fuld statistik); `mb_channel_parse_config_json()` — en bevidst ikke-generisk JSON-parser (fast, kendt skema, ingen grund til en fuld rekursiv parser) til `PUT`-bodyen, ATOMISK som designdokumentet kræver: mangler eller er blot ét felt ugyldigt, afvises hele requestet. 13 nye tests.

**`src/modbus_channel.cpp`:** læser nu config fra NVS ved boot (`config_get().channel[n]`) i stedet for hardkodede konstanter. Ny `modbus_channel_apply_config()` — live-omkonfigurering routet gennem kanalens EGEN kø (samme mekanisme som almindelige transaktioner), så en igangværende transaktion altid færdiggøres på den GAMLE config før omkobling, uden en separat lås. Ny pr.-kanal-statistik (`mb_channel_stats_t`): total/successful requests, timeout/CRC/exception-tællere, seneste fejls slave-ID/adresse/type/uptime. Deaktiverede kanaler (`enabled:false`) afvises øjeblikkeligt med `MB_NOT_ENABLED` uden UART-adgang.

**`src/http_server.cpp`:** `GET /api/channels` (liste), `GET /api/channels/{n}`, `PUT /api/channels/{n}/config` — wildcard-URI-matching aktiveret (`httpd_uri_match_wildcard`) og en håndskrevet sti-parser, da ESP-IDF's `esp_http_server` ikke selv understøtter path-parametre. `src/config.cpp` fik en ny `config_set_channel()` der persisterer EFTER kanalen selv er live-omkonfigureret (aldrig omvendt — et strømudfald midt i kaldet må aldrig efterlade en UART der kører med en config, flash ikke kender).

**Filer ændret:** `lib/board_config/board_config.h/.cpp`, `lib/channel_config/channel_config.h/.cpp` (nye), `test/test_channel_config/` (nyt), `test/test_board_config/test_board_config.cpp`, `src/modbus_channel.h/.cpp`, `src/http_server.cpp`, `src/config.h/.cpp`.

**To fejl fundet og rettet UNDER selve live-verifikationen:**
1. **Boot-rækkefølge:** `modbus_channel_init_all()` blev kaldt FØR `config_begin()` i `main.cpp` — kanalerne læste dermed et nul-initialiseret (baudrate=0!) `g_config`, hvilket fik `HardwareSerial::begin()` til at forsøge baud-auto-detektion og hænge boardet ved boot. Rettet: `config_begin()` flyttet til `main.cpp::setup()`, kaldt FØR `modbus_channel_init_all()`; det nu overflødige kald i `provisioning.cpp::provisioning_begin()` fjernet.
2. **Forkert gateway-exception for deaktiverede kanaler:** `MB_NOT_ENABLED` faldt til `default`-grenen i `modbus_tcp_server.cpp`s `gateway_exception_for()` og gav fejlagtigt 0x0B ("Target Device Failed to Respond") i stedet for det korrekte 0x0A ("Gateway Path Unavailable") — en deaktiveret kanal er en util-tilgængelig sti, ikke en tavs slave.

**Live-verificeret PÅ FYSISK HARDWARE (efter begge rettelser):** schema 2→3-migration af boardets allerede-gemte config (rigtig WiFi/PLC-IP/REST-credentials/token) uden datatab, verificeret over en ÆGTE hardware-genstart (DTR/RTS-reset). `curl` mod alle tre endpoints: `GET /api/channels` (liste), `GET /api/channels/{n}`, `PUT /api/channels/{n}/config` — inkl. 404 for `n=3`, 401 uden auth, atomisk afvisning (400) af en ufuldstændig PUT-body UDEN sideeffekt (efterfølgende GET viste uændret config). Ny config overlevede en ægte reboot. En rigtig Modbus TCP-transaktion mod slave 9 på kanal A opdaterede korrekt `total_requests`/`successful_requests` i `GET /api/channels/1`. En deaktiveret kanal B afviste et forsøg med den nu-korrekte 0x0A-exception, uden at røre UART'en.

## [0.9.0.3 build 0012] — 2026-09-12 — fix: FØRSTE VELLYKKEDE live Modbus RTU-transaktion (Fase 4 afsluttet)

**Afslutter debug-serien fra v0.9.0.1/.2** — Jan har fysisk slave-device 9 på kanal A, og efter 3 rettelser fungerer hele kæden nu ende-til-ende på rigtig hardware.

**Rettelser (se BUGS.md for fuld detalje):**
1. `src/modbus_channel.cpp`: støj-tømningsløkke før afsendelse tidsbegrænset til 50 ms (var ubegrænset — kunne hænge hele kanal-tasken permanent).
2. `src/modbus_channel.cpp`: inter-character-timeouten i svar-læsningen måles nu korrekt fra sidste modtagne byte (var fejlagtigt målt fra transaktionens start — ville afbryde efter 1 byte).
3. `src/modbus_tcp_server.cpp`: `WiFiClient::readBytes()` erstattet med en selv-tidsbegrænset `read_exact()` (ikke-blokerende `available()`/`read()`) — ESP32's egen `setTimeout()` viste sig ikke pålidelig, og kunne lade en enkelt klient blokere hele portens lyttetask permanent.

**Live-resultat:** 17+ sammenhængende, korrekte Modbus TCP→RTU-transaktioner (FC03, slave 9, kanal A, register 0 = `0x4616`), heap stabil (ingen lækage) gennem hele testen.

**Filer ændret:** `src/modbus_channel.cpp`, `src/modbus_tcp_server.cpp`.

## [0.9.0.1 build 0010] — 2026-09-12 — debug: kanal-fejl-logging (live RTU-test, slave 9 på kanal A)

**Baggrund:** Jan har nu et rigtigt Modbus-device på slave-adresse 9, kanal A. Første scan-forsøg (FC03, addr 0, qty 1) via Modbus TCP gav en gateway-exception (0x0B) uden nogen forklaring på HVORFOR — `src/modbus_channel.cpp` manglede den seriel-logning CLAUDE.md regel 11 kræver for kanal-fejl.

**Tilføjet:** `channel_task()` logger nu til seriel konsol ved enhver ikke-OK `mb_error_code_t` fra `execute_transaction()` — kanal-navn, slave-ID, function code, og fejlkode som tekst (`MODBUS-FEJL kanal mb_ch_a: slave=9 fc=3 -> MB_TIMEOUT`).

**Live-test-resultat med den nye logging:** `MB_TIMEOUT` — boardet sendte forespørgslen korrekt, men modtog INTET svar fra slave 9 (ikke en CRC- eller adresse-fejl, ren stilhed på bussen). Peger på baudrate-mismatch (kanalen er hardkodet 9600 baud, RS485), forkert A/B-polaritet, manglende fælles GND, eller manglende terminering — afventer flere detaljer fra Jan om det fysiske device.

**Midlertidig test-config:** `plc_ip` er sat til denne test-maskines IP (10.1.1.75) i stedet for den rigtige PLC (10.1.1.153) for at kunne scanne fra en almindelig PC under fejlsøgningen — skal sættes tilbage til PLC'ens IP når den fysiske RTU-fejl er fundet.

**Filer ændret:** `src/modbus_channel.cpp`.

## [0.9.0 build 0009] — 2026-09-12 — Fase 4: Modbus TCP-data-plan + rigtig kanal-eksekvering

**Baggrund:** Jan har nu Fase 1-hardwaren fysisk på plads (RS485/RS232 wired til UART1/UART2) og bad om at fortsætte direkte med den rigtige (ikke-stub) Fase 4-implementering.

**`lib/modbus_tcp/` (ny, hardware-uafhængig):** MBAP-header (§4.1) — parsing (`mb_mbap_parse_header`), PDU-udtrækning med streng længde-validering (`mb_mbap_extract_pdu`), svar-bygning (`mb_mbap_build_response`). 10 nye unit-tests (125 i alt), fixture baseret på Modbus-specens klassiske FC03-eksempel udvidet med MBAP-framing.

**`src/modbus_channel.cpp` (ny):** Lag 2 (ARCHITECTURE.md) — én FreeRTOS-task pr. kanal, egen kø, synkron `modbus_channel_submit()` via en per-kald semafor. Selve RTU-transaktionen (DE/RE-toggling for RS485, to-fase timeout: fuld timeout til første byte, kort inter-character-timeout herefter) er portet fra `reference-plc-source/src/modbus_master.cpp`s mønster, men bygget på `lib/modbus_pdu`s allerede-testede framing/CRC/svar-komplethed i stedet for at gentage den logik. GPIO'er jf. §2.0.1: kanal A TX=17/RX=16/MODE_SEL=4/DIR=27, kanal B TX=18/RX=19/MODE_SEL=23/DIR=25. Baudrate/RS485-vs-RS232 er **hardkodet** (9600 baud, RS485) indtil Fase 5's kanal-config-REST-endpoint findes.

**`src/modbus_tcp_server.cpp` (ny):** 2 lyttesockets (502=kanal A, 503=kanal B, §4.1), én FreeRTOS-task pr. port. Håndhæver §4.3's ene, faste `plc_ip`-permit FØR noget Modbus-indhold parses (peer-IP sammenlignes ved `accept()`) — intet `plc_ip` sat betyder data-planet er fejl-lukket for ALLE, ikke fejl-åbent. Ved en kanal-fejl (timeout/CRC/ukendt slave/optaget) sendes en standard Modbus-gateway-exception (0x0A "Gateway Path Unavailable" eller 0x0B "Gateway Target Device Failed to Respond") i stedet for enten at hænge eller lukke forbindelsen tavst. En almindelig Modbus-exception FRA slaven selv relayes uændret (det er gyldigt indhold, ikke en transportfejl).

**Wiring:** `modbus_channel_init_all()` kaldes fra `main.cpp::setup()` (uafhængigt af WiFi-status). `modbus_tcp_server_begin()` kaldes fra `provisioning.cpp::attempt_connect()` lige efter `http_server_begin()`, kun ved vellykket WiFi-forbindelse.

**Filer ændret:** `lib/modbus_tcp/modbus_tcp.h/.cpp` (nye), `test/test_modbus_tcp/test_modbus_tcp.cpp` (ny), `src/modbus_channel.h/.cpp` (nye), `src/modbus_tcp_server.h/.cpp` (nye), `src/main.cpp`, `src/provisioning.cpp`.

**Status:** `pio test -e native` → 125/125 bestået. `pio run -e esp32dev` → bygger rent. **IKKE ENDNU verificeret mod en rigtig RTU-slave eller Jans PLC** — kræver en live-test-session (kanal, slave-ID, baudrate skal afklares med Jan) før denne feature kan meldes fuldt færdig, jf. CLAUDE.md regel 14.

## [0.8.0 build 0008] — 2026-09-12 — CLI-synlighed, REST-auth-mode, første skema-migration

**Politik-reversering (Jan): al config synlig i CLI'en.** CLAUDE.md regel 6 og EXPANSION_BOARD_DESIGN.md §4.4/§3.4.1 opdateret: den serielle CLI (fysisk USB-adgang, samme tillidsniveau som boardet selv/en factory-reset) maskerer IKKE længere WiFi/REST-adgangskoder eller management-tokenet — `show`/`status` viser dem i klartekst. Gælder UDELUKKENDE denne CLI, aldrig REST-API'et. `show`/`status` viser nu også firmware-version+build (var kun i den separate `version`-kommando før).

**`rest auth token|basic|both`** (Jan: "auth-metoden vi bruger skal kunne config'es i CLI'en") — ny CLI-kommando + `mb_rest_auth_mode_t` (persisteret felt). `lib/rest_auth`s `mb_rest_auth_check()` afviser nu eksplicit en slået-fra metode med en ny, adskilt resultatkode (`MB_REST_AUTH_METHOD_DISABLED`) i stedet for at blande den sammen med "forkerte credentials".

**Første rigtige NVS-skema-migration (schema 1→2, §3.5):** `mb_board_config_v1_t` tilføjet som en frossen kopi af schema 1's layout. `mb_config_load_from_blob()` genkender nu en v1-blob (ved størrelse + v1-checksum + `schema_version==1`) og migrerer den til v2 i stedet for at falde til fabriksdefaults — `rest_auth_mode` får default-værdien `BOTH`. `mb_rest_auth_mode_t` fik en eksplicit `: uint8_t`-underliggende type (var implicit `int`, upålideligt i en persisteret `#pragma pack(1)`-blob).

**Filer ændret:** `lib/rest_auth/rest_auth.h/.cpp` (auth_mode, METHOD_DISABLED), `lib/board_config/board_config.h/.cpp` (v1-struct, migration, checksum_v1), `lib/provisioning_cli/provisioning_cli.h/.cpp` (rest_auth_mode-felt, `rest auth`-kommando, firmware-linje i show), `src/http_server.cpp` (sender auth_mode, skelner method-disabled-besked), `src/provisioning.cpp` (token+auth_mode i status, token i show, rest-save udvidet til "rest"-præfiks generelt).

**Verificeret PÅ FYSISK HARDWARE, inkl. den præcise situation §3.5 er skrevet for:** boardet havde allerede en schema-1-config gemt fra tidligere sessioner (rigtig WiFi, REST-credentials, management-token). Efter opgradering til dette build blev den migreret korrekt — IDENTISK WiFi-ssid/password, REST-user/pass og (kritisk) SAMME management-token som før, ikke nulstillet til fabriksdefaults. `rest auth token` sat via CLI, derefter `curl` med Basic Auth mod boardet → `401` "denne auth-metode er slået fra" (ikke den generiske 401-besked); `curl` med det korrekte Bearer-token → `200`. Nulstillet til `rest auth both` bagefter. `pio test -e native` → 115/115 bestået, inkl. en manuelt konstrueret v1-blob-migrationstest og en v1-korruptionstest.

## [0.7.0 build 0007] — 2026-09-11 — REST-management-API-fundament (Fase 5, start)

**Filer tilføjet:**
- `lib/rest_auth/rest_auth.h/.cpp` — hardware-uafhængig: egen base64-decoder (RFC 4648, ingen ekstern afhængighed — vektorer krydsverificeret med Pythons `base64`-modul), `mb_rest_auth_check()` der prøver `Authorization: Bearer <token>` og `Authorization: Basic <base64(user:pass)>` (§4.4's dual auth-model — én af de to skal matche). Konstant-tids strengsammenligning for selve credential-tjekket (ikke header-parsing) for at undgå en timing-side-channel. Adgangskoder kan indeholde `:` (split kun på FØRSTE `:`, jf. RFC 7617).
- `lib/rest_status/rest_status.h/.cpp` — `mb_status_build_json()` (samme skema som designdokumentets `GET /api/status`-eksempel, §4.2, udvidet med `wifi`/`provisioned`) og `mb_status_build_error_json()` (samme `{"ok":false,...}`-stil som PLC-repoets REST-API). `error_code` udelades bevidst for rene HTTP-/auth-lags-fejl (ingen `mb_error_code_t`-værdi repræsenterer "unauthorized" — tvang ind i det skema ville være forkert, ikke kun upraktisk).
- `src/http_server.h/.cpp` — ESP-IDF `esp_http_server` på port 8080 (§4.2). `GET /api/status`, autentificeret. Startes automatisk fra `src/provisioning.cpp`'s `attempt_connect()` ved vellykket forbindelse (både manuel `connect` og automatisk genforbindelse ved boot).
- `test/test_rest_auth/test_rest_auth.cpp` — 18 tests: base64-decode (gyldig/ugyldig længde/tegn/kapacitet), Bearer (match/mismatch/ikke-konfigureret/versalfølsomhed), Basic (match/mismatch/ikke-konfigureret/password-med-kolon/ugyldig-base64/manglende-kolon), manglende header, ukendt scheme.
- `test/test_rest_status/test_rest_status.cpp` — 6 tests: status-JSON forbundet/ikke-forbundet (ip/rssi udelades korrekt), for lille buffer, balancerede `{}`, fejl-JSON med/uden `error_code`.

**Filer ændret:** `src/provisioning.cpp` (kalder `http_server_begin()`, viser REST-API-URL i `status`).

**Verificeret LIVE på fysisk hardware, over det RIGTIGE netværk (ikke simuleret):** boardet var allerede forbundet til et rigtigt WiFi (Jan havde selv provisioneret det via CLI'en undervejs) — satte `rest user`/`rest pass` via CLI'en og kørte `curl` fra udviklingsmaskinen mod boardets IP: ingen auth → `401` + korrekt fejl-JSON; forkert password → `401`; korrekt Basic Auth → `200` + korrekt status-JSON (`api_version`, `fw_version`/`fw_build` matcher `version.json`, `active_channels:2`, `wifi.connected:true` med rigtig IP/RSSI). `pio test -e native` → 127/127 bestået.

## [0.6.0 build 0006] — 2026-09-11 — NVS-persistering (config.cpp) — Fase 3 afsluttet

**Filer tilføjet:**
- `lib/board_config/board_config.h/.cpp` — hardware-uafhængig, native-testbar: `mb_board_config_t` (`#pragma pack(1)`-blob, schema-versioneret §3.5, CRC16-checksum via egen lille implementering — bevidst IKKE `lib/modbus_pdu`s, for begrebsmæssig adskillelse), `mb_config_load_from_blob()`/`mb_config_save_to_blob()` (robust mod: intet gemt, forkert størrelse, checksum-korruption, "fremtidig"/nedgraderet schema — alle falder sikkert tilbage til defaults, aldrig udefineret opførsel), `mb_config_token_from_random_bytes()` (ren hex-encoding, RNG-uafhængig), `mb_config_apply_provisioning_state()`/`mb_config_to_provisioning_state()` (transformation til/fra `mb_provisioning_state_t`, sidstnævnte bruges til auto-genforbindelse ved boot).
- `src/config.h/.cpp` — ESP32/`Preferences`-laget (NVS): `config_begin()` (indlæser ved boot), `config_apply_and_save()`, `config_factory_reset()`, `config_ensure_mgmt_token()` (genererer via `esp_fill_random()` FØRSTE gang, ellers no-op), `config_mark_provisioned()`.
- `test/test_board_config/test_board_config.cpp` — 11 tests: defaults, save/load-roundtrip, undersized buffer, intet gemt, forkert størrelse, checksum-korruption (bevidst væltet byte), "fremtidig schema-version" (manuelt konstrueret, da `save_to_blob()` altid tvinger nuværende version), token-hex-encoding (kendt input→output), `apply_provisioning_state` (feltoverførsel + garanti om at `mgmt_token` ALDRIG røres).

**Filer ændret:**
- `src/provisioning.cpp` — kobler `config.cpp` ind: `connect`-succes persisterer og udsteder (første gang) et management-token, vist ÉN gang (§3.4.1). Ny `save`-kommando (gem uden forbindelsesforsøg — Jan: "vi kan ikke save i cli til nvs"). REST-credentials persisteres uafhængigt af WiFi-status. Automatisk WiFi-genforbindelse ved boot hvis `provisioned`. `factory-reset confirm` rydder nu rent faktisk NVS (kaldte tidligere kun `ESP.restart()`).
- `lib/provisioning_cli/provisioning_cli.h/.cpp` — ny `PROV_ACTION_SAVE` + `"save"`-kommando, tilføjet til `help`.

**To fejl fundet og rettet undervejs (se BUGS.md):**
1. `show`/`status` viste ikke reel WiFi-forbindelsesstatus (Jan: "viser ikke connect status") — ny `wifi_status_text()` dækker alle `wl_status_t`-værdier eksplicit (var tidligere et uinformativt "ukendt/fejl" for normale før-forbindelse-tilstande), kaldt efter BÅDE `show` og `status`.
2. Fabriksnyt/factory-reset'et board printede en skræmmende (men ufarlig) `Preferences`-fejl-log-linje ved allerførste boot (read-only `begin()` på et endnu-ikke-oprettet NVS-namespace) — rettet til read-write `begin()`.

**Verificeret på fysisk hardware:** scriptet pyserial-test der beviser ÆGTE NVS-persistering på tværs af RIGTIGE hardware-genstarter (portlukning/genåbning trigger DTR/RTS-chip-reset, ikke kun et soft-state-skift i testscriptet) — REST-credentials og en `save`'et (ikke `connect`'et) WiFi-SSID overlevede en genstart; `factory-reset confirm` ryddede begge dele korrekt i en efterfølgende session. `pio test -e native` → 79/79 bestået.

## [0.5.0 build 0005] — 2026-09-11 — CLI-udvidelse (multi-linje, REST-auth, status, historik) + 3 designbeslutninger

**Designdokument-revision (EXPANSION_BOARD_DESIGN.md), 3 beslutninger bekræftet af Jan:**
1. **Variant A: 2 kanaler** (nyt §2.0) — første hardware-revision bruger ESP32's egne 2 frie UART-periferier direkte (UART1/UART2), ikke eksterne SPI-UART-expander-chips. Begrundet eksplicit mod §2.1's FEAT-408-regel: kun 2-3 UART-periferier aktive på en FRISK chip (UART0=CLI, UART1/UART2=kanaler) er indenfor den allerede-dokumenterede "1-2 kanaler uden dedikeret afprøvning er OK"-margin — IKKE en gentagelse af FEAT-408s "3. periferi på en chip der allerede havde 2 aktive"-scenarie. Forudsætning: ESP32 UDEN PSRAM (erratummet er PSRAM-cache-specifikt). Variant B (8 kanaler, SPI-expander) bevares i dokumentet som senere/større udgave.
2. **Dual REST-auth** (§4.4) — Bearer-token (uændret) OG HTTP Basic Auth (nyt, brugernavn/adgangskode sat via CLI) accepteres BEGGE, ikke den ene i stedet for den anden.
3. **Diagnostisk Modbus read/write via REST** (§4.2, nye endpoints `/api/channels/{n}/read`+`/write`) — et SUPPLEMENT til ad-hoc test/fejlsøgning (curl/Postman), ikke en erstatning for Modbus TCP-data-planet (§4.1), som forbliver PLC'ens høj-frekvente driftsvej (§1.3's JSON-overhead-analyse står stadig).
4. §4.1 udvidet med et konkret register-mapping-eksempel: port vælger kanal, MBAP `Unit ID` vælger RTU-slave-adresse, PDU'en (og dermed registeradressen) rejser uændret igennem — boardet omnummererer intet.

**CLI-udvidelse (Jan):**
- **Multi-linje output** — `show`/`help` printer nu ét felt/én kommando pr. linje (`\r\n`-separeret) i stedet for pipe-separeret étlinjetekst. Ny `append_line()`-hjælper i `lib/provisioning_cli/provisioning_cli.cpp` (overflow-sikker mod `snprintf`s returværdi, som kan overstige den faktiske skrevne længde ved afkortning). `MB_PROV_MSG_MAX_LEN`: 192 → 1024 (fulde help-tekst målt til 717 bytes efter udvidelsen — god margin efter v0.4.0's afkortnings-bug).
- **`rest user <navn>` / `rest pass <kode>`** — REST-API Basic Auth-credentials, nye felter i `mb_provisioning_state_t`. Password lækker aldrig i klartekst (samme disciplin som WiFi-password).
- **`status`** — ny kommando, `PROV_ACTION_STATUS`-resultatkode. Selve indholdet (uptime, fri heap, WiFi-status/IP/RSSI) sammensættes i `src/provisioning.cpp` (runtime-data `lib/` ikke har adgang til).
- **Kommando-historik** — op/ned-piletaster, 8-entry ringbuffer (`src/provisioning.cpp`), ANSI-escape-sekvens-genkendelse (`ESC [ A`/`ESC [ B`), skærm-redraw ved recall. Kun historik-navigation understøttes (ingen cursor-inde-i-linjen-redigering).

**Filer ændret:** `lib/provisioning_cli/provisioning_cli.h/.cpp`, `src/provisioning.cpp`, `test/test_provisioning_cli/test_provisioning_cli.cpp` (34 nye tests, 67 i alt).

**Verificeret på fysisk hardware:** scriptet pyserial-test — multi-linje-output (`\r\n`-optælling), `rest`/`status`-kommandoer, OG kommando-historik med faktisk afsendte raw ANSI-escape-byte-sekvenser (`\x1b[A`/`\x1b[B`) og verifikation af den redrawede linje samt korrekt genudførelse efter Enter. `pio test -e native` → 67/67 bestået.

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
