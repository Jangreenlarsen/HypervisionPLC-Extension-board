# GPIO-mapping — HypervisionPLC Extension Board (Variant A)

Samlet, hurtigt-opslags-reference for alle GPIO'er brugt på det fysiske board: et **30-pin ESP32-WROOM-32 DevKit** (IKKE WROVER/PSRAM). Fuld begrundelse for hvert valg står i [EXPANSION_BOARD_DESIGN.md](EXPANSION_BOARD_DESIGN.md) §2.0.1 — denne fil er et rent opslagsværk, ikke den autoritative kilde ved uoverensstemmelse.

**Seneste ændring:** 2026-09-25 (v0.31.0) — CJMCU-752 (SC16IS752, 2× UART over I2C) giver kanal C+D: GPIO21/22 er nu I2C (SDA/SCL), GPIO36 er EXP_SEL-jumperen (3,3 V = modul monteret, ekstern 10 kΩ pull-down påkrævet), GPIO34 er reserveret til modulets IRQ (ikke brugt af firmwaren endnu — kanalerne polles). Se afsnittet "CJMCU-752 — kanal C og D" nederst. Tidligere (2026-09-15): — Aktivitets-LED'erne (GPIO26/33) er nu faktisk drevet af firmwaren (tændt under en RTU-transaktion), ikke længere blot reserveret/urørt. Tidligere (2026-09-14): MODE_SEL er nu en fabriks-INPUT (fysisk jumper/strap, sat ved fremstilling), ikke længere et firmware-/REST-styret output. Samlet tidligere samme dag til én delt GPIO for hele boardet (var én pr. kanal); den frigjorte GPIO23 bruges til W5500's RST-pin.

| GPIO | Funktion | Formål |
|---|---|---|
| 0 | *(undgås)* | Boot-strapping-pin |
| 1 | UART0 TX | Seriel CLI (USB) |
| 2 | *(undgås)* | Boot-strapping-pin |
| 3 | UART0 RX | Seriel CLI (USB) |
| 4 | **MODE_SEL — hele boardet** | RS232/RS485-valg, delt af begge kanaler — INPUT (`INPUT_PULLUP`), fysisk jumper/strap sat ved fremstilling, læst af firmwaren ÉN gang ved boot (ikke settable via REST) |
| 5 | *(undgås)* | Boot-strapping-pin |
| 6–11 | *(undgås)* | Internt forbundet til SPI-flash — brug ALDRIG |
| 12 | *(undgås)* | Boot-strapping-pin |
| 13 | W5500 — MOSI | SPI |
| 14 | W5500 — SCK | SPI |
| 15 | *(undgås)* | Boot-strapping-pin |
| 16 | Kanal A — UART RX | Fra transceiver-muxen |
| 17 | Kanal A — UART TX | Til begge transceiveres driver-input |
| 18 | Kanal B — UART TX | Til begge transceiveres driver-input |
| 19 | Kanal B — UART RX | Fra transceiver-muxen |
| 21 | **I2C SDA** — CJMCU-752 | v0.31.0: UART-expander til kanal C+D (400 kHz) |
| 22 | **I2C SCL** — CJMCU-752 | v0.31.0 |
| 23 | W5500 — RST | Software-styret nulstilling |
| 25 | Kanal B — DIR | DE/RE-retning (dynamisk, kun relevant ved RS485) |
| 26 | Kanal A — aktivitets-LED | Tændt under en RTU-transaktion (v0.23.1) |
| 27 | Kanal A — DIR | DE/RE-retning (dynamisk, kun relevant ved RS485) |
| 32 | W5500 — CS | SPI chip select |
| 33 | Kanal B — aktivitets-LED | Tændt under en RTU-transaktion (v0.23.1) |
| 34 | *(reserveret, input-only)* | CJMCU-752's IRQ-udgang (open-drain) — v0.31.0: forbindes, men firmwaren poller i dag og bruger den ikke |
| 35 | W5500 — MISO | SPI (input-only) |
| 36 | **EXP_SEL — CJMCU-752 monteret** | v0.31.0: INPUT, læst ÉN gang ved boot. Jumper til **3,3 V = monteret** (4 kanaler), ingen jumper = 2 kanaler. **Ekstern 10 kΩ pull-down til GND påkrævet** (GPIO34-39 har ingen intern pull) |
| 39 | W5500 — INT | Interrupt (input-only, interrupt-drevet drift) |

## Fysisk pin-layout (30-pin ESP32 DevKit, USB-stik opad)

Boardet er et **30-pin ESP32-WROOM-32 DevKit**. GPIO-nummeret er det autoritative — det står trykt direkte på kredsløbskortet ved hver pin, uanset fabrikant. Rækkefølgen top/bund herunder følger den mest almindelige 30-pin-layout, men kan variere ganske lidt mellem klonfabrikanter — tjek altid dit boards eget silketryk før tilslutning.

**Venstre side (top → bund):**

| GPIO | Bruges til |
|---|---|
| 36 (VP) | EXP_SEL (CJMCU-752 monteret) |
| 39 (VN) | W5500 — INT |
| 34 | CJMCU-752 IRQ (reserveret) |
| 35 | W5500 — MISO |
| 32 | W5500 — CS |
| 33 | Kanal B — aktivitets-LED |
| 25 | Kanal B — DIR |
| 26 | Kanal A — aktivitets-LED |
| 27 | Kanal A — DIR |
| 14 | W5500 — SCK |
| 12 | *(undgås — boot-strapping)* |
| 13 | W5500 — MOSI |
| GND | — |
| VIN | — |

