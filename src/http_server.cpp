#include "http_server.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_http_server.h>

#include "config.h"
#include "rest_auth.h"
#include "rest_status.h"

namespace {

httpd_handle_t g_server = nullptr;

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

}  // namespace

void http_server_begin() {
  if (g_server != nullptr) return;  // allerede startet — undgår dobbelt-start ved gen-forbindelse

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 8080;  // §4.2 — bevidst IKKE 80, adskilt fra evt. fremtidig provisioning-relateret HTTP

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

  Serial.println("REST management-API startet paa port 8080 (GET /api/status).");
}
