#include <unity.h>

#include <cstring>

#include "modbus_tcp.h"

void setUp(void) {}
void tearDown(void) {}

// Fælles fixture: Transaction ID=1, Unit ID=0x11 (17), PDU=FC03 read holding
// registers (samme spec-eksempel som lib/modbus_pdu bruger, §4.1).
static const uint8_t kValidAdu[] = {
    0x00, 0x01,              // Transaction ID = 1
    0x00, 0x00,              // Protocol ID = 0
    0x00, 0x06,              // Length = 6 (Unit ID + 5 PDU-bytes)
    0x11,                    // Unit ID = 17
    0x03, 0x00, 0x6B, 0x00, 0x03,  // PDU: FC03, addr=0x006B, qty=3
};

void test_parse_header_valid(void) {
  mb_mbap_header_t header;
  const mb_mbap_result_t r = mb_mbap_parse_header(kValidAdu, sizeof(kValidAdu), &header);
  TEST_ASSERT_EQUAL(MB_MBAP_OK, r);
  TEST_ASSERT_EQUAL_HEX16(1, header.transaction_id);
  TEST_ASSERT_EQUAL_HEX16(0, header.protocol_id);
  TEST_ASSERT_EQUAL_HEX16(6, header.length);
  TEST_ASSERT_EQUAL_HEX8(0x11, header.unit_id);
}

void test_parse_header_rejects_too_short(void) {
  mb_mbap_header_t header;
  const mb_mbap_result_t r = mb_mbap_parse_header(kValidAdu, 6, &header);  // kun 6 af 7 header-bytes
  TEST_ASSERT_EQUAL(MB_MBAP_TOO_SHORT, r);
}

void test_parse_header_rejects_nonzero_protocol_id(void) {
  uint8_t bad[sizeof(kValidAdu)];
  memcpy(bad, kValidAdu, sizeof(bad));
  bad[3] = 0x01;  // protocol_id = 1, ikke Modbus
  mb_mbap_header_t header;
  TEST_ASSERT_EQUAL(MB_MBAP_INVALID_PROTOCOL_ID, mb_mbap_parse_header(bad, sizeof(bad), &header));
}

void test_extract_pdu_valid(void) {
  mb_mbap_header_t header;
  const uint8_t *pdu = nullptr;
  size_t pdu_len = 0;
  const mb_mbap_result_t r = mb_mbap_extract_pdu(kValidAdu, sizeof(kValidAdu), &header, &pdu, &pdu_len);
  TEST_ASSERT_EQUAL(MB_MBAP_OK, r);
  TEST_ASSERT_EQUAL_size_t(5, pdu_len);
  const uint8_t expected_pdu[] = {0x03, 0x00, 0x6B, 0x00, 0x03};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_pdu, pdu, 5);
}

void test_extract_pdu_rejects_length_mismatch_too_few_bytes(void) {
  // Headeren siger length=6 (5 PDU-bytes), men vi giver kun 4 PDU-bytes.
  mb_mbap_header_t header;
  const uint8_t *pdu = nullptr;
  size_t pdu_len = 0;
  const mb_mbap_result_t r = mb_mbap_extract_pdu(kValidAdu, sizeof(kValidAdu) - 1, &header, &pdu, &pdu_len);
  TEST_ASSERT_EQUAL(MB_MBAP_LENGTH_MISMATCH, r);
}

void test_extract_pdu_rejects_length_mismatch_too_many_bytes(void) {
  uint8_t extra[sizeof(kValidAdu) + 1];
  memcpy(extra, kValidAdu, sizeof(kValidAdu));
  extra[sizeof(kValidAdu)] = 0xFF;  // en byte for meget i forhold til header.length
  mb_mbap_header_t header;
  const uint8_t *pdu = nullptr;
  size_t pdu_len = 0;
  TEST_ASSERT_EQUAL(MB_MBAP_LENGTH_MISMATCH, mb_mbap_extract_pdu(extra, sizeof(extra), &header, &pdu, &pdu_len));
}

