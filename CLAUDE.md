# Projekt: HypervisionPLC Extension board

Dette er Claudes system-prompt for dette projekt. Den læses altid først og følges uden undtagelser.

---

## Sprog (UFRAVIGELIG)

Al kommunikation med Jan i dette projekt foregår på **dansk** — svar, statusopdateringer, spørgsmål, afrapportering og forklaringer skrives på dansk, ikke engelsk. Dokumentation (CLAUDE.md, ARCHITECTURE.md, FEATURES.md, BUGS.md, CHANGELOG.md, RELEASE_NOTES.md, commit-beskeder) følger samme konvention. Kode-identifikatorer (funktions-/variabelnavne) kan fortsat være engelske efter normal programmeringskonvention — det er samtalen og projektdokumentationen, ikke kildekoden, denne regel gælder.

---

## Projektbeskrivelse

HypervisionPLC Extension Board er et selvstændigt ESP32-baseret firmware- og hardwareprojekt, der tilføjer op til 8 uafhængige Modbus RTU Master-kanaler til en Hypervision PLC via netværk, uden at røre PLC'ens egen chip.

**Baggrund:** PLC-projektet (`Modbus_server_slave_ESP32`) forsøgte (FEAT-408) at tilføje en 2. Modbus Master direkte på PLC'ens egen ESP32 og stødte på en hardware-blocker — heap-korruption ved aktivering af UART-hardware-periferi #1, med stærk mistanke om et ESP32 PSRAM-cache-erratum. Se [EXPANSION_BOARD_DESIGN.md](EXPANSION_BOARD_DESIGN.md) §0 for den fulde root-cause-analyse. Løsningen er et fysisk separat "gateway"-board, der ejer og driver de ekstra feltbusser selv.

**Arkitektur i korte træk:** en ESP32 gateway-MCU forbundet via SPI til eksterne UART-expander-chips (MAX14830/SC16IS75x), der hver driver op til 8 RS485/RS232-feltbus-kanaler (modulær bestykning, auto-detekteret ved boot — se §2.2.2). Boardet er bevidst "tyndt": al forretningslogik, prioritering og caching forbliver på PLC'en; boardet udfører kun de fysiske Modbus RTU-transaktioner og eksponerer dem via en hybrid netværks-grænseflade — Modbus TCP (data-plan, port 502-509) + autentificeret REST/JSON (management-plan: kanal-config, firewall, OTA, port 8080).

**Kerneprincip ("single pane of glass"):** boardet har INGEN egen driftsbrugerflade — kun en minimal seriel provisioning-CLI over USB (§3.4/§3.4.1) for at komme på nettet første gang. Al løbende konfiguration sker fra PLC'ens eksisterende System-side.

**Scope for dette repo:** KUN expansion-boardets eget hardware+firmware (design-dokumentets §1-4, §9-10) — et selvstændigt firmware-projekt uden kode-afhængighed til PLC-repoet. PLC-side-integrationen (§5: `modbus_expansion.cpp`, `web/system.html`-udvidelse, CLI/ST Logic) sker INDE I `Modbus_server_slave_ESP32`-repoet og er et separat, fremtidigt arbejde — UDENFOR scope her. `reference-plc-source/` indeholder statiske kode-kopier fra PLC-repoet, brugt udelukkende som implementeringsskabeloner (se `reference-plc-source/README.md`).

**Målgruppe:** industrielle installatører/driftsteknikere der udvider en Hypervision PLC-installation med flere feltbus-kanaler — samme bruger som allerede administrerer PLC'en.

