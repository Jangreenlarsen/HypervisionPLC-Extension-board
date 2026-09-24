# Implementeringsplan: firmwareopdatering (OTA) af Expansion Boards fra PLC'en

**Til:** PLC-udviklingsteamet (`Modbus_server_slave_ESP32` / HypervisionPLC-repoet)
**Fra:** HypervisionPLC Extension board-repoet, firmware **v0.30.0** (build 0047)
**Status på board-siden:** implementeret og afprøvet på fysisk hardware (se afsnit 9)

Dette dokument beskriver PRÆCIS hvad boardet gør under en firmwareopdatering, og hvad PLC'en skal implementere for at styre den. API-kontrakten i afsnit 3 er den autoritative — den er også optaget i [PLC_INTEGRATION_MANUAL.md](PLC_INTEGRATION_MANUAL.md) §4.7.

---

## 1. Mål og rammer

- Installatøren opdaterer et Expansion Boards firmware **fra PLC'ens web-UI** (I/O-fanen → "Modbus Expansion Boards"), uden USB og uden direkte netværksadgang til boardet.
- **Kun PLC'en taler med boardet.** Browseren uploader `.bin`-filen til PLC'en, som streamer den videre til boardet. Browseren ser aldrig boardets IP-port 8080 eller management-tokenet (tokenet forlader aldrig PLC'en, jf. SECURITY_INDEX).
- **Et board må aldrig ende "mursten" på grund af en opdatering.** Boardet har derfor selv et sikkerhedsnet (afsnit 2): forkerte filer afvises, og en ny firmware der ikke bliver bekræftet af PLC'en, rulles automatisk tilbage.

> **Bemærk om firewall:** boardets `plc ip`-regel begrænser i dag KUN Modbus TCP (port 502/503). REST-API'et på port 8080 (inkl. OTA) er beskyttet af token/login, ikke af IP. Hvis kun PLC'en skal kunne nå port 8080, kræver det en netværksfirewall — eller en fremtidig board-ændring (ikke en del af denne plan).

---

## 2. Boardets sikkerhedsnet (allerede implementeret)

| Beskyttelse | Hvad boardet gør | Hvad PLC'en får at se |
|---|---|---|
| Forkert filtype | Første byte skal være `0xE9` (ESP32-image) | `400 ota_invalid_image` |
| For stor fil | Afvises før noget skrives (fx PLC'ens egen, større firmware) | `413 ota_too_large` |
| Forkert firmware | Imaget skal indeholde boardets identitets-markør `HVEXT-FWID:hypervisionplc-extension-board:<version>;` | `400 ota_wrong_firmware` |
| Beskadiget overførsel | ESP-IDF's indbyggede image-checksum/SHA-256 + valgfri `X-Firmware-MD5` | `500 ota_verify_failed` |
| Firmware der ikke virker | Første opstart efter OTA "afventer bekræftelse". Bekræftes den ikke inden **600 s**, eller crasher/genstarter den inden, starter boardet igen på den forrige firmware | `pending_confirm`, `last_update_rolled_back` i `GET /api/ota/status` |
| Opdatering oven i en ubekræftet | Nyt upload afvises mens der afventes bekræftelse (ville overskrive rollback-målet) | `409 ota_pending_confirm` |
| Samtidige uploads | Kun ét ad gangen | `409 ota_in_progress` |

Et mislykket upload efterlader ALTID boardet på den firmware det allerede kører — uploadet skrives til den inaktive partition og aktiveres først ved en eksplicit reboot.

---

## 3. Boardets API (kontrakten PLC'en implementerer imod)

Alle kald: `http://<board-ip>:8080`, header `Authorization: Bearer <token>`. Manglende/forkert auth → `401`. Alle svar er JSON; fejl har formen `{"ok":false,"error":"<kode>","message":"<tekst til brugeren>"}` — **vis altid `message` til brugeren**, ikke en generisk tekst.

### 3.1 `POST /api/ota` — upload

- **Body:** den rå `.bin`-fil (IKKE multipart), `Content-Type: application/octet-stream`.
- **`Content-Length` er påkrævet** (boardets HTTP-server understøtter ikke chunked transfer-encoding) → ellers `411 length_required`.
- **Valgfri header `X-Firmware-MD5: <32 hex-tegn>`** — MD5 af hele filen. Anbefales (afsnit 5.3). Ugyldigt format → `400 bad_md5`; forkert værdi → `500 ota_verify_failed` ("MD5 Check Failed").
- Boardet svarer først **efter** sidste byte er skrevet og verificeret (ca. 1-2 s ekstra). Målt: 896 KB på 7,6 s over Ethernet.
- **Succes (200):**
  ```json
  {"ok":true,"message":"Firmware uploadet og verificeret - kald POST /api/reboot for at aktivere",
   "bytes":896688,"new_version":"0.30.0-b0048","md5_verified":true}
  ```
- **Fejl:** `400 ota_invalid_image | ota_wrong_firmware | bad_md5 | ota_begin_failed`, `409 ota_in_progress | ota_pending_confirm`, `411 length_required`, `413 ota_too_large`, `500 ota_failed | ota_verify_failed`.

### 3.2 `POST /api/reboot` — aktivér

`{"ok":true,"message":"Genstarter..."}`, genstarter ca. 500 ms senere. Boardet er typisk tilbage på netværket efter 5-10 s (Ethernet/DHCP).

> ⚠️ **En reboot MENS der afventes bekræftelse = rollback.** Bootloaderen behandler en ubekræftet firmware der genstarter som fejlet. Det er bevidst (og kan bruges som "rul tilbage nu", afsnit 4 trin 7) — men PLC'en må aldrig sende en reboot i det vindue ved et uheld.

### 3.3 `GET /api/ota/status`

```json
{"state":"idle","received":0,"total":0,"percent":0,"error":"",
 "running_version":"0.30.0-b0048","new_version":"",
 "pending_confirm":true,"confirm_remaining_s":594,"last_update_rolled_back":false}
```

| Felt | Betydning |
|---|---|
| `state`, `received`, `total`, `percent`, `error` | Resultatet af SENESTE upload siden boardets opstart (`idle` efter en genstart) |
| `running_version` | Den KØRENDE firmwares identitet, `MAJOR.MINOR.PATCH-bNNNN` |
| `new_version` | Version i det senest uploadede (endnu ikke aktiverede) image, ellers `""` |
| `pending_confirm` | `true` = første opstart efter OTA, afventer `POST /api/ota/confirm` |
| `confirm_remaining_s` | Sekunder til automatisk rollback (0 når ikke `pending_confirm`) |
| `last_update_rolled_back` | `true` = seneste OTA blev rullet tilbage (ubekræftet eller crash) |

> **Brug `running_version` fra dette endpoint** til at afgøre om opdateringen er gennemført — ikke `fw_version` fra `/api/status` (den mangler build-nummeret, og to builds af samme version kan ikke skelnes).
>
> **Status kan IKKE aflæses mens et upload står på:** boardets HTTP-server har én arbejdstråd, som er optaget af uploadet. Fremdriften skal PLC'en selv tælle (antal bytes videresendt), se afsnit 5.1.

### 3.4 `POST /api/ota/confirm` — bekræft

- Bekræftet: `{"ok":true,"confirmed":true,"running_version":"0.30.0-b0048","message":"Firmware bekraeftet - rollback annulleret"}`
- Intet at bekræfte (allerede bekræftet): `200` med `"confirmed":false` — **idempotent, må altid kaldes.**
- Fejl: `500 ota_confirm_failed` — firmwaren afventer stadig; prøv igen.

---

## 4. Det samlede forløb (sekvens)

```
Browser                         PLC                                   Board
   |  vælg .bin                  |                                      |
   |  (JS: tjek 0xE9 + markør,   |                                      |
   |   udtræk version, MD5)      |                                      |
   |  bekræftelsesdialog         |                                      |
1  |-- POST /api/expansion/boards/{id}/ota (rå body, X-Firmware-MD5) -->|
   |                             |-- POST /api/ota (stream, samme MD5) ->|  skriver til inaktiv partition
   |   (XHR upload-progress)     |<------ 200 {new_version} ------------|  verificerer
   |<-- 200 {new_version} -------|                                      |
2  |-- POST .../{id}/reboot ---->|-- POST /api/reboot ------------------>|  genstarter
3  |   poll .../{id}/ota-status  |-- GET /api/ota/status (hver 3. s) --->|  (utilgængelig 5-15 s)
   |                             |<-- running_version=new, pending ------|  afventer bekræftelse (600 s)
4  |                             |-- GET /api/status + /api/channels --->|  PLC'ens sundhedstjek
5  |                             |-- POST /api/ota/confirm ------------->|  gyldig — rollback annulleret
   |<-- "Opdateret til X" -------|                                      |
6  |                             |-- GET /api/capabilities, genskriv kanal-config (manual §6.2-6.3)
```

**Trin 3 — afgørelse af udfaldet** (poll hver 3. s, maks. 90 s):

| Observation | Betydning | Handling |
|---|---|---|
| `running_version == new_version` og `pending_confirm:true` | Ny firmware kører | → trin 4 |
| `running_version == gammel version` og `last_update_rolled_back:true` | Ny firmware crashede ved opstart; bootloaderen rullede tilbage | Vis fejl: "Opdateringen fejlede — boardet kører fortsat X". STOP. |
| `running_version == new_version` og `pending_confirm:false` | Allerede bekræftet (fx af en anden PLC-session/CLI) | Vis succes |
| Intet svar i 90 s | Boardet kommer ikke på nettet med den nye firmware | Vis: "Boardet svarer ikke. Det ruller selv tilbage inden 10 minutter — prøv at læse status igen derefter." |

**Trin 4 — PLC'ens sundhedstjek før bekræftelse** (minimum): `GET /api/status` svarer 200 med forventet `api_version`, og `GET /api/channels` svarer 200. Fejler det → bekræft IKKE (trin 7).

**Trin 7 (kun ved fejl i trin 4) — "Rul tilbage nu":** `POST /api/reboot` mens `pending_confirm:true` → boardet starter på den forrige firmware. Poll derefter `GET /api/ota/status` og vis `running_version` + `last_update_rolled_back:true`.

---

## 5. Hvad PLC'en skal implementere

### 5.1 Nyt endpoint: `POST /api/expansion/boards/{id}/ota` (streaming-relay) — `src/api_handlers.cpp`

Synkron handler i PLC'ens httpd-task — **samme mønster som PLC'ens egen `api_handler_ota_upload()`** (chunk-buffer på heap, `httpd_req_recv`-løkke). Filen gemmes IKKE på PLC'en; hver bid skrives straks videre til boardet.

```
1. PLC-auth (samme krav som de øvrige /api/expansion/*-endpoints).
2. Slå board {id} op; ukendt/ikke-konfigureret → 404.
3. expansion_api_is_busy() → 409 "Et andet expansion-board-kald er i gang".
   Tag "in progress"-slottet for hele relay'et (boardet kan alligevel ikke
   svare på andet imens, jf. afsnit 3.3).
4. req->content_len == 0 → 411. (Boardet afviser selv for store filer med 413.)
5. Læs valgfri X-Firmware-MD5 fra browserens request (videresendes uændret).
6. WiFiClient c; c.connect(board_ip, 8080) med 3 s timeout; c.setTimeout(60 s).
   Fejl → 502 {"ok":false,"error":"board_unreachable","message":"..."}.
7. Skriv request-header til boardet:
     POST /api/ota HTTP/1.1\r\n
     Host: <board_ip>\r\n
     Authorization: Bearer <token>\r\n
     Content-Type: application/octet-stream\r\n
     Content-Length: <content_len>\r\n
     [X-Firmware-MD5: <md5>\r\n]
     Connection: close\r\n\r\n
8. Løkke til content_len bytes er videresendt:
     n = httpd_req_recv(req, buf, 2048)   (HTTPD_SOCK_ERR_TIMEOUT → prøv igen)
     n <= 0 → browseren forsvandt: c.stop() og afbryd (boardet ser en
              afbrudt forbindelse, kasserer uploadet og bliver på den
              gamle firmware)
     c.write(buf, n) skal returnere n — ellers c.stop(), 502.
9. Læs boardets svar (vent op til 30 s — boardet verificerer efter sidste
   byte): statuslinjen giver HTTP-status, body er JSON.
10. Returnér boardets HTTP-status + JSON-body UÆNDRET til browseren.
11. Log hændelsen (board, version, resultat) i PLC'ens egen log.
```

Brugeren skal forvente at PLC'ens web-UI er optaget under relay'et (10-60 s), præcis som ved PLC'ens egen firmwareopdatering. Boardets Modbus-kanaler kører videre under uploadet, men enkelte transaktioner kan blive forsinket mens der skrives til flash (ikke målt) — planlæg derfor opdateringer uden for kritisk drift. Under selve genstarten (5-15 s) er boardets kanaler utilgængelige.

### 5.2 Nye async-kald i `src/expansion_api_client.cpp`

Samme mønster som de eksisterende `expansion_api_start_*()` (baggrundstask + `GET /api/expansion/action-status`):

| Funktion | Boardkald | PLC-endpoint (forslag) |
|---|---|---|
| `expansion_api_start_ota_status(i)` | `GET /api/ota/status` | `POST /api/expansion/boards/{id}/ota-status` |
| `expansion_api_start_ota_confirm(i)` | `POST /api/ota/confirm` | `POST /api/expansion/boards/{id}/ota-confirm` |
| `expansion_api_start_reboot(i)` | `POST /api/reboot` | `POST /api/expansion/boards/{id}/reboot` |

Behold `http.setTimeout(3000)` — under trin 3's polling er "ingen forbindelse" det FORVENTEDE svar i 5-15 s og skal ikke vises som fejl, kun som "venter på at boardet starter…".

### 5.3 Web-UI (`web/io.html`, "Modbus Expansion Boards")

Pr. board:
- Vis `running_version` og en advarsel hvis `last_update_rolled_back:true` eller `pending_confirm:true` (med `confirm_remaining_s` og knapperne **Bekræft** / **Rul tilbage nu**).
- Knap **"Opdatér firmware…"** → filvælger (`.bin`).

Når en fil er valgt — FØR noget sendes:
1. Tjek `bytes[0] === 0xE9`, ellers "Ikke en firmware-fil".
2. Søg filen efter `HVEXT-FWID:hypervisionplc-extension-board:` og læs versionen op til `;`. Findes den ikke: "Dette er ikke en Expansion Board-firmware" (fx PLC'ens egen `.bin`). Boardet tjekker det samme autoritativt — dette er kun for hurtig, tydelig feedback.
3. Bekræftelsesdialog: "Opdatér board *navn* fra *running_version* til *ny version*? Boardets Modbus-kanaler er utilgængelige i ca. 15 s under genstart."
4. Beregn MD5 af filen i browseren (fx en lille indlejret MD5-funktion — WebCrypto understøtter ikke MD5) og send den som `X-Firmware-MD5`. Så dækker integritetstjekket hele vejen browser → PLC → board.
5. Upload med `XMLHttpRequest` (ikke `fetch`) — `xhr.upload.onprogress` giver en ægte fremdriftsbjælke, fordi PLC'en videresender i samme takt som den modtager.
6. Kør trin 2-6 fra afsnit 4 automatisk, med en tekst pr. trin: "Uploader… 43 %" → "Genstarter boardet…" → "Venter på boardet…" → "Kontrollerer…" → "Bekræfter…" → "Opdateret til 0.30.0-b0048 ✓".
7. Vis ALTID boardets `message` ved fejl.

### 5.4 (Valgfrit) CLI på PLC'en — `cli_commands_modbus_expansion.cpp`

`expansion ota status <board>`, `expansion ota confirm <board>`, `expansion reboot <board>` — nyttigt til fejlsøgning. Upload af selve filen sker kun via web-UI'et.

---

## 6. Tidskonstanter

| Konstant | Værdi | Hvor |
|---|---|---|
| Bekræftelses-deadline | 600 s fra boardets opstart | board (`kOtaConfirmTimeoutS`) |
| PLC's poll efter reboot | hver 3 s, maks. 90 s | PLC |
| Relay socket-timeout | 60 s pr. læs/skriv, 30 s på boardets slutsvar | PLC |
| Maks. filstørrelse | OTA-partitionens størrelse (i dag 1 310 720 bytes) — boardet svarer 413 | board |

---

## 7. Fejlsituationer og hvad brugeren skal se

| Situation | Board-svar | Tekst til brugeren (forslag) |
|---|---|---|
| PLC'ens egen `.bin` valgt | (fanget i browseren) / `413`/`400 ota_wrong_firmware` | "Dette er ikke en Expansion Board-firmware." |
| Afbrudt upload (netværk, browser lukket) | forbindelse afbrudt | "Upload afbrudt — boardet kører uændret videre på *version*." |
| MD5 matcher ikke | `500 ota_verify_failed` | "Filen blev beskadiget under overførslen — prøv igen." |
| Boardet afventer allerede bekræftelse | `409 ota_pending_confirm` | "Boardet har en ubekræftet opdatering — bekræft eller rul tilbage først." |
| Ny firmware crasher ved opstart | trin 3: gammel `running_version`, `last_update_rolled_back:true` | "Opdateringen fejlede, boardet rullede selv tilbage til *version*." |
| PLC'ens sundhedstjek fejler | — | "Den nye firmware svarer ikke korrekt — ruller tilbage." (trin 7) |
| PLC genstartes midt i forløbet | boardet ruller selv tilbage efter 600 s | Ved næste visning: advarsel pga. `last_update_rolled_back:true` |

---

## 8. Acceptkriterier for PLC-implementeringen

1. En gyldig board-firmware kan uploades fra web-UI'et; boardet ender på den nye version med `pending_confirm:false` — uden nogen manuel handling ud over at vælge filen og bekræfte dialogen.
2. PLC'ens egen `.bin` afvises med en forståelig tekst, og boardet er upåvirket.
3. Lukkes browseren midt i uploadet, kører boardet uændret videre på den gamle version.
4. Hvis PLC'en ikke bekræfter (fx trin 4 slås bevidst fra i en test), kører boardet efter ~10 minutter igen den gamle version, og UI'et viser rollback-advarslen.
5. "Rul tilbage nu" under `pending_confirm` bringer boardet tilbage på den gamle version.
6. Tokenet optræder aldrig i noget svar til browseren eller i PLC'ens log.
7. Fejlbeskeder viser boardets `message`.

---

## 9. Board-siden: afprøvet på fysisk hardware (v0.30.0)

Udført mod et rigtigt board over Ethernet med `curl` (samme HTTP-kald PLC'en skal lave):

| Test | Resultat |
|---|---|
| OTA 0.29.1 → 0.30.0, reboot | Kørte 0.30.0, `pending_confirm:true`, 594 s tilbage |
| Upload mens `pending_confirm` | `409 ota_pending_confirm` |
| `POST /api/ota/confirm` ×2 | 1.: `confirmed:true`; 2.: `confirmed:false` (idempotent) |
| PLC'ens egen firmware (1 808 736 bytes) | `413 ota_too_large` — afvist før noget skrives |
| Gyldigt ESP32-image med ødelagt identitets-markør | `400 ota_wrong_firmware` |
| Ugyldig / forkert `X-Firmware-MD5` | `400 bad_md5` / `500 ota_verify_failed` |
| Chunked upload uden `Content-Length` | `411 length_required` |
| Gyldig upload med korrekt MD5 | `200`, `new_version` + `md5_verified:true` |
| Ingen bekræftelse efter reboot | Rollback præcis ved deadline: kørte `0.30.0-b0047` igen ~14 s senere, `last_update_rolled_back:true` |
| `POST /api/reboot` mens `pending_confirm` | Øjeblikkelig rollback til forrige version, `last_update_rolled_back:true` |
| Endelig v0.30.0 via OTA m. MD5 → reboot → confirm | `md5_verified:true`, `pending_confirm:false`, `last_update_rolled_back:false` |

**Ikke afprøvet på hardware:** CLI-delen (`ota confirm`, OTA-linjerne i `status`) — kun dækket af native-tests (seriel port var optaget under testen).
