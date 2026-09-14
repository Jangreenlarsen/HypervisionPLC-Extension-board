# Release Notes

---

## v0.17.0 — 2026-09-14 — MODE_SEL (GPIO4) er nu en fabriks-input, ikke et PUT-bart felt

RS232/RS485-valget (MODE_SEL, GPIO4) er ændret fra et firmware-styret output til en rigtig hardware-INPUT — installatøren/fabrikanten forbinder GPIO4 til 3.3V (RS485) eller GND (RS232) fysisk, ÉN gang, og firmwaren læser den værdi ved hver opstart. `mode` kan derfor ikke længere sættes via `PUT /api/channels/{n}/config` — det er nu en ren læseværdi (`GET /api/channels/{n}`, `GET /api/status`s `board_mode`, seriel `status`/`show`). Baudrate/parity/stop_bits/timeout osv. er upåvirket og stadig fuldt konfigurerbare.

**Baggrund:** Jan opdagede at boardet altid rapporterede `rs485`, uanset hvad han påtrykte GPIO4 udefra — fordi pinden siden v0.14.0 var en OUTPUT, ikke en INPUT firmwaren læste. Se BUGS.md for detaljer.

**Næste skridt**: fysisk montering og test af W5500-modulet (afventer stadig fra v0.13.0); live-verifikation af denne ændring på fysisk hardware.

---

## v0.16.0 — 2026-09-14 — Ethernet-status og board_mode i den serielle CLI

Den serielle CLI's `status`- og `show`-kommandoer viser nu Ethernet-linkstatus (`eth.connection`/`eth.ip`) og boardets RS232/RS485-mode (`board_mode`) — begge var hidtil kun synlige via REST-API'et.

**Næste skridt**: fysisk montering og test af W5500-modulet (afventer stadig fra v0.13.0).

---

## v0.15.0 — 2026-09-14 — `GET /api/status` får et direkte `board_mode`-felt

`GET /api/status` rapporterer nu direkte om boardet kører RS232 eller RS485 (`"board_mode":"rs485"`) — siden det siden v0.14.0 er én fælles indstilling for hele boardet, giver det mere mening at have det tilgængeligt her end kun indirekte via en kanals egen config.

**Næste skridt**: fysisk montering og test af W5500-modulet (afventer stadig fra v0.13.0).

---

## v0.14.0 — 2026-09-14 — Hardware-revision: delt MODE_SEL, W5500 får en rigtig RST-pin

MODE_SEL (valget mellem RS232 og RS485) er nu ÉN fælles indstilling for hele boardet i stedet for én pr. kanal — kanal A og B kan derfor ikke længere have forskellig mode. Til gengæld blev der frigjort en GPIO, som nu bruges til en rigtig, software-styret nulstillings-pin til W5500-Ethernet-modulet (i stedet for kun at stole på modulets eget power-on-reset).

REST-API'et er uændret at kalde på (`PUT /api/channels/{n}/config` tager stadig `mode` pr. kanal), men ændrer du mode på den ene kanal, følger den anden automatisk med — det er en bevidst konsekvens af at de nu deler samme fysiske hardware-pin, ikke en fejl.

**Live-verificeret, inkl. kanal B's FØRSTE test nogensinde mod en rigtig slave:** Jan flyttede test-devicet til kanal B under testen — 6/6 sammenhængende, korrekte Modbus-transaktioner. Mode-spejlingen bekræftet at virke og overleve en ægte genstart.

**Næste skridt**: fysisk montering og test af W5500-modulet (afventer stadig fra v0.13.0).

---

## v0.13.0 — 2026-09-14 — Valgfri W5500-Ethernet (dual-stack med WiFi)

