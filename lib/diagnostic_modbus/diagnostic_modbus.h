#pragma once

#include <cstddef>
#include <cstdint>

// §4.2: Diagnostisk Modbus read/write via REST — ad-hoc test/fejlsøgning
// (curl/Postman) uden at åbne en Modbus TCP-forbindelse. Supplement til,
// IKKE erstatning for, Modbus TCP-data-planet (§4.1). Genbruger IKKE
// lib/modbus_pdu's framing direkte (den er RTU-frame-orienteret) — bygger i
// stedet den rå PDU (function code + data) selv, præcis som
// lib/modbus_tcp/lib/channel_config allerede har hver sit smalle,
// begrebsmæssigt adskilte ansvar.

constexpr size_t MB_DIAG_MAX_WRITE_VALUES = 32;  // rigeligt til diagnostisk brug, ikke høj-frekvent drift

// --- Læsning (FC01/02/03/04) ---

struct mb_diag_read_request_t {
  uint8_t slave_id;
  uint8_t function_code;
  uint16_t address;
  uint16_t quantity;
};

// Parser POST /api/channels/{n}/read's JSON-body: function_code (1/2/3/4),
// slave_id (1-247), address (0-65535), quantity (1-2000). Alle felter
// påkrævet — samme "alt eller intet"-princip som §4.2's kanal-config.
bool mb_diag_parse_read_request(const char *json, size_t len, mb_diag_read_request_t *out);

// Bygger request-PDU'en (5 bytes: fc + addr(2) + qty(2)) til at sende via
// modbus_channel_submit(). Returnerer 0 hvis out_capacity er for lille.
size_t mb_diag_build_read_pdu(const mb_diag_read_request_t *req, uint8_t *out_pdu, size_t out_capacity);

// Bygger succes-JSON'en ud fra et IKKE-exception response_pdu (kaldstedet
// tjekker selv mb_diag_is_exception() først). FC03/04 giver 16-bit
// register-værdier, FC01/02 giver bit-udpakkede 0/1-værdier.
size_t mb_diag_build_read_values_json(const mb_diag_read_request_t *req, const uint8_t *response_pdu,
                                       size_t response_pdu_len, char *out, size_t out_capacity);

// --- Skrivning (FC05/06/16) ---

struct mb_diag_write_request_t {
  uint8_t slave_id;
  uint8_t function_code;
  uint16_t address;
  uint16_t values[MB_DIAG_MAX_WRITE_VALUES];
  uint16_t value_count;  // 1 for FC05/06, N for FC16
};

// Parser POST /api/channels/{n}/write's JSON-body. FC05: "value" (bool).
// FC06: "value" (0-65535). FC16: "values" (array af 0-65535, maks
// MB_DIAG_MAX_WRITE_VALUES). function_code/slave_id/address altid påkrævet.
bool mb_diag_parse_write_request(const char *json, size_t len, mb_diag_write_request_t *out);

// Bygger request-PDU'en til at sende via modbus_channel_submit(). Returnerer
// 0 hvis out_capacity er for lille.
size_t mb_diag_build_write_pdu(const mb_diag_write_request_t *req, uint8_t *out_pdu, size_t out_capacity);

// Bygger bekræftelses-JSON'en ud fra et IKKE-exception response_pdu.
size_t mb_diag_build_write_confirmation_json(const mb_diag_write_request_t *req, const uint8_t *response_pdu,
                                              size_t response_pdu_len, char *out, size_t out_capacity);

// --- Fælles (begge retninger) ---

// True hvis response_pdu er en Modbus-exception (høj bit sat i function code-byten).
bool mb_diag_is_exception(const uint8_t *response_pdu, size_t response_pdu_len);

// Bygger exception-JSON'en for et response_pdu hvor mb_diag_is_exception() er true.
size_t mb_diag_build_exception_json(uint8_t slave_id, const uint8_t *response_pdu, size_t response_pdu_len, char *out,
                                     size_t out_capacity);
