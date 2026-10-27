# GPIO-mapping — HypervisionPLC Extension Board

Samlet, hurtigt-opslags-reference for alle ben brugt på det fysiske board: et **30-pin ESP32-WROOM-32 DevKit** (IKKE WROVER/PSRAM) med op til to RS485-moduler direkte på ESP32'ens UART'er (kanal A og B) og et valgfrit **CJMCU-752**-modul (SC16IS752, 2× UART over I2C) til kanal C og D. Fuld begrundelse for hvert valg står i [EXPANSION_BOARD_DESIGN.md](EXPANSION_BOARD_DESIGN.md) §2.0.1 — denne fil er et opslagsværk, og hvor den afviger fra koden, gælder **koden** ([src/modbus_channel.cpp](src/modbus_channel.cpp), [src/uart_port_native.cpp](src/uart_port_native.cpp), [src/uart_expander.cpp](src/uart_expander.cpp), [lib/sc16is75x/](lib/sc16is75x/)).

**Seneste ændring:** 2026-10-27 — gennemgået mod firmware v0.36.0: hvordan RS485-retningen (DIR/DE/RE) styres for hver kanal, chippens ben (fra NXP-databladet), RS485-modulernes ben, LED'er og opstartsrækkefølge. Historik: 2026-09-25 (v0.31.0) CJMCU-752 giver kanal C+D (GPIO21/22 = I2C, GPIO36 = EXP_SEL); 2026-09-15 aktivitets-LED'er drevet af firmwaren; 2026-09-14 MODE_SEL er en fabriks-INPUT, delt af alle kanaler, og GPIO23 gik til W5500's RST.

## ESP32-ben

| GPIO | Funktion | Type | Formål og detaljer |
|---|---|---|---|
| 0 | *(undgås)* | | Boot-strapping-pin |
| 1 | UART0 TX | | Seriel CLI (USB) |
| 2 | *(undgås)* | | Boot-strapping-pin |
| 3 | UART0 RX | | Seriel CLI (USB) |
| 4 | **MODE_SEL — hele boardet** | INPUT_PULLUP | Jumper: **3,3 V eller ubunden = RS485**, **GND = RS232**. Læses ÉN gang ved boot (10 µs sættetid). Gælder alle kanaler. Ikke ændrbar via REST |
| 5 | *(undgås)* | | Boot-strapping-pin |
| 6–11 | *(undgås)* | | Internt forbundet til SPI-flash — brug ALDRIG |
| 12 | *(undgås)* | | Boot-strapping-pin |
| 13 | W5500 — MOSI | | SPI |
| 14 | W5500 — SCK | | SPI |
| 15 | *(undgås)* | | Boot-strapping-pin |
| 16 | **Kanal A — RX** | UART1 RX | Fra RS485-modulets **RO** |
| 17 | **Kanal A — TX** | UART1 TX | Til RS485-modulets **DI** |
| 18 | **Kanal B — TX** | UART2 TX | Til RS485-modulets **DI** |
| 19 | **Kanal B — RX** | UART2 RX | Fra RS485-modulets **RO** |
| 21 | **I2C SDA** — CJMCU-752 | I2C, 400 kHz | Kanal C+D. Kræver pull-up til 3,3 V (typisk 4,7 kΩ — tjek om modulet har dem) |
| 22 | **I2C SCL** — CJMCU-752 | I2C, 400 kHz | Kanal C+D. Pull-up som ovenfor |
| 23 | W5500 — RST | OUTPUT | Software-styret nulstilling |
| 25 | **Kanal B — DIR** | OUTPUT | **Styrer RS485-modulets DE + /RE** (høj = send). Se "RS485-retning" |
| 26 | **Kanal A — aktivitets-LED** | OUTPUT, aktiv høj | Høj under en transaktion |
| 27 | **Kanal A — DIR** | OUTPUT | **Styrer RS485-modulets DE + /RE** (høj = send) |
| 32 | W5500 — CS | | SPI chip select |
| 33 | **Kanal B — aktivitets-LED** | OUTPUT, aktiv høj | Høj under en transaktion |
| 34 | *(reserveret, input-only)* | | CJMCU-752's IRQ. Ikke brugt af firmwaren (kanalerne polles). Hvis forbundet: **1 kΩ pull-up til 3,3 V påkrævet** (open-drain, databladet) |
| 35 | W5500 — MISO | | SPI (input-only) |
| 36 | **EXP_SEL — CJMCU-752 monteret** | INPUT (ingen intern pull) | Jumper til **3,3 V = monteret**. **10 kΩ pull-down til GND påkrævet.** Læses 5 gange (1 ms mellemrum) ved boot — alle skal være høje |
| 39 | W5500 — INT | | Interrupt (input-only) |

