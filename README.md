# HypervisionPLC Extension Board

Firmware + hardware for en selvstændig Modbus RTU-gateway ("expansion board"): op til 8 boards × 8 RS485/RS232-kanaler, netværksforbundet til en Hypervision PLC.

**Status:** Design færdigt, implementering ikke påbegyndt (Fase 1 af 8 — se [FEATURES.md](FEATURES.md)).

## Hvad er dette?

Hypervision PLC'en (separat repo, `Modbus_server_slave_ESP32`) kan i dag tale Modbus RTU Slave + 1× Master, begge over sin egen, indbyggede UART-hardware. Et forsøg på at tilføje en 2. Master direkte på PLC-chippen stødte på en hardware-blocker (heap-korruption, formentlig et ESP32 PSRAM-cache-erratum — se designdokumentets §0).

Løsningen er dette board: en fysisk separat ESP32-gateway der ejer ekstra RS485/RS232-feltbusser og eksponerer dem til PLC'en over netværk — Modbus TCP (data) + REST/JSON (management) — i stedet for over intern UART-hardware.

Boardet har bevidst ingen egen driftsbrugerflade — kun en minimal WiFi-opsætningsside for at komme på nettet første gang. Al løbende konfiguration (kanaler, firewall, firmware-opdatering) sker fra PLC'ens eksisterende System-side ("single pane of glass").

**Fuld begrundelse, arkitektur og protokol-spec:** [EXPANSION_BOARD_DESIGN.md](EXPANSION_BOARD_DESIGN.md) — læs dette FØRST, det er den autoritative kilde for enhver designbeslutning i dette repo.

## Scope

Dette repo dækker **kun** expansion-boardets eget hardware og firmware (designdokumentets §1-4, §9-10) — et selvstændigt firmware-projekt uden kode-afhængighed til PLC-repoet.

PLC-side-integrationen (designdokumentets §5: `modbus_expansion.cpp`, udvidelse af `web/system.html`, CLI/ST Logic) sker INDE I `Modbus_server_slave_ESP32`-repoet og er et separat, fremtidigt arbejde — udenfor scope her.

## Kom i gang

- Arbejdsregler og projektkonventioner: [CLAUDE.md](CLAUDE.md)
- Arkitektur (lag, ansvarsfordeling): [ARCHITECTURE.md](ARCHITECTURE.md)
- Aktuel status: [FEATURES.md](FEATURES.md) · [BUGS.md](BUGS.md) · [CHANGELOG.md](CHANGELOG.md) · [RELEASE_NOTES.md](RELEASE_NOTES.md)
- Kode-referencer fra PLC-repoet (statiske kopier, ikke live): [reference-plc-source/](reference-plc-source/)

## Reference-stack

- **Gateway-MCU:** ESP32, PlatformIO + Arduino-core (+ enkelte ESP-IDF-komponenter hvor nødvendigt), FreeRTOS — én task pr. Modbus-kanal
- **Feltbus-hardware:** UART-expander over SPI (MAX14830 / SC16IS750 / SC16IS752) → SP3485 (RS485) / MAX3232 (RS232) transceiver-par pr. kanal, valgt via expander-GPIO
- **Netværk:** WiFi (indbygget) + valgfri Ethernet (W5500 over SPI)
- **Protokoller:** Modbus TCP (data-plan, port 502-509) + REST/JSON (management-plan, port 8080, Bearer-token-auth)
