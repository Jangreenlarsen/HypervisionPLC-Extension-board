#include <unity.h>

#include <cstring>

#include "channel_config.h"

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// mb_is_valid_baudrate
// ---------------------------------------------------------------------------

void test_valid_baudrates_accepted(void) {
  TEST_ASSERT_TRUE(mb_is_valid_baudrate(1200));
  TEST_ASSERT_TRUE(mb_is_valid_baudrate(9600));
  TEST_ASSERT_TRUE(mb_is_valid_baudrate(19200));
  TEST_ASSERT_TRUE(mb_is_valid_baudrate(115200));
}

void test_invalid_baudrates_rejected(void) {
  TEST_ASSERT_FALSE(mb_is_valid_baudrate(0));
  TEST_ASSERT_FALSE(mb_is_valid_baudrate(9601));
  TEST_ASSERT_FALSE(mb_is_valid_baudrate(300));
}

// ---------------------------------------------------------------------------
// mb_channel_build_json
// ---------------------------------------------------------------------------

void test_build_json_ok_status(void) {
  mb_channel_config_t config{};
  config.enabled = true;
  config.mode = MB_CHANNEL_MODE_RS485;
  config.baudrate = 9600;
  config.parity = MB_CHANNEL_PARITY_NONE;
  config.stop_bits = 1;
  config.timeout_ms = 500;
  config.inter_frame_delay_ms = 0;

  mb_channel_stats_t stats{};
  stats.total_requests = 10;
  stats.successful_requests = 9;
  stats.timeout_errors = 1;
  stats.has_last_error = false;

  char buf[512];
  const size_t len = mb_channel_build_json(1, &config, &stats, buf, sizeof(buf));
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"channel\":1"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"enabled\":true"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"mode\":\"rs485\""));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"baudrate\":9600"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"parity\":\"none\""));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"status\":\"ok\""));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"total_requests\":10"));
}

void test_build_json_disabled_status_overrides_error(void) {
  mb_channel_config_t config{};
  config.enabled = false;
  config.mode = MB_CHANNEL_MODE_RS232;
  config.baudrate = 19200;
  config.parity = MB_CHANNEL_PARITY_EVEN;
  config.stop_bits = 2;
  config.timeout_ms = 1000;

  mb_channel_stats_t stats{};
  stats.has_last_error = true;  // ville normalt give "error" - "disabled" har forrang

  char buf[512];
  mb_channel_build_json(2, &config, &stats, buf, sizeof(buf));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"status\":\"disabled\""));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"mode\":\"rs232\""));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"parity\":\"even\""));
}

void test_build_json_error_status(void) {
  mb_channel_config_t config{};
  config.enabled = true;
  config.mode = MB_CHANNEL_MODE_RS485;
  config.baudrate = 9600;
  config.parity = MB_CHANNEL_PARITY_ODD;
  config.stop_bits = 1;
  config.timeout_ms = 500;

  mb_channel_stats_t stats{};
  stats.has_last_error = true;
  stats.last_error_slave_id = 12;
  stats.last_error_address = 40010;
  stats.last_error_type = 1;
  stats.last_error_at_uptime_s = 86112;

  char buf[512];
  mb_channel_build_json(1, &config, &stats, buf, sizeof(buf));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"status\":\"error\""));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"parity\":\"odd\""));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"last_error_slave_id\":12"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"last_error_address\":40010"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"last_error_at_uptime_s\":86112"));
}

void test_build_json_rejects_undersized_buffer(void) {
  mb_channel_config_t config{};
  mb_channel_stats_t stats{};
  char buf[8];  // alt for lille
  TEST_ASSERT_EQUAL_size_t(0, mb_channel_build_json(1, &config, &stats, buf, sizeof(buf)));
}

// ---------------------------------------------------------------------------
// mb_channel_parse_config_json
// ---------------------------------------------------------------------------

const char *kFullValidJson =
    "{\"enabled\":true,\"mode\":\"rs485\",\"baudrate\":9600,\"parity\":\"none\","
    "\"stop_bits\":1,\"timeout_ms\":500,\"inter_frame_delay_ms\":0}";

void test_parse_valid_full_json(void) {
  mb_channel_config_t config{};
  const bool ok = mb_channel_parse_config_json(kFullValidJson, strlen(kFullValidJson), &config);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_TRUE(config.enabled);
  TEST_ASSERT_EQUAL(MB_CHANNEL_MODE_RS485, config.mode);
  TEST_ASSERT_EQUAL_UINT32(9600, config.baudrate);
  TEST_ASSERT_EQUAL(MB_CHANNEL_PARITY_NONE, config.parity);
  TEST_ASSERT_EQUAL_UINT8(1, config.stop_bits);
  TEST_ASSERT_EQUAL_UINT32(500, config.timeout_ms);
  TEST_ASSERT_EQUAL_UINT32(0, config.inter_frame_delay_ms);
}

