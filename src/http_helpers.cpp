#include "http_helpers.h"

#include "config.h"
#include "rest_auth.h"
#include "rest_status.h"
#include "syslog_sender.h"

void send_json_error(httpd_req_t *req, const char *http_status, int error_code, const char *error,
                      const char *message) {
  char body[192];
  const size_t len = mb_status_build_error_json(error_code, error, message, body, sizeof(body));
  httpd_resp_set_status(req, http_status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, body, len > 0 ? len : HTTPD_RESP_USE_STRLEN);
}

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
  // v0.26.0 (CLAUDE.md regel 11: "auth-afvisninger" skal logges struktureret)
  // — nu ogsaa til syslog, ikke kun seriel konsol (som denne funktion
  // faktisk aldrig loggede til overhovedet foer syslog-featuren).
  syslog_logf(MB_SYSLOG_FACILITY_REST, 1, "401 unauthorized: %s (%s)", req->uri, message);
  send_json_error(req, "401 Unauthorized", -1, "unauthorized", message);
  return false;
}
