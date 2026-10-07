# HypervisionPLC Extension Board

Firmware til en selvstændig Modbus RTU-gateway ("expansion board") på ESP32. Boardet giver en Hypervision PLC ekstra RS485/RS232-feltbus-kanaler over netværk — Modbus TCP til data og REST/JSON til administration — uden at røre PLC'ens egen chip.

**Status (v0.35.0, 2026-10-07):** I drift og hardware-verificeret sammen med PLC-firmware v7.9.68.x.

- **2 eller 4 kanaler:**
  - Kanal A+B kører på ESP32'ens egne UART'er.
  - Kanal C+D kommer fra et CJMCU-752-modul (SC16IS752 over I2C), når det er monteret. Verificeret som 4×RS485 med en rigtig slave på kanal C.
- **Netværk:** WiFi og valgfri W5500-Ethernet (dual-stack).
- **Styres helt fra PLC'en**, inklusive firmwareopdatering med automatisk rollback.

Se [FEATURES.md](FEATURES.md) for den detaljerede status.

## Hvad er dette?

Hypervision PLC'en kan tale Modbus RTU som slave og som én master over sin egen UART-hardware. Et forsøg på at tilføje en 2. master direkte på PLC-chippen stødte på en hardware-blocker (heap-korruption, formentlig et ESP32 PSRAM-cache-erratum, se designdokumentets §0).

Løsningen er dette board: en fysisk separat ESP32-gateway, der ejer de ekstra feltbusser og stiller dem til rådighed for PLC'en over netværket.

Boardet har bevidst ingen egen driftsbrugerflade, kun en seriel CLI over USB til første netværksopsætning. Al løbende konfiguration sker fra PLC'ens web-GUI ("single pane of glass").

## Funktioner

| Område | Indhold |
|---|---|
| **Kanaler** | 2 (A, B) eller 4 (A–D med CJMCU-752). RS485 eller RS232 for hele boardet (MODE_SEL-jumper). Baudrate 1200–115200, paritet, stopbits, timeout og inter-frame delay pr. kanal |
| **Modbus TCP (data)** | Én port pr. kanal: 502 (A), 503 (B), 504 (C), 505 (D). FC01–06, FC15, FC16. Kun PLC-IP'en har adgang (allowlist). Et FC05/FC06-svar, der ikke er et præcist ekko af forespørgslen, meldes til PLC'en som exception 0x04 (`MB_RESPONSE_MISMATCH`) i stedet for en falsk succes |
| **REST (administration, port 8080)** | Status, kanalopsætning, diagnostisk læs/skriv, `GET /api/capabilities` (understøttede function codes uden bustrafik), hostname, PLC-IP, syslog, config-udtræk til PLC-backup, OTA. Bearer-token eller Basic Auth |
| **Hardware-signalering** | `board_type` (fx `4xRS485`) og antal aktive kanaler i `/api/status`, så PLC'en viser det rigtige kanaltal. `reset_reason` fortæller, hvorfor boardet sidst genstartede (strøm, reset, software/OTA, crash, watchdog, brownout) |
| **Firmwareopdatering** | Via PLC'en: identitetstjek af image, MD5, bekræftelse efter opstart og automatisk rollback, hvis den ikke bekræftes |
| **Drift og fejlsøgning** | Aktivitets-LED pr. kanal, syslog (RFC 3164, op til 4 modtagere), leveled Modbus-debug i CLI'en, sektionsopdelt `help` |

## Sammen med PLC'en

PLC-siden er færdig og ligger i PLC-repoet (`HypervisionPLC`, firmware v7.9.68.x):

