#include <unity.h>

#include <cstring>

#include "diagnostic_modbus.h"

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Read: parse
// ---------------------------------------------------------------------------

void test_parse_read_request_valid(void) {
  const char *json = "{\"function_code\":3,\"slave_id\":9,\"address\":0,\"quantity\":2}";
  mb_diag_read_request_t req{};
  TEST_ASSERT_TRUE(mb_diag_parse_read_request(json, strlen(json), &req));
  TEST_ASSERT_EQUAL_UINT8(3, req.function_code);
  TEST_ASSERT_EQUAL_UINT8(9, req.slave_id);
  TEST_ASSERT_EQUAL_UINT16(0, req.address);
  TEST_ASSERT_EQUAL_UINT16(2, req.quantity);
}

void test_parse_read_request_rejects_invalid_function_code(void) {
  const char *json = "{\"function_code\":7,\"slave_id\":9,\"address\":0,\"quantity\":2}";
  mb_diag_read_request_t req{};
  TEST_ASSERT_FALSE(mb_diag_parse_read_request(json, strlen(json), &req));
}

void test_parse_read_request_rejects_missing_field(void) {
  const char *json = "{\"function_code\":3,\"slave_id\":9,\"address\":0}";
  mb_diag_read_request_t req{};
  TEST_ASSERT_FALSE(mb_diag_parse_read_request(json, strlen(json), &req));
}

void test_parse_read_request_rejects_slave_id_zero(void) {
  const char *json = "{\"function_code\":3,\"slave_id\":0,\"address\":0,\"quantity\":2}";
  mb_diag_read_request_t req{};
  TEST_ASSERT_FALSE(mb_diag_parse_read_request(json, strlen(json), &req));
}

// ---------------------------------------------------------------------------
// Read: build PDU + response JSON
// ---------------------------------------------------------------------------

void test_build_read_pdu_matches_expected_bytes(void) {
  mb_diag_read_request_t req{9, 3, 0x006B, 3};
  uint8_t pdu[5];
  TEST_ASSERT_EQUAL_size_t(5, mb_diag_build_read_pdu(&req, pdu, sizeof(pdu)));
  const uint8_t expected[] = {0x03, 0x00, 0x6B, 0x00, 0x03};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, pdu, 5);
}

void test_build_read_pdu_rejects_undersized_buffer(void) {
  mb_diag_read_request_t req{9, 3, 0, 1};
  uint8_t pdu[4];
  TEST_ASSERT_EQUAL_size_t(0, mb_diag_build_read_pdu(&req, pdu, sizeof(pdu)));
}

void test_build_read_values_json_holding_registers(void) {
  mb_diag_read_request_t req{9, 3, 0, 2};
  const uint8_t response_pdu[] = {0x03, 0x04, 0x00, 0xFF, 0x12, 0x34};  // 2 registre: 0x00FF, 0x1234
  char out[256];
  const size_t len = mb_diag_build_read_values_json(&req, response_pdu, sizeof(response_pdu), out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ok\":true"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"values\":[255,4660]"));
}

void test_build_read_values_json_coils(void) {
  mb_diag_read_request_t req{9, 1, 0, 10};
  // byte0 = 0b00000101 (bit0=1,bit2=1), byte1 = 0b00000001 (bit8=1) - kun de foerste 10 bits taeller
  const uint8_t response_pdu[] = {0x01, 0x02, 0x05, 0x01};
  char out[256];
  const size_t len = mb_diag_build_read_values_json(&req, response_pdu, sizeof(response_pdu), out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"values\":[1,0,1,0,0,0,0,0,1,0]"));
}

void test_build_read_values_json_rejects_truncated_pdu(void) {
  mb_diag_read_request_t req{9, 3, 0, 2};
  const uint8_t response_pdu[] = {0x03, 0x04, 0x00, 0xFF};  // byte_count=4 men kun 2 databytes med
  char out[256];
  TEST_ASSERT_EQUAL_size_t(0, mb_diag_build_read_values_json(&req, response_pdu, sizeof(response_pdu), out, sizeof(out)));
}

// ---------------------------------------------------------------------------
// Write: parse
// ---------------------------------------------------------------------------

void test_parse_write_request_fc06(void) {
  const char *json = "{\"function_code\":6,\"slave_id\":9,\"address\":10,\"value\":1234}";
  mb_diag_write_request_t req{};
  TEST_ASSERT_TRUE(mb_diag_parse_write_request(json, strlen(json), &req));
  TEST_ASSERT_EQUAL_UINT8(6, req.function_code);
  TEST_ASSERT_EQUAL_UINT16(10, req.address);
  TEST_ASSERT_EQUAL_UINT16(1, req.value_count);
  TEST_ASSERT_EQUAL_UINT16(1234, req.values[0]);
}

