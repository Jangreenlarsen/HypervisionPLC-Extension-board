#pragma once

#include <esp_http_server.h>

// Delt mellem src/http_server.cpp og src/ota_handler.cpp — udtrukket for at
// undgå at duplikere selve AUTH-TJEKKET (sikkerhedskritisk, må ikke kunne
// afvige mellem to kopier). Se §4.4 (dual auth-model).

// Udtrækker "Authorization"-headeren og validerer den mod aktuel config
// (Bearer-token ELLER Basic Auth). Sender selv et 401-JSON-svar og
// returnerer false hvis uautoriseret — kaldstedet skal da returnere uden at
// gøre mere.
bool require_auth(httpd_req_t *req);

// Bygger og sender et REST-fejlsvar i samme `{"ok":false,...}`-stil som
// resten af API'et (se lib/rest_status). `error_code` < 0 udelader feltet.
void send_json_error(httpd_req_t *req, const char *http_status, int error_code, const char *error,
                      const char *message);
