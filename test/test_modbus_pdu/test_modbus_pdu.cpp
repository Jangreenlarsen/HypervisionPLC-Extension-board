#include <unity.h>

#include <cstring>

#include "modbus_pdu.h"

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// CRC16 — verificeret uafhængigt af C++-implementeringen med et separat
// Python-script (samme algoritme, transskriberet fra bunden), inkl. det
// klassiske Modbus-spec-eksempel (slave 0x11, FC03, addr 0x006B, qty 3).
// ---------------------------------------------------------------------------

void test_crc16_empty_buffer_is_ffff(void) {
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, mb_pdu_calc_crc16(nullptr, 0));
}

void test_crc16_single_zero_byte(void) {
  const uint8_t data[] = {0x00};
  TEST_ASSERT_EQUAL_HEX16(0x40BF, mb_pdu_calc_crc16(data, sizeof(data)));
}

void test_crc16_spec_example(void) {
  const uint8_t data[] = {0x11, 0x03, 0x00, 0x6B, 0x00, 0x03};
  TEST_ASSERT_EQUAL_HEX16(0x8776, mb_pdu_calc_crc16(data, sizeof(data)));
}

// ---------------------------------------------------------------------------
// mb_pdu_build_rtu_request
// ---------------------------------------------------------------------------

void test_build_rtu_request_fc06(void) {
  const uint8_t pdu[] = {0x06, 0x00, 0x10, 0x00, 0xFF};
  uint8_t frame[MB_RTU_FRAME_MAX_LEN];

  const size_t len = mb_pdu_build_rtu_request(0x01, pdu, sizeof(pdu), frame, sizeof(frame));

  const uint8_t expected[] = {0x01, 0x06, 0x00, 0x10, 0x00, 0xFF, 0xC8, 0x4F};
  TEST_ASSERT_EQUAL_size_t(sizeof(expected), len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame, sizeof(expected));
}

void test_build_rtu_request_rejects_zero_length_pdu(void) {
  uint8_t frame[MB_RTU_FRAME_MAX_LEN];
  TEST_ASSERT_EQUAL_size_t(0, mb_pdu_build_rtu_request(0x01, nullptr, 0, frame, sizeof(frame)));
}

void test_build_rtu_request_rejects_undersized_buffer(void) {
  const uint8_t pdu[] = {0x06, 0x00, 0x10, 0x00, 0xFF};
  uint8_t frame[4];  // frame skal være 8 bytes — for lille
  TEST_ASSERT_EQUAL_size_t(0, mb_pdu_build_rtu_request(0x01, pdu, sizeof(pdu), frame, sizeof(frame)));
}

// ---------------------------------------------------------------------------
// mb_pdu_expected_response_frame_len
// ---------------------------------------------------------------------------

void test_expected_len_fc03_single_register(void) {
  const uint8_t req[] = {0x03, 0x00, 0x00, 0x00, 0x01};  // qty=1
  size_t len = 0;
  TEST_ASSERT_EQUAL(MB_PDU_VALID, mb_pdu_expected_response_frame_len(req, sizeof(req), &len));
  TEST_ASSERT_EQUAL_size_t(7, len);  // addr(1)+fc(1)+bytecount(1)+data(2)+crc(2)
}

void test_expected_len_fc01_ten_coils(void) {
  const uint8_t req[] = {0x01, 0x00, 0x00, 0x00, 0x0A};  // qty=10 -> byte_count=2
  size_t len = 0;
  TEST_ASSERT_EQUAL(MB_PDU_VALID, mb_pdu_expected_response_frame_len(req, sizeof(req), &len));
  TEST_ASSERT_EQUAL_size_t(7, len);
}

void test_expected_len_fc06_write_single(void) {
  const uint8_t req[] = {0x06, 0x00, 0x10, 0x00, 0xFF};
  size_t len = 0;
  TEST_ASSERT_EQUAL(MB_PDU_VALID, mb_pdu_expected_response_frame_len(req, sizeof(req), &len));
  TEST_ASSERT_EQUAL_size_t(8, len);
}

