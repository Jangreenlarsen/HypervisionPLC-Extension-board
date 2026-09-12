#pragma once

#include <cstddef>
#include <cstdint>

// Ren base64-decoding (RFC 4648), ingen hardware-afhængighed — bruges til at
// afkode "Authorization: Basic <base64>"-headeren (§4.4). Returnerer antal
// afkodede bytes, eller 0 ved ugyldig input (forkert længde, ugyldigt tegn,
// forkert placeret padding, eller for lille `out_capacity`). Sætter en
// afsluttende '\0' hvis der er plads — bekvemt når resultatet (som her)
// forventes at være tekst ("bruger:kode").
size_t mb_base64_decode(const char *input, uint8_t *out, size_t out_capacity);

// Legitimationsoplysninger boardet aktuelt accepterer — begge kan være sat
// samtidig (§4.4, dual auth-model), et REST-kald skal blot matche ÉN af dem.
struct mb_rest_credentials_t {
  const char *mgmt_token;
  bool has_mgmt_token;
  const char *rest_user;
  const char *rest_pass;
  bool has_rest_auth;  // true kun hvis BÅDE user og pass er sat
};

enum mb_rest_auth_result_t {
  MB_REST_AUTH_OK = 0,
  MB_REST_AUTH_MISSING_HEADER,        // ingen Authorization-header overhovedet
  MB_REST_AUTH_UNSUPPORTED_SCHEME,    // hverken "Bearer " eller "Basic " præfiks
  MB_REST_AUTH_MALFORMED,             // fx ugyldig base64, eller intet ':' i det afkodede Basic-felt
  MB_REST_AUTH_INVALID_CREDENTIALS    // korrekt format, men matcher ikke (eller intet er konfigureret at matche mod)
};

// Validerer en rå "Authorization"-header-værdi mod `credentials`. Prøver
// "Bearer <token>" og "Basic <base64(user:pass)>" — accepterer den ene ELLER
// den anden, ikke begge på samme kald (§4.4: to sideordnede metoder).
// Bruger konstant-tids strengsammenligning for selve credential-tjekket
// (ikke for header-parsing) for at undgå at responstiden lækker HVOR i en
// hemmelighed en sammenligning fejlede.
mb_rest_auth_result_t mb_rest_auth_check(const char *auth_header, const mb_rest_credentials_t *credentials);
