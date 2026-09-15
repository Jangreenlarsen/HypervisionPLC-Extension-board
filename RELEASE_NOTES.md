# Release Notes

---

## v0.28.0 — 2026-09-15 — `GET /api/capabilities` + korrekt "ukendt function code"-fejl

Implementerer PLC-udviklingsteamets eget designforslag (`DESIGN_GUIDE_MODBUS_EXPANSION_FC_CAPABILITIES.md`): et nyt, rent deklarativt `GET /api/capabilities`-endpoint der viser hvilke function codes boardet understøtter (separat for Modbus TCP-data-planet og REST-diagnostikken) uden at skulle sende en rigtig test-transaktion. Desuden en ny, dedikeret fejlkode for "boardet kender slet ikke denne function code" (`MB_UNSUPPORTED_FUNCTION`, REST `error_code:10`, Modbus TCP-exception `0x01` "Illegal Function") — adskilt fra den hidtidige, bredere `MB_INVALID_ADDRESS`/`0x0A`, som nu kun betyder "ugyldig adresse for en ellers kendt function code" eller "kanal util-gaengelig".

**Live-verificeret** på fysisk hardware: `GET /api/capabilities` svarede byte-for-byte som forventet, og et rå Modbus TCP-kald med en helt ukendt function code (0x07) fik nu korrekt exception `0x01` "Illegal Function" i stedet for det tidligere `0x0A`. Fund undervejs: den nye fejlkode kan pt. kun nås via Modbus TCP-data-planet, ikke REST-diagnostikken — REST'ens egen parser filtrerer allerede ukendte function codes fra tidligere i kæden.

---

## v0.27.1 — 2026-09-15 — FC15's REST-kontrakt rettet til booleans

Efter et krydstjek mod PLC-udviklingsteamets egen spec viste det sig at v0.27.0's FC15-understøttelse i REST-diagnostikken (`POST /api/channels/{n}/write`) forventede et tal-array (`[1,0,1]`), mens både PLC-teamets forslag OG denne kodebases egen FC05-konvention bruger booleans (`[true,false,true]`). Rettet — boardets Modbus TCP-data-plan (port 502/503) var allerede fuldt ud i overensstemmelse med deres spec og krævede ingen ændring.

**Live-verificeret** på fysisk hardware mod alle 3 scenarier i PLC-teamets spec: REST med den nye boolske kontrakt producerede en byte-identisk RTU-frame til deres eget eksempel, den gamle tal-kontrakt afvises nu korrekt, og et rå Modbus TCP FC15-kald (port 503) fik nu korrekt `0x0B` ("Target Device Failed to Respond") i stedet for det tidligere `0x0A` ("Path Unavailable") — whitelist-fixet fra v0.27.0 virker som forventet.

---

## v0.27.0 — 2026-09-15 — FC15 (Write Multiple Coils) understøttet

Modbus function code 15 (Write Multiple Coils) er nu understøttet — både på kernen (`lib/modbus_pdu`) og i den diagnostiske REST-skrivning (`POST /api/channels/{n}/write`, `function_code: 15`). Hidtil gav en FC15-forespørgsel en misvisende "Gateway Path Unavailable"-exception. FC16 (Write Multiple Registers) var allerede understøttet fra tidligere.

**Live-verificeret** på fysisk hardware: gatewayens FC15-frame-bygning er bekræftet byte-perfekt korrekt (set via `debug modbus`s rå hex-dump). Den tilsluttede testslave svarede ikke — den understøtter tilsyneladende ikke FC15 (ikke en gateway-fejl). Fuld ende-til-ende-test kræver et FC15-kapabelt testudstyr.

---

## v0.26.1 — 2026-09-15 — `no syslog` rydder alle modtagere på én gang

Ny `no syslog`/`no syslog all`-kommando i den serielle CLI — fjerner alle konfigurerede syslog-modtagere med ét kald, i stedet for at skulle fjerne dem én ad gangen med `syslog remove <tag>`.

**Næste skridt**: live-verifikation på fysisk hardware.

---

## v0.26.0 — 2026-09-15 — Syslog-klient med op til 4 modtagere

Boardet kan nu sende sine driftshændelser til op til 4 UDP-syslog-servere samtidig: `syslog add <ip> <port> <tag> <level 1-8>` i den serielle CLI. Genbruger den samme 1-8-verbositetsskala som `debug modbus`-featuren (v0.25.0), men som en uafhængig, altid-aktiv udgangskanal — en konfigureret modtager ser fuld detalje uanset om nogen kigger på den serielle konsol. Dækker Modbus-kanaltrafik/-fejl og REST-auth-afvisninger. Persisteret i NVS (modsat debug-niveauet), virker straks efter `save` — ingen reboot nødvendig.

**Live-verificeret** på fysisk hardware: en rigtig UDP-modtager modtog alle forventede pakker med korrekt facility/severity/hostname/tag under en rigtig Modbus-transaktion og et REST-401-forsøg, og en konfigureret modtager overlevede en rigtig genstart.

---

