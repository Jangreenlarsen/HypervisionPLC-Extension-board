#include <unity.h>

#include <cstring>

#include "rest_status.h"

void setUp(void) {}
void tearDown(void) {}

void test_status_json_when_connected(void) {
  const mb_status_data_t data = {
      "0.6.0", "0006", 3661, 123456, 2, true, "192.168.1.50", -47, true, MB_CHANNEL_MODE_RS485,
      false, nullptr, "not_detected",
  };
  char out[512];
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
  TEST_ASSERT_NOT_NULL(strstr(out, "\"board_mode\":\"rs485\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"wifi\":{\"connected\":true,\"ip\":\"192.168.1.50\",\"rssi_dbm\":-47}"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ethernet\":{\"connected\":false,\"status\":\"not_detected\"}"));
}

void test_status_json_board_mode_rs232(void) {
  const mb_status_data_t data = {
      "0.14.0", "0017", 3661, 123456, 2, true, "192.168.1.50", -47, true, MB_CHANNEL_MODE_RS232,
      false, nullptr, "not_detected",
  };
  char out[512];
  const size_t len = mb_status_build_json(&data, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"board_mode\":\"rs232\""));
}

// v0.31.0 (Jan: "board skal signalere til plc at det er et 4 x rs232 eller
// 4 x rs485, alt efter jumper").
void test_status_json_board_type_and_expander(void) {
  struct {
    uint8_t channels;
    mb_channel_mode_t mode;
    const char *expander;
    const char *want_type;
    const char *want_expander;
  } cases[] = {
      {4, MB_CHANNEL_MODE_RS485, "ok", "\"board_type\":\"4xRS485\"", "\"expander\":\"ok\""},
      {4, MB_CHANNEL_MODE_RS232, "ok", "\"board_type\":\"4xRS232\"", "\"expander\":\"ok\""},
      {4, MB_CHANNEL_MODE_RS485, "not_found", "\"board_type\":\"4xRS485\"", "\"expander\":\"not_found\""},
      {2, MB_CHANNEL_MODE_RS232, "not_fitted", "\"board_type\":\"2xRS232\"", "\"expander\":\"not_fitted\""},
      {2, MB_CHANNEL_MODE_RS485, nullptr, "\"board_type\":\"2xRS485\"", "\"expander\":\"not_fitted\""},
  };
  for (const auto &c : cases) {
    const mb_status_data_t data = {
        "0.31.0", "0049", 10, 300000, c.channels, false, nullptr, 0, true, c.mode, true, "10.1.1.26", "connected",
        c.expander,
    };
    char out[512];
    TEST_ASSERT_TRUE(mb_status_build_json(&data, out, sizeof(out)) > 0);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, c.want_type), c.want_type);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, c.want_expander), c.want_expander);
  }
}

void test_status_json_when_disconnected_omits_ip_rssi(void) {
  const mb_status_data_t data = {
      "0.6.0", "0006", 10, 300000, 2, false, nullptr, 0, false, MB_CHANNEL_MODE_RS485,
      false, nullptr, "not_detected",
  };
  char out[512];
  const size_t len = mb_status_build_json(&data, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);

  TEST_ASSERT_NOT_NULL(strstr(out, "\"wifi\":{\"connected\":false}"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"provisioned\":false"));
  TEST_ASSERT_NULL_MESSAGE(strstr(out, "\"ip\":"), "ip-felt bor ikke vaere til stede naar ikke forbundet");
  TEST_ASSERT_NULL_MESSAGE(strstr(out, "rssi_dbm"), "rssi-felt bor ikke vaere til stede naar ikke forbundet");
}

void test_status_json_ethernet_connected(void) {
  const mb_status_data_t data = {
      "0.12.0", "0015", 10, 300000, 2, false, nullptr, 0, true, MB_CHANNEL_MODE_RS485,
      true, "10.1.1.50", "connected",
  };
  char out[512];
  const size_t len = mb_status_build_json(&data, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"wifi\":{\"connected\":false}"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ethernet\":{\"connected\":true,\"ip\":\"10.1.1.50\",\"status\":\"connected\"}"));
}

void test_status_json_ethernet_link_down_reports_status_while_disconnected(void) {
  // v0.18.0: modul fundet (SPI-kommunikation OK), men intet netvaerkskabel -
  // "status" skal skelne dette fra "not_detected", selvom "connected" er
  // false i begge tilfaelde.
  const mb_status_data_t data = {
      "0.18.0", "0021", 10, 300000, 2, false, nullptr, 0, true, MB_CHANNEL_MODE_RS485,
      false, nullptr, "link_down",
  };
  char out[512];
  const size_t len = mb_status_build_json(&data, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ethernet\":{\"connected\":false,\"status\":\"link_down\"}"));
}

void test_status_json_rejects_undersized_buffer(void) {
  const mb_status_data_t data = {
      "0.6.0", "0006", 10, 300000, 2, false, nullptr, 0, false, MB_CHANNEL_MODE_RS485,
      false, nullptr, "not_detected",
  };
  char out[8];  // alt for lille
  TEST_ASSERT_EQUAL_size_t(0, mb_status_build_json(&data, out, sizeof(out)));
}