void test_parse_write_request_fc05_true_maps_to_ff00(void) {
  const char *json = "{\"function_code\":5,\"slave_id\":9,\"address\":3,\"value\":true}";
  mb_diag_write_request_t req{};
  TEST_ASSERT_TRUE(mb_diag_parse_write_request(json, strlen(json), &req));
  TEST_ASSERT_EQUAL_HEX16(0xFF00, req.values[0]);
}

void test_parse_write_request_fc05_false_maps_to_0000(void) {
  const char *json = "{\"function_code\":5,\"slave_id\":9,\"address\":3,\"value\":false}";
  mb_diag_write_request_t req{};
  TEST_ASSERT_TRUE(mb_diag_parse_write_request(json, strlen(json), &req));
  TEST_ASSERT_EQUAL_HEX16(0x0000, req.values[0]);
}

void test_parse_write_request_fc16_values_array(void) {
  const char *json = "{\"function_code\":16,\"slave_id\":9,\"address\":0,\"values\":[1,2,3]}";
  mb_diag_write_request_t req{};
  TEST_ASSERT_TRUE(mb_diag_parse_write_request(json, strlen(json), &req));
  TEST_ASSERT_EQUAL_UINT16(3, req.value_count);
  TEST_ASSERT_EQUAL_UINT16(1, req.values[0]);
  TEST_ASSERT_EQUAL_UINT16(2, req.values[1]);
  TEST_ASSERT_EQUAL_UINT16(3, req.values[2]);
}

void test_parse_write_request_fc15_values_array(void) {
  // v0.27.1: "values" er BOOLEANS (matcher PLC-teamets foreslåede kontrakt
  // OG FC05's egen bool-konvention), ikke 0/1-tal som FC16.
  const char *json = "{\"function_code\":15,\"slave_id\":9,\"address\":0,\"values\":[true,false,true,true]}";
  mb_diag_write_request_t req{};
  TEST_ASSERT_TRUE(mb_diag_parse_write_request(json, strlen(json), &req));
  TEST_ASSERT_EQUAL_UINT16(4, req.value_count);
  TEST_ASSERT_EQUAL_UINT16(1, req.values[0]);
  TEST_ASSERT_EQUAL_UINT16(0, req.values[1]);
  TEST_ASSERT_EQUAL_UINT16(1, req.values[2]);
  TEST_ASSERT_EQUAL_UINT16(1, req.values[3]);
}

void test_parse_write_request_fc15_rejects_missing_values(void) {
  const char *json = "{\"function_code\":15,\"slave_id\":9,\"address\":0}";
  mb_diag_write_request_t req{};
  TEST_ASSERT_FALSE(mb_diag_parse_write_request(json, strlen(json), &req));
}

void test_parse_write_request_fc15_rejects_numeric_values(void) {
  // v0.27.1: FC16-stil tal-array skal bevidst AFVISES for FC15 - kontrakten
  // kræver booleans, ikke 0/1-tal (samme skarpe skelnen som FC05's "value").
  const char *json = "{\"function_code\":15,\"slave_id\":9,\"address\":0,\"values\":[1,0,1]}";
  mb_diag_write_request_t req{};
  TEST_ASSERT_FALSE(mb_diag_parse_write_request(json, strlen(json), &req));
}

void test_parse_write_request_rejects_invalid_function_code(void) {
  const char *json = "{\"function_code\":3,\"slave_id\":9,\"address\":0,\"value\":1}";
  mb_diag_write_request_t req{};
  TEST_ASSERT_FALSE(mb_diag_parse_write_request(json, strlen(json), &req));
}

void test_parse_write_request_fc16_rejects_missing_values(void) {
  const char *json = "{\"function_code\":16,\"slave_id\":9,\"address\":0}";
  mb_diag_write_request_t req{};
  TEST_ASSERT_FALSE(mb_diag_parse_write_request(json, strlen(json), &req));
}

// ---------------------------------------------------------------------------
// Write: build PDU + confirmation JSON
// ---------------------------------------------------------------------------

void test_build_write_pdu_fc06(void) {
  mb_diag_write_request_t req{};
  req.function_code = 6;
  req.address = 0x0001;
  req.values[0] = 0x0003;
  req.value_count = 1;
  uint8_t pdu[5];
  TEST_ASSERT_EQUAL_size_t(5, mb_diag_build_write_pdu(&req, pdu, sizeof(pdu)));
  const uint8_t expected[] = {0x06, 0x00, 0x01, 0x00, 0x03};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, pdu, 5);
}

void test_build_write_pdu_fc16(void) {
  mb_diag_write_request_t req{};
  req.function_code = 16;
  req.address = 0x0000;
  req.values[0] = 1;
  req.values[1] = 2;
  req.value_count = 2;
  uint8_t pdu[10];
  const size_t len = mb_diag_build_write_pdu(&req, pdu, sizeof(pdu));
  TEST_ASSERT_EQUAL_size_t(10, len);
  const uint8_t expected[] = {0x10, 0x00, 0x00, 0x00, 0x02, 0x04, 0x00, 0x01, 0x00, 0x02};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, pdu, 10);
}