**Højre side (top → bund):**

| GPIO | Bruges til |
|---|---|
| 23 | W5500 — RST |
| 22 | I2C SCL (CJMCU-752) |
| 1 (TX0) | Seriel CLI (USB) |
| 3 (RX0) | Seriel CLI (USB) |
| 21 | I2C SDA (CJMCU-752) |
| GND | — |
| 19 | Kanal B — UART RX |
| 18 | Kanal B — UART TX |
| 5 | *(undgås — boot-strapping)* |
| 17 | Kanal A — UART TX |
| 16 | Kanal A — UART RX |
| 4 | MODE_SEL — hele boardet |
| 0 | *(undgås — boot-strapping)* |
| 2 | *(undgås — boot-strapping)* |
| 15 | *(undgås — boot-strapping)* |
| 3V3 | — |

## Vigtigt at vide

- **MODE_SEL (GPIO4) gælder BEGGE kanaler samtidig** — RS232 og RS485 kan ikke blandes mellem kanal A og B. Det er en fysisk INPUT (jumper/strap til 3.3V=RS485 eller GND=RS232), sat ÉN gang ved fremstilling — IKKE et felt der kan sættes via REST-API'et; `PUT /api/channels/{n}/config` ignorerer et evt. `mode`-felt, og `mode` optræder kun som en læseværdi i `GET`-svar.
- **DIR (GPIO25/27)** er dynamisk — toggles af firmwaren omkring hver RS485-sending, rørt slet ikke i RS232-mode.
- **W5500 har ingen strapping-pin-konflikt** — alle dens GPIO'er (13, 14, 23, 32, 35, 39) er valgt bevidst udenom ESP32'ens boot-strapping-pins.
- **Aktivitets-LED (GPIO26/33)** tændes af `channel_task()` (`src/modbus_channel.cpp`) for den præcise varighed af en RTU-transaktion — succes ELLER fejl/timeout blinker ens; en deaktiveret kanal (`enabled:false`) blinker IKKE, da der ikke sker nogen reel bus-aktivitet.
- GPIO21/22 er I2C-bussen til CJMCU-752 (v0.31.0) — andre I2C-enheder kan sidde på samme bus, så længe de ikke bruger adresserne 0x48-0x57.

## CJMCU-752 — kanal C og D (v0.31.0)

CJMCU-752 er et modul med NXP SC16IS752 (2× UART med 64 byte FIFO) og en **1,8432 MHz-krystal**. Det giver kanal C og D (Modbus TCP-port 504 og 505) oveni kanal A og B.

| CJMCU-752-ben | Forbindes til | Bemærkning |
|---|---|---|
| VCC | 3,3 V | **Ikke 5 V** — modulets logikniveau følger forsyningen, og ESP32'en tåler kun 3,3 V |
| GND | GND | |
| SDA | GPIO21 | Modulets I2C/SPI-valg skal stå på **I2C** |
| SCL | GPIO22 | |
| IRQ | GPIO34 | Valgfri i dag (reserveret) |
| A0 / A1 | frit valg | Firmwaren prøver alle 16 adresser (0x48-0x57) ved boot |
| TXA / RXA | Kanal C's transceiver (DI / RO) | TTL-niveau — kræver egen SP3485 (RS485) og/eller MAX3232 (RS232) |
| RTSA | Kanal C's SP3485 DE + /RE | Chippen styrer selv retningen (høj under afsendelse) — ingen ESP32-GPIO |
| TXB / RXB | Kanal D's transceiver (DI / RO) | |
| RTSB | Kanal D's SP3485 DE + /RE | |
| GPIO0 | Kanal C's aktivitets-LED (via modstand) | Tændt under en transaktion |
| GPIO1 | Kanal D's aktivitets-LED (via modstand) | |

- **EXP_SEL (GPIO36) til 3,3 V** fortæller firmwaren, at modulet er monteret. Siger jumperen "monteret", men chippen svarer ikke på I2C, melder boardet stadig 4 kanaler, men kanal C/D har `"status":"unavailable"`, `/api/status` viser `"expander":"not_found"`, og alle transaktioner afvises — så hardwarefejlen er synlig.
- **Alle 4 kanaler følger samme MODE_SEL-jumper (GPIO4)** — boardet er enten 4×RS485 eller 4×RS232 (`board_type` i `/api/status`).
- **Baudrate på kanal C/D: 1200-115200** (krystallens grænse). Højere afvises af `PUT /api/channels/{3,4}/config` med `400 invalid_baudrate`.
- **I RS232-mode** holder chippen RTS-benet lavt, så en evt. monteret SP3485's driver er slukket.

## Se også

- [EXPANSION_BOARD_DESIGN.md](EXPANSION_BOARD_DESIGN.md) §2.0.1 — den fulde begrundelse, AND-gate-logik og hardware-revisionshistorik
- [PLC_INTEGRATION_MANUAL.md](PLC_INTEGRATION_MANUAL.md) — REST-API'et der konfigurerer kanalerne (§4.4's boks om MODE_SEL-sideeffekten)