void test_extract_pdu_rejects_zero_length(void) {
  uint8_t bad[sizeof(kValidAdu)];
  memcpy(bad, kValidAdu, sizeof(bad));
  bad[4] = 0x00;
  bad[5] = 0x00;  // length = 0 — ugyldigt, skal mindst daekke Unit ID
  mb_mbap_header_t header;
  const uint8_t *pdu = nullptr;
  size_t pdu_len = 0;
  TEST_ASSERT_EQUAL(MB_MBAP_LENGTH_MISMATCH, mb_mbap_extract_pdu(bad, 7, &header, &pdu, &pdu_len));
}

void test_build_response_matches_expected_bytes(void) {
  const uint8_t response_pdu[] = {0x03, 0x02, 0x00, 0xFF};  // FC03-svar, 1 register
  uint8_t out[MB_MBAP_HEADER_LEN + 4];
  const size_t len = mb_mbap_build_response(1, 0x11, response_pdu, sizeof(response_pdu), out, sizeof(out));

  const uint8_t expected[] = {
      0x00, 0x01,  // Transaction ID = 1 (ekko af requestets)
      0x00, 0x00,  // Protocol ID = 0
      0x00, 0x05,  // Length = 5 (Unit ID + 4 PDU-bytes)
      0x11,        // Unit ID = 17 (ekko af requestets)
      0x03, 0x02, 0x00, 0xFF,
  };
  TEST_ASSERT_EQUAL_size_t(sizeof(expected), len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, sizeof(expected));
}

void test_build_response_rejects_undersized_buffer(void) {
  const uint8_t response_pdu[] = {0x03, 0x02, 0x00, 0xFF};
  uint8_t out[4];  // for lille (kraever 7+4=11)
  TEST_ASSERT_EQUAL_size_t(0, mb_mbap_build_response(1, 0x11, response_pdu, sizeof(response_pdu), out, sizeof(out)));
}

// Round-trip: byg en response, udtraek PDU'en igen, bekraeft identisk.
void test_roundtrip_build_then_extract(void) {
  const uint8_t pdu[] = {0x10, 0x00, 0x00, 0x00, 0x02};  // FC16-svar
  uint8_t adu[MB_MBAP_HEADER_LEN + 5];
  const size_t adu_len = mb_mbap_build_response(42, 5, pdu, sizeof(pdu), adu, sizeof(adu));
  TEST_ASSERT_TRUE(adu_len > 0);

  mb_mbap_header_t header;
  const uint8_t *extracted_pdu = nullptr;
  size_t extracted_len = 0;
  TEST_ASSERT_EQUAL(MB_MBAP_OK, mb_mbap_extract_pdu(adu, adu_len, &header, &extracted_pdu, &extracted_len));
  TEST_ASSERT_EQUAL_HEX16(42, header.transaction_id);
  TEST_ASSERT_EQUAL_HEX8(5, header.unit_id);
  TEST_ASSERT_EQUAL_size_t(sizeof(pdu), extracted_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(pdu, extracted_pdu, sizeof(pdu));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();

  RUN_TEST(test_parse_header_valid);
  RUN_TEST(test_parse_header_rejects_too_short);
  RUN_TEST(test_parse_header_rejects_nonzero_protocol_id);

  RUN_TEST(test_extract_pdu_valid);
  RUN_TEST(test_extract_pdu_rejects_length_mismatch_too_few_bytes);
  RUN_TEST(test_extract_pdu_rejects_length_mismatch_too_many_bytes);
  RUN_TEST(test_extract_pdu_rejects_zero_length);

  RUN_TEST(test_build_response_matches_expected_bytes);
  RUN_TEST(test_build_response_rejects_undersized_buffer);

  RUN_TEST(test_roundtrip_build_then_extract);

  return UNITY_END();
}
