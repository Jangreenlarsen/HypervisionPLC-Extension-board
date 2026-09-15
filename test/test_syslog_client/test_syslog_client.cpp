#include <unity.h>

#include <cstring>

#include "syslog_client.h"

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// mb_syslog_severity_from_level — level 1-8 -> RFC 3164 severity 0-7
// ---------------------------------------------------------------------------

void test_severity_from_level_bijection(void) {
  TEST_ASSERT_EQUAL_UINT8(0, mb_syslog_severity_from_level(1));
  TEST_ASSERT_EQUAL_UINT8(1, mb_syslog_severity_from_level(2));
  TEST_ASSERT_EQUAL_UINT8(6, mb_syslog_severity_from_level(7));
  TEST_ASSERT_EQUAL_UINT8(7, mb_syslog_severity_from_level(8));
}

void test_severity_from_level_clamps_out_of_range(void) {
  TEST_ASSERT_EQUAL_UINT8(0, mb_syslog_severity_from_level(0));
  TEST_ASSERT_EQUAL_UINT8(7, mb_syslog_severity_from_level(9));
  TEST_ASSERT_EQUAL_UINT8(7, mb_syslog_severity_from_level(255));
}

// ---------------------------------------------------------------------------
// mb_syslog_build_packet
// ---------------------------------------------------------------------------

void test_build_packet_pri_field(void) {
  char buf[MB_SYSLOG_PACKET_MAX_LEN];
  // facility local0 (16) * 8 + severity(level 1)=0 -> PRI 128
  const size_t len = mb_syslog_build_packet(MB_SYSLOG_FACILITY_MODBUS, 1, "modbus_b", "board1", 0, "hej", buf,
                                             sizeof(buf));
  TEST_ASSERT_GREATER_THAN(0, len);
  TEST_ASSERT_EQUAL_STRING_LEN("<128>", buf, 5);
}

void test_build_packet_pri_field_highest_level(void) {
  char buf[MB_SYSLOG_PACKET_MAX_LEN];
  // facility local0 (16) * 8 + severity(level 8)=7 -> PRI 135
  const size_t len = mb_syslog_build_packet(MB_SYSLOG_FACILITY_MODBUS, 8, "modbus_b", "board1", 0, "hej", buf,
                                             sizeof(buf));
  TEST_ASSERT_GREATER_THAN(0, len);
  TEST_ASSERT_EQUAL_STRING_LEN("<135>", buf, 5);
}

void test_build_packet_different_facility(void) {
  char buf[MB_SYSLOG_PACKET_MAX_LEN];
  // facility local2 (18) * 8 + severity(level 1)=0 -> PRI 144
  const size_t len =
      mb_syslog_build_packet(MB_SYSLOG_FACILITY_REST, 1, "rest", "board1", 0, "401 unauthorized", buf, sizeof(buf));
  TEST_ASSERT_GREATER_THAN(0, len);
  TEST_ASSERT_EQUAL_STRING_LEN("<144>", buf, 5);
}

void test_build_packet_contains_hostname_tag_message(void) {
  char buf[MB_SYSLOG_PACKET_MAX_LEN];
  mb_syslog_build_packet(MB_SYSLOG_FACILITY_MODBUS, 1, "mytag", "myhost", 3661, "test besked", buf, sizeof(buf));
  TEST_ASSERT_NOT_NULL(strstr(buf, "myhost"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "mytag:"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "test besked"));
}

void test_build_packet_uptime_formats_as_hhmmss(void) {
  char buf[MB_SYSLOG_PACKET_MAX_LEN];
  // 3661s = 1h 1m 1s
  mb_syslog_build_packet(MB_SYSLOG_FACILITY_MODBUS, 1, "t", "h", 3661, "m", buf, sizeof(buf));
  TEST_ASSERT_NOT_NULL(strstr(buf, "01:01:01"));
}

void test_build_packet_truncates_to_capacity(void) {
  char buf[16];
  const size_t len = mb_syslog_build_packet(MB_SYSLOG_FACILITY_MODBUS, 1, "tag", "host", 0,
                                             "en meget lang besked der ikke kan vaere der", buf, sizeof(buf));
  TEST_ASSERT_LESS_THAN(sizeof(buf), len);
  TEST_ASSERT_EQUAL_size_t(strlen(buf), len);
}

void test_build_packet_rejects_null_args(void) {
  char buf[MB_SYSLOG_PACKET_MAX_LEN];
  TEST_ASSERT_EQUAL_size_t(0, mb_syslog_build_packet(MB_SYSLOG_FACILITY_MODBUS, 1, nullptr, "h", 0, "m", buf,
                                                       sizeof(buf)));
  TEST_ASSERT_EQUAL_size_t(0, mb_syslog_build_packet(MB_SYSLOG_FACILITY_MODBUS, 1, "t", nullptr, 0, "m", buf,
                                                       sizeof(buf)));
  TEST_ASSERT_EQUAL_size_t(0, mb_syslog_build_packet(MB_SYSLOG_FACILITY_MODBUS, 1, "t", "h", 0, nullptr, buf,
                                                       sizeof(buf)));
  TEST_ASSERT_EQUAL_size_t(0, mb_syslog_build_packet(MB_SYSLOG_FACILITY_MODBUS, 1, "t", "h", 0, "m", nullptr,
                                                       sizeof(buf)));
  TEST_ASSERT_EQUAL_size_t(0, mb_syslog_build_packet(MB_SYSLOG_FACILITY_MODBUS, 1, "t", "h", 0, "m", buf, 0));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();

  RUN_TEST(test_severity_from_level_bijection);
  RUN_TEST(test_severity_from_level_clamps_out_of_range);

  RUN_TEST(test_build_packet_pri_field);
  RUN_TEST(test_build_packet_pri_field_highest_level);
  RUN_TEST(test_build_packet_different_facility);
  RUN_TEST(test_build_packet_contains_hostname_tag_message);
  RUN_TEST(test_build_packet_uptime_formats_as_hhmmss);
  RUN_TEST(test_build_packet_truncates_to_capacity);
  RUN_TEST(test_build_packet_rejects_null_args);

  return UNITY_END();
}