*Note GPIO16–19:* kanal A bruger ESP32'ens UART1 og kanal B UART2 (`HardwareSerial(1)` / `(2)`), 8 databit, paritet og stopbit efter kanalens opsætning. UART0 ejes af CLI'en.

## RS485-retning (DIR / DE / RE) — sådan styrer firmwaren den

Ét signal pr. kanal styrer **både DE og /RE** på RS485-modulet (de to ben forbindes sammen): **høj = sender** (driver til, modtager fra), **lav = modtager**. Hvem der driver signalet, afhænger af kanalen:

| Kanal | Styres af | Signal | Hvile (modtage) | Ledning |
|---|---|---|---|---|
| **A** | **ESP32** (software) | GPIO27 | Lav | → modulets DE + /RE |
| **B** | **ESP32** (software) | GPIO25 | Lav | → modulets DE + /RE |
| **C** | **UART-chippen** (hardware, auto-RTS) | RTSA — chip-ben 1 (TSSOP28) / 30 (HVQFN32) | Lav | → modulets DE + /RE |
| **D** | **UART-chippen** (hardware, auto-RTS) | RTSB — chip-ben 17 / 16 | Lav | → modulets DE + /RE |

**Kanal A og B — ESP32 styrer retningen** (`execute_transaction()`, kun i RS485-mode):

1. Tøm modtagebufferen for støj (højst 50 ms).
2. **DIR høj**, vent 50 µs (driveren skal nå at slå til).
3. Send hele frame'en og vent til UART'en er helt færdig (`flush`).
4. Vent **ét ekstra tegn (11 bit ÷ baudrate) + 100 µs**, så sidste stopbit er helt ude på linjen.
5. **DIR lav** — nu lyttes der efter svar.

**Kanal C og D — chippen styrer retningen selv** (ingen ESP32-ben, ingen mikrosekund-forsinkelser):

- Firmwaren sætter `EFCR = 0x30` (bit 4 **RTSCON** = senderen styrer RTS-benet, bit 5 **RTSINVER** = RTS høj under afsendelse, lav ved modtagelse). `MCR = 0x00`.
- Chippen sætter selv RTS **høj** i det øjeblik, firmwaren lægger data i send-FIFO'en, og slipper den **lav** efter sidste bit er sendt (databladet, afsnit 9.1-9.2).
- Firmwaren venter, til chippen melder "sender og skifteregister tomme" (LSR bit 6, højst 500 ms), før den lytter efter svar.

**RS232-mode** (MODE_SEL = GND): ESP32 rører aldrig DIR (forbliver lav). På C/D sættes `EFCR = 0x00` og `MCR = 0x02`, som tvinger RTS-benet **lavt**, så en evt. monteret RS485-driver er slukket. *Et RS485-modul sender derfor aldrig i RS232-mode — er boardet kun bygget med RS485-moduler, skal GPIO4 stå på 3,3 V (eller ubunden).*

**Under opstart (kort):**
- **Kanal A/B:** GPIO27/25 er indgange (flydende), indtil firmwaren sætter dem lave. *Anbefaling:* 10 kΩ pull-down på DE/RE-ledningen, så driveren er slukket under reset.
- **Kanal C/D:** databladet siger, at RTS-benet er **højt efter en reset** (DE aktiv), til firmwaren har skrevet EFCR for kanalen (få millisekunder efter start). Firmwaren sender intet i den periode.

## RS485-modul pr. kanal (DI, DE, RE, RO, VCC, GND)

Gælder det almindelige 4-bens RS485-modul. **Brug 3,3 V-moduler (SP3485/MAX3485).** Et 5 V-modul (MAX485) giver 5 V på RO, som ESP32'ens GPIO ikke tåler (på C/D tåler chippens indgange 5 V, men brug alligevel 3,3 V overalt).