void test_expected_len_fc16_write_multiple(void) {
  const uint8_t req[] = {0x10, 0x00, 0x00, 0x00, 0x02, 0x04, 0x00, 0x0A, 0x01, 0x02};  // 2 registre
  size_t len = 0;
  TEST_ASSERT_EQUAL(MB_PDU_VALID, mb_pdu_expected_response_frame_len(req, sizeof(req), &len));
  TEST_ASSERT_EQUAL_size_t(8, len);
}

void test_expected_len_fc15_write_multiple_coils(void) {
  const uint8_t req[] = {0x0F, 0x00, 0x00, 0x00, 0x03, 0x01, 0x05};  // 3 coils, byte_count=1
  size_t len = 0;
  TEST_ASSERT_EQUAL(MB_PDU_VALID, mb_pdu_expected_response_frame_len(req, sizeof(req), &len));
  TEST_ASSERT_EQUAL_size_t(8, len);
}

void test_expected_len_rejects_fc15_bytecount_mismatch(void) {
  // qty=9 (skal give byte_count=2), men byte_count-feltet siger 1 — inkonsistent request
  const uint8_t req[] = {0x0F, 0x00, 0x00, 0x00, 0x09, 0x01, 0xFF};
  size_t len = 0;
  TEST_ASSERT_EQUAL(MB_PDU_MALFORMED_REQUEST, mb_pdu_expected_response_frame_len(req, sizeof(req), &len));
}

void test_expected_len_rejects_fc15_quantity_over_spec_limit(void) {
  const uint8_t req[] = {0x0F, 0x00, 0x00, 0x07, 0xB1, 246, 0x00};  // qty=1969 > 1968-grænsen
  size_t len = 0;
  TEST_ASSERT_EQUAL(MB_PDU_MALFORMED_REQUEST, mb_pdu_expected_response_frame_len(req, sizeof(req), &len));
}

// v0.28.0 (DESIGN_GUIDE_MODBUS_EXPANSION_FC_CAPABILITIES.md §1) —
// krydstjekker MB_PDU_SUPPORTED_FUNCTIONS (den erklærede liste, brugt af
// GET /api/capabilities) mod mb_pdu_expected_response_frame_len()'s
// FAKTISKE switch-cases, for ALLE 256 mulige function-code-byte-værdier.
// Formålet er eksplicit at forhindre den "to lag drifter fra hinanden"-
// fejlklasse designdokumentet selv advarer om (den ramte allerede PLC-siden
// én gang for FC15/16) — tilføjes en case til switchen uden at opdatere
// arrayet (eller omvendt), fejler denne test.
void test_supported_functions_array_matches_switch(void) {
  for (int fc = 0; fc <= 0xFF; fc++) {
    // 6-byte facade-PDU — nok bytes til at komme forbi request_pdu_len-
    // tjekket for alle i dag understøttede FC'er (5 for FC01-06, >=6 for
    // FC15/16); det er UNDERSTØTTELSE (ikke gyldighed) denne test tjekker.
    uint8_t pdu[6] = {static_cast<uint8_t>(fc), 0x00, 0x00, 0x00, 0x01, 0x00};
    size_t len = 0;
    const mb_pdu_validation_t result = mb_pdu_expected_response_frame_len(pdu, sizeof(pdu), &len);

    bool in_array = false;
    for (size_t i = 0; i < MB_PDU_SUPPORTED_FUNCTION_COUNT; i++) {
      if (MB_PDU_SUPPORTED_FUNCTIONS[i] == static_cast<uint8_t>(fc)) {
        in_array = true;
        break;
      }
    }

    if (in_array) {
      TEST_ASSERT_NOT_EQUAL_MESSAGE(
          MB_PDU_UNSUPPORTED_FUNCTION, result,
          "FC er i MB_PDU_SUPPORTED_FUNCTIONS, men switchen afviser den som unsupported - listerne er drevet fra hinanden");
    } else {
      TEST_ASSERT_EQUAL_MESSAGE(
          MB_PDU_UNSUPPORTED_FUNCTION, result,
          "FC er IKKE i MB_PDU_SUPPORTED_FUNCTIONS, men switchen accepterer den - listerne er drevet fra hinanden");
    }
  }
}

