# GPIO-mapping — HypervisionPLC Extension Board (Variant A)

Samlet, hurtigt-opslags-reference for alle GPIO'er brugt på et ESP32-WROOM-32 DevKit (IKKE WROVER/PSRAM). Fuld begrundelse for hvert valg står i [EXPANSION_BOARD_DESIGN.md](EXPANSION_BOARD_DESIGN.md) §2.0.1 — denne fil er et rent opslagsværk, ikke den autoritative kilde ved uoverensstemmelse.

**Seneste ændring:** 2026-09-14 — MODE_SEL samlet til én delt GPIO for hele boardet (var én pr. kanal); den frigjorte GPIO23 bruges nu til W5500's RST-pin.

| GPIO | Funktion | Formål |
|---|---|---|
| 0 | *(undgås)* | Boot-strapping-pin |
| 1 | UART0 TX | Seriel CLI (USB) |
| 2 | *(undgås)* | Boot-strapping-pin |
| 3 | UART0 RX | Seriel CLI (USB) |
| 4 | **MODE_SEL — hele boardet** | RS232/RS485-valg, delt af begge kanaler (statisk, sat via `PUT /api/channels/{n}/config`) |
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

## Vigtigt at vide

- **MODE_SEL (GPIO4) gælder BEGGE kanaler samtidig** — RS232 og RS485 kan ikke blandes mellem kanal A og B. Sætter du `mode` på den ene kanal via REST-API'et (`PUT /api/channels/{n}/config`), spejles ændringen automatisk til den anden.
- **DIR (GPIO25/27)** er dynamisk — toggles af firmwaren omkring hver RS485-sending, rørt slet ikke i RS232-mode.
- **W5500 har ingen strapping-pin-konflikt** — alle dens GPIO'er (13, 14, 23, 32, 35, 39) er valgt bevidst udenom ESP32'ens boot-strapping-pins.
- GPIO21/22 og GPIO26/33 er bevidst friholdt/reserveret, ikke i brug endnu.

## Se også

- [EXPANSION_BOARD_DESIGN.md](EXPANSION_BOARD_DESIGN.md) §2.0.1 — den fulde begrundelse, AND-gate-logik og hardware-revisionshistorik
- [PLC_INTEGRATION_MANUAL.md](PLC_INTEGRATION_MANUAL.md) — REST-API'et der konfigurerer kanalerne (§4.4's boks om MODE_SEL-sideeffekten)
