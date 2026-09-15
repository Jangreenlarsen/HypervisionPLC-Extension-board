# HypervisionPLC Extension Board

Firmware til en selvstændig Modbus RTU-gateway ("expansion board") på ESP32: tilføjer RS485/RS232-feltbus-kanaler til en Hypervision PLC via netværk — Modbus TCP (data) + REST/JSON (management) — uden at røre PLC'ens egen chip.

**Status (v0.27.0):** Variant A (2 kanaler via ESP32's egne UART1/UART2) er implementeret og live-verificeret på fysisk hardware — begge kanaler har hver især talt Modbus RTU korrekt med en rigtig slave (FC01-06/15/16). Fase 1-5 af den oprindelige plan er færdige: hardware-bring-up, provisionering, Modbus TCP-data-plan, og hele management-API'et (status, kanal-config, diagnostisk read/write, OTA). Valgfri W5500-Ethernet er implementeret OG live-verificeret (dual-stack med WiFi). Siden er der tilføjet driftsfunktioner ud over den oprindelige plan: konfigurerbart hostname, aktivitets-LED'er, diagnostisk `test`-kommando i den serielle CLI, leveled Modbus-debug-output (`debug modbus ...`), og en syslog-klient (RFC 3164/UDP, op til 4 modtagere). Se [FEATURES.md](FEATURES.md) for den løbende, detaljerede status.

## Hvad er dette?

Hypervision PLC'en (separat repo, `Modbus_server_slave_ESP32`) kan tale Modbus RTU Slave + 1× Master, begge over sin egen, indbyggede UART-hardware. Et forsøg på at tilføje en 2. Master direkte på PLC-chippen stødte på en hardware-blocker (heap-korruption, formentlig et ESP32 PSRAM-cache-erratum — se designdokumentets §0).

Løsningen er dette board: en fysisk separat ESP32-gateway der ejer ekstra RS485/RS232-feltbusser og eksponerer dem til PLC'en over netværk, i stedet for over intern UART-hardware.

Boardet har bevidst ingen egen driftsbrugerflade ud over en minimal seriel CLI (USB) til første netværksopsætning. Al løbende konfiguration (kanaler, firmware-opdatering) sker via et autentificeret REST-API, tænkt til at blive styret fra PLC'ens egen System-side ("single pane of glass").

**Fuld begrundelse, arkitektur og protokol-spec:** [EXPANSION_BOARD_DESIGN.md](EXPANSION_BOARD_DESIGN.md) — den oprindelige designvision (op til 8 kanaler pr. board). **Det faktisk implementerede API** (Variant A, live-verificeret): [PLC_INTEGRATION_MANUAL.md](PLC_INTEGRATION_MANUAL.md) — brug DENNE ved uoverensstemmelse.

## Scope

Dette repo dækker **kun** expansion-boardets eget hardware og firmware — et selvstændigt firmware-projekt uden kode-afhængighed til PLC-repoet.

PLC-side-integrationen (`modbus_expansion.cpp`, udvidelse af `web/system.html`, CLI/ST Logic) sker INDE I `Modbus_server_slave_ESP32`-repoet og er et separat, fremtidigt arbejde — udenfor scope her.

## Kom i gang

- **API-reference (det der faktisk virker):** [PLC_INTEGRATION_MANUAL.md](PLC_INTEGRATION_MANUAL.md)
- **GPIO-tilslutning:** [GPIO_MAPPING.md](GPIO_MAPPING.md)
- Fuldt design-dokument: [EXPANSION_BOARD_DESIGN.md](EXPANSION_BOARD_DESIGN.md)
- Arbejdsregler og projektkonventioner: [CLAUDE.md](CLAUDE.md)
- Arkitektur (lag, ansvarsfordeling): [ARCHITECTURE.md](ARCHITECTURE.md)
- Aktuel status: [FEATURES.md](FEATURES.md) · [BUGS.md](BUGS.md) · [CHANGELOG.md](CHANGELOG.md) · [RELEASE_NOTES.md](RELEASE_NOTES.md)
- Kode-referencer fra PLC-repoet (statiske kopier, ikke live): [reference-plc-source/](reference-plc-source/)

## Reference-stack

- **Gateway-MCU:** ESP32-WROOM-32 DevKit, PlatformIO + Arduino-core (+ ESP-IDF-komponenter hvor nødvendigt, fx OTA og Ethernet), FreeRTOS — én task pr. Modbus-kanal
- **Feltbus-hardware (Variant A):** ESP32's egne UART1/UART2 direkte til RS485 (SP3485)/RS232 (MAX3232) transceiver-par, valgt via én delt MODE_SEL-GPIO — se [GPIO_MAPPING.md](GPIO_MAPPING.md)
- **Netværk:** WiFi (indbygget) + valgfri Ethernet (W5500 over SPI, dual-stack)
- **Protokoller:** Modbus TCP (data-plan, port 502-503) + REST/JSON (management-plan, port 8080, Bearer-token/Basic Auth)