void test_expected_len_rejects_unsupported_function(void) {
  const uint8_t req[] = {0x07, 0x00};  // FC07 er udenfor §4.1's scope (FC01-06/16)
  size_t len = 0;
  TEST_ASSERT_EQUAL(MB_PDU_UNSUPPORTED_FUNCTION, mb_pdu_expected_response_frame_len(req, sizeof(req), &len));
}

void test_expected_len_rejects_zero_quantity(void) {
  const uint8_t req[] = {0x03, 0x00, 0x00, 0x00, 0x00};  // qty=0 er ugyldigt
  size_t len = 0;
  TEST_ASSERT_EQUAL(MB_PDU_MALFORMED_REQUEST, mb_pdu_expected_response_frame_len(req, sizeof(req), &len));
}

void test_expected_len_rejects_fc03_quantity_over_spec_limit(void) {
  const uint8_t req[] = {0x03, 0x00, 0x00, 0x00, 126};  // > 125 registre, over Modbus-spec-grænsen
  size_t len = 0;
  TEST_ASSERT_EQUAL(MB_PDU_MALFORMED_REQUEST, mb_pdu_expected_response_frame_len(req, sizeof(req), &len));
}

void test_expected_len_rejects_fc16_bytecount_mismatch(void) {
  // qty=2 (skal give byte_count=4), men byte_count-feltet siger 2 — inkonsistent request
  const uint8_t req[] = {0x10, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x0A};
  size_t len = 0;
  TEST_ASSERT_EQUAL(MB_PDU_MALFORMED_REQUEST, mb_pdu_expected_response_frame_len(req, sizeof(req), &len));
}

// ---------------------------------------------------------------------------
// mb_pdu_response_frame_complete
// ---------------------------------------------------------------------------

void test_frame_complete_fc03_incomplete_then_complete(void) {
  const uint8_t req[] = {0x03, 0x00, 0x00, 0x00, 0x01};
  const uint8_t full_response[] = {0x01, 0x03, 0x02, 0x00, 0xFF, 0xF8, 0x04};  // 7 bytes

  for (size_t n = 0; n < sizeof(full_response) - 1; n++) {
    TEST_ASSERT_FALSE_MESSAGE(mb_pdu_response_frame_complete(req, sizeof(req), full_response, n),
                               "skulle IKKE vaere komplet foer alle 7 bytes er modtaget");
  }
  TEST_ASSERT_TRUE(mb_pdu_response_frame_complete(req, sizeof(req), full_response, sizeof(full_response)));
}

void test_frame_complete_exception_at_five_bytes(void) {
  const uint8_t req[] = {0x03, 0x00, 0x00, 0x00, 0x01};  // forventer normalt 7 bytes
  const uint8_t exception_frame[] = {0x01, 0x83, 0x02, 0xC0, 0xF1};  // men slaven svarer med exception (5 bytes)

  TEST_ASSERT_TRUE(mb_pdu_response_frame_complete(req, sizeof(req), exception_frame, sizeof(exception_frame)));
}

// ---------------------------------------------------------------------------
// mb_pdu_parse_rtu_response
// ---------------------------------------------------------------------------

void test_parse_valid_fc03_response(void) {
  const uint8_t frame[] = {0x01, 0x03, 0x02, 0x00, 0xFF, 0xF8, 0x04};
  uint8_t pdu[MB_PDU_MAX_LEN];
  size_t pdu_len = 0;

  const mb_pdu_parse_result_t result =
      mb_pdu_parse_rtu_response(0x01, frame, sizeof(frame), pdu, &pdu_len, sizeof(pdu));

  TEST_ASSERT_EQUAL(MB_PDU_RESULT_OK, result);
  const uint8_t expected_pdu[] = {0x03, 0x02, 0x00, 0xFF};
  TEST_ASSERT_EQUAL_size_t(sizeof(expected_pdu), pdu_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_pdu, pdu, sizeof(expected_pdu));
}

