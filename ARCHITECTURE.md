# Arkitektur: HypervisionPLC Extension board

## Principper

1. **Ingen lag må springe et lag over.**
2. **Netværks-/protokollaget må ALDRIG initiere en fysisk SPI/UART-transaktion selv** — det lægger kun en forespørgsel i en kanals kø og venter på resultatet fra kanal-eksekveringslaget. Kun kanal-eksekveringslaget (§"Lag 2") taler med hardware-abstraktionslaget.
3. **Alle lag eksponerer kun et veldefineret interface** — interne implementeringsdetaljer er skjulte.

---

## Lag-oversigt

```
┌──────────────────────────────────────────────────────────┐
│  Lag 1 — Netværks-/protokollag                             │
│  modbus_tcp_server.cpp (data, port 502-509)                 │
│  http_server.cpp (REST management, port 8080)                │
│  firewall.cpp (IP-allowlist, håndhæves FØR Modbus-parsing)    │
│  provisioning.cpp (WiFi AP-mode bootstrap-side)                │
└───────────────────────────┬────────────────────────────────┘
                            │ PDU (function code + data) + adresse,
                            │ lagt i kanalens FreeRTOS-kø
┌───────────────────────────▼────────────────────────────────┐
│  Lag 2 — Kanal-eksekveringslag                                │
│  modbus_channel.cpp × active_channels (1-8)                    │
│  — én FreeRTOS-task pr. kanal, egen kø, egen statistik           │
│  — Modbus RTU frame-opbygning/CRC/parsing                         │
│  — RS485: DE/RE-toggling omkring send. RS232: fuld-duplex, intet DE/RE │
└───────────────────────────┬────────────────────────────────┘
                            │ "skriv/læs byte(s) på kanal N" (abstraheret)
┌───────────────────────────▼────────────────────────────────┐
│  Lag 3 — Hardware-abstraktionslag                              │
│  uart_expander.cpp — SPI-driver for MAX14830/SC16IS75x           │
│  (delt SPI-bus, mutex-beskyttet, separat CS-linje pr. kanal)       │
└───────────────────────────┬────────────────────────────────┘
                            │ SPI
                  Fysiske UART-expander-ICs
                            │
                  RS485 (SP3485) / RS232 (MAX3232) transceiver-par pr. kanal
                            │
                        Feltbusser
```

**Cross-cutting** (bruges af flere lag, er ikke selv en del af transaktions-kæden ovenfor):
- `config.cpp` — NVS-persisteret konfiguration (kanaler, firewall-allowlist, auth-token, schema-version — §3.5 i designdokumentet). Læses af Lag 1 (auth/firewall/kanal-opsætning) og Lag 2 (kanal-parametre ved task-start).
- `net_driver.cpp` — WiFi/Ethernet-forbindelse; forudsætning for at Lag 1 overhovedet kan lytte på noget.
- `ota_handler.cpp` — modtages via Lag 1 (`POST /api/ota`), men skriver til sin egen, uafhængige flash-partition — rører ikke Lag 2/3.
- Watchdog — overvåger alle lag, reset-årsag persisteres til RTC/NVS.

---

## Lag-beskrivelse

### Lag 1 — Netværks-/protokollag
- **Ansvar**: eksponerer boardet mod PLC'en. Data-plan (Modbus TCP, 8 porte, MBAP-parsing) og management-plan (REST/JSON, Bearer-token-auth, 1 port). Håndhæver IP-allowlist på data-planets `accept()` FØR noget Modbus-indhold parses. Oversætter mellem netværksrepræsentation (MBAP-header / JSON) og den interne PDU + kanal-adresse, der lægges i en kanals kø.
- **Filer**: `modbus_tcp_server.cpp/.h`, `http_server.cpp/.h`, `firewall.cpp/.h`, `provisioning.cpp/.h`, `net_driver.cpp/.h`
- **Regler**: må ALDRIG selv åbne SPI-bussen eller tale direkte til en UART-expander (Lag 3) — al feltbus-adgang går via en kanals kø (Lag 2). Må ALDRIG blokere synkront på en feltbus-transaktion; lægger forespørgslen i kø og venter på tasken's resultat via semaphore/kø-svar, ikke en direkte round-trip.

### Lag 2 — Kanal-eksekveringslag
- **Ansvar**: udfører selve Modbus RTU-transaktionen (frame-opbygning, CRC, timing, DE/RE for RS485 eller fuld-duplex for RS232) for ÉN kanal ad gangen, fuldstændig isoleret fra de øvrige kanal-tasks. Fører egen statistik (total/success/timeout/crc/exception, seneste fejl).
- **Filer**: `modbus_channel.cpp/.h`
- **Regler**: ingen delt tilstand mellem kanal-instanser ud over selve SPI-bussen (mutex-beskyttet i Lag 3 — samme mønster som PLC-repoets `g_modbus_uart_mutex`). Må ikke selv parse MBAP eller JSON — modtager en allerede udpakket PDU + slave-adresse fra Lag 1.

### Lag 3 — Hardware-abstraktionslag
- **Ansvar**: abstraherer UART-expander-chippens SPI-register-interface bag et simpelt "skriv/læs byte(s) på kanal N"-interface, inkl. modevalg-GPIO for RS485/RS232 (§2.2.1). Ejer SPI-mutex'en. Udfører auto-detektion af monterede kanaler ved boot (§2.2.2).
- **Filer**: `uart_expander.cpp/.h`
- **Regler**: kender intet til Modbus-protokollen (hverken RTU eller TCP) — rent transport-/hardware-lag, genanvendeligt uændret hvis expander-chip-familien skiftes (fx SC16IS750 i stedet for MAX14830).

---

## Projektstruktur

```
.
├── CLAUDE.md
├── version.json
├── ARCHITECTURE.md
├── FEATURES.md
├── BUGS.md
├── CHANGELOG.md
├── RELEASE_NOTES.md
├── EXPANSION_BOARD_DESIGN.md       # fuldt design-dokument — kilde til alle arkitekturbeslutninger ovenfor
├── reference-plc-source/           # statiske kode-referencer fra Hypervision PLC-repoet
├── .claude/
│   └── settings.local.json
├── .gitignore                      # udelukker .pio/ (build-cache/toolchains)
├── platformio.ini                  # PlatformIO build-config: esp32dev (target) + native (host-side unit-tests)
├── src/                             # Lag 1-3 + cross-cutting orchestration (ESP32/Arduino-specifik, IKKE native-testbar)
│   └── main.cpp                    # (stub — provisioning/net_driver/modbus_tcp_server/... følger i Fase 1+)
├── lib/                             # Hardware-uafhængig, native-testbar logik — se note nedenfor
│   └── modbus_pdu/                 # CRC16 + RTU-frame-building/parsing (Lag 2's protokol-kerne, delt med Lag 1)
└── test/                            # PlatformIO native unit-tests (`pio test -e native`, ingen hardware nødvendig)
    └── test_modbus_pdu/
```

**Hvorfor `lib/` og ikke `src/include/` for hardware-uafhængig logik:** PlatformIOs Library Dependency Finder linker kun et `lib/<modul>` ind der hvor det faktisk `#include`s (af enten `src/` eller `test/`) — modsat `src/`, som for `native`-miljøet ellers skal ekskluderes fil for fil for at undgå at trække Arduino/FreeRTOS/SPI-afhængige filer ind i en testbuild der ikke har den slags hardware til rådighed. Enhver fremtidig logik der skal kunne køre i `pio test -e native` (config-parsing, cache/dedup, schema-migration, §3.5) hører derfor til i `lib/`, ikke i `src/`.