| Modul-ben | Kanal A | Kanal B | Kanal C | Kanal D |
|---|---|---|---|---|
| **DI** (data ind, fra os) | ESP32 GPIO17 | ESP32 GPIO18 | Chip TXA (ben 3 / 32) | Chip TXB (ben 23 / 22) |
| **RO** (data ud, til os) | ESP32 GPIO16 | ESP32 GPIO19 | Chip RXA (ben 4 / 1) | Chip RXB (ben 24 / 23) |
| **DE** | ESP32 GPIO27 | ESP32 GPIO25 | Chip RTSA (ben 1 / 30) | Chip RTSB (ben 17 / 16) |
| **/RE** | samme ledning som DE | samme ledning som DE | samme ledning som DE | samme ledning som DE |
| **VCC** | 3V3 | 3V3 | 3V3 | 3V3 |
| **GND** | GND | GND | GND | GND |

- **Bind IKKE /RE til GND.** Så hører modulet sin egen afsendelse, og den ender som starten på svaret.
- Bus-siden (A og B på modulet) går til feltbussen; **120 Ω terminering** i begge ender af hver bus. Producenter mærker A/B forskelligt — bytter slaven ikke svar, så vend ledningerne.
- Selvstyrende moduler uden DE/RE-ben (kun TXD/RXD) bruger hverken DIR eller RTS; så udgår den ledning.
- Er boardet bygget med både RS485 og RS232 pr. kanal (designdokumentets dual-population), sidder der en AND-port (MODE_SEL × DIR) og en mux mellem ESP32 og transceiverne; tabellen ovenfor gælder de rene RS485-moduler.

## Aktivitets-LED'er

Hver kanal har en LED, der er **tændt (aktiv høj) under en hel RTU-transaktion**, uanset om den lykkes eller timer ud. Den blinker ikke for en deaktiveret kanal. Hver LED forbindes via en seriemodstand til GND (anbefalet ca. 1 kΩ — værdien er ikke noget, firmwaren bestemmer).

