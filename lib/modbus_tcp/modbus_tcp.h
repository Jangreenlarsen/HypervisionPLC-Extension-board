#pragma once

#include <cstddef>
#include <cstdint>

// MBAP-header (Modbus Application Protocol, §4.1) — 7 bytes: Transaction ID
// (2), Protocol ID (2, altid 0 for Modbus), Length (2, antal FØLGENDE bytes:
// Unit ID + PDU), Unit ID (1). Ingen CRC — TCP garanterer selv integritet.
constexpr size_t MB_MBAP_HEADER_LEN = 7;
constexpr uint16_t MB_MBAP_PROTOCOL_ID = 0;

struct mb_mbap_header_t {
  uint16_t transaction_id;
  uint16_t protocol_id;
  uint16_t length;  // Unit ID (1 byte) + PDU-længde
  uint8_t unit_id;  // §4.1's RETTELSE: dette ER RTU-slavens fysiske adresse (1-247), ikke en dummy-værdi
};

enum mb_mbap_result_t {
  MB_MBAP_OK = 0,
  MB_MBAP_TOO_SHORT,               // færre end MB_MBAP_HEADER_LEN bytes til rådighed
  MB_MBAP_INVALID_PROTOCOL_ID,     // protocol_id != 0 — ikke Modbus (et andet protokol multiplekset på porten)
  MB_MBAP_LENGTH_MISMATCH,         // header.length matcher ikke det faktisk modtagne antal PDU-bytes
  MB_MBAP_PDU_TOO_LARGE            // header.length antyder en PDU der ikke kan være gyldig (§4, MB_PDU_MAX_LEN)
};

// Fortolker de første MB_MBAP_HEADER_LEN bytes af `buf` som en MBAP-header.
mb_mbap_result_t mb_mbap_parse_header(const uint8_t *buf, size_t len, mb_mbap_header_t *out_header);

// Givet en fuld ADU (header + PDU) på `total_len` bytes, udtrækker header OG
// en pointer/længde ind i `adu` for selve PDU'en (ingen kopiering). Validerer
// at header.length + MB_MBAP_HEADER_LEN - 1 (unit_id talt med i length, ikke
// i header-længden) == total_len, dvs. at der IKKE er for få/for mange bytes
// i forhold til hvad headeren selv siger.
mb_mbap_result_t mb_mbap_extract_pdu(const uint8_t *adu, size_t total_len, mb_mbap_header_t *out_header,
                                      const uint8_t **out_pdu, size_t *out_pdu_len);

// Bygger en svar-ADU (header + pdu) i `out_buf`. Returnerer antal skrevne
// bytes, eller 0 hvis `out_capacity` er for lille.
size_t mb_mbap_build_response(uint16_t transaction_id, uint8_t unit_id, const uint8_t *pdu, size_t pdu_len,
                               uint8_t *out_buf, size_t out_capacity);
