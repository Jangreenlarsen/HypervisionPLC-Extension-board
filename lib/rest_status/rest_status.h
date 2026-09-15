#pragma once

#include <cstddef>
#include <cstdint>

#include "board_config.h"  // mb_channel_mode_t

// API-versionering (kontrakten, ikke firmware-versionen) — bumpes KUN ved et
// brydende skift i selve endpoint-kontrakten, jf. EXPANSION_BOARD_DESIGN.md
// §4.2. Aldrig ved en ren tilføjelse af et nyt, valgfrit felt.
constexpr uint16_t MB_REST_API_VERSION = 1;

// Rene værdier `GET /api/status` skal rapportere — ingen hardware-kald heri,
// kaldstedet (src/http_server.cpp) indsamler dem (uptime, heap, WiFi...) og
// overfører dem hertil, så selve JSON-formateringen er native-testbar.
struct mb_status_data_t {
  const char *fw_version;   // fra FW_VERSION (extract_version.py) — "(ukendt)" hvis ikke bygget med den
  const char *fw_build;
  uint32_t uptime_s;
  uint32_t heap_free_bytes;
  uint8_t active_channels;  // fast 2 for Variant A (§2.0) — ingen auto-detektion at rapportere
  bool wifi_connected;
  const char *wifi_ip;      // ignoreres hvis !wifi_connected
  int8_t wifi_rssi_dbm;     // ignoreres hvis !wifi_connected
  bool provisioned;

  // Hardware-revision 2026-09-14 (§2.0.1): MODE_SEL er ÉN delt GPIO for hele
  // boardet — RS232/RS485 er derfor reelt en BOARD-egenskab, ikke en
  // pr.-kanal-egenskab (selvom `GET /api/channels/{n}` stadig rapporterer
  // den pr. kanal, for bagudkompatibilitet — begge kanaler er altid ens).
  // Direkte, eksplicit rapporteret her, så en klient ikke skal udlede den
  // indirekte via en tilfældig kanals `mode`-felt.
  mb_channel_mode_t board_mode;

  // Valgfri Ethernet (W5500, §1.3/§2.2) — dual-stack med WiFi, ingen egen
  // provisionering (ren DHCP). `eth_connected` = link op (kabel + PHY-link),
  // IKKE nødvendigvis en IP endnu.
  bool eth_connected;
  const char *eth_ip;       // ignoreres hvis !eth_connected, eller tom streng hvis link op men endnu ingen DHCP-lease

  // v0.18.0 (Jan: diag for om W5500 reelt virker eller om det "bare" er en
  // link-fejl) — én af "not_detected"/"link_down"/"waiting_dhcp"/"connected"
  // (se eth_driver_status_string() i src/eth_driver.cpp, den autoritative
  // kilde til strengene — duplikeres bevidst ikke her, kaldstedet
  // http_server.cpp overfører blot pointeren). ALDRIG nullptr.
  const char *eth_status;
};

// Bygger `GET /api/status`-JSON-svaret (EXPANSION_BOARD_DESIGN.md §4.2's
// eksempel-skema, udvidet med wifi/provisioned-felter). Returnerer antal
// skrevne bytes (ekskl. terminerende '\0'), eller 0 hvis `out_capacity` er
// for lille.
size_t mb_status_build_json(const mb_status_data_t *data, char *out, size_t out_capacity);

// Bygger et REST-fejlsvar i samme stil som PLC-repoets REST-API (§4.2):
// { "ok": false, "error": "<error>", "message": "<message>" }
// `error_code` er valgfri (< 0 for at udelade feltet) — kun Modbus-relaterede
// fejl (mb_error_code_t, §4) har et meningsfuldt error_code; rene HTTP-/
// auth-lags-fejl (fx 401) har det ikke, og feltet udelades da bevidst i
// stedet for at tvinge en mb_error_code_t-værdi der ikke passer.
size_t mb_status_build_error_json(int error_code, const char *error, const char *message, char *out,
                                   size_t out_capacity);

// v0.28.0 (DESIGN_GUIDE_MODBUS_EXPANSION_FC_CAPABILITIES.md §1) — rene
// værdier `GET /api/capabilities` skal rapportere. To SEPARATE FC-lister
// (modbus_tcp vs. rest_diagnostic), bevidst — de kan i princippet divergere
// (designdokumentets egen begrundelse: netop den slags "de to lag drifter
// fra hinanden"-fejl har allerede ramt PLC-siden én gang for FC15/16).
// Begge peger i dag på samme underliggende liste (lib/modbus_pdu's
// MB_PDU_SUPPORTED_FUNCTIONS) fra kaldstedet (src/http_server.cpp), men
// JSON-strukturen holder dem uafhængige.
struct mb_capabilities_data_t {
  const char *fw_version;

  const uint8_t *modbus_tcp_fcs;
  size_t modbus_tcp_fc_count;
  uint16_t modbus_tcp_max_read_quantity;
  uint16_t modbus_tcp_max_write_quantity;

  const uint8_t *rest_diagnostic_fcs;
  size_t rest_diagnostic_fc_count;
  uint16_t rest_diagnostic_max_read_quantity;
  uint16_t rest_diagnostic_max_write_quantity;
};

// Bygger `GET /api/capabilities`-JSON'en (se DESIGN_GUIDE_MODBUS_EXPANSION_
// FC_CAPABILITIES.md §1 for det fulde skema/eksempel). Rent deklarativt —
// INGEN bus-trafik, INGEN sideeffekter, svarer identisk uanset om nogen
// slave er tilsluttet/online. Returnerer 0 hvis `out_capacity` er for lille.
size_t mb_status_build_capabilities_json(const mb_capabilities_data_t *data, char *out, size_t out_capacity);