void test_parse_detects_crc_error(void) {
  uint8_t frame[] = {0x01, 0x03, 0x02, 0x00, 0xFF, 0xF8, 0x05};  // sidste byte aendret (var 0x04)
  uint8_t pdu[MB_PDU_MAX_LEN];
  size_t pdu_len = 0;

  TEST_ASSERT_EQUAL(MB_PDU_RESULT_CRC_ERROR,
                     mb_pdu_parse_rtu_response(0x01, frame, sizeof(frame), pdu, &pdu_len, sizeof(pdu)));
}

void test_parse_detects_slave_mismatch(void) {
  const uint8_t frame[] = {0x01, 0x03, 0x02, 0x00, 0xFF, 0xF8, 0x04};
  uint8_t pdu[MB_PDU_MAX_LEN];
  size_t pdu_len = 0;

  TEST_ASSERT_EQUAL(MB_PDU_RESULT_SLAVE_MISMATCH,
                     mb_pdu_parse_rtu_response(0x02, frame, sizeof(frame), pdu, &pdu_len, sizeof(pdu)));
}

void test_parse_detects_exception_response(void) {
  const uint8_t frame[] = {0x01, 0x83, 0x02, 0xC0, 0xF1};  // FC03|0x80, exception code 0x02
  uint8_t pdu[MB_PDU_MAX_LEN];
  size_t pdu_len = 0;

  const mb_pdu_parse_result_t result =
      mb_pdu_parse_rtu_response(0x01, frame, sizeof(frame), pdu, &pdu_len, sizeof(pdu));

  TEST_ASSERT_EQUAL(MB_PDU_RESULT_EXCEPTION, result);
  TEST_ASSERT_EQUAL_size_t(2, pdu_len);
  TEST_ASSERT_EQUAL_HEX8(0x83, pdu[0]);
  TEST_ASSERT_EQUAL_HEX8(0x02, pdu[1]);
}

void test_parse_rejects_undersized_output_buffer(void) {
  const uint8_t frame[] = {0x01, 0x03, 0x02, 0x00, 0xFF, 0xF8, 0x04};  // svar-pdu er 4 bytes
  uint8_t pdu[2];  // for lille
  size_t pdu_len = 0;

  TEST_ASSERT_EQUAL(MB_PDU_RESULT_BUFFER_TOO_SMALL,
                     mb_pdu_parse_rtu_response(0x01, frame, sizeof(frame), pdu, &pdu_len, sizeof(pdu)));
}

void test_parse_rejects_too_short_frame(void) {
  const uint8_t frame[] = {0x01, 0x03};  // kun 2 bytes, under minimum (4)
  uint8_t pdu[MB_PDU_MAX_LEN];
  size_t pdu_len = 0;

  TEST_ASSERT_EQUAL(MB_PDU_RESULT_TOO_SHORT,
                     mb_pdu_parse_rtu_response(0x01, frame, sizeof(frame), pdu, &pdu_len, sizeof(pdu)));
}

// ---------------------------------------------------------------------------
// Round-trip: build request -> (simuleret slave-svar) -> parse response,
// for FC16 (Write Multiple Registers), verificeret med samme uafhængige
// Python-CRC-script som spec-eksemplet ovenfor.
// ---------------------------------------------------------------------------