Boardet kan nu få en fysisk W5500-Ethernet-modul tilsluttet ved siden af WiFi — begge forbindelser kan være aktive samtidig, og al eksisterende netværksfunktionalitet (Modbus TCP, REST-API'et) virker uændret uanset hvilken vej trafikken kommer ind. Ethernet konfigureres slet ikke manuelt — den henter bare en IP via DHCP så snart et kabel er tilsluttet. `GET /api/status` viser nu Ethernet-linkstatus og -IP ved siden af WiFi's.

GPIO-allokeringen (SCK=14, MOSI=13, CS=32, MISO=35, INT=39) blev aftalt med Jan dagen før og er låst fast i designdokumentet — ingen konflikt med de eksisterende Modbus-kanalers UART-pins.

**Live-boot-testet UDEN fysisk hardware** (ingen W5500-modul monteret endnu) — boardet forbliver fuldt funktionsdygtigt (WiFi/CLI/Modbus/REST upåvirket), og fandt undervejs en reel bug (manglende GPIO-interrupt-service-opsætning), som blev rettet og genverificeret. **Selve Ethernet-forbindelsen (link/DHCP/faktisk dataoverførsel) er endnu ikke testet** — det kræver at Jan fysisk monterer modulet.

**Næste skridt**: fysisk montering og test af W5500-modulet.

---

## v0.12.0 — 2026-09-12 — OTA-firmwareopdatering (Fase 5 afsluttet)

Boardet kan nu opdateres over netværket: `POST /api/ota` (upload en `.bin`-fil som rå body — samme mønster som `curl --data-binary @firmware.bin`), `GET /api/ota/status` (følg fremdriften), `POST /api/reboot` (aktivér den uploadede firmware). Uploadet skrives direkte til den inaktive OTA-partition og verificeres automatisk — men aktiveres først når du selv kalder `/api/reboot`, så en ny firmware aldrig kommer som en overraskelse.

Med denne feature er hele Fase 5's oprindelige plan for management-API'et gennemført: status, kanal-config, diagnostisk read/write og nu OTA.

**Live-verificeret:** en ugyldig upload afvises korrekt uden at påvirke boardet; et rigtigt firmware-upload (816 KB) blev skrevet, verificeret og — først efter et eksplicit `/api/reboot` — aktiveret. Al persisteret config (kanal-indstillinger, PLC-firewall) overlevede uændret.

**Næste skridt**: ingen flere planlagte Fase 5-punkter tilbage — se FEATURES.md for hvad der herefter står for tur (kanal B-test, Variant B, robusthedstest).

---

## v0.11.0 — 2026-09-12 — Diagnostisk Modbus read/write REST-endpoints

To nye endpoints til ad-hoc test/fejlsøgning uden at skulle åbne en Modbus TCP-forbindelse: `POST /api/channels/{n}/read` (læs coils/discrete inputs/holding-/input-registre) og `POST /api/channels/{n}/write` (skriv én coil/ét register eller flere registre). Begge tager en simpel JSON-body (function code, slave-ID, adresse, quantity/value(s)) og returnerer resultatet som JSON — perfekt til at teste en ny feltbus-slave direkte med `curl` eller Postman, uden om PLC'ens egen drift på Modbus TCP-data-planet.

En Modbus-exception fra selve slaven (fx "Illegal Data Address") rapporteres som et almindeligt, vellykket REST-svar med exception-koden i indholdet — kun rigtige kanal-/transport-fejl (timeout, deaktiveret kanal) giver en HTTP-fejlstatus.

**Live-verificeret**: læsning mod en rigtig slave gav korrekte værdier; forsøg på skrivning afslørede at test-devicet kun understøtter læsning (FC06 timer ud, FC16 giver en ægte "Illegal Function"-exception fra slaven) — begge fejl-veje bekræftet at virke korrekt.

**Næste skridt**: OTA-firmwareopdatering (`POST /api/ota`), som afslutter Fase 5's planlagte endpoints.

---

## v0.10.0 — 2026-09-12 — UART-kanal-config REST-endpoints

Boardets to Modbus-kanaler kan nu konfigureres over netværket i stedet for at være hardkodet: `GET /api/channels`, `GET /api/channels/{n}` og `PUT /api/channels/{n}/config` lader en autentificeret klient (fremover: PLC'ens System-side) sætte baudrate, RS232/RS485-mode, paritet, stop-bits, timeout og inter-frame-delay pr. kanal — persisteret i flash, og anvendt live uden en genstart. Hver kanal har nu også løbende statistik (antal forespørgsler, fejltyper, seneste fejl) tilgængelig via samme `GET`.

En kanal kan slås fra (`enabled:false`) og afvises så øjeblikkeligt uden at røre UART-hardwaren — nu med den korrekte 0x0A-gateway-exception (var fejlagtigt 0x0B, rettet under live-testen).

**Live-verificeret på fysisk hardware:** boardets eksisterende config migrerede korrekt til det nye skema (verificeret over en ægte genstart), alle tre endpoints afprøvet med `curl` (inkl. 404/401/atomisk 400-afvisning), en rigtig Modbus-transaktion opdaterede kanal-statistikken korrekt. Fandt og rettede undervejs en boot-hæng (forkert initialiseringsrækkefølge kunne give baudrate=0 ved opstart).

**Næste skridt**: de resterende Fase 5-endpoints (diagnostisk read/write, OTA).

---

## v0.9.0.3 — 2026-09-12 — Fase 4 afsluttet: FØRSTE vellykkede live Modbus RTU-transaktion

Efter tre rettede firmware-bugs (se CHANGELOG/BUGS.md) fungerer expansion-boardet nu ende-til-ende på rigtig hardware: en Modbus TCP-forespørgsel fra en PC (fremover: PLC'en) bliver korrekt relayet til en fysisk RTU-slave over kanal A og svaret sendt retur — 17+ sammenhængende, korrekte transaktioner verificeret, ingen heap-lækage.

De tre bugs var alle robusthedsproblemer der kunne få boardet til at hænge permanent (kanal-tasken eller TCP-lyttesocket'en) — ikke wiring/RS485-problemer, som ellers var den oprindelige mistanke.

**Næste skridt**: samme test på kanal B, samt de resterende REST-endpoints (kanal-config, diagnostisk read/write, OTA, Fase 5).

---

## v0.9.0.1 (debug) — 2026-09-12 — kanal-fejl-logging til live RTU-fejlsøgning

Under den første live Modbus RTU-test (slave 9, kanal A) manglede boardet struktureret fejl-logging for kanal-transaktioner (CLAUDE.md regel 11). Tilføjet nu — enhver kanal-fejl vises på seriel konsol med kanal, slave-ID, function code og fejlkode.

Første test-resultat: `MB_TIMEOUT` — intet svar fra slave 9 overhovedet. Fejlsøgningen fortsætter (baudrate/wiring).

---

## v0.9.0 — 2026-09-12 — Fase 4: Modbus TCP-data-plan + rigtig kanal-eksekvering

Boardet kan nu rent faktisk udføre Modbus RTU-transaktioner over de to fysiske UART-kanaler og videreformidle dem som Modbus TCP (port 502=kanal A, 503=kanal B) — Fase 4 er kodemæssigt på plads, nu hvor Jan har den fysiske hardware (RS485/RS232 wired til UART1/UART2) klar.

Ny lag-2-eksekvering (`modbus_channel.cpp`): én FreeRTOS-task pr. kanal, RTU-framing genbruger den allerede-testede `lib/modbus_pdu`. Ny TCP-server (`modbus_tcp_server.cpp`): MBAP-parsing (`lib/modbus_tcp`, ny), §4.3's ene faste PLC-IP-permit håndhævet før noget Modbus-indhold parses, og standard gateway-exceptions (0x0A/0x0B) når feltbus-slaven ikke svarer korrekt.

Baudrate og RS232-vs-RS485 er indtil videre hardkodet (9600 baud, RS485) — bliver konfigurerbart når Fase 5's kanal-config-REST-endpoint bygges.

**Verifikationsstatus:** `pio test -e native` (125/125) og `pio run -e esp32dev` er bestået — men den egentlige ende-til-ende-test mod en fysisk RTU-slave og Jans rigtige PLC er IKKE gennemført endnu. Det er næste skridt.

**Næste skridt**: live Modbus TCP-test mod Jans PLC og en rigtig RTU-slave (kræver kanal/slave-ID/baudrate-detaljer), derefter de resterende REST-endpoints (kanal-config, diagnostisk read/write, OTA).

---

## v0.8.0 — 2026-09-12 — CLI-synlighed, REST-auth-mode-valg, første skema-migration

Den serielle CLI viser nu ALT konfigureret data i klartekst (WiFi/REST-adgangskoder, management-tokenet) — en bevidst politik-ændring, da fysisk USB-adgang allerede er tillidsgrænsen; REST-API'et (netværksvendt) returnerer fortsat aldrig disse hemmeligheder. `show`/`status` viser nu også firmware-version+build.

Ny CLI-kommando `rest auth token|basic|both` lader dig eksplicit slå Bearer-token eller Basic Auth fra for REST-API'et.

Denne opdatering er også den FØRSTE rigtige test af NVS-skema-migrationssystemet (§3.5): boardets allerede-gemte config (fra tidligere test-sessioner) blev korrekt migreret til det nye skema UDEN datatab — samme WiFi, REST-credentials og management-token som før opgraderingen.

**Næste skridt**: de resterende REST-endpoints (kanal-config, diagnostisk read/write, OTA) og Fase 1's fysiske hardware-bring-up (UART1/UART2 + RS232/RS485-transceivere, §2.0.1's GPIO-allokering).

---

## v0.7.0 — 2026-09-11 — REST-management-API-fundament (Fase 5, start)

Boardet har nu et rigtigt, autentificeret REST-API på port 8080 — `GET /api/status` accepterer enten det auto-genererede Bearer-token eller brugernavn/adgangskode (§4.4's dual auth-model). Testet LIVE med `curl` mod det fysiske board over det rigtige netværk (boardet var allerede forbundet, provisioneret af Jan selv via CLI'en): manglende/forkert auth giver `401`, korrekt Basic Auth giver `200` og en korrekt status-JSON med firmware-version, uptime, heap, WiFi-status og kanaltal.

**Næste skridt**: de resterende REST-endpoints — kanal-config, diagnostisk Modbus read/write, firewall-allowlist, OTA — er stadig tilbage af Fase 5. Fase 4 (Modbus TCP-server) og Fase 1 (fysisk UART1/UART2-hardware) venter fortsat på fysisk hardware.

---

## v0.6.0 — 2026-09-11 — NVS-persistering (Fase 3 afsluttet)

Boardets konfiguration overlever nu en genstart. `config.cpp`/`lib/board_config/` gemmer WiFi, PLC-IP og REST-credentials i NVS (schema-versioneret, §3.5), og udsteder automatisk et management-API-token ved første vellykkede `connect` (vist én gang, som designet). Ny CLI-kommando `save` gemmer uden at forsøge en forbindelse. Boardet forsøger nu selv at genoprette WiFi-forbindelsen ved boot, hvis det allerede er provisioneret. Verificeret med et scriptet test der beviser ægte persistering på tværs af rigtige hardware-genstarter, ikke kun in-memory-tilstand.

To mindre fejl fundet og rettet undervejs: `show`/`status` viste ikke den faktiske WiFi-forbindelsesstatus tydeligt nok, og et fabriksnyt board printede en ufarlig, men skræmmende fejl-log-linje ved første boot.

**Næste skridt**: Fase 4 (Modbus TCP-server, 2 porte 502-503) og Fase 5 (den fulde REST-management-API — kanal-config, diagnostisk read/write, OTA, status — §4.2) er de næste store byggesten. Fase 1 (fysisk hardware-bring-up: UART1/UART2 + RS232/RS485-transceivere) kræver fortsat fysisk hardware og kan ikke udføres af Claude alene.

---

## v0.5.0 — 2026-09-11 — CLI-udvidelse + 3 designbeslutninger

Tre arkitekturbeslutninger er bekræftet og indarbejdet i EXPANSION_BOARD_DESIGN.md: (1) boardets første hardware-revision bliver **Variant A — 2 kanaler via ESP32's egne UART1/UART2**, ikke det oprindelige 8-kanals SPI-expander-design (som bevares i dokumentet som en senere Variant B); (2) REST-API'et vil acceptere **både** det eksisterende Bearer-token **og** et nyt Basic Auth brugernavn/adgangskode; (3) kommende REST-endpoints for Modbus read/write bliver et **diagnostisk supplement**, ikke en erstatning for Modbus TCP-data-planet.

CLI'en er udvidet på Jans opfordring: `show`/`help` er nu multi-linje (ikke længere étlinjetekst), nye kommandoer `rest user`/`rest pass` (REST-credentials) og `status` (systemstatus: uptime, heap, WiFi), samt kommando-historik via op/ned-piletaster. Alt testet native (67/67) og interaktivt på fysisk hardware, inkl. et scriptet test der sender ægte ANSI-escape-byte-sekvenser for piletasterne.

**Næste skridt**: `config.cpp` (NVS-persistering, resten af Fase 3) er den naturlige næste byggesten — uden den kan `connect`/`rest user`/`rest pass` ikke overleve en genstart, og REST-API'ets auth kan ikke rent faktisk håndhæves. Herefter Fase 4 (Modbus TCP-server, 2 porte) og Fase 5 (den fulde REST-management-API, §4.2 — kanal-config, diagnostisk read/write, OTA, status).

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