**Reference-stack**:
- **Gateway-MCU / firmware**: ESP32, PlatformIO + Arduino-core (samme toolchain-familie som PLC'en), FreeRTOS — én task pr. Modbus-kanal
- **Feltbus-hardware**: UART-expander over SPI (MAX14830 / SC16IS750 / SC16IS752) → SP3485 (RS485) / MAX3232 (RS232) transceiver-par pr. kanal, valgt via expander-GPIO (§2.2.1)
- **Netværk**: WiFi (indbygget) + valgfri Ethernet (W5500 over SPI)
- **Protokoller**: Modbus TCP (data-plan, 8 porte) + REST/JSON (management-plan, Bearer-token-auth)
- **Deployment**: fysisk PCB monteret i el-skab; første opsætning via seriel CLI over USB (§3.4/§3.4.1); al efterfølgende drift/konfiguration/OTA-opdatering fjernstyret fra PLC'ens System-side — ingen SSH/lokal netværksadgang i normal drift

---

## Faste regler

1. **Versionering (UFRAVIGELIG)**: Projektet versioneres via [version.json](version.json). Denne fil er den **eneste** kilde til versionsnumre — alle andre steder (backend, frontend, changelog) læser herfra.
   - Normalt format: `{ "version": "MAJOR.MINOR.PATCH", "build": "NNNN" }`
   - **build**: incrementeres med **1** ved HVERT commit med kodeændringer (0001 → 0002 → ...).
   - **PATCH**: incrementeres ved bug fixes (afsluttede). Build nulstilles IKKE.
   - **MINOR**: incrementeres ved nye features. PATCH sættes til 0.
   - **MAJOR**: incrementeres ved breaking changes (fx skema-ændringer der kræver migration) eller store milepæle. MINOR og PATCH sættes til 0.
   - **Debugging-format (4 decimaler)**: Under aktiv fejlsøgning bruges `MAJOR.MINOR.PATCH.D` hvor D starter på 1 og incrementeres for hvert debug-commit: `v0.1.0.1`, `v0.1.0.2`, osv. Commit-beskeder præfikses `vX.X.X.D-bNNNN: debug: beskrivelse`.
   - **Afslutning af debugging**: Når Jan bekræfter *"nu virker det som det skal"*, incrementeres PATCH og D nulstilles: `v0.1.0.3` → `v0.1.1.0`. Commit markeres som `fix:` og afslutter debug-serien.
   - **Kun-dokumentations-commits** (RELEASE_NOTES.md, CHANGELOG.md, BUGS.md, FEATURES.md uden kodeændringer): bump IKKE version — lav commit uden versionsbump.
   - **RELEASE_NOTES.md skal opdateres ved ETHVERT commit der ændrer kode** — features, bugfixes og debug-afslutninger. Dokumentations-commits er undtaget. Glem aldrig dette.
   - Changelog-entries tagges med versionsnummer: `## [0.1.0 build 0001] — YYYY-MM-DD — beskrivelse`.
   - Claude **skal** opdatere `version.json` og vise den nye version i commit-beskeden.

2. **Ny funktionalitet (features)** skal ALTID registreres i [FEATURES.md](FEATURES.md) *før* implementering påbegyndes. Opdatér status når den er færdig.

3. **Bugs** skal ALTID registreres i [BUGS.md](BUGS.md) så snart de opdages. Opdatér med løsning når de er fikset.

4. **Alle kodeændringer** skal logges i [CHANGELOG.md](CHANGELOG.md) med version, dato, berørte filer og kort beskrivelse. Nyeste øverst.

5. **Lag-arkitekturen** beskrevet i [ARCHITECTURE.md](ARCHITECTURE.md) skal respekteres til enhver tid. Intet lag må springe et lag over eller kaldes "baglæns" — fx må netværks-/protokollaget (`modbus_tcp_server.cpp`, `http_server.cpp`) aldrig tale direkte til `uart_expander.cpp`s SPI-interface, kun til en kanals kø (`modbus_channel.cpp`).

6. **Hemmeligheder og eksterne nøgler (UFRAVIGELIG, revideret — to forskellige regler for to forskellige grænseflader)**: API-nøgler, adgangskoder og andre hemmeligheder må ALDRIG committes eller logges i git. Projektets hemmeligheder er: WiFi-adgangskoden, REST-brugernavn/adgangskode og management-API'ets Bearer-token — alle sat via den serielle CLI (§3.4) og opbevaret i boardets NVS/flash.
    - **REST-API'et (netværksvendt, §4.2/§4.4):** hemmeligheder må ALDRIG sendes tilbage til en klient. Tokenet er write-only fra det øjeblik det er genereret — intet endpoint må nogensinde returnere det, kun bekræfte at gyldige credentials blev præsenteret (401 ved manglende/forkert).
    - **Den serielle CLI (Jan, bekræftet — fysisk USB-adgang er allerede tillidsgrænsen):** her gælder IKKE samme regel. `show`/`status` viser ALT konfigureret data i klartekst, INKL. WiFi-password, REST-password og management-tokenet — maskering på dette lag giver ingen reel beskyttelse (en angriber med fysisk adgang kan alligevel factory-reset'e og selv sætte nye credentials), kun friktion for den legitime installatør. Denne undtagelse gælder UDELUKKENDE den serielle CLI, aldrig REST-API'et eller nogen fremtidig netværksgrænseflade.

7. **Read/write rettigheder**: Claude har forhåndsgodkendelse (via [.claude/settings.local.json](.claude/settings.local.json)) til at læse, skrive og redigere filer i projektmappen. Samme forhåndsgodkendelse dækker **alle kommandoer der er relevante for at køre og teste projektet** — uden at spørge først (fx `pio test`, `pio run`/`pio check`). Kun ægte destruktive handlinger (force-push, `git reset --hard`, sletning af andet end egne midlertidige testdata) kræver stadig eksplicit accept, jf. `deny`-listen.

8. **Versionskontrol**: Projektet er et git-repo. Efter enhver logisk afsluttet ændring skal Claude lave en git commit med en beskrivende commit-besked. Aldrig bulk-commits af urelaterede ændringer.

9. **GitHub branch-strategi (UFRAVIGELIG)**:
   - `dev` — aktiv udviklingsbranch. **Al ny kode commites hertil.** Claude arbejder altid på `dev`.
   - `main` — stabil release-branch. Kun opdateret via PR/merge fra `dev` når en release er klar. Produktion følger `main`.
   - Claude skal pushe til `origin dev` efter hvert commit — aldrig direkte til `main`.
   - Merge `dev` → `main` gøres manuelt af Jan når en release er godkendt.

10. **Push og merge efter commit (UFRAVIGELIG)**: Efter ethvert commit skal Claude automatisk:
    - Pushe til `origin dev` — **uden at spørge først**
    - Spørge Jan: *"Vil du også merge til `main` og pushe?"*
    - Hvis ja: merge `dev` → `main` med `--no-ff` og pushe `origin main`
    - Hvis nej: forblive på `dev` og informere om at `main` ikke er opdateret
    - Svarer Jan blot *"main"*, betyder det ja til hele kæden (push `dev` → merge → push `main`), ikke kun det ene led.

11. **Runtime-logging**: Kritiske handlinger og fejl (kanal-fejl, OTA-forsøg, firewall-ændringer, auth-afvisninger, watchdog-resets) logges struktureret — via **syslog** (v0.26.0, `syslog_log()`/`syslog_logf()`, altid forsøgt, uafhængig af CLI-debug-tilstand, egen verbositet pr. konfigureret modtager) og/eller via **seriel konsol** (kun via CLI'ens `debug modbus ...`-system, v0.25.0). **Kanal-fejl (v0.28.1, Jan bekræftet) vises IKKE længere ubetinget på den serielle konsol** — kun når `debug modbus ...` er slået til for den pågældende kanal; syslog er upåvirket af dette og modtager fortsat alt. Design nye kritiske fejl/hændelser efter samme mønster: syslog altid, seriel konsol kun bag debug — ikke en ubetinget `Serial.print()`. Log-niveau for `ESP_LOGE/W/I/D`-makroer (adskilt fra dette) konfigureres via `CORE_DEBUG_LEVEL`/build-flag i `platformio.ini`, ikke en miljøvariabel (der er ingen OS-proces at sætte den på). Reset-årsag persisteres til RTC/NVS ved hver genstart (`watchdog_monitor.cpp`-mønsteret, jf. designdokumentets §3.3/§3.6) — kritisk for at kunne diagnosticere fejl der kun viser sig i felten, uden fysisk adgang til boardet.

12. **Kodekvalitets-foranalyse (UFRAVIGELIG)**: Dette er en fast metode Claude *altid* anvender — både løbende mens der skrives ny kode, og som selvtjek før en feature/fix meldes færdig. Punkterne er generaliserede lektioner fra reelle fejl i tidligere projekter:
    - **Null/fejl-propagering**: ethvert kald der kan returnere `None`/`null` eller kaste en fejl, skal have sit resultat tjekket *før* det bruges. Antag aldrig succes.
    - **Fejlbeskeder til brugeren**: frontend skal *altid* vise den specifikke fejlbesked fra backend, aldrig kun en generisk besked når en specifik findes. Enhver `catch`-blok skal enten vise fejlen eller have en eksplicit, begrundet kommentar om hvorfor den bevidst undertrykkes.
    - **Adgangskontrol-lockout**: permission-/rolle-systemer skal altid beskyttes mod at ende i en tilstand uden nogen med adgang til at rette det igen (fx sidste admin fjernet). Håndhæv den slags regler i **backend**, ikke kun som en UI-bekvemmelighed der er triviel at omgå.
    - **Eksterne API'er og "tomhed"**: brug `is not None` — aldrig ren truthiness — for tal-/valgfri-felter der lovligt kan være `0`/tomme. Håndtér altid et catch-all for uventede fejlstatusser fra eksterne API'er (ikke kun de statuskoder man tilfældigvis har tænkt på).
    - **"Tomhed"-repræsentationer generelt**: når en bug rettes for én repræsentation af "tom" (fx `null`), tjek samtidig alle andre ækvivalente (tom streng, whitespace-only) for samme klasse fejl — ret hele klassen, ikke kun den rapporterede instans.
    - **Sikkerhedskonfiguration**: usikre default-værdier (hemmeligheder, nøgler, adgangskoder) skal advare eller nægte at starte hvis de stadig er i brug ved opstart — aldrig glide stille igennem til en kørende instans.
    - **Samtidige delvise opdateringer ("last write wins")**: en funktion der opdaterer et dokument må aldrig læse hele dokumentet, ændre ét felt i hukommelsen og skrive det hele tilbage ("read-modify-write") hvis flere sådanne kald kan ske i hurtig rækkefølge. Brug i stedet punkt-sti-`$set` (kun de faktisk ændrede felter), så to samtidige kald der rører *forskellige* felter ikke overskriver hinanden.
    - **Bulk-operationer mod eksterne API'er**: fejl der rammer *hele batchen* (manglende/ugyldig nøgle, rate-limit) skal håndteres adskilt fra fejl der kun rammer ét element. Tjek forudsætninger *før* løkken, og stop batchen straks ved et rate-limit i stedet for at hamre en allerede-blokeret API.
    - **Test i den faktiske runtime-kontekst, ikke kun logikken**: skal koden køre under en bestemt sandkasse/isolation (systemd-service, container, begrænset bruger), er det ikke nok at teste logikken i et privilegeret shell — verificér i den kontekst koden rent faktisk kører i produktion, *før* funktionen meldes færdig.
    - **Rammeværkets fejl har en anden form end vores egne**: når et lag oversætter fejl til noget brugeren kan læse, dækker det typisk kun de fejl vi selv kaster. Rammeværket kaster sine egne, i sit eget format (fx en valideringsfejl som en *liste* af objekter frem for en streng). Tjek eksplicit *begge* former.
    - **Regler der kun gælder én gren**: når en regel indføres for ét tilfælde (kun ved oprettelse, kun for den ene ressource), så gennemgå de øvrige grene *med det samme* — opdatering, den anden ressource, import-vejen. En regel der kun holder halvvejs opdages typisk først som en fejlmelding fra Jan.
    - Ved større funktioner (auth, permissions, betalinger, data-integritet) skal Claude proaktivt overveje disse punkter under implementering, ikke først vente på at Jan beder om en fejl-gennemgang.

13. **Tests**: Ny logik der kan gå galt uden at nogen opdager det — PDU/CRC-parsing, MBAP-framing, JSON-fejlformat, cache/dedup, adaptiv backoff, NVS-schema-migration — skal have en test der kan køre UDEN fysisk hardware. Kør **alle** relevante testsuiter før noget meldes færdigt:
    - `pio test -e native` — logik-unittests (protokol/config/cache) afkoblet fra Arduino/ESP-IDF-hardware-API'er
    - `pio run` — verificér at firmwaren bygger for target-boardet (miljønavn fastlægges når `platformio.ini` oprettes i Fase 1)
    - Adfærd der kun kan verificeres MED fysisk hardware (SPI-timing, RS485/RS232-transceiver-skift, kanal-auto-detektion, OTA-partitionsskift) kan ikke automatiseres her — følg i stedet fase-testplanen og acceptance-kriterierne i [EXPANSION_BOARD_DESIGN.md](EXPANSION_BOARD_DESIGN.md) §9-§10, og sig eksplicit i afrapporteringen hvilke af disse der IKKE er verificeret på rigtig hardware endnu.

14. **CLI-kommandoer skal altid have en enable/disable-modpart (Jan, v0.26.1)**: enhver ny seriel CLI-kommando (§3.4.1) der TÆNDER/aktiverer/tilføjer noget skal have en tilsvarende `no <kommando>`-variant der slukker/rydder det igen — samme Cisco-inspirerede mønster som allerede etableret: `debug modbus ...` / `no debug modbus` (v0.25.0), `syslog add ...` / `no syslog` (v0.26.0/v0.26.1). Gælder fremadrettet for ALLE nye kommandoer af denne art, ikke kun disse to — design `no <kommando>` ind FRA START, ikke som en efterfølgende tilføjelse Jan skal bede om separat.

---

## Workflow for enhver opgave

1. Tilføj entry i `FEATURES.md` (feature) eller `BUGS.md` (bug).
2. Implementer ændringen i det korrekte lag jf. `ARCHITECTURE.md`.
3. Opdater `version.json`: bump build (altid), bump version (hvis feature/bugfix/breaking).
4. Tilføj entry i `CHANGELOG.md` med `[version build NNNN]` prefix.
5. Opdater `RELEASE_NOTES.md` hvis kode er ændret.
6. Kør testsuiter/verifikation før noget meldes færdigt.
7. `git add` + `git commit` med besked der inkluderer version: `v0.1.0-b0001: beskrivelse`.
8. `git push origin dev` til GitHub.
9. Spørg Jan: *"Vil du også merge til `main`?"* — merge og push `origin main` hvis ja.

---

## Projektstruktur

Jf. `EXPANSION_BOARD_DESIGN.md` §3.1 og `ARCHITECTURE.md`. `net_driver.cpp` og resten fra `modbus_tcp_server.cpp` og nedefter tilføjes i takt med faserne i FEATURES.md:

```
.
├── CLAUDE.md
├── ARCHITECTURE.md
├── FEATURES.md
├── BUGS.md
├── CHANGELOG.md
├── RELEASE_NOTES.md
├── README.md
├── version.json
├── EXPANSION_BOARD_DESIGN.md       # fuldt design-dokument — kilde til alle arkitekturbeslutninger
├── PLC_INTEGRATION_MANUAL.md       # AUTORITATIV reference for det FAKTISK implementerede API — til PLC-side-integrationsarbejdet (Modbus_server_slave_ESP32-repoet)
├── GPIO_MAPPING.md                 # Samlet GPIO-opslagstabel (alle devices) — rent opslagsværk, EXPANSION_BOARD_DESIGN.md §2.0.1 er den autoritative kilde
├── reference-plc-source/           # statiske kode-referencer fra Hypervision PLC-repoet (se dens egen README.md)
├── .claude/
│   └── settings.local.json
├── .gitignore                      # udelukker .pio/ (build-cache/toolchains)
├── platformio.ini                  # PlatformIO build-config: esp32dev (target) + native (host-side unit-tests)
├── extract_version.py              # injicerer version.json som FW_VERSION/FW_BUILD i firmwaren (kun esp32dev)
├── src/                             # firmware-kildekode (ESP32/Arduino-specifik — se ARCHITECTURE.md for hvorfor)
│   ├── main.cpp                    # setup(): config_begin() FØR modbus_channel_init_all() (raekkefoelgen er kritisk, se BUGS.md v0.10.0)
│   ├── provisioning.cpp/.h         # seriel CLI-I/O + rigtigt WiFi-connect + config.cpp-kobling (§3.4) — v0.6.0, faerdig
│   ├── config.cpp/.h               # NVS (ESP32 Preferences) omkring lib/board_config/, inkl. pr.-kanal-config — v0.10.0, faerdig
│   ├── modbus_channel.cpp/.h       # × 2 (Variant A), én FreeRTOS-task pr. kanal, laeser config fra NVS, live-omkonfigurering + statistik — bruger lib/modbus_pdu/ — v0.11.0, live-verificeret (kanal A)
│   ├── modbus_tcp_server.cpp/.h    # data-plan, port 502-503 (Variant A, §2.0/§4.1), plc_ip-permit (§4.3) inline — Fase 4, live-verificeret
│   ├── http_server.cpp/.h          # REST management-API, port 8080 (§4.2) — status/channels(get/put/post read-write) — v0.11.0, faerdig
│   ├── http_helpers.h/.cpp         # require_auth()/send_json_error(), delt mellem http_server.cpp og ota_handler.cpp — v0.12.0
│   ├── ota_handler.cpp/.h          # POST /api/ota, GET /api/ota/status, POST /api/reboot — v0.12.0, faerdig
│   └── syslog_sender.h/.cpp        # ESP32 UDP-afsendelse for lib/syslog_client/'s pakker, mutex-beskyttet — v0.26.0
├── lib/                             # hardware-uafhængig, native-testbar logik (se ARCHITECTURE.md)
│   ├── modbus_pdu/                 # CRC16 + RTU-frame-building/parsing (FC01-06/15/16) — v0.2.0, færdig, FC15 tilføjet v0.27.0
│   ├── modbus_tcp/                 # MBAP-header parsing/bygning (§4.1) — v0.9.0, faerdig
│   ├── provisioning_cli/           # seriel CLI-kommando-parsing/validering (§3.4.1) — v0.5.0, færdig
│   ├── board_config/               # persisteret config-schema (schema 7, inkl. pr.-kanal-config + syslog-modtagere) + serialisering (§3.5) — v0.10.0, faerdig
│   ├── channel_config/             # mb_is_valid_baudrate(), JSON build/parse for §4.2's kanal-config — v0.10.0, faerdig
│   ├── diagnostic_modbus/          # JSON<->PDU for §4.2's diagnostiske read/write (FC01-06/15/16) — v0.11.0, faerdig
│   ├── ota_validation/             # ESP32-firmware-magic-byte-tjek — v0.12.0, faerdig
│   ├── rest_auth/                  # base64 + Bearer/Basic Auth-tjek (§4.4) — v0.7.0, færdig
│   ├── rest_status/                # JSON-builders for GET /api/status + REST-fejlsvar — v0.7.0, færdig
│   └── syslog_client/              # RFC 3164-pakkeformatering (facility/severity, hardware-uafhængig) — v0.26.0
└── test/                            # PlatformIO native unit-tests (`pio test -e native`) — 304 tests i alt
    ├── test_modbus_pdu/
    ├── test_modbus_tcp/
    ├── test_provisioning_cli/
    ├── test_board_config/
    ├── test_channel_config/
    ├── test_diagnostic_modbus/
    ├── test_ota_validation/
    ├── test_rest_auth/
    ├── test_rest_status/
    └── test_syslog_client/
```
