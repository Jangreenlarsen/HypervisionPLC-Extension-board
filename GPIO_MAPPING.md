# GPIO-mapping — HypervisionPLC Extension Board (Variant A)

Samlet, hurtigt-opslags-reference for alle GPIO'er brugt på det fysiske board: et **30-pin ESP32-WROOM-32 DevKit** (IKKE WROVER/PSRAM). Fuld begrundelse for hvert valg står i [EXPANSION_BOARD_DESIGN.md](EXPANSION_BOARD_DESIGN.md) §2.0.1 — denne fil er et rent opslagsværk, ikke den autoritative kilde ved uoverensstemmelse.

**Seneste ændring:** 2026-09-14 — MODE_SEL er nu en fabriks-INPUT (fysisk jumper/strap, sat ved fremstilling), ikke længere et firmware-/REST-styret output. Samlet tidligere samme dag til én delt GPIO for hele boardet (var én pr. kanal); den frigjorte GPIO23 bruges til W5500's RST-pin.

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
| 21 | *(fri)* | Reserveret til fremtidig I2C (SDA) |
| 22 | *(fri)* | Reserveret til fremtidig I2C (SCL) |
| 23 | W5500 — RST | Software-styret nulstilling |
| 25 | Kanal B — DIR | DE/RE-retning (dynamisk, kun relevant ved RS485) |
| 26 | Kanal A — aktivitets-LED *(valgfri)* | Diagnostik |
| 27 | Kanal A — DIR | DE/RE-retning (dynamisk, kun relevant ved RS485) |
| 32 | W5500 — CS | SPI chip select |
| 33 | Kanal B — aktivitets-LED *(valgfri)* | Diagnostik |
| 34 | *(fri, input-only)* | Reserveret, fx fabriksnulstillings-knap |
| 35 | W5500 — MISO | SPI (input-only) |
| 36 | *(fri, input-only)* | Reserveret |
| 39 | W5500 — INT | Interrupt (input-only, interrupt-drevet drift) |

## Fysisk pin-layout (30-pin ESP32 DevKit, USB-stik opad)

Boardet er et **30-pin ESP32-WROOM-32 DevKit**. GPIO-nummeret er det autoritative — det står trykt direkte på kredsløbskortet ved hver pin, uanset fabrikant. Rækkefølgen top/bund herunder følger den mest almindelige 30-pin-layout, men kan variere ganske lidt mellem klonfabrikanter — tjek altid dit boards eget silketryk før tilslutning.

**Venstre side (top → bund):**

| GPIO | Bruges til |
|---|---|
| 36 (VP) | *(fri)* |
| 39 (VN) | W5500 — INT |
| 34 | *(fri, fx fabriksnulstil-knap)* |
| 35 | W5500 — MISO |
| 32 | W5500 — CS |
| 33 | Kanal B — aktivitets-LED *(valgfri)* |
| 25 | Kanal B — DIR |
| 26 | Kanal A — aktivitets-LED *(valgfri)* |
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
| 22 | *(fri, reserveret I2C SCL)* |
| 1 (TX0) | Seriel CLI (USB) |
| 3 (RX0) | Seriel CLI (USB) |
| 21 | *(fri, reserveret I2C SDA)* |
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
- GPIO21/22 og GPIO26/33 er bevidst friholdt/reserveret, ikke i brug endnu.

## Se også

- [EXPANSION_BOARD_DESIGN.md](EXPANSION_BOARD_DESIGN.md) §2.0.1 — den fulde begrundelse, AND-gate-logik og hardware-revisionshistorik
- [PLC_INTEGRATION_MANUAL.md](PLC_INTEGRATION_MANUAL.md) — REST-API'et der konfigurerer kanalerne (§4.4's boks om MODE_SEL-sideeffekten)
