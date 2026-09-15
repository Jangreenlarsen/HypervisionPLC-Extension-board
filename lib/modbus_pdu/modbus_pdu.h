#pragma once

#include <cstddef>
#include <cstdint>

// Transaktions-fejlkoder — matcher PLC-repoets mb_error_code_t 1:1, værdi for
// værdi (se reference-plc-source/include/types.h og EXPANSION_BOARD_DESIGN.md
// §4), udvidet med MB_CHANNEL_UNREACHABLE (ny, expansion-board-specifik).
enum mb_error_code_t {
  MB_OK = 0,
  MB_TIMEOUT = 1,
  MB_CRC_ERROR = 2,
  MB_EXCEPTION = 3,
  MB_MAX_REQUESTS_EXCEEDED = 4,
  MB_NOT_ENABLED = 5,
  MB_INVALID_SLAVE = 6,
  MB_INVALID_ADDRESS = 7,
  MB_BUS_BUSY = 8,
  MB_CHANNEL_UNREACHABLE = 9
};

constexpr size_t MB_PDU_MAX_LEN = 253;                          // Modbus-spec: FC + op til 252 databytes
constexpr size_t MB_RTU_FRAME_MAX_LEN = 1 + MB_PDU_MAX_LEN + 2;  // adresse + PDU + CRC(2)
constexpr size_t MB_RTU_EXCEPTION_FRAME_LEN = 5;                 // adresse + (fc|0x80) + exception-kode + CRC(2)

// CRC16 (Modbus RTU) — algoritme og bytefølge (CRC low byte først) er
// byte-for-byte identisk med reference-plc-source/src/modbus_master.cpp's
// modbus_master_calc_crc(), verificeret mod spec-eksemplet i test/test_modbus_pdu.
uint16_t mb_pdu_calc_crc16(const uint8_t *buffer, size_t len);

// Bygger en RTU-forespørgsels-frame (adresse + pdu + CRC) for `slave_id` ud
// fra en allerede opbygget PDU (function code + data, modtaget fra TCP-laget,
// §4.1). Returnerer frame-længden, eller 0 hvis pdu_len er 0/for stor, eller
// out_capacity er for lille.
size_t mb_pdu_build_rtu_request(uint8_t slave_id, const uint8_t *pdu, size_t pdu_len,
                                 uint8_t *out_frame, size_t out_capacity);

enum mb_pdu_validation_t {
  MB_PDU_VALID = 0,
  MB_PDU_UNSUPPORTED_FUNCTION = 1,  // function code udenfor FC01-06/16 (§4.1's scope)
  MB_PDU_MALFORMED_REQUEST = 2      // forkert længde, eller quantity/byte_count udenfor Modbus-spec-grænser
};

// Beregner hvor mange bytes en komplet, IKKE-exception RTU-svar-frame vil
// være (adresse + svar-pdu + CRC) for en given forespørgsels-PDU. Kun
// FC01/02/03/04/05/06/15/16 genkendes (§4.1) — alt andet giver
// MB_PDU_UNSUPPORTED_FUNCTION, som kalderen bør afvise FØR transmission
// (jf. Modbus exception 0x01 "illegal function", ikke en transportfejl).
mb_pdu_validation_t mb_pdu_expected_response_frame_len(const uint8_t *request_pdu, size_t request_pdu_len,
                                                        size_t *out_expected_len);

// True når `frame_so_far` (de første `received_len` bytes af et RTU-svar)
// udgør en komplet frame — enten den FC-specifikke forventede længde fra
// mb_pdu_expected_response_frame_len(), eller en komplet 5-byte
// exception-frame (høj bit sat i funktionskode-byten). Generaliserer
// længde-logikken fra reference-plc-source/src/modbus_master.cpp's
// modtage-løkke til at virke for enhver af de understøttede function codes
// i stedet for ét fast kald.
bool mb_pdu_response_frame_complete(const uint8_t *request_pdu, size_t request_pdu_len,
                                     const uint8_t *frame_so_far, size_t received_len);

enum mb_pdu_parse_result_t {
  MB_PDU_RESULT_OK = 0,                // gyldigt, ikke-exception svar — out_pdu indeholder svar-PDU'en
  MB_PDU_RESULT_CRC_ERROR = 1,
  MB_PDU_RESULT_SLAVE_MISMATCH = 2,    // frame'ens adresse-byte matcher ikke expected_slave_id
  MB_PDU_RESULT_EXCEPTION = 3,         // slaven svarede med en Modbus-exception — out_pdu[0]=fc|0x80, out_pdu[1]=exception-kode
  MB_PDU_RESULT_TOO_SHORT = 4,         // frame_len < 4 (mindste mulige RTU-frame: adresse+fc+CRC(2))
  MB_PDU_RESULT_BUFFER_TOO_SMALL = 5   // out_pdu_capacity utilstrækkelig til at rumme svar-PDU'en
};

// Validerer en fuldt modtaget RTU-svar-frame (adresse + pdu + CRC) mod den
// forventede slave-adresse, verificerer CRC, og udtrækker svar-PDU'en
// (function code + data, uden adresse og CRC) til out_pdu.
mb_pdu_parse_result_t mb_pdu_parse_rtu_response(uint8_t expected_slave_id, const uint8_t *frame, size_t frame_len,
                                                 uint8_t *out_pdu, size_t *out_pdu_len, size_t out_pdu_capacity);
