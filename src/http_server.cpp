#include "http_server.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_http_server.h>

#include <cstdlib>
#include <cstring>

#include "config.h"
#include "diagnostic_modbus.h"
#include "modbus_channel.h"
#include "rest_auth.h"
#include "rest_status.h"

namespace {

httpd_handle_t g_server = nullptr;

void send_json_error(httpd_req_t *req, const char *http_status, int error_code, const char *error,
                      const char *message) {
  char body[192];
  const size_t len = mb_status_build_error_json(error_code, error, message, body, sizeof(body));
  httpd_resp_set_status(req, http_status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, body, len > 0 ? len : HTTPD_RESP_USE_STRLEN);
}

bool channel_id_from_number(int n, ModbusChannelId *out) {
  if (n == 1) {
    *out = ModbusChannelId::kA;
    return true;
  }
  if (n == 2) {
    *out = ModbusChannelId::kB;
    return true;
  }
  return false;
}

// Udtrækker kanal-nummeret (1-baseret, §4.2) og om stien slutter på
// "/config" fra en `/api/channels...`-sti. ESP-IDF's httpd har ingen
// indbygget path-parameter-udtrækning — kun wildcard-MATCHING (`*` i
// registreringen) — så selve parsingen af `{n}` sker her, i selve handleren.
// `*out_channel_number == -1` betyder "/api/channels" uden suffiks (listen).
bool parse_channels_uri(const char *uri, int *out_channel_number, bool *out_has_config_suffix) {
  static const char kPrefix[] = "/api/channels";
  const size_t prefix_len = sizeof(kPrefix) - 1;
  if (strncmp(uri, kPrefix, prefix_len) != 0) {
    return false;
  }
  const char *rest = uri + prefix_len;

  if (*rest == '\0' || *rest == '?') {
    *out_channel_number = -1;
    *out_has_config_suffix = false;
    return true;
  }
  if (*rest != '/') {
    return false;
  }
  rest++;

  char *end = nullptr;
  const long n = strtol(rest, &end, 10);
  if (end == rest) {
    return false;  // intet tal umiddelbart efter "/api/channels/"
  }
  *out_channel_number = static_cast<int>(n);
  rest = end;

  if (*rest == '\0' || *rest == '?') {
    *out_has_config_suffix = false;
    return true;
  }
  if (strncmp(rest, "/config", 7) == 0 && (rest[7] == '\0' || rest[7] == '?')) {
    *out_has_config_suffix = true;
    return true;
  }
  return false;  // ukendt trailing-sti
}

// Samme grundmønster som parse_channels_uri(), men for de simple
// "/api/channels/{n}/<action>"-stier (§4.2's diagnostiske read/write) hvor
// kun ÉN bestemt handling er relevant — undgår at overbelaste
// parse_channels_uri()'s enum-lignende out-parametre med endnu en variant.
bool parse_channel_number_with_suffix(const char *uri, const char *expected_suffix, int *out_channel_number) {
  static const char kPrefix[] = "/api/channels/";
  const size_t prefix_len = sizeof(kPrefix) - 1;
  if (strncmp(uri, kPrefix, prefix_len) != 0) {
    return false;
  }
  const char *rest = uri + prefix_len;

  char *end = nullptr;
  const long n = strtol(rest, &end, 10);
  if (end == rest) {
    return false;
  }
  rest = end;

  const size_t suffix_len = strlen(expected_suffix);
  if (strncmp(rest, expected_suffix, suffix_len) != 0) {
    return false;
  }
  const char after = rest[suffix_len];
  if (after != '\0' && after != '?') {
    return false;
  }
  *out_channel_number = static_cast<int>(n);
  return true;
}

// §4.3: allowlisten dækker KUN data-plan-portene (502-509/502-503) — ALDRIG
// management-API-porten selv (her). Se firewall.cpp (Fase 5, resten) for
// hvorfor: et fejlkonfigureret allowlist-kald må aldrig kunne spærre PLC'en
// ude fra det ENESTE sted den kan rette fejlen igen.

// Udtrækker "Authorization"-headeren og validerer den mod aktuel config
// (Bearer-token ELLER Basic Auth, §4.4). Sender selv et 401-JSON-svar og
// returnerer false hvis uautoriseret — kaldstedet skal da returnere uden at
// gøre mere.
bool require_auth(httpd_req_t *req) {
  char auth_header[160];
  const size_t len = httpd_req_get_hdr_value_len(req, "Authorization");
  if (len == 0 || len >= sizeof(auth_header) ||
      httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header)) != ESP_OK) {
    auth_header[0] = '\0';
  }

  const mb_board_config_t &cfg = config_get();
  const mb_rest_credentials_t creds = {
      cfg.mgmt_token, cfg.has_mgmt_token, cfg.rest_user, cfg.rest_pass,
      cfg.has_rest_user && cfg.has_rest_pass, cfg.rest_auth_mode,
  };

  const mb_rest_auth_result_t auth_result = mb_rest_auth_check(auth_header, &creds);
  if (auth_result == MB_REST_AUTH_OK) {
    return true;
  }

  const char *message = (auth_result == MB_REST_AUTH_METHOD_DISABLED)
                            ? "Denne auth-metode er slaaet fra (se 'rest auth' i den serielle CLI)"
                            : "Manglende eller ugyldig Authorization-header";
  char body[160];
  const size_t body_len = mb_status_build_error_json(-1, "unauthorized", message, body, sizeof(body));
  httpd_resp_set_status(req, "401 Unauthorized");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, body, body_len > 0 ? body_len : HTTPD_RESP_USE_STRLEN);
  return false;
}