## v0.25.1 — 2026-09-15 — Debug-output er nu støjfrit

`debug modbus`-featuren blandede sig hidtil med den generiske fejl-log fra BEGGE kanaler, uanset hvilken man rent faktisk debuggede — en kanal med et helt uafhængigt problem (fx ingen slave tilsluttet) kunne oversvømme den kanal man forsøgte at kigge rent på. Den generiske fejl-linje undertrykkes nu helt, så snart mindst én kanal har debug slået til — debug-outputtet selv viser stadig alt relevant for den/de kanaler man debugger.

**Næste skridt**: live-verifikation på fysisk hardware.

---

## v0.25.0 — 2026-09-15 — Leveled Modbus-debug-output i den serielle CLI

Ny kommando i den serielle CLI: `debug modbus <a|b|all> level <1-8>` slår detaljeret debug-output til på kanal A, B eller begge — fra en kort start/slut-linje pr. transaktion (level 1) til fuld rå hex-dump af både TX- og RX-rammerne (level 7-8). `no debug modbus`/`no debug all` slår det fra igen. Bevidst IKKE gemt i konfigurationen — nulstilles altid til fra ved genstart, så det aldrig glemmes tændt i normal drift. Vist som live-status under `status`.

**Live-verificeret** på fysisk hardware mod en rigtig slave (kanal B, adr. 9): under selve verifikationen viste level 8 sig faktisk at BRYDE de transaktioner den skulle observere (et debug-print pr. modtaget byte stjal tid fra Modbus RTU'ens strikse inter-character-timing) — fundet og rettet med det samme (se BUGS.md v0.25.0), derefter bekræftet stabil (5/5 gentagne kald). Ingen stack-relaterede nedbrud.

---

## v0.24.1 — 2026-09-15 — `test`-kommandoen forklarer nu sin egen ventetid

`test`-kommandoen viser nu en kort besked om at den serielle CLI venter på svar/timeout, så det er tydeligt at det er CLI-terminalen (ikke boardets kanaler) der er optaget imens. Bekræftet ved en konkret måling at de to Modbus-kanaler reelt kører helt uafhængigt af hinanden — en kanal der timer ud påvirker ikke den anden kanals egen respons-tid.

**Næste skridt**: ingen — ren afklaring, live-verifikation følger som vanligt.

---

## v0.24.0 — 2026-09-15 — Diagnostisk Modbus-test direkte fra CLI'en

Den serielle CLI kan nu udføre en diagnostisk Modbus-læsning uden curl/REST: `test <kanal 1|2> <slave_id> <function_code 1-4> <adresse> <antal>`. Samme kapabilitet som REST-API'ets `POST /api/channels/{n}/read`, men direkte i terminalen — praktisk til hurtig test af en tilsluttet slave-enhed eller aktivitets-LED'en, uden at skulle åbne et separat værktøj. Kun læsning — ingen skrivning fra CLI'en.

**Næste skridt**: live-verifikation på fysisk hardware mod den rigtige slave på kanal B.

---

## v0.23.1 — 2026-09-15 — Aktivitets-LED'erne virker nu

De to kanalers aktivitets-LED'er (reserveret på boardet siden v0.13.0, men aldrig faktisk brugt) tændes nu under en RTU-transaktion og slukkes igen bagefter — samme "der sker noget her"-blink som en almindelig RS485/RS232-adapter. Gælder både succesfulde og fejlende transaktioner; en deaktiveret kanal blinker ikke.

**Næste skridt**: fysisk bekræftelse af at LED'erne rent faktisk blinker på boardet — ikke verificerbart herfra.

---

## v0.23.0 — 2026-09-14 — Ny `token regenerate`-kommando

Den serielle CLI kan nu generere et helt nyt management-API-token (`token regenerate`) uden at røre noget andet — ingen grund til at bruge en fuld `factory-reset` bare for at rotere tokenet. Det gamle token holder øjeblikkeligt op med at virke, så husk at opdatere det i PLC'ens System-side under "Modbus Expansion Boards" bagefter.

**Næste skridt**: live-verifikation på fysisk hardware.

---

## v0.22.1 — 2026-09-14 — REST Basic Auth-credentials skjules i CLI'en når de ikke bruges

`show` og `status` i den serielle CLI viser nu kun `rest.user`/`rest.pass` når REST-API'ets auth-mode er `both` — ikke i `token`- eller `basic`-mode, hvor de enten slet ikke bruges eller ville have givet et misvisende billede af hvad der reelt kræves. Selve værdierne bliver stadig gemt i baggrunden, så intet går tabt hvis man senere skifter tilbage til `both`.

**Næste skridt**: live-verifikation på fysisk hardware.

---

## v0.22.0 — 2026-09-14 — Konfigurerbart DHCP-hostname

Boardet får nu et rigtigt, meningsfuldt hostname på DHCP-serveren i stedet for et generisk Espressif-navn — som standard `hypervision-ext-XXXXXX` (udledt af boardets unikke MAC-adresse), men kan overstyres via `hostname <navn>` i den serielle CLI (og ryddes tilbage til default med `hostname auto`). Gælder for både WiFi og Ethernet. WiFi'ens hostname opdateres med det samme ved næste `connect`; Ethernet's kræver et `reboot`.

**Næste skridt**: live-verifikation af hostname på fysisk hardware — bekræfte at DHCP-serveren rent faktisk viser det nye navn for begge interfaces.

---

## v0.21.0 — 2026-09-14 — WiFi kan nu også slås til/fra via CLI

Den serielle CLI kan nu slå WiFi helt fra (`wifi disable`) eller til (`wifi enable`), mirroring den tilsvarende Ethernet-kommando fra v0.20.0 — nyttigt for et board der udelukkende skal køre på Ethernet. Advarer (uden at blokere) hvis Ethernet også er deaktiveret, så boardet ikke ved et uheld ender uden nogen netværksadgang overhovedet.

Fandt og rettede samtidig en vigtig bagvedliggende fejl: REST-API'et og Modbus TCP-serverne blev hidtil kun startet når WiFi forbandt — et rent Ethernet-board ville derfor aldrig have fået dem startet, uanset hvor godt Ethernet-forbindelsen ellers virkede. De starter nu uafhængigt af hvilken netværksvej der rent faktisk er oppe.

**Næste skridt**: live-verifikation af `wifi disable`/`wifi enable` på fysisk hardware, samt at REST/Modbus TCP fortsat starter korrekt på et Ethernet-only-board.

---

## v0.20.0 — 2026-09-14 — Ethernet enable/disable/static-IP via CLI + tilfældig persisteret MAC

Den serielle CLI kan nu styre Ethernet direkte: `eth enable`/`eth disable` slår W5500-driveren helt til/fra, og `eth mode dhcp|static` + `eth ip/mask/gw <a.b.c.d>` giver en statisk IP i stedet for DHCP — begge dele kun via CLI (`save` + `reboot`), ikke via REST. Boardet får desuden nu altid en unik, tilfældig MAC-adresse (persisteret i NVS ved første opstart) i stedet for W5500-chippens usikre default på `00:00:00:00:00:00`, som ellers ville give MAC-kollisioner hvis flere boards sad på samme netværk. MAC'en vises i `show`/`status` som `eth.mac`.

**Næste skridt**: live-verifikation af enable/disable, static-IP og MAC-tildeling på fysisk hardware.

---

## v0.19.1 — 2026-09-14 — W5500-Ethernet virker nu fysisk (fix)

Boardets W5500-Ethernet-modul virker nu for første gang på rigtig hardware. Efter Jan monterede et fysisk modul, kunne boardet stadig ikke tale med chippen over SPI, selvom al wiring var korrekt (link-LED lyste fint). Root cause: en manglende del af SPI-konfigurationen (`command_bits`/`address_bits`), fundet ved at sammenligne med et søsterprojekt med samme hardware i produktion. Efter rettelsen: link kommer op, DHCP tildeler en IP, og både REST-API'et og Modbus TCP kan nås direkte over Ethernet-kablet — samtidig med WiFi.

**Næste skridt**: selvstændig test af Modbus TCP (ikke kun REST) over Ethernet-interfacet, samt langtidstest af dual-stack-stabiliteten.

---

## v0.19.0 — 2026-09-14 — `reboot`-kommando i den serielle CLI

Den serielle CLI har nu en `reboot`-kommando — et blødt, ikke-destruktivt genstart, der ikke rydder nogen konfiguration (modsat `factory-reset confirm`). Samme funktion som REST-API'ets `POST /api/reboot` (v0.12.0), men direkte fra den serielle forbindelse — nyttigt til hurtig hardware-iteration uden at skulle fysisk afbryde strømmen.

**Næste skridt**: live-verifikation af `reboot`-kommandoen på fysisk hardware.

---

## v0.18.0 — 2026-09-14 — Detaljeret W5500-Ethernet-diagnostik

Ethernet-status er nu meget mere præcis end det gamle binære "link op/nede". Boardet kan nu rapportere fire tydelige tilstande — både i den serielle CLI (`status`/`show`) og via `GET /api/status`s `ethernet.status`: intet modul fundet (tjek den fysiske tilslutning), modul fundet men link nede (tjek kabel/switch), link oppe og venter på DHCP, eller fuldt forbundet. Det gør det muligt at se med det samme om et Ethernet-problem er en hardware-/wiring-sag eller "bare" et løst/manglende netværkskabel.

Fandt og rettede undervejs en reel bug i selve diagnostikken: statussen blev sat for tidligt (før hardwaren reelt var talt med over SPI), så et board UDEN noget W5500-modul fejlagtigt viste "modul fundet, link nede" i stedet for "intet modul fundet". Rettet og live-verificeret — boardet viser nu korrekt "intet W5500-modul fundet".

**Næste skridt**: fysisk montering og test af W5500-modulet (afventer stadig fra v0.13.0) — de tre øvrige tilstande (link nede/venter på DHCP/forbundet) er endnu ikke verificerbare mod rigtig hardware.

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
