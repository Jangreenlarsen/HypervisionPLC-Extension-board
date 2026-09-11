# Features

Format: `[status] vX.X.X — beskrivelse`
Status: `planned` | `in-progress` | `done`

Backlog seedet fra implementeringsfaserne i [EXPANSION_BOARD_DESIGN.md](EXPANSION_BOARD_DESIGN.md) §9-§10. Fase 6-7 (PLC-side integration, multi-board-konfiguration) hører til `Modbus_server_slave_ESP32`-repoet, jf. designdokumentets §5 — udenfor scope her og derfor ikke listet.

---

## Færdige features

- [x] done — v0.4.0 — Seriel provisioning-CLI koblet til rigtig hardware (`src/provisioning.cpp`, `src/main.cpp`): læser linjer fra `Serial`, kalder `lib/provisioning_cli/`, og udfører et RIGTIGT WiFi-forbindelsesforsøg (`WiFi.begin()`, DHCP eller statisk IP, 30 sek. timeout jf. §3.4.1) — testet interaktivt på fysisk ESP32-hardware (ikke kun `pio test -e native`). Lokal ekko + backspace-håndtering. Fandt og rettede undervejs en rigtig bug (`MB_PROV_MSG_MAX_LEN` for lille til `help`-teksten — se BUGS.md) opdaget ved test mod ægte seriel forbindelse, ikke af den native testsuite. Firmwaren rapporterer nu også egen version+build (boot-banner + `version`-kommando), automatisk fra `version.json` (`extract_version.py`). **Stadig mangler** (resten af Fase 3, nedenfor): NVS-persistering, management-API-token-udstedelse, firewall-allowlist-seed — kræver `config.cpp`.
- [x] done — v0.3.0 — Seriel provisioning-CLI kommando-kerne (`lib/provisioning_cli/`): tokenizer (inkl. citerede værdier med mellemrum, fx SSID'er), felt-validering (SSID 1-32 tegn, WPA2-password 8-63 tegn, IPv4, dhcp/static-mode), og tilstand for `wifi ssid/pass/open/mode/ip/mask/gw`, `plc ip`, `show`, `connect`, `factory-reset confirm`, `help` (EXPANSION_BOARD_DESIGN.md §3.4.1). Password lækker aldrig i klartekst i nogen besked (maskeret som `********`) — eksplicit testet. Erstatter en tidligere planlagt WiFi AP-mode-webside, som er droppet fra designet (§3.4: seriel/USB kræver fysisk adgang, ingen trådløs angrebsflade, ingen mode-switching-kompleksitet). 33 unit-tests, alle bestået. Hardware-uafhængig — selve NVS-skrivning/WiFi-forbindelse/reboot i `src/provisioning.cpp` følger i Fase 3 og kræver fysisk hardware.
- [x] done — v0.2.0 — Modbus RTU PDU-kerne (`lib/modbus_pdu/`): CRC16, RTU-frame-opbygning, FC-baseret svar-længde-prædiktion og svar-parsing (exception-detektion, CRC/slave-validering) for FC01/02/03/04/05/06/16. Hardware-uafhængig, genbruges af både den kommende kanal-eksekvering (Lag 2, RTU-framing) og TCP-serveren (Lag 1, MBAP i stedet for RTU-framing om samme PDU) — port af mønsteret i `reference-plc-source/src/modbus_master.cpp`. 23 unit-tests (`pio test -e native`), CRC-værdier krydsverificeret med en uafhængig Python-implementering. Bygger også rent for ESP32-target (`pio run -e esp32dev`).

## Planlagte features

- [ ] planned — Fase 1: Hardware-bring-up — gateway-MCU (ESP32) + WiFi + 1 UART-expander-chip (2-4 kanaler) på breadboard; verificér SPI-kommunikation og én RS485-kanal mod en kendt Modbus RTU-slave
- [ ] planned — Fase 2: Fuld hardware (8 kanaler, 2× expander-chip) — parallel-test af alle 8 kanaler samtidigt uden krydsforstyrrelse; verificér kanal-auto-detektion (§2.2.2) med bevidst delvist bestykket testopstilling
- [ ] planned — Fase 3 (resten): `config.cpp` — NVS-persistering af WiFi/firewall/token med schema-versionering (§3.5), management-API-token-udstedelse ved første provisionering, firewall-allowlist seedet med PLC'ens IP. Seriel CLI + kommando-parsing + rigtigt WiFi-connect er allerede færdigt (v0.3.0/v0.4.0, se ovenfor) — resten er ren NVS/persistering-logik.
- [ ] planned — Fase 4: Data-plan — Modbus TCP-server på 8 porte (502-509, §4.1), testet med et standard Modbus TCP-værktøj (fx `mbpoll`) mod fysisk slave
- [ ] planned — Fase 5: Management-API — REST-endpoints for status/kanal-config/firewall/OTA (§4.2), Bearer-token-auth (§4.4), testet med curl/Postman
- [ ] planned — Fase 8 (board-siden): Robusthedstest — isoleret RS485-kanal-fejl påvirker ikke øvrige kanaler, genstart midt i drift håndteres uden manuel indgriben, firewall afviser IP udenfor allowlist, 24-timers belastningstest uden heap-fragmentering

Se §10 i designdokumentet for den fulde, detaljerede acceptance-kriterie-liste pr. fase.