esp_err_t status_handler(httpd_req_t *req) {
  if (!require_auth(req)) return ESP_OK;

  const mb_board_config_t &cfg = config_get();
  const bool connected = WiFi.status() == WL_CONNECTED;
  IPAddress ip = connected ? WiFi.localIP() : IPAddress();
  String ip_str = ip.toString();

  const mb_status_data_t data = {
#ifdef FW_VERSION
      FW_VERSION, FW_BUILD,
#else
      "ukendt", "ukendt",
#endif
      static_cast<uint32_t>(millis() / 1000),
      ESP.getFreeHeap(),
      2,  // active_channels — fast for Variant A, §2.0
      connected,
      ip_str.c_str(),
      connected ? static_cast<int8_t>(WiFi.RSSI()) : 0,
      cfg.provisioned,
  };

  char body[384];
  const size_t body_len = mb_status_build_json(&data, body, sizeof(body));

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, body, body_len > 0 ? body_len : HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// GET /api/channels og GET /api/channels/{n} (§4.2) — begge håndteres her
// via wildcard-registreringen, adskilt ved fraværet/tilstedeværelsen af et
// kanal-nummer i stien (parse_channels_uri()).
esp_err_t channels_get_handler(httpd_req_t *req) {
  if (!require_auth(req)) return ESP_OK;

  int channel_number = -1;
  bool has_config_suffix = false;
  if (!parse_channels_uri(req->uri, &channel_number, &has_config_suffix) || has_config_suffix) {
    send_json_error(req, "404 Not Found", -1, "not_found", "Ukendt sti");
    return ESP_OK;
  }

  if (channel_number == -1) {
    char body[1024];
    size_t offset = 0;
    body[offset++] = '[';
    for (int n = 1; n <= static_cast<int>(MB_CHANNEL_COUNT); n++) {
      ModbusChannelId id;
      channel_id_from_number(n, &id);  // n er altid 1..MB_CHANNEL_COUNT her
      const mb_channel_config_t cfg = modbus_channel_get_config(id);
      const mb_channel_stats_t stats = modbus_channel_get_stats(id);
      const size_t written = mb_channel_build_json(n, &cfg, &stats, body + offset, sizeof(body) - offset);
      if (written == 0) {
        send_json_error(req, "500 Internal Server Error", -1, "internal_error", "Kunne ikke bygge kanal-listen");
        return ESP_OK;
      }
      offset += written;
      if (n < static_cast<int>(MB_CHANNEL_COUNT)) {
        body[offset++] = ',';
      }
    }
    body[offset++] = ']';
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, body, offset);
    return ESP_OK;
  }

  ModbusChannelId id;
  if (!channel_id_from_number(channel_number, &id)) {
    send_json_error(req, "404 Not Found", -1, "not_found", "Ukendt kanal-nummer (§4.2: n=1..active_channels)");
    return ESP_OK;
  }

  const mb_channel_config_t cfg = modbus_channel_get_config(id);
  const mb_channel_stats_t stats = modbus_channel_get_stats(id);
  char body[512];
  const size_t body_len = mb_channel_build_json(channel_number, &cfg, &stats, body, sizeof(body));
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, body, body_len > 0 ? body_len : HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// PUT /api/channels/{n}/config (§4.2) — ATOMISK: hele objektet skal med i
// ét kald, jf. mb_channel_parse_config_json()'s "alle felter eller afvis".
esp_err_t channel_config_put_handler(httpd_req_t *req) {
  if (!require_auth(req)) return ESP_OK;

  int channel_number = -1;
  bool has_config_suffix = false;
  if (!parse_channels_uri(req->uri, &channel_number, &has_config_suffix) || !has_config_suffix ||
      channel_number < 0) {
    send_json_error(req, "404 Not Found", -1, "not_found", "Ukendt sti — forventede /api/channels/{n}/config");
    return ESP_OK;
  }

  ModbusChannelId id;
  if (!channel_id_from_number(channel_number, &id)) {
    send_json_error(req, "404 Not Found", -1, "not_found", "Ukendt kanal-nummer (§4.2: n=1..active_channels)");
    return ESP_OK;
  }

  if (req->content_len == 0 || req->content_len >= 512) {
    send_json_error(req, "400 Bad Request", -1, "bad_request", "Tom eller for stor request-body (maks 511 bytes)");
    return ESP_OK;
  }

  char body[512];
  const int received = httpd_req_recv(req, body, static_cast<size_t>(req->content_len));
  if (received <= 0) {
    send_json_error(req, "400 Bad Request", -1, "bad_request", "Kunne ikke læse request-body");
    return ESP_OK;
  }
  body[received] = '\0';

  mb_channel_config_t new_config;
  if (!mb_channel_parse_config_json(body, static_cast<size_t>(received), &new_config)) {
    send_json_error(req, "400 Bad Request", -1, "invalid_config",
                     "Ugyldig eller ufuldstændig kanal-config — §4.2 kræver ALLE felter i ét atomisk kald");
    return ESP_OK;
  }

  if (!modbus_channel_apply_config(id, new_config)) {
    send_json_error(req, "500 Internal Server Error", -1, "apply_failed", "Kunne ikke anvende ny kanal-config");
    return ESP_OK;
  }
  // Persistér EFTER kanalen selv er live-omkonfigureret — et strømudfald
  // midt i kaldet efterlader så i værste fald flash uændret (gammel config
  // stadig gemt), aldrig en UART der kører med en config, flash ikke ved af.
  config_set_channel(static_cast<size_t>(channel_number - 1), new_config);

  const mb_channel_stats_t stats = modbus_channel_get_stats(id);
  char resp_body[512];
  const size_t resp_len = mb_channel_build_json(channel_number, &new_config, &stats, resp_body, sizeof(resp_body));
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, resp_body, resp_len > 0 ? resp_len : HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// POST /api/channels/{n}/read og POST /api/channels/{n}/write (§4.2) — ad-hoc
// diagnose via curl/Postman, IKKE data-planet (Modbus TCP, §4.1, forbliver
// PLC'ens høj-frekvente vej). Genbruger modbus_channel_submit() direkte —
// samme indgang til Lag 2 som TCP-serveren, jf. ARCHITECTURE.md regel 2.
esp_err_t channel_read_write_post_handler(httpd_req_t *req) {
  if (!require_auth(req)) return ESP_OK;

  int channel_number = -1;
  const bool is_read = parse_channel_number_with_suffix(req->uri, "/read", &channel_number);
  const bool is_write = !is_read && parse_channel_number_with_suffix(req->uri, "/write", &channel_number);
  if (!is_read && !is_write) {
    send_json_error(req, "404 Not Found", -1, "not_found", "Ukendt sti — forventede /api/channels/{n}/read eller /write");
    return ESP_OK;
  }

  ModbusChannelId id;
  if (!channel_id_from_number(channel_number, &id)) {
    send_json_error(req, "404 Not Found", -1, "not_found", "Ukendt kanal-nummer (§4.2: n=1..active_channels)");
    return ESP_OK;
  }

  if (req->content_len == 0 || req->content_len >= 512) {
    send_json_error(req, "400 Bad Request", -1, "bad_request", "Tom eller for stor request-body (maks 511 bytes)");
    return ESP_OK;
  }
  char body[512];
  const int received = httpd_req_recv(req, body, static_cast<size_t>(req->content_len));
  if (received <= 0) {
    send_json_error(req, "400 Bad Request", -1, "bad_request", "Kunne ikke læse request-body");
    return ESP_OK;
  }
  body[received] = '\0';

  mb_diag_read_request_t read_req{};
  mb_diag_write_request_t write_req{};
  uint8_t request_pdu[8 + MB_DIAG_MAX_WRITE_VALUES * 2];
  size_t request_pdu_len = 0;
  uint8_t slave_id = 0;

  if (is_read) {
    if (!mb_diag_parse_read_request(body, static_cast<size_t>(received), &read_req)) {
      send_json_error(req, "400 Bad Request", -1, "invalid_request",
                       "Ugyldig eller ufuldstændig read-request (kræver function_code 1/2/3/4, slave_id, address, quantity)");
      return ESP_OK;
    }
    request_pdu_len = mb_diag_build_read_pdu(&read_req, request_pdu, sizeof(request_pdu));
    slave_id = read_req.slave_id;
  } else {
    if (!mb_diag_parse_write_request(body, static_cast<size_t>(received), &write_req)) {
      send_json_error(
          req, "400 Bad Request", -1, "invalid_request",
          "Ugyldig eller ufuldstændig write-request (kræver function_code 5/6/16, slave_id, address, value/values)");
      return ESP_OK;
    }
    request_pdu_len = mb_diag_build_write_pdu(&write_req, request_pdu, sizeof(request_pdu));
    slave_id = write_req.slave_id;
  }

  if (request_pdu_len == 0) {
    send_json_error(req, "500 Internal Server Error", -1, "internal_error", "Kunne ikke bygge Modbus-PDU'en");
    return ESP_OK;
  }

  uint8_t response_pdu[MB_PDU_MAX_LEN];
  size_t response_pdu_len = 0;
  const mb_error_code_t result = modbus_channel_submit(id, slave_id, request_pdu, request_pdu_len, response_pdu,
                                                         &response_pdu_len, sizeof(response_pdu));

  if (result != MB_OK) {
    // Kanal-niveau-fejl (timeout/deaktiveret/CRC osv., §4) — IKKE en
    // Modbus-exception fra slaven selv (den håndteres nedenfor). Samme
    // error_code-konvention som resten af API'et (mb_error_code_t-værdien).
    send_json_error(req, "502 Bad Gateway", static_cast<int>(result), "channel_error",
                     "Kanalen kunne ikke gennemføre transaktionen — se error_code (mb_error_code_t)");
    return ESP_OK;
  }

  if (mb_diag_is_exception(response_pdu, response_pdu_len)) {
    // En Modbus-exception FRA SLAVEN er stadig et vellykket REST-kald — det
    // diagnostiske svar rapporterer trofast hvad slaven faktisk sagde.
    char err_body[192];
    const size_t err_len = mb_diag_build_exception_json(slave_id, response_pdu, response_pdu_len, err_body, sizeof(err_body));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, err_body, err_len > 0 ? err_len : HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  char resp_body[4608];  // FC01/02 kan i teorien returnere op til ~2000 bit-vaerdier
  const size_t resp_len = is_read
                               ? mb_diag_build_read_values_json(&read_req, response_pdu, response_pdu_len, resp_body,
                                                                 sizeof(resp_body))
                               : mb_diag_build_write_confirmation_json(&write_req, response_pdu, response_pdu_len,
                                                                        resp_body, sizeof(resp_body));
  if (resp_len == 0) {
    send_json_error(req, "500 Internal Server Error", -1, "internal_error", "Kunne ikke bygge svar-JSON'en");
    return ESP_OK;
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, resp_body, resp_len);
  return ESP_OK;
}

}  // namespace

void http_server_begin() {
  if (g_server != nullptr) return;  // allerede startet — undgår dobbelt-start ved gen-forbindelse

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 8080;  // §4.2 — bevidst IKKE 80, adskilt fra evt. fremtidig provisioning-relateret HTTP
  config.uri_match_fn = httpd_uri_match_wildcard;  // kræves for "/api/channels*"-stierne (§4.2's {n})
  // Default-stakken (4096 bytes) er for lille til channel_read_write_post_handler,
  // der kan holde en JSON-body, en request-/response-PDU og et svar med op til
  // ~2000 diagnostiske bit-værdier i sine egne lokale buffere samtidig.
  config.stack_size = 10240;

  if (httpd_start(&g_server, &config) != ESP_OK) {
    Serial.println("FEJL: kunne ikke starte REST management-API (port 8080).");
    return;
  }

  const httpd_uri_t status_uri = {
      .uri = "/api/status",
      .method = HTTP_GET,
      .handler = status_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(g_server, &status_uri);

  const httpd_uri_t channels_get_uri = {
      .uri = "/api/channels*",
      .method = HTTP_GET,
      .handler = channels_get_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(g_server, &channels_get_uri);

  const httpd_uri_t channel_config_put_uri = {
      .uri = "/api/channels/*",
      .method = HTTP_PUT,
      .handler = channel_config_put_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(g_server, &channel_config_put_uri);

  const httpd_uri_t channel_read_write_uri = {
      .uri = "/api/channels/*",
      .method = HTTP_POST,
      .handler = channel_read_write_post_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(g_server, &channel_read_write_uri);

  Serial.println("REST management-API startet paa port 8080 (status/channels/diagnostic read-write).");
}
