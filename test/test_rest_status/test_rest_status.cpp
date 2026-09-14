#include <unity.h>

#include <cstring>

#include "rest_status.h"

void setUp(void) {}
void tearDown(void) {}

void test_status_json_when_connected(void) {
  const mb_status_data_t data = {
      "0.6.0", "0006", 3661, 123456, 2, true, "192.168.1.50", -47, true, false, nullptr,
  };
  char out[256];
  const size_t len = mb_status_build_json(&data, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_EQUAL_size_t(strlen(out), len);

  TEST_ASSERT_NOT_NULL(strstr(out, "\"api_version\":1"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"fw_version\":\"0.6.0\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"fw_build\":\"0006\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"uptime_s\":3661"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"heap_free_bytes\":123456"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"active_channels\":2"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"provisioned\":true"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"wifi\":{\"connected\":true,\"ip\":\"192.168.1.50\",\"rssi_dbm\":-47}"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ethernet\":{\"connected\":false}"));
}

void test_status_json_when_disconnected_omits_ip_rssi(void) {
  const mb_status_data_t data = {
      "0.6.0", "0006", 10, 300000, 2, false, nullptr, 0, false, false, nullptr,
  };
  char out[256];
  const size_t len = mb_status_build_json(&data, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);

  TEST_ASSERT_NOT_NULL(strstr(out, "\"wifi\":{\"connected\":false}"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"provisioned\":false"));
  TEST_ASSERT_NULL_MESSAGE(strstr(out, "\"ip\":"), "ip-felt bor ikke vaere til stede naar ikke forbundet");
  TEST_ASSERT_NULL_MESSAGE(strstr(out, "rssi_dbm"), "rssi-felt bor ikke vaere til stede naar ikke forbundet");
}

void test_status_json_ethernet_connected(void) {
  const mb_status_data_t data = {
      "0.12.0", "0015", 10, 300000, 2, false, nullptr, 0, true, true, "10.1.1.50",
  };
  char out[256];
  const size_t len = mb_status_build_json(&data, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"wifi\":{\"connected\":false}"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ethernet\":{\"connected\":true,\"ip\":\"10.1.1.50\"}"));
}

void test_status_json_rejects_undersized_buffer(void) {
  const mb_status_data_t data = {
      "0.6.0", "0006", 10, 300000, 2, false, nullptr, 0, false, false, nullptr,
  };
  char out[8];  // alt for lille
  TEST_ASSERT_EQUAL_size_t(0, mb_status_build_json(&data, out, sizeof(out)));
}

void test_status_json_is_balanced_braces(void) {
  const mb_status_data_t data = {
      "0.6.0", "0006", 3661, 123456, 2, true, "192.168.1.50", -47, true, true, "192.168.1.99",
  };
  char out[256];
  mb_status_build_json(&data, out, sizeof(out));

  int depth = 0;
  for (const char *p = out; *p != '\0'; p++) {
    if (*p == '{') depth++;
    if (*p == '}') depth--;
    TEST_ASSERT_TRUE_MESSAGE(depth >= 0, "ubalanceret '}' fundet foer matchende '{'");
  }
  TEST_ASSERT_EQUAL_MESSAGE(0, depth, "ubalancerede krøllede parenteser i JSON-output");
}

void test_error_json_with_error_code(void) {
  char out[128];
  const size_t len = mb_status_build_error_json(6, "invalid_slave", "Slave-ID skal vaere 1-247", out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_EQUAL_STRING("{\"ok\":false,\"error_code\":6,\"error\":\"invalid_slave\",\"message\":\"Slave-ID skal vaere 1-247\"}",
                           out);
}

void test_error_json_without_error_code(void) {
  char out[128];
  const size_t len = mb_status_build_error_json(-1, "unauthorized", "Manglende eller ugyldig Authorization-header",
                                                 out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_NULL_MESSAGE(strstr(out, "error_code"),
                            "error_code boer udelades for rene HTTP-/auth-fejl, ikke tvinges til en vaerdi");
  TEST_ASSERT_NOT_NULL(strstr(out, "\"error\":\"unauthorized\""));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();

  RUN_TEST(test_status_json_when_connected);
  RUN_TEST(test_status_json_when_disconnected_omits_ip_rssi);
  RUN_TEST(test_status_json_ethernet_connected);
  RUN_TEST(test_status_json_rejects_undersized_buffer);
  RUN_TEST(test_status_json_is_balanced_braces);
  RUN_TEST(test_error_json_with_error_code);
  RUN_TEST(test_error_json_without_error_code);

  return UNITY_END();
}