void test_roundtrip_fc15(void) {
  // 3 coils fra adresse 0, mønster 101 (coil0=1, coil1=0, coil2=1) => byte 0x05.
  // CRC-værdier verificeret med samme uafhængige Python-CRC-script som FC16-testen.
  const uint8_t pdu[] = {0x0F, 0x00, 0x00, 0x00, 0x03, 0x01, 0x05};
  uint8_t request_frame[MB_RTU_FRAME_MAX_LEN];
  const size_t request_len = mb_pdu_build_rtu_request(0x01, pdu, sizeof(pdu), request_frame, sizeof(request_frame));

  const uint8_t expected_request[] = {0x01, 0x0F, 0x00, 0x00, 0x00, 0x03, 0x01, 0x05, 0x4F, 0x54};
  TEST_ASSERT_EQUAL_size_t(sizeof(expected_request), request_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_request, request_frame, sizeof(expected_request));

  // Simuleret slave-svar: adresse+fc+startadresse+quantity ekkoet, CRC fra Python-scriptet
  const uint8_t response_frame[] = {0x01, 0x0F, 0x00, 0x00, 0x00, 0x03, 0x15, 0xCA};
  uint8_t response_pdu[MB_PDU_MAX_LEN];
  size_t response_pdu_len = 0;
  const mb_pdu_parse_result_t result = mb_pdu_parse_rtu_response(0x01, response_frame, sizeof(response_frame),
                                                                   response_pdu, &response_pdu_len,
                                                                   sizeof(response_pdu));

  TEST_ASSERT_EQUAL(MB_PDU_RESULT_OK, result);
  const uint8_t expected_response_pdu[] = {0x0F, 0x00, 0x00, 0x00, 0x03};
  TEST_ASSERT_EQUAL_size_t(sizeof(expected_response_pdu), response_pdu_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_response_pdu, response_pdu, sizeof(expected_response_pdu));
}

void test_roundtrip_fc16(void) {
  const uint8_t pdu[] = {0x10, 0x00, 0x00, 0x00, 0x02, 0x04, 0x00, 0x0A, 0x01, 0x02};
  uint8_t request_frame[MB_RTU_FRAME_MAX_LEN];
  const size_t request_len = mb_pdu_build_rtu_request(0x01, pdu, sizeof(pdu), request_frame, sizeof(request_frame));

  const uint8_t expected_request[] = {0x01, 0x10, 0x00, 0x00, 0x00, 0x02, 0x04,
                                       0x00, 0x0A, 0x01, 0x02, 0x53, 0xFC};
  TEST_ASSERT_EQUAL_size_t(sizeof(expected_request), request_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_request, request_frame, sizeof(expected_request));

  // Simuleret slave-svar: adresse+fc+startadresse+quantity ekkoet, CRC fra Python-scriptet
  const uint8_t response_frame[] = {0x01, 0x10, 0x00, 0x00, 0x00, 0x02, 0x41, 0xC8};
  uint8_t response_pdu[MB_PDU_MAX_LEN];
  size_t response_pdu_len = 0;
  const mb_pdu_parse_result_t result = mb_pdu_parse_rtu_response(0x01, response_frame, sizeof(response_frame),
                                                                   response_pdu, &response_pdu_len,
                                                                   sizeof(response_pdu));

  TEST_ASSERT_EQUAL(MB_PDU_RESULT_OK, result);
  const uint8_t expected_response_pdu[] = {0x10, 0x00, 0x00, 0x00, 0x02};
  TEST_ASSERT_EQUAL_size_t(sizeof(expected_response_pdu), response_pdu_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_response_pdu, response_pdu, sizeof(expected_response_pdu));
}

// ---------------------------------------------------------------------------
// mb_pdu_decode (v0.28.2/v0.28.3, Jan: "kan vi ikke få en modbus protocol
// frame pakke decode med i det debug output" / "kan vi gøre det output
// mere lækket ... kompakt felt-format") — kompakt "FC: xx, Addr: n, ..."-
// format, samme stil som kaldstedets omkringliggende "ID:"/"CRC:"-felter
// (src/modbus_channel.cpp), ikke fulde engelske saetninger.
// ---------------------------------------------------------------------------