void test_parse_handles_field_order_and_whitespace(void) {
  const char *json =
      "{ \"timeout_ms\": 750, \"inter_frame_delay_ms\": 10, \"baudrate\": 19200,\n"
      "  \"mode\": \"rs232\", \"parity\": \"odd\", \"stop_bits\": 2, \"enabled\": false }";
  mb_channel_config_t config{};
  const bool ok = mb_channel_parse_config_json(json, strlen(json), &config);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_FALSE(config.enabled);
  TEST_ASSERT_EQUAL(MB_CHANNEL_MODE_RS232, config.mode);
  TEST_ASSERT_EQUAL_UINT32(19200, config.baudrate);
  TEST_ASSERT_EQUAL(MB_CHANNEL_PARITY_ODD, config.parity);
  TEST_ASSERT_EQUAL_UINT8(2, config.stop_bits);
  TEST_ASSERT_EQUAL_UINT32(750, config.timeout_ms);
  TEST_ASSERT_EQUAL_UINT32(10, config.inter_frame_delay_ms);
}

void test_parse_rejects_missing_field(void) {
  // §4.2: atomisk - mangler blot ét felt, skal HELE requestet afvises.
  const char *json_missing_baudrate =
      "{\"enabled\":true,\"mode\":\"rs485\","
      "\"parity\":\"none\",\"stop_bits\":1,\"timeout_ms\":500,\"inter_frame_delay_ms\":0}";
  mb_channel_config_t config{};
  TEST_ASSERT_FALSE(mb_channel_parse_config_json(json_missing_baudrate, strlen(json_missing_baudrate), &config));
}

void test_parse_rejects_invalid_mode(void) {
  const char *json =
      "{\"enabled\":true,\"mode\":\"rs422\",\"baudrate\":9600,\"parity\":\"none\","
      "\"stop_bits\":1,\"timeout_ms\":500,\"inter_frame_delay_ms\":0}";
  mb_channel_config_t config{};
  TEST_ASSERT_FALSE(mb_channel_parse_config_json(json, strlen(json), &config));
}

void test_parse_rejects_invalid_baudrate(void) {
  const char *json =
      "{\"enabled\":true,\"mode\":\"rs485\",\"baudrate\":31337,\"parity\":\"none\","
      "\"stop_bits\":1,\"timeout_ms\":500,\"inter_frame_delay_ms\":0}";
  mb_channel_config_t config{};
  TEST_ASSERT_FALSE(mb_channel_parse_config_json(json, strlen(json), &config));
}

void test_parse_rejects_invalid_stop_bits(void) {
  const char *json =
      "{\"enabled\":true,\"mode\":\"rs485\",\"baudrate\":9600,\"parity\":\"none\","
      "\"stop_bits\":3,\"timeout_ms\":500,\"inter_frame_delay_ms\":0}";
  mb_channel_config_t config{};
  TEST_ASSERT_FALSE(mb_channel_parse_config_json(json, strlen(json), &config));
}

void test_parse_rejects_zero_timeout(void) {
  const char *json =
      "{\"enabled\":true,\"mode\":\"rs485\",\"baudrate\":9600,\"parity\":\"none\","
      "\"stop_bits\":1,\"timeout_ms\":0,\"inter_frame_delay_ms\":0}";
  mb_channel_config_t config{};
  TEST_ASSERT_FALSE(mb_channel_parse_config_json(json, strlen(json), &config));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();

  RUN_TEST(test_valid_baudrates_accepted);
  RUN_TEST(test_invalid_baudrates_rejected);

  RUN_TEST(test_build_json_ok_status);
  RUN_TEST(test_build_json_disabled_status_overrides_error);
  RUN_TEST(test_build_json_error_status);
  RUN_TEST(test_build_json_rejects_undersized_buffer);

  RUN_TEST(test_parse_valid_full_json);
  RUN_TEST(test_parse_handles_field_order_and_whitespace);
  RUN_TEST(test_parse_rejects_missing_field);
  RUN_TEST(test_parse_rejects_invalid_mode);
  RUN_TEST(test_parse_rejects_invalid_baudrate);
  RUN_TEST(test_parse_rejects_invalid_stop_bits);
  RUN_TEST(test_parse_rejects_zero_timeout);

  return UNITY_END();
}
