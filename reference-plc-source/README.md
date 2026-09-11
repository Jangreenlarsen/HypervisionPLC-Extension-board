# Reference-kopier fra Hypervision PLC-repoet

Disse filer er **statiske kopier**, ikke en live-forbindelse til `Modbus_server_slave_ESP32`-repoet. De er hentet fra commit `998aa77` (2026-09-08) og vil ikke automatisk følge senere ændringer i PLC-repoet.

De er inkluderet fordi `EXPANSION_BOARD_DESIGN.md` (projektets rod) eksplicit peger på dem som implementeringsskabeloner — se dokumentets Appendiks A for hvad hver enkelt fil konkret skal bruges som reference for.

| Fil | Bruges som reference for |
|---|---|
| `src/modbus_master.cpp` | Modbus RTU PDU/CRC/framing — genbrugelig for både RTU-siden og TCP-gateway-laget |
| `src/mb_async.cpp` | Kø/cache/adaptiv-backoff-designet (§5.1.1 i designdokumentet) |
| `include/types.h` | `mb_error_code_t`, `modbus_master_config_t` — datastrukturer API-kontrakten er designet til at matche |
| `include/constants.h` | `CONFIG_SCHEMA_VERSION`-mønster og relaterede konstanter |
| `src/api_handlers.cpp` | PLC'ens REST-API-stil (auth-mønster, JSON-fejl-format) — master-reference for expansion-boardets management-API |
| `src/ota_handler.cpp` | Dual-partition OTA-mønster (rå binær body, verifikation før aktivering) |
| `src/config_load.cpp` | NVS schema-migrationsmønster |
| `src/watchdog_monitor.cpp` | Reset-årsag-persistering og software-watchdog-mønster |
| `web/system.html` | Det kort-baserede UI-mønster det nye "Modbus Expansion Boards"-kort skal følge |
| `BUGS_INDEX.md` | Søg "FEAT-408" for den fulde forhistorie/root-cause-analyse designet bygger på |

**Hvis noget her virker forældet eller uklart** i forhold til den aktuelle PLC-kodebase, er det fordi disse er øjebliksbilleder — spørg Jan om adgang til den friske `Modbus_server_slave_ESP32`-repo hvis en fil skal genbekræftes.