void test_decode_fc03_request(void) {
  const uint8_t pdu[] = {0x03, 0x00, 0x00, 0x00, 0x01};
  char out[128];
  const size_t len = mb_pdu_decode(pdu, sizeof(pdu), false, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_EQUAL_STRING("FC: 03, Addr: 0, Qty: 1", out);
}

void test_decode_fc03_response(void) {
  // register 0 = 0x4616 (17942) - samme register denne kodebase konsekvent
  // har brugt til live-verifikation hele projektet igennem.
  const uint8_t pdu[] = {0x03, 0x02, 0x46, 0x16};
  char out[128];
  const size_t len = mb_pdu_decode(pdu, sizeof(pdu), true, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_EQUAL_STRING("FC: 03, Values: [17942]", out);
}

void test_decode_fc01_request(void) {
  const uint8_t pdu[] = {0x01, 0x00, 0x00, 0x00, 0x0A};
  char out[128];
  const size_t len = mb_pdu_decode(pdu, sizeof(pdu), false, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_EQUAL_STRING("FC: 01, Addr: 0, Qty: 10", out);
}

void test_decode_fc01_response(void) {
  // byte_count=1 (8 bits, praecis): 0x05 = 0b00000101 -> bit0=1,bit1=0,bit2=1,resten 0
  const uint8_t pdu[] = {0x01, 0x01, 0x05};
  char out[128];
  const size_t len = mb_pdu_decode(pdu, sizeof(pdu), true, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_EQUAL_STRING("FC: 01, Values: [1,0,1,0,0,0,0,0]", out);
}

void test_decode_fc05_request_and_response(void) {
  // FC05 er et bogstaveligt ekko - request og svar er BYTE-IDENTISKE, saa
  // decode-teksten er ogsaa identisk. Retningen (>TX>/<RX<) kommer fra
  // kaldstedets eget praefiks, ikke fra selve decode-indholdet.
  const uint8_t pdu[] = {0x05, 0x00, 0x03, 0xFF, 0x00};
  char out[128];
  size_t len = mb_pdu_decode(pdu, sizeof(pdu), false, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_EQUAL_STRING("FC: 05, Addr: 3, Value: ON", out);

  len = mb_pdu_decode(pdu, sizeof(pdu), true, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_EQUAL_STRING("FC: 05, Addr: 3, Value: ON", out);
}

void test_decode_fc06_request(void) {
  const uint8_t pdu[] = {0x06, 0x00, 0x0A, 0x04, 0xD2};  // addr=10, value=1234
  char out[128];
  const size_t len = mb_pdu_decode(pdu, sizeof(pdu), false, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_EQUAL_STRING("FC: 06, Addr: 10, Value: 1234", out);
}

void test_decode_fc15_request(void) {
  const uint8_t pdu[] = {0x0F, 0x00, 0x00, 0x00, 0x03, 0x01, 0x05};  // qty=3, values=[1,0,1]
  char out[128];
  const size_t len = mb_pdu_decode(pdu, sizeof(pdu), false, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_EQUAL_STRING("FC: 0F, Addr: 0, Qty: 3, Values: [1,0,1]", out);
}

void test_decode_fc15_response(void) {
  const uint8_t pdu[] = {0x0F, 0x00, 0x00, 0x00, 0x03};
  char out[128];
  const size_t len = mb_pdu_decode(pdu, sizeof(pdu), true, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_EQUAL_STRING("FC: 0F, Addr: 0, Qty: 3", out);
}

void test_decode_fc16_request(void) {
  const uint8_t pdu[] = {0x10, 0x00, 0x00, 0x00, 0x02, 0x04, 0x00, 0x01, 0x00, 0x02};
  char out[128];
  const size_t len = mb_pdu_decode(pdu, sizeof(pdu), false, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_EQUAL_STRING("FC: 10, Addr: 0, Qty: 2, Values: [1,2]", out);
}

void test_decode_fc16_response(void) {
  const uint8_t pdu[] = {0x10, 0x00, 0x00, 0x00, 0x02};
  char out[128];
  const size_t len = mb_pdu_decode(pdu, sizeof(pdu), true, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_EQUAL_STRING("FC: 10, Addr: 0, Qty: 2", out);
}

void test_decode_exception_response(void) {
  const uint8_t pdu[] = {0x83, 0x02};  // FC03|0x80, Illegal Data Address
  char out[128];
  const size_t len = mb_pdu_decode(pdu, sizeof(pdu), true, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_EQUAL_STRING("FC: 83, Exception: 02 (Illegal Data Address)", out);
}

void test_decode_rejects_unknown_function(void) {
  const uint8_t pdu[] = {0x07, 0x00};
  char out[128];
  const size_t len = mb_pdu_decode(pdu, sizeof(pdu), false, out, sizeof(out));
  TEST_ASSERT_EQUAL_size_t(0, len);
}

void test_decode_truncates_long_value_list(void) {
  // FC16-response ekkoer ingen values, saa brug FC16-REQUEST med 25 registre
  // (>kDecodeMaxValues=20) for at teste afkortningen.
  uint8_t pdu[6 + 25 * 2];
  pdu[0] = 0x10;
  pdu[1] = 0x00;
  pdu[2] = 0x00;
  pdu[3] = 0x00;
  pdu[4] = 25;
  pdu[5] = 25 * 2;
  for (size_t i = 0; i < 25; i++) {
    pdu[6 + i * 2] = 0x00;
    pdu[6 + i * 2 + 1] = static_cast<uint8_t>(i);
  }
  char out[256];
  const size_t len = mb_pdu_decode(pdu, sizeof(pdu), false, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "...og 5 mere"), "25 vaerdier skal afkortes til 20 + '...og 5 mere'");
  TEST_ASSERT_NULL_MESSAGE(strstr(out, ",24]"), "det 25. element skal IKKE vaere med i den afkortede liste");
}

void test_decode_rejects_undersized_buffer(void) {
  const uint8_t pdu[] = {0x03, 0x00, 0x00, 0x00, 0x01};
  char out[8];
  const size_t len = mb_pdu_decode(pdu, sizeof(pdu), false, out, sizeof(out));
  TEST_ASSERT_EQUAL_size_t(0, len);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();

  RUN_TEST(test_crc16_empty_buffer_is_ffff);
  RUN_TEST(test_crc16_single_zero_byte);
  RUN_TEST(test_crc16_spec_example);

  RUN_TEST(test_build_rtu_request_fc06);
  RUN_TEST(test_build_rtu_request_rejects_zero_length_pdu);
  RUN_TEST(test_build_rtu_request_rejects_undersized_buffer);

  RUN_TEST(test_expected_len_fc03_single_register);
  RUN_TEST(test_expected_len_fc01_ten_coils);
  RUN_TEST(test_expected_len_fc06_write_single);
  RUN_TEST(test_expected_len_fc16_write_multiple);
  RUN_TEST(test_expected_len_fc15_write_multiple_coils);
  RUN_TEST(test_expected_len_rejects_fc15_bytecount_mismatch);
  RUN_TEST(test_expected_len_rejects_fc15_quantity_over_spec_limit);
  RUN_TEST(test_expected_len_rejects_unsupported_function);
  RUN_TEST(test_supported_functions_array_matches_switch);
  RUN_TEST(test_expected_len_rejects_zero_quantity);
  RUN_TEST(test_expected_len_rejects_fc03_quantity_over_spec_limit);
  RUN_TEST(test_expected_len_rejects_fc16_bytecount_mismatch);

  RUN_TEST(test_frame_complete_fc03_incomplete_then_complete);
  RUN_TEST(test_frame_complete_exception_at_five_bytes);

  RUN_TEST(test_parse_valid_fc03_response);
  RUN_TEST(test_parse_detects_crc_error);
  RUN_TEST(test_parse_detects_slave_mismatch);
  RUN_TEST(test_parse_detects_exception_response);
  RUN_TEST(test_parse_rejects_undersized_output_buffer);
  RUN_TEST(test_parse_rejects_too_short_frame);

  RUN_TEST(test_roundtrip_fc15);
  RUN_TEST(test_roundtrip_fc16);

  RUN_TEST(test_decode_fc03_request);
  RUN_TEST(test_decode_fc03_response);
  RUN_TEST(test_decode_fc01_request);
  RUN_TEST(test_decode_fc01_response);
  RUN_TEST(test_decode_fc05_request_and_response);
  RUN_TEST(test_decode_fc06_request);
  RUN_TEST(test_decode_fc15_request);
  RUN_TEST(test_decode_fc15_response);
  RUN_TEST(test_decode_fc16_request);
  RUN_TEST(test_decode_fc16_response);
  RUN_TEST(test_decode_exception_response);
  RUN_TEST(test_decode_rejects_unknown_function);
  RUN_TEST(test_decode_truncates_long_value_list);
  RUN_TEST(test_decode_rejects_undersized_buffer);

  return UNITY_END();
}