- **I/O-siden → Modbus Expansion Boards:** Tilføj et board (nr., navn, IP, token).
  - **Test forbindelse:** henter boardets status.
  - **Redigér:** samler opsætning og vedligehold:
    - kanaler A–D
    - hostname (sættes til navnet fra PLC'en)
    - PLC-IP
    - syslog
    - opdatér firmware
    - genskab (skriver PLC'ens kopi af opsætningen tilbage, fx efter udskiftning af et board)
- **ST Logic:** `MBX_READ_HOLDING(board, kanal, slave, addr)` m.fl. Kanalen kan skrives som bogstav: `MBX_READ_HOLDING(1, C, 9, 2)`.
- **PLC-backup:** indeholder en kopi af boardets opsætning, men aldrig token eller kodeord.

API-kontrakten mellem board og PLC står i [PLC_INTEGRATION_MANUAL.md](PLC_INTEGRATION_MANUAL.md).

## Hardware i korte træk

- **MCU:** ESP32-WROOM-32 DevKit (30-pin)
- **Kanal A+B:** UART1/UART2 til SP3485 (RS485) eller MAX3232 (RS232)
- **Kanal C+D (valgfrit):** CJMCU-752 på I2C:
  - GPIO21 = SDA, GPIO22 = SCL
  - automatisk RS485-retning via chippens RTS
  - LED'er på modulets GPIO0/1
- **Jumpere:**
  - **MODE_SEL (GPIO4):** RS232/RS485 for alle kanaler.
  - **EXP_SEL (GPIO36) til 3,3 V:** CJMCU-752 er monteret. Svarer modulet ikke, kører boardet videre på 2 kanaler.
- **Ethernet (valgfrit):** W5500 på SPI (GPIO13/14/32/35, RST 23, INT 39)

Fuld pin-oversigt: [GPIO_MAPPING.md](GPIO_MAPPING.md).

## Kom i gang

1. Flash firmwaren (PlatformIO, `pio run -e esp32dev -t upload`).
2. Gå ind i den serielle CLI (USB, 115200 baud):
   - Sæt WiFi eller Ethernet og `plc ip`.
   - Kør `save`.
   - Notér tokenet fra `status`.
3. Tilføj boardet på PLC'ens I/O-side med IP og token. Al øvrig opsætning sker derfra.

## Dokumentation

- **API-reference (det der faktisk virker):** [PLC_INTEGRATION_MANUAL.md](PLC_INTEGRATION_MANUAL.md)
- **GPIO-tilslutning:** [GPIO_MAPPING.md](GPIO_MAPPING.md)
- **Function code-support (design):** [DESIGN_GUIDE_MODBUS_EXPANSION_FC_CAPABILITIES.md](DESIGN_GUIDE_MODBUS_EXPANSION_FC_CAPABILITIES.md)
- **OTA fra PLC'en (plan og kontrakt):** [PLC_OTA_INTEGRATION_PLAN.md](PLC_OTA_INTEGRATION_PLAN.md)
- **Oprindeligt design** (visionen med op til 8 kanaler pr. board): [EXPANSION_BOARD_DESIGN.md](EXPANSION_BOARD_DESIGN.md). Ved uoverensstemmelse gælder PLC_INTEGRATION_MANUAL.md.
- **Arkitektur (lag, ansvarsfordeling):** [ARCHITECTURE.md](ARCHITECTURE.md)
- **Arbejdsregler:** [CLAUDE.md](CLAUDE.md)
- **Status:**
  - [FEATURES.md](FEATURES.md)
  - [BUGS.md](BUGS.md)
  - [CHANGELOG.md](CHANGELOG.md)
  - [RELEASE_NOTES.md](RELEASE_NOTES.md)
- **Kode-referencer fra PLC-repoet** (statiske kopier): [reference-plc-source/](reference-plc-source/)

## Teknik

- **Firmware:** PlatformIO + Arduino-core (ESP-IDF-komponenter til OTA og Ethernet), FreeRTOS med én task pr. Modbus-kanal
- **Netværk:** WiFi plus valgfri W5500 (dual-stack)
- **Protokoller:** Modbus TCP (data, port 502–505) og REST/JSON (administration, port 8080)
- **Test:** `pio test -e native` (hardware-uafhængige biblioteker under `lib/`)
