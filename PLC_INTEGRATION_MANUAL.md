# PLC-integrations-manual: HypervisionPLC Extension Board

Denne manual dokumenterer expansion-boardets **fulde, faktisk implementerede** grænseflade, som den ser ud efter v0.15.0 (build 0018) — til brug når PLC-siden (`Modbus_server_slave_ESP32`-repoet, `modbus_expansion.cpp`/`expansion_api_client.cpp`, §5 i [EXPANSION_BOARD_DESIGN.md](EXPANSION_BOARD_DESIGN.md)) skal implementeres. Alt heri er verificeret mod rigtig hardware, ikke kun designet — se [CHANGELOG.md](CHANGELOG.md) for de enkelte live-verifikationer.

**Forskel fra designdokumentet:** [EXPANSION_BOARD_DESIGN.md](EXPANSION_BOARD_DESIGN.md) beskriver den fulde, oprindelige vision (op til 8 kanaler, OTA, osv.). Denne manual beskriver kun det der **rent faktisk er bygget og testet** i denne repo lige nu — Variant A, 2 kanaler. Er der uoverensstemmelse, er DENNE fil den autoritative kilde for hvad et board faktisk gør i dag.

---

## Indhold

1. [Arkitektur i korte træk](#1-arkitektur-i-korte-træk)
2. [Første opsætning (provisionering)](#2-første-opsætning-provisionering)
3. [Modbus TCP — data-planet (høj-frekvent drift)](#3-modbus-tcp--data-planet-høj-frekvent-drift)
4. [REST management-API (port 8080)](#4-rest-management-api-port-8080) (inkl. 4.8 `GET /api/capabilities`)
5. [Fejlkoder-reference](#5-fejlkoder-reference)
6. [Anbefalet PLC-side-integrationsflow](#6-anbefalet-plc-side-integrationsflow)
7. [Kendte begrænsninger lige nu](#7-kendte-begrænsninger-lige-nu)

---

## 1. Arkitektur i korte træk

- Boardet har **2 faste kanaler** (Variant A, §2.0): kanal A (n=1) og kanal B (n=2), hver sin fysiske UART (RS485 pt., RS232-mode findes i config men er ikke hardware-testet).
- **To adskilte netværksgrænseflader**, forskellige trust-niveauer:
  - **Modbus TCP (port 502/503)** — data-planet, PLC'ens høj-frekvente driftsvej. INGEN protokol-auth (Modbus-standard-begrænsning) — beskyttet af ét fast `plc_ip`-permit (§4.3, se afsnit 3).
  - **REST/JSON (port 8080)** — management-planet. Kræver Bearer-token ELLER Basic Auth på ALLE endpoints uden undtagelse. IKKE begrænset af `plc_ip`-permittet — bevidst, så en fejlkonfigureret/skiftet PLC-IP aldrig kan spærre administratoren ude (se §4.3).
- Boardet har INGEN egen driftsbrugerflade. Al løbende konfiguration sker fra PLC-siden via REST-API'et. Den serielle CLI (USB, fysisk adgang) er UDELUKKENDE til første netværksopsætning + nødadgang.

---

## 2. Første opsætning (provisionering)

Sker over **seriel CLI (USB), aldrig over netværket** — dette er en bevidst, ufravigelig sikkerhedsbeslutning (§3.4). PLC-siden skal ikke og kan ikke automatisere dette skridt; det er en engangs-installatør-opgave pr. board.

Minimal kommandosekvens (se `help` for alle kommandoer):

```
wifi ssid "MitNetvaerk"
wifi pass "MitPassword123"
plc ip 10.1.1.153        # PLC'ens IP - se afsnit 3, kritisk for data-planet
connect
```

Efter `connect` lykkes:
- Boardet forbinder til WiFi, starter REST-API'et (port 8080) og Modbus TCP-serveren (port 502/503).
- Et management-API-token genereres automatisk (kun ved ALLERFØRSTE `connect`) og vises på skærmen — **dette er den værdi PLC-siden skal bruge som Bearer-token** (se afsnit 4.1). Det kan altid genses med `status`.
- `rest user`/`rest pass` kan sættes for at bruge Basic Auth i stedet for/ved siden af tokenet.

**Genopsætning/fejlsøgning:** `show`/`status` viser AL config i klartekst (adgangskoder, token) — dette er en bevidst undtagelse, kun gældende for denne serielle CLI (fysisk USB-adgang = allerede tillidsgrænsen). REST-API'et returnerer aldrig disse værdier.

---

## 3. Modbus TCP — data-planet (høj-frekvent drift)

Dette er den vej PLC'en skal bruge til NORMAL, høj-frekvent Modbus-drift — REST-API'ets `/read`/`/write` (afsnit 4.5) er kun til ad-hoc diagnose.

### 3.1 Port → kanal-mapping

| Port | Kanal |
|---|---|
| 502 | A (n=1) |
| 503 | B (n=2) |

### 3.2 Forbindelse og framing

- Standard Modbus TCP (MBAP-header): Transaction ID (ekko), Protocol ID (altid 0), Length, **Unit ID = den fysiske RTU-slaves adresse** (1-247) — IKKE en dummy-værdi. PLC'en sætter Unit ID til den slave den faktisk vil tale med på den fysiske bus bag den valgte kanal.
- PDU'en (function code + data) passerer **uændret** igennem — ingen registerombytning, ingen indirection. Adresser/values i requestet er nøjagtigt hvad der sendes til RTU-slaven.
- **Åbn ÉN vedvarende TCP-forbindelse pr. kanal og genbrug den** til alle transaktioner. Boardets TCP-server har ét sekventielt kø pr. kanal — mange kortvarige forbindelser (åbn/luk pr. request) er ikke understøttet godt og bør undgås (se BUGS.md v0.9.0.1 for baggrund; roden er rettet, men mange samtidige korte forbindelser er stadig ineffektivt).
- Understøttede function codes: **FC01, FC02, FC03, FC04, FC05, FC06, FC15, FC16**. Andet giver en gateway-exception (se nedenfor).

### 3.3 Fejlhåndtering — to forskellige slags "fejl"

1. **En Modbus-exception FRA slaven selv** (høj bit sat i function code-byten, fx `0x83` for en FC03-fejl) — relayes UÆNDRET. Det er slavens eget svar, ikke boardets.
2. **En gateway-exception FRA BOARDET** (samme byte-format, men genereret af boardet selv når det IKKE kunne gennemføre transaktionen):
   - **`0x0A` (Gateway Path Unavailable)** — kanalen er util-tilgængelig: deaktiveret (`enabled:false`, se afsnit 4.3), ugyldig adresse/PDU, eller kanalen optaget.
   - **`0x0B` (Gateway Target Device Failed to Respond)** — slaven svarede slet ikke (timeout) eller svarede forkert (CRC-fejl, forkert adresse i svaret).

PLC'ens Modbus TCP-klient bør derfor tolke ETHVERT svar med høj bit sat som en exception (standard Modbus-adfærd) — der er ingen måde at skelne "slavens egen exception" fra "boardets gateway-exception" i selve PDU'en (det er med vilje — begge er gyldige Modbus-exceptions for en standard-klient). Skal de skelnes, brug i stedet de diagnostiske REST-endpoints (afsnit 4.5), som eksplicit rapporterer forskellen.

### 3.4 `plc_ip`-permittet (§4.3)

- Boardet accepterer KUN TCP-forbindelser til port 502/503 fra ÉN, fast IP — den der blev sat med `plc ip <a.b.c.d>` under provisionering (afsnit 2).
- **Fejl-lukket, ikke fejl-åbent:** er `plc_ip` slet ikke sat, er data-planet lukket for ALLE.
- Afvises en forbindelse, sker det ved `accept()` — FØR noget Modbus-indhold overhovedet parses. Ingen respons sendes, forbindelsen lukkes blot.
- **Vigtigt for PLC-siden:** hvis PLC'en nogensinde skifter IP (ny DHCP-lease, ny netværkskonfiguration), SKAL boardets `plc_ip` opdateres via den serielle CLI igen (`plc ip <ny-ip>`, `save`). Der er intet REST-endpoint til dette — det er bevidst uden for netværkets rækkevidde (§4.3's begrundelse: en fejlkonfigureret/kompromitteret PLC må ikke selv kunne omdirigere data-planet-adgangen).
- Denne begrænsning gælder KUN port 502/503 — REST-API'et (port 8080) er upåvirket, uanset `plc_ip`.

---

## 4. REST management-API (port 8080)

Alle endpoints kræver autentificering (afsnit 4.1). Alle svar er `application/json`.

### 4.1 Autentificering

To metoder, konfigureret på boardet via `rest auth token|basic|both` (CLI):

- **Bearer-token:** `Authorization: Bearer <token>` — tokenet fra provisionering (afsnit 2). Anbefalet til PLC'en (maskinvenligt, ingen menneskelig indblanding nødvendig).
- **Basic Auth:** `Authorization: Basic <base64(bruger:kode)>` — sat via `rest user`/`rest pass`.

Manglende/forkert auth → `401`:
```json
{"ok":false,"error":"unauthorized","message":"Manglende eller ugyldig Authorization-header"}
```
Præsenteres en metode der er eksplicit slået fra (`rest auth`-kommandoen), er beskeden i stedet: `"Denne auth-metode er slaaet fra (se 'rest auth' i den serielle CLI)"`.

### 4.2 `GET /api/status`

```json
{
  "api_version": 1,
  "fw_version": "0.15.0",
  "fw_build": "0018",
  "uptime_s": 86412,
  "heap_free_bytes": 221856,
  "active_channels": 2,
  "provisioned": true,
  "board_mode": "rs485",
  "wifi": {"connected": true, "ip": "10.1.1.229", "rssi_dbm": -62},
  "ethernet": {"connected": false, "status": "not_detected"}
}
```
`board_mode` (`"rs485"` eller `"rs232"`, v0.15.0) er boardets AKTUELLE RS232/RS485-mode — siden begge kanaler siden v0.14.0 deler én fysisk MODE_SEL-GPIO (§2.0.1), er dette den samme værdi som ENHVER kanals `mode`-felt i `GET /api/channels`. Foretrukket direkte kilde til board-mode fremfor at udlede den fra en tilfældig kanal. **Ren læseværdi (hardware-revision 2026-09-14, 2. ændring):** afspejler MODE_SEL-jumperens fysiske position, læst af firmwaren ved boot — kan IKKE påvirkes via REST, se afsnit 4.4.

`ethernet.status` (v0.18.0) skelner mere præcist end `connected` alene: `"not_detected"` (intet W5500-modul fundet på SPI-bussen — hardware-/wiring-problem), `"link_down"` (modul fundet og driver kører, men PHY'en rapporterer intet link — netværkskabel/switch-port), `"waiting_dhcp"` (link oppe, venter på IP) eller `"connected"` (link oppe + IP). `connected` (boolean, uændret siden v0.13.0) er `true` UDELUKKENDE ved `"connected"`-status — de tre øvrige giver alle `connected:false`, men `status` fortæller PLC-siden PRÆCIS hvorfor, i stedet for kun at vide at Ethernet ikke virker lige nu.
`wifi`-objektet er kun `{"connected":false}` hvis ikke forbundet (`ip`/`rssi_dbm` udelades da). `ethernet`-objektet (§1.3/§2.2, valgfrit W5500-modul, v0.13.0) er tilsvarende kun `{"connected":true,"ip":"..."}` når linket er oppe — INGEN `rssi_dbm` (kablet, ikke relevant). WiFi og Ethernet kan begge være `connected:true` samtidig (dual-stack) — boardet har ingen provisionering for Ethernet, den henter blot en IP via DHCP så snart et kabel er tilsluttet. `api_version` er en separat protokol-kontrakt-version (bumpes KUN ved brydende ændringer i selve API'et) — PLC-siden bør logge en advarsel, ikke fejle stille, hvis denne ikke matcher hvad klienten er skrevet imod.

### 4.3 `GET /api/channels` og `GET /api/channels/{n}`

`n` er **1-baseret**: 1=kanal A, 2=kanal B. `n` udenfor `1..active_channels` giver `404`.

`GET /api/channels` returnerer et JSON-array af nøjagtigt samme objekt-form som `GET /api/channels/{n}`, ét pr. aktiv kanal:

```json
{
  "channel": 1,
  "enabled": true,
  "mode": "rs485",
  "baudrate": 9600,
  "parity": "none",
  "stop_bits": 1,
  "timeout_ms": 500,
  "inter_frame_delay_ms": 0,
  "status": "ok",
  "total_requests": 142,
  "successful_requests": 140,
  "timeout_errors": 2,
  "crc_errors": 0,
  "exception_errors": 0,
  "last_error_slave_id": 12,
  "last_error_address": 40010,
  "last_error_type": 1,
  "last_error_at_uptime_s": 86112
}
```
- `mode`: `"rs485"` eller `"rs232"` — **LÆSEVÆRDI** (hardware-revision 2026-09-14, se afsnit 4.4's boks): afspejler MODE_SEL-jumperens fysiske position, sat ved fremstilling, IKKE noget der sættes via `PUT`. `parity`: `"none"`/`"even"`/`"odd"`.
- `status`: `"disabled"` (enabled=false, uanset statistik) → `"error"` (seneste transaktion fejlede) → `"ok"`.
- Statistikken er **runtime-only** — nulstilles ved reboot, IKKE persisteret. Der findes intet reset-endpoint endnu (planlagt, ikke bygget — se afsnit 7).
- `last_error_type` er en `mb_error_code_t`-værdi — se afsnit 5.

### 4.4 `PUT /api/channels/{n}/config`

**Atomisk — ALLE felter er påkrævet i ét kald.** Mangler blot ét, eller er ét ugyldigt, afvises HELE requestet (`400`) uden nogen sideeffekt (uændret config).

Request-body (**IKKE** `mode` — se boksen nedenfor):
```json
{
  "enabled": true,
  "baudrate": 19200,
  "parity": "none",
  "stop_bits": 1,
  "timeout_ms": 500,
  "inter_frame_delay_ms": 0
}
```
- Gyldige baudrates: 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200.
- `stop_bits`: 1 eller 2. `timeout_ms`: > 0.
- Svaret er den nye config + statistikken (samme form som `GET /api/channels/{n}`), `200` ved succes.
- Anvendes LIVE med det samme (ingen reboot nødvendig) og persisteres til flash — overlever en genstart. En igangværende transaktion på kanalen fuldføres altid på den GAMLE config før omkobling.
- Sæt `enabled:false` for at deaktivere en kanal helt — Modbus TCP-forespørgsler til den kanal afvises derefter øjeblikkeligt med gateway-exception `0x0A` (afsnit 3.3), uden at røre UART'en.

**VIGTIGT — `mode` kan IKKE sættes via `PUT` (hardware-revision 2026-09-14, 2. ændring):** RS232/RS485-valget (MODE_SEL) er en fysisk jumper/strap på boardet, sat ÉN gang ved fremstilling — IKKE et firmware-/API-styret valg. `mode` indgår derfor ikke i `PUT`-bodyens felter; et evt. tilstedeværende `"mode"`-felt i requestet ignoreres stiltiende (kræves hverken til stede eller fraværende — atomik-kravet ovenfor gælder kun de felter der reelt er listet her). Den faktiske, hardware-udlæste `mode` ses i `GET`-svaret og i `GET /api/status`s `board_mode` — begge kanaler viser altid samme værdi, da de deler samme fysiske MODE_SEL-node (§2.0.1). Ønsker installatøren en anden RS232/RS485-mode, kræver det at flytte den fysiske jumper og genstarte boardet — det kan IKKE gøres fjernstyret via REST.

### 4.5 `POST /api/channels/{n}/read` — diagnostisk læsning

**Til ad-hoc test/fejlsøgning — IKKE beregnet til høj-frekvent drift** (brug Modbus TCP, afsnit 3, til det). Udfører ÉN synkron Modbus RTU-transaktion og returnerer resultatet direkte, uden at PLC'en selv skal åbne en Modbus TCP-forbindelse.

Request:
```json
{"function_code": 3, "slave_id": 9, "address": 0, "quantity": 2}
```
- `function_code`: 1 (Read Coils), 2 (Read Discrete Inputs), 3 (Read Holding Registers) eller 4 (Read Input Registers).
- `slave_id`: 1-247. `address`: 0-65535. `quantity`: 1-2000 (Modbus-spec'ens egne grænser pr. function code håndhæves af kanal-laget — en for stor `quantity` afvises som en kanal-fejl, ikke en synlig valideringsfejl her).

Svar ved succes (FC03/04 — 16-bit register-værdier):
```json
{"ok": true, "function_code": 3, "slave_id": 9, "address": 0, "quantity": 2, "values": [17942, 0]}
```
Svar ved succes (FC01/02 — bit-udpakkede 0/1-værdier, én pr. adresse):
```json
{"ok": true, "function_code": 1, "slave_id": 9, "address": 0, "quantity": 4, "values": [1, 0, 1, 0]}
```
Svar ved en Modbus-exception FRA SLAVEN (HTTP `200` — kaldet lykkedes, indholdet rapporterer fejlen):
```json
{"ok": false, "error": "modbus_exception", "slave_id": 9, "exception_code": 2}
```
Svar ved en kanal-/transportfejl (HTTP `502`):
```json
{"ok": false, "error_code": 1, "error": "channel_error", "message": "..."}
```

### 4.6 `POST /api/channels/{n}/write` — diagnostisk skrivning

Samme princip som `/read`. `function_code`: 5 (Write Single Coil), 6 (Write Single Register), 15 (Write Multiple Coils, v0.27.0) eller 16 (Write Multiple Registers).

FC05 (coil — boolsk):
```json
{"function_code": 5, "slave_id": 9, "address": 3, "value": true}
```
FC06 (ét register):
```json
{"function_code": 6, "slave_id": 9, "address": 10, "value": 1234}
```
FC15 (flere coils, maks 32 pr. kald — `values` er BOOLEANS, ikke tal, samme konvention som FC05's `value`):
```json
{"function_code": 15, "slave_id": 9, "address": 0, "values": [true, false, true, true]}
```
FC16 (flere registre, maks 32 pr. kald):
```json
{"function_code": 16, "slave_id": 9, "address": 0, "values": [1, 2, 3]}
```
Svar ved succes:
```json
{"ok": true, "function_code": 6, "slave_id": 9, "address": 10, "quantity": 1}
```
Exception-/fejl-svar: samme form som `/read` (afsnit 4.5).

**Bemærk:** ikke alle slave-devices understøtter alle function codes — nogle svarer med en ægte Modbus-exception (`Illegal Function`, kode 1), andre svarer slet ikke (giver en `502 channel_error` med `MB_TIMEOUT`). Begge er set og verificeret i praksis (se CHANGELOG.md v0.11.0).

### 4.7 OTA-firmwareopdatering (v0.30.0: bekræftelse + automatisk rollback)

**Den fulde PLC-side-implementeringsplan (forløb, relay-handler, web-UI, fejltekster, acceptkriterier) står i [PLC_OTA_INTEGRATION_PLAN.md](PLC_OTA_INTEGRATION_PLAN.md).** Kort API-reference:

- **`POST /api/ota`** — rå binær body (`.bin`-filen direkte, IKKE multipart — samme mønster som `curl --data-binary @firmware.bin`). **`Content-Length` påkrævet** (ellers `411 length_required` — chunked transfer-encoding understøttes ikke). Valgfri header **`X-Firmware-MD5: <32 hex>`** (v0.30.0) verificeres mod det skrevne image. Skrives chunket til den inaktive OTA-partition og verificeres. Svar ved succes:
  ```json
  {"ok": true, "message": "Firmware uploadet og verificeret - kald POST /api/reboot for at aktivere", "bytes": 896688, "new_version": "0.30.0-b0048", "md5_verified": true}
  ```
  Afvisninger (boardet forbliver ALTID på den kørende firmware): `400 ota_invalid_image` (ikke `0xE9`), `400 ota_wrong_firmware` (v0.30.0 — imaget mangler boardets identitets-markør `HVEXT-FWID:hypervisionplc-extension-board:<version>;`, fx PLC'ens egen firmware), `400 bad_md5`, `400 ota_begin_failed`, `409 ota_in_progress`, `409 ota_pending_confirm` (v0.30.0 — se nedenfor), `413 ota_too_large` (v0.30.0), `500 ota_failed`/`ota_verify_failed`.
- **`POST /api/reboot`** — **påkrævet efter en vellykket `/api/ota`** for at aktivere den nye firmware. Svarer `{"ok": true, "message": "Genstarter..."}` og genstarter ca. 500 ms senere.
- **Bekræftelse + rollback (v0.30.0):** første opstart af en ny firmware er **"afventer bekræftelse"**. PLC'en skal kalde **`POST /api/ota/confirm`** (når den har verificeret boardet) inden **600 s** — ellers, eller hvis den nye firmware crasher/genstarter inden, starter boardet igen på den forrige firmware. **En `POST /api/reboot` i dette vindue ruller derfor bevidst tilbage.** Mens der afventes bekræftelse, afvises nye uploads med `409 ota_pending_confirm` (de ville overskrive rollback-målet). `POST /api/ota/confirm` er idempotent: `{"ok":true,"confirmed":true|false,"running_version":"...","message":"..."}`; `500 ota_confirm_failed` = prøv igen.
- **`GET /api/ota/status`:**
  ```json
  {"state": "idle|in_progress|success|failed", "received": 0, "total": 0, "percent": 0, "error": "",
   "running_version": "0.30.0-b0048", "new_version": "", "pending_confirm": true, "confirm_remaining_s": 594,
   "last_update_rolled_back": false}
  ```
  `running_version` (inkl. build-nummer) er den autoritative kilde til "hvilken firmware kører" efter en opdatering. **Kan ikke aflæses mens et upload står på** — boardets HTTP-server har én arbejdstråd; PLC'en tæller selv fremdriften.
- Kun ÉT OTA-upload ad gangen.

### 4.8 `GET /api/capabilities` (v0.28.0)

Rent DEKLARATIVT — ingen bus-trafik, ingen sideeffekter, svarer øjeblikkeligt uanset om nogen slave er tilsluttet/online. Samme auth-regel som `/api/status`. Implementerer `DESIGN_GUIDE_MODBUS_EXPANSION_FC_CAPABILITIES.md`s §1 (fundet i dette repos rod — PLC-udviklingsteamets eget forslag).

```json
{
  "api_version": 1,
  "fw_version": "0.28.0",
  "modbus_tcp": {
    "supported_function_codes": [1, 2, 3, 4, 5, 6, 15, 16],
    "max_read_quantity": 2000,
    "max_write_quantity": 1968
  },
  "rest_diagnostic": {
    "supported_function_codes": [1, 2, 3, 4, 5, 6, 15, 16],
    "max_read_quantity": 2000,
    "max_write_quantity": 32
  }
}
```

**To separate lister** (`modbus_tcp` vs. `rest_diagnostic`) — de kan i princippet divergere (samme lektion som FC15/16-uoverensstemmelsen mellem lagene, se v0.27.1 i `CHANGELOG.md`). I dag understøtter begge lag identiske function codes, men rapporteres uafhængigt, så en fremtidig divergens ikke skjules bag én fælles liste.

**`max_read_quantity`/`max_write_quantity`:** for `modbus_tcp` er dette den BREDESTE Modbus-spec-grænse på tværs af de understøttede læse-/skrive-FC'er (fx læsning: FC01/02 tillader op til 2000, FC03/04 kun 125 — feltet rapporterer den bredeste, 2000; en klient der vil kende den PRÆCISE grænse for en given FC skal stadig kende Modbus-spec'en selv, dette felt er en overordnet deklaration, ikke en pr.-FC-tabel). For `rest_diagnostic` er tallene derimod REST-lagets egne, flade, håndhævede lofter (§4.5/§4.6) — ens for alle FC'er på det lag.

Se afsnit 6 punkt 2 nedenfor for hvordan dette anbefales brugt sammen med den periodiske `/api/status`-healthcheck.

---

## 5. Fejlkoder-reference

`mb_error_code_t`-værdier, brugt i `last_error_type` (afsnit 4.3) og `error_code` i `channel_error`-svar (afsnit 4.5/4.6):

| Værdi | Navn | Betydning |
|---|---|---|
| 0 | `MB_OK` | Ingen fejl |
| 1 | `MB_TIMEOUT` | Slaven svarede slet ikke indenfor kanalens `timeout_ms` |
| 2 | `MB_CRC_ERROR` | Svaret blev modtaget, men CRC'en stemte ikke |
| 3 | `MB_EXCEPTION` | (reserveret — Modbus-exceptions fra slaven relayes i dag som almindelige PDU'er, ikke via denne kode) |
| 4 | `MB_MAX_REQUESTS_EXCEEDED` | (reserveret, ikke i brug endnu) |
| 5 | `MB_NOT_ENABLED` | Kanalen er deaktiveret (`enabled:false`) |
| 6 | `MB_INVALID_SLAVE` | Svarets adresse-byte matchede ikke den forespurgte slave |
| 7 | `MB_INVALID_ADDRESS` | Ugyldig adresse/quantity/PDU-længde for en ELLERS kendt function code (v0.28.0: dækker IKKE længere "ukendt FC helt", se værdi 10) |
| 8 | `MB_BUS_BUSY` | Kanalens interne kø var fuld (en anden transaktion optog den) |
| 9 | `MB_CHANNEL_UNREACHABLE` | Generisk intern fejl (buffer for lille, e.l.) |
| 10 | `MB_UNSUPPORTED_FUNCTION` (v0.28.0) | Function code er HELT UKENDT af boardets gateway — se `GET /api/capabilities` (afsnit 4.8) for at forespørge dette PROAKTIVT, uden at skulle ramme denne fejl live først |

Modbus-standard exception-koder (fra slaven ELLER boardets egen gateway, se afsnit 3.3):

| Kode | Navn |
|---|---|
| 0x01 | Illegal Function — fra slaven selv, ELLER fra boardets egen gateway (v0.28.0) specifikt når function code er ukendt (`MB_UNSUPPORTED_FUNCTION`) |
| 0x02 | Illegal Data Address |
| 0x03 | Illegal Data Value |
| 0x04 | Slave Device Failure |
| 0x0A | Gateway Path Unavailable (KUN fra boardets egen gateway — deaktiveret/util-gaengelig kanal, IKKE længere "ukendt FC", se 0x01 ovenfor) |
| 0x0B | Gateway Target Device Failed to Respond (KUN fra boardets egen gateway) |

---

## 6. Anbefalet PLC-side-integrationsflow

1. **Registrering/opsætning** (én gang pr. board, af installatøren): provisionér boardet (afsnit 2), notér IP + management-token, indtast begge i PLC'ens System-side ("Modbus Expansion Boards"-kortet, §5.2). Kald samtidig `GET /api/capabilities` (afsnit 4.8) én gang og cache resultatet sammen med boardets `id` — ingen bus-trafik/sideeffekter, kan gøres proaktivt uden at "brænde" et rigtigt skriv til udstyret bag boardet.
2. **Sundhedstjek** (periodisk, lav frekvens — fx hvert 30.-60. sekund): `GET /api/status`. Timeout/forbindelsesfejl → markér boardet "unreachable" i UI'en. Tjek `api_version` matcher forventet. Sammenlign også `fw_version` mod den cachede capabilities-snapshots `fw_version` — ved mismatch (boardet er opdateret siden sidst) genforespørges `GET /api/capabilities`.
3. **Kanal-config-synk ved (gen)opstart af boardet** (opdaget via `GET /api/status`s manglende svar → fornyet svar, ELLER PLC'ens egen opstart): genskriv ALLE kanalers config via `PUT /api/channels/{n}/config` fra PLC'ens egen, gemte "sandhed" — boardet husker sin PERSISTEREDE config (den overlever reboot), men PLC-siden bør stadig eksplicit synkronisere efter enhver mistanke om boardets egen genstart, i tilfælde af at en administrator har ændret noget direkte på boardet (§4.2's designdokument-note).
4. **Normal drift (høj-frekvent):** Modbus TCP direkte (afsnit 3) — ÉN vedvarende forbindelse pr. kanal, genbrugt for alle transaktioner. IKKE REST-`/read`/`/write`.
5. **Ad-hoc diagnose/test fra UI'en** (fx en "test-forbindelse"-knap i PLC'ens web-UI): REST-`/read`/`/write` (afsnit 4.5/4.6) — lav frekvens, menneske-initieret.
6. **Fejlvisning:** kombinér `GET /api/channels/{n}`s statistik (afsnit 4.3) med Modbus TCP-lagets egne modtagne exceptions (afsnit 3.3) for et fuldt billede — statistikken viser TENDENSER (kanal X har mange timeouts), Modbus TCP-svarene viser DEN ENKELTE transaktions udfald.

---

## 7. Kendte begrænsninger lige nu

- **Kun 2 kanaler** (Variant A). Variant B (8 kanaler, SPI-expander) er designet (§2.2) men ikke bygget.
- **W5500-Ethernet (v0.13.0) er implementeret men IKKE hardware-verificeret** — kun boot-testet uden fysisk modul tilsluttet (fejler sikkert, resten af boardet upåvirket). Link/DHCP/faktisk dataoverførsel over Ethernet er endnu ikke testet.
- **RS232-mode er ikke hardware-testet** — kun RS485 er verificeret med et rigtigt device. `mode` kan IKKE sættes via REST (se nedenfor) — kræver at MODE_SEL-jumperen fysisk flyttes til GND, hvilket ikke er afprøvet endnu.
- **Ingen `POST /api/channels/{n}/reset-stats`/`POST /api/stats/reset`** — statistikken (afsnit 4.3) kan kun nulstilles ved reboot.
- **OTA-uploadet har intet "annullér"-endpoint** — det afbrydes ved at lukke forbindelsen (boardet kasserer da uploadet og bliver på den kørende firmware). Fremdriften kan ikke aflæses via `GET /api/ota/status` mens uploadet står på (én HTTP-arbejdstråd).
- **`active_channels` er altid 2, ikke auto-detekteret** — designdokumentets §2.2.2 (auto-detektion af bestykning) gælder kun Variant B.
- **RS232/RS485-mode er nu ÉN fabriksvalgt hardware-input for HELE boardet, ikke et PUT-bart felt** (hardware-revision 2026-09-14, §2.0.1) — kanal A og B kan ikke have forskellig mode, og mode kan ikke ændres via REST uden en fysisk jumper-omkobling. Se afsnit 4.4's boks om dette.
- Se [FEATURES.md](FEATURES.md)'s "Planlagte features" for den fulde, opdaterede liste over hvad der mangler.
