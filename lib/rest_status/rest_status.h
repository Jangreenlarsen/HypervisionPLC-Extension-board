#pragma once

#include <cstddef>
#include <cstdint>

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

  // Valgfri Ethernet (W5500, §1.3/§2.2) — dual-stack med WiFi, ingen egen
  // provisionering (ren DHCP). `eth_connected` = link op (kabel + PHY-link),
  // IKKE nødvendigvis en IP endnu.
  bool eth_connected;
  const char *eth_ip;       // ignoreres hvis !eth_connected, eller tom streng hvis link op men endnu ingen DHCP-lease
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