void test_build_write_pdu_fc15(void) {
  // 3 coils: 1,0,1 -> byte 0x05 (bit0=1, bit1=0, bit2=1)
  mb_diag_write_request_t req{};
  req.function_code = 15;
  req.address = 0x0000;
  req.values[0] = 1;
  req.values[1] = 0;
  req.values[2] = 1;
  req.value_count = 3;
  uint8_t pdu[7];
  const size_t len = mb_diag_build_write_pdu(&req, pdu, sizeof(pdu));
  TEST_ASSERT_EQUAL_size_t(7, len);
  const uint8_t expected[] = {0x0F, 0x00, 0x00, 0x00, 0x03, 0x01, 0x05};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, pdu, 7);
}

void test_build_write_pdu_fc15_spans_multiple_bytes(void) {
  // 9 coils, alle 1 -> byte_count=2, bytes = 0xFF, 0x01 (kun bit0 sat i 2. byte)
  mb_diag_write_request_t req{};
  req.function_code = 15;
  req.address = 0x0000;
  for (size_t i = 0; i < 9; i++) req.values[i] = 1;
  req.value_count = 9;
  uint8_t pdu[8];
  const size_t len = mb_diag_build_write_pdu(&req, pdu, sizeof(pdu));
  TEST_ASSERT_EQUAL_size_t(8, len);
  const uint8_t expected[] = {0x0F, 0x00, 0x00, 0x00, 0x09, 0x02, 0xFF, 0x01};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, pdu, 8);
}

void test_build_write_confirmation_json(void) {
  mb_diag_write_request_t req{};
  req.function_code = 6;
  req.slave_id = 9;
  req.address = 10;
  req.value_count = 1;
  const uint8_t response_pdu[] = {0x06, 0x00, 0x0A, 0x04, 0xD2};
  char out[256];
  const size_t len = mb_diag_build_write_confirmation_json(&req, response_pdu, sizeof(response_pdu), out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ok\":true"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"address\":10"));
}

// ---------------------------------------------------------------------------
// Fælles: exception-detektion
// ---------------------------------------------------------------------------

void test_is_exception_detects_high_bit(void) {
  const uint8_t response_pdu[] = {0x83, 0x02};  // FC03|0x80, Illegal Data Address
  TEST_ASSERT_TRUE(mb_diag_is_exception(response_pdu, sizeof(response_pdu)));
}

void test_is_exception_false_for_normal_response(void) {
  const uint8_t response_pdu[] = {0x03, 0x02, 0x00, 0xFF};
  TEST_ASSERT_FALSE(mb_diag_is_exception(response_pdu, sizeof(response_pdu)));
}

void test_build_exception_json(void) {
  const uint8_t response_pdu[] = {0x83, 0x02};
  char out[256];
  const size_t len = mb_diag_build_exception_json(9, response_pdu, sizeof(response_pdu), out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ok\":false"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"exception_code\":2"));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();

  RUN_TEST(test_parse_read_request_valid);
  RUN_TEST(test_parse_read_request_rejects_invalid_function_code);
  RUN_TEST(test_parse_read_request_rejects_missing_field);
  RUN_TEST(test_parse_read_request_rejects_slave_id_zero);

  RUN_TEST(test_build_read_pdu_matches_expected_bytes);
  RUN_TEST(test_build_read_pdu_rejects_undersized_buffer);
  RUN_TEST(test_build_read_values_json_holding_registers);
  RUN_TEST(test_build_read_values_json_coils);
  RUN_TEST(test_build_read_values_json_rejects_truncated_pdu);

  RUN_TEST(test_parse_write_request_fc06);
  RUN_TEST(test_parse_write_request_fc05_true_maps_to_ff00);
  RUN_TEST(test_parse_write_request_fc05_false_maps_to_0000);
  RUN_TEST(test_parse_write_request_fc16_values_array);
  RUN_TEST(test_parse_write_request_fc15_values_array);
  RUN_TEST(test_parse_write_request_fc15_rejects_missing_values);
  RUN_TEST(test_parse_write_request_fc15_rejects_numeric_values);
  RUN_TEST(test_parse_write_request_rejects_invalid_function_code);
  RUN_TEST(test_parse_write_request_fc16_rejects_missing_values);

  RUN_TEST(test_build_write_pdu_fc06);
  RUN_TEST(test_build_write_pdu_fc16);
  RUN_TEST(test_build_write_pdu_fc15);
  RUN_TEST(test_build_write_pdu_fc15_spans_multiple_bytes);
  RUN_TEST(test_build_write_confirmation_json);

  RUN_TEST(test_is_exception_detects_high_bit);
  RUN_TEST(test_is_exception_false_for_normal_response);
  RUN_TEST(test_build_exception_json);

  return UNITY_END();
}