| Kanal | Kilde | Detaljer |
|---|---|---|
| A | ESP32 GPIO26 | Udgang, lav ved start |
| B | ESP32 GPIO33 | Udgang, lav ved start |
| C | Chip GPIO0 (ben 18 / 17) | Skrives via chippens IOSTATE-register; IODIR sættes først (`0x03` = GPIO0+1 udgange), derefter IOSTATE = 0 (v0.34.1: ellers stod LED'erne tændt fra boot) |
| D | Chip GPIO1 (ben 19 / 18) | Samme; en delt skygge-kopi af IOSTATE under bus-låsen sikrer, at C og D ikke overskriver hinanden |

## CJMCU-752 / SC16IS752 — kanal C og D

CJMCU-752 er et modul med NXP **SC16IS752** (2× UART, 64 byte FIFO pr. retning) og en **1,8432 MHz-krystal**. Kanal C = chippens UART A, kanal D = chippens UART B. Modbus TCP-porte: A 502, B 503, **C 504, D 505**. Ben-numrene herunder er fra [NXP-databladet](https://www.nxp.com/docs/en/data-sheet/SC16IS752_SC16IS762.pdf) (Rev. 9.1, tabel 2) for TSSOP28 / HVQFN32. *Benmærkningen på selve CJMCU-printet kan afvige — tjek silketrykket på dit modul.*

### Forbindelser mellem ESP32 og chip

| ESP32 | Chip-signal | Chip-ben | Bemærkning |
|---|---|---|---|
| 3V3 | VDD | 8 / 5, 13, 28 | **Kun 3,3 V** |
| GND | VSS | 22 / 12, 21, 29 | |
| GPIO21 | SDA | 14 / 11 | I2C-data |
| GPIO22 | SCL | 13 / 10 | I2C-clock |
| GPIO34 | IRQ | 15 / 14 | Valgfri; open-drain aktiv lav; **1 kΩ pull-up til 3,3 V** hvis forbundet; ubrugt af firmwaren |
| 3V3 | I2C/SPI-valg | 9 / 6 | **Høj = I2C** |
| — | SO | 12 / 9 | **Ubrugt** i I2C-mode (må ikke forbindes) |
| — | RESET | 5 / 2 | Aktiv lav, skal ligge højt. Firmwaren bruger software-reset (IOControl) og styrer ikke benet |
| GND | CS/A0, SI/A1 | 10 / 7, 11 / 8 | **A0 og A1 er begge bundet til GND på vores board → I2C-adresse 0x4D** (tabel nedenfor) |
| GPIO36 | *(EXP_SEL — ikke på chippen)* | | Jumper til 3,3 V = modul monteret |

### Chippens UART-ben

| Kanal | Signal | Chip-ben | Går til |
|---|---|---|---|
| **C** (UART A) | TXA | 3 / 32 | Modulets DI |
| C | RXA | 4 / 1 | Modulets RO |
| C | RTSA | 1 / 30 | Modulets DE + /RE (auto-retning) |
| C | CTSA | 2 / 31 | Ubrugt (hardware-flowkontrol er slået fra) |
| C | GPIO0 | 18 / 17 | Aktivitets-LED C |
| **D** (UART B) | TXB | 23 / 22 | Modulets DI |
| D | RXB | 24 / 23 | Modulets RO |
| D | RTSB | 17 / 16 | Modulets DE + /RE (auto-retning) |
| D | CTSB | 16 / 15 | Ubrugt |
| D | GPIO1 | 19 / 18 | Aktivitets-LED D |

Chippens øvrige GPIO2-7 (ben 20, 21, 25-28 / 19, 20, 24-27) bruges ikke og er indgange.

### I2C-adresse (A1 og A0)

Databladet angiver adresserne i 8-bit-form (0x90-0xAE); herunder er de omregnet til 7-bit, som firmwaren bruger. **På vores board er A1 og A0 begge bundet til GND, dvs. adressen er 0x4D** (A1 = VSS, A0 = VSS). **Firmwaren prøver alle 16** (0x48-0x57) ved boot, så en anden adressering virker også.

| A1 \ A0 | VDD | VSS | SCL | SDA |
|---|---|---|---|---|
| **VDD** | 0x48 | 0x49 | 0x4A | 0x4B |
| **VSS** | 0x4C | 0x4D | 0x4E | 0x4F |
| **SCL** | 0x50 | 0x51 | 0x52 | 0x53 |
| **SDA** | 0x54 | 0x55 | 0x56 | 0x57 |

### Hvad firmwaren sætter i chippen (pr. kanal)

| Register | Værdi | Betydning |
|---|---|---|
| DLL / DLH | divisor = 1 843 200 ÷ (16 × baud) | Baudrate. Skal ramme inden for ±1 %; **1200-115200 er eksakte, højere afvises** |
| LCR | `0x03` + paritet/stopbit (`+0x04` = 2 stopbit, `+0x08` paritet til, `+0x10` lige) | 8 databit |
| FCR | `0x07` | FIFO til, begge FIFO'er nulstillet |
| IER | `0x00` | Ingen interrupts — kanalerne **polles** |
| MCR | `0x00` (RS485) / `0x02` (RS232) | RS232: RTS-benet tvinges lavt |
| EFCR | `0x30` (RS485) / `0x00` (RS232) | RS485: auto-RTS, høj under afsendelse |
| IODIR / IOSTATE | `0x03` / bit 0-1 | LED-udgange (kanal C/D) |

I2C-subadressen er `(register << 3) | (kanal << 1)`. Send: højst 32 byte pr. I2C-skrivning, begrænset af chippens ledige send-plads (TXLVL), højst 500 ms. Modtag: modtage-niveau (RXLVL) læses, derefter hele FIFO'en i ét burst (højst 64 byte).

## Opstartsrækkefølge for benene

1. **MODE_SEL (GPIO4)** læses (`INPUT_PULLUP`): høj/ubunden → RS485, lav → RS232.
2. **EXP_SEL (GPIO36)** læses 5 gange; alle skal være høje, ellers antages intet modul (2 kanaler).
3. Hvis EXP_SEL er høj: I2C startes (GPIO21/22, 400 kHz) og adresserne 0x48-0x57 afprøves med en scratch-register-test (skriv `0xA5` og `0x5A`, læs tilbage). Den første, der svarer, bruges.
4. Chippen **software-nulstilles**, LED-benene sættes til udgange, og begge LED'er slukkes.
5. **Svarer chippen**: 4 kanaler (A-D), `expander: "ok"`. **Svarer den ikke**: boardet kører på **2 kanaler** (A-B), `expander: "not_found"`, og fejlen logges til syslog. Er EXP_SEL lav: 2 kanaler, `expander: "not_fitted"`.
6. DIR-benene (GPIO27/25) og LED-benene (GPIO26/33) sættes til udgange, lave; kanalerne konfigureres (UART-opsætning, for C/D chip-registrene ovenfor).

## Fysisk pin-layout (30-pin ESP32 DevKit, USB-stik opad)

GPIO-nummeret er det autoritative — det står trykt direkte på kredsløbskortet ved hver pin, uanset fabrikant. Rækkefølgen top/bund herunder følger den mest almindelige 30-pin-layout, men kan variere ganske lidt mellem klonfabrikanter — tjek altid dit boards eget silketryk før tilslutning.

**Venstre side (top → bund):**

| GPIO | Bruges til |
|---|---|
| 36 (VP) | EXP_SEL (CJMCU-752 monteret) |
| 39 (VN) | W5500 — INT |
| 34 | CJMCU-752 IRQ (reserveret) |
| 35 | W5500 — MISO |
| 32 | W5500 — CS |
| 33 | Kanal B — aktivitets-LED |
| 25 | Kanal B — DIR (DE + /RE) |
| 26 | Kanal A — aktivitets-LED |
| 27 | Kanal A — DIR (DE + /RE) |
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
| 19 | Kanal B — RX (fra modulets RO) |
| 18 | Kanal B — TX (til modulets DI) |
| 5 | *(undgås — boot-strapping)* |
| 17 | Kanal A — TX (til modulets DI) |
| 16 | Kanal A — RX (fra modulets RO) |
| 4 | MODE_SEL — hele boardet |
| 0 | *(undgås — boot-strapping)* |
| 2 | *(undgås — boot-strapping)* |
| 15 | *(undgås — boot-strapping)* |
| 3V3 | — |

## Vigtigt at vide

- **MODE_SEL (GPIO4) gælder ALLE kanaler samtidig** — RS232 og RS485 kan ikke blandes mellem kanalerne. `PUT /api/channels/{n}/config` ignorerer et evt. `mode`-felt; `mode` er kun en læseværdi.
- **W5500 har ingen strapping-pin-konflikt** — alle dens GPIO'er (13, 14, 23, 32, 35, 39) er valgt udenom ESP32'ens boot-strapping-pins.
- **Kanal C/D: baudrate 1200-115200** (krystallens grænse). Højere afvises af `PUT /api/channels/{3,4}/config` med `400 invalid_baudrate`.
- **GPIO21/22 er I2C-bussen til CJMCU-752** — andre I2C-enheder kan sidde på samme bus, så længe de ikke bruger adresserne 0x48-0x57.
- **GPIO34-39 har ingen intern pull-up/-down.** Derfor kræver EXP_SEL (GPIO36) en ekstern 10 kΩ pull-down; en svævende pin kan ellers give tilfældigt "monteret" (derfor de 5 aflæsninger).
- **Spænding:** alt kører på 3,3 V. Modulernes VCC må ikke være 5 V (se RS485-modul-afsnittet).

## Se også

- [docs/ESP32_W5500_UART_RS485_KOBLING.html](docs/ESP32_W5500_UART_RS485_KOBLING.html) — kablingsdiagram med alle ben for de to varianter af boardet (2 kanaler, og 4 kanaler med CJMCU-752)
- [EXPANSION_BOARD_DESIGN.md](EXPANSION_BOARD_DESIGN.md) §2.0.1 — den fulde begrundelse, AND-gate-logik og hardware-revisionshistorik
- [PLC_INTEGRATION_MANUAL.md](PLC_INTEGRATION_MANUAL.md) — REST-API'et der konfigurerer kanalerne
- [NXP SC16IS752 datablad](https://www.nxp.com/docs/en/data-sheet/SC16IS752_SC16IS762.pdf) — ben (tabel 2), adresser (tabel 32), auto-RS485 (afsnit 9)
