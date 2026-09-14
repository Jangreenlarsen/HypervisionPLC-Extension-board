# Changelog

Nyeste øverst. Format: `## [version build NNNN] — YYYY-MM-DD — beskrivelse`

---

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