void test_status_json_is_balanced_braces(void) {
  const mb_status_data_t data = {
      "0.6.0", "0006", 3661, 123456, 2, true, "192.168.1.50", -47, true, MB_CHANNEL_MODE_RS485,
      true, "192.168.1.99", "connected",
  };
  char out[512];  // §CLAUDE.md regel 14: nok margin til at ALDRIG stille afsløre en for-lille-buffer-fejl som "ubalancerede krøller"
  const size_t len = mb_status_build_json(&data, out, sizeof(out));
  TEST_ASSERT_TRUE_MESSAGE(len > 0, "mb_status_build_json fejlede (buffer for lille?) - resten af testen er meningsløs");

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

// ---------------------------------------------------------------------------
// mb_status_build_capabilities_json (v0.28.0, GET /api/capabilities)
// ---------------------------------------------------------------------------

void test_capabilities_json_full_shape(void) {
  const uint8_t fcs[] = {1, 2, 3, 4, 5, 6, 15, 16};
  const mb_capabilities_data_t data = {
      "0.28.0", fcs, sizeof(fcs), 2000, 1968, fcs, sizeof(fcs), 2000, 32,
  };
  char out[512];
  const size_t len = mb_status_build_capabilities_json(&data, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_EQUAL_size_t(strlen(out), len);

  TEST_ASSERT_NOT_NULL(strstr(out, "\"api_version\":1"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"fw_version\":\"0.28.0\""));
  TEST_ASSERT_NOT_NULL(
      strstr(out, "\"modbus_tcp\":{\"supported_function_codes\":[1,2,3,4,5,6,15,16],\"max_read_quantity\":2000,"
                  "\"max_write_quantity\":1968}"));
  TEST_ASSERT_NOT_NULL(
      strstr(out, "\"rest_diagnostic\":{\"supported_function_codes\":[1,2,3,4,5,6,15,16],\"max_read_quantity\":2000,"
                  "\"max_write_quantity\":32}"));
}

void test_capabilities_json_lists_can_differ(void) {
  // Designdokumentets pointe: de to lister skal kunne divergere uden at
  // skjule det bag én fælles liste.
  const uint8_t tcp_fcs[] = {1, 2, 3, 4, 5, 6, 16};  // FC15 mangler her
  const uint8_t rest_fcs[] = {1, 2, 3, 4, 5, 6, 15, 16};
  const mb_capabilities_data_t data = {
      "0.27.0", tcp_fcs, sizeof(tcp_fcs), 2000, 123, rest_fcs, sizeof(rest_fcs), 2000, 32,
  };
  char out[512];
  const size_t len = mb_status_build_capabilities_json(&data, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"modbus_tcp\":{\"supported_function_codes\":[1,2,3,4,5,6,16]"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"rest_diagnostic\":{\"supported_function_codes\":[1,2,3,4,5,6,15,16]"));
}

void test_capabilities_json_is_balanced_braces(void) {
  const uint8_t fcs[] = {1, 2, 3, 4, 5, 6, 15, 16};
  const mb_capabilities_data_t data = {
      "0.28.0", fcs, sizeof(fcs), 2000, 1968, fcs, sizeof(fcs), 2000, 32,
  };
  char out[512];
  const size_t len = mb_status_build_capabilities_json(&data, out, sizeof(out));
  TEST_ASSERT_TRUE_MESSAGE(len > 0, "mb_status_build_capabilities_json fejlede - resten af testen er meningsløs");

  int depth = 0;
  for (const char *p = out; *p != '\0'; p++) {
    if (*p == '{') depth++;
    if (*p == '}') depth--;
    TEST_ASSERT_TRUE_MESSAGE(depth >= 0, "ubalanceret '}' fundet foer matchende '{'");
  }
  TEST_ASSERT_EQUAL_MESSAGE(0, depth, "ubalancerede krøllede parenteser i JSON-output");
}

void test_capabilities_json_rejects_undersized_buffer(void) {
  const uint8_t fcs[] = {1, 2, 3, 4, 5, 6, 15, 16};
  const mb_capabilities_data_t data = {
      "0.28.0", fcs, sizeof(fcs), 2000, 1968, fcs, sizeof(fcs), 2000, 32,
  };
  char out[8];
  const size_t len = mb_status_build_capabilities_json(&data, out, sizeof(out));
  TEST_ASSERT_EQUAL_size_t(0, len);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();

  RUN_TEST(test_status_json_when_connected);
  RUN_TEST(test_status_json_board_mode_rs232);
  RUN_TEST(test_status_json_board_type_and_expander);
  RUN_TEST(test_status_json_when_disconnected_omits_ip_rssi);
  RUN_TEST(test_status_json_ethernet_connected);
  RUN_TEST(test_status_json_ethernet_link_down_reports_status_while_disconnected);
  RUN_TEST(test_status_json_rejects_undersized_buffer);
  RUN_TEST(test_status_json_is_balanced_braces);
  RUN_TEST(test_error_json_with_error_code);
  RUN_TEST(test_error_json_without_error_code);

  RUN_TEST(test_capabilities_json_full_shape);
  RUN_TEST(test_capabilities_json_lists_can_differ);
  RUN_TEST(test_capabilities_json_is_balanced_braces);
  RUN_TEST(test_capabilities_json_rejects_undersized_buffer);

  return UNITY_END();
}
