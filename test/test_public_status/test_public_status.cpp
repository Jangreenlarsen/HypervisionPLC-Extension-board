#include <unity.h>

#include <cstring>
#include <set>
#include <string>

#include "public_status.h"

void setUp(void) {}
void tearDown(void) {}

static mb_public_status_t sample(uint8_t channels) {
  mb_public_status_t d{};
  d.hostname = "ExtensionB1";
  d.fw_version = "0.36.0";
  d.fw_build = "0058";
  d.uptime_s = 90061;
  d.heap_free_bytes = 217468;
  d.reset_reason = "software";
  d.board_mode = MB_CHANNEL_MODE_RS485;
  d.expander = channels == 4 ? "ok" : "not_fitted";
  d.wifi_connected = false;
  d.eth_status = "connected";
  d.ota_pending_confirm = false;
  d.ota_last_update_rolled_back = false;
  d.channel_count = channels;
  for (uint8_t i = 0; i < channels; i++) {
    mb_public_channel_t &c = d.channels[i];
    c.letter = static_cast<char>('A' + i);
    c.status = "ok";
    c.mode = MB_CHANNEL_MODE_RS485;
    c.baudrate = 9600;
    c.parity = MB_CHANNEL_PARITY_NONE;
    c.stop_bits = 1;
    c.total_requests = 1000 + i;
    c.successful_requests = 990 + i;
    c.timeout_errors = 7;
    c.crc_errors = 2;
    c.exception_errors = 1;
  }
  return d;
}

// Alle "nøgle":-navne i en flad JSON-streng (værdier indeholder aldrig '":').
static std::set<std::string> keys_in(const std::string &json) {
  std::set<std::string> keys;
  size_t i = 0;
  while ((i = json.find('"', i)) != std::string::npos) {
    const size_t end = json.find('"', i + 1);
    if (end == std::string::npos) break;
    if (end + 1 < json.size() && json[end + 1] == ':') keys.insert(json.substr(i + 1, end - i - 1));
    i = end + 1;
  }
  return keys;
}

void test_json_has_expected_fields(void) {
  const mb_public_status_t d = sample(4);
  char out[1536];
  const size_t n = mb_public_status_build_json(&d, out, sizeof(out));
  TEST_ASSERT_EQUAL_size_t(strlen(out), n);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"hostname\":\"ExtensionB1\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"fw_version\":\"0.36.0\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"uptime_s\":90061"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"board_type\":\"4xRS485\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"active_channels\":4"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"expander\":\"ok\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ethernet\":\"connected\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"channel\":\"A\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"channel\":\"D\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"total_requests\":1003"));
  TEST_ASSERT_EQUAL('}', out[n - 1]);
}

// VAGTEN: den offentlige side har INGEN login, så kun disse nøgler må
// nogensinde optræde. Tilføjes et nyt felt, fejler denne test, indtil nogen
// bevidst har vurderet at feltet tåler at være offentligt og føjet det hertil.
void test_json_keys_are_exactly_the_public_allowlist(void) {
  const mb_public_status_t d = sample(4);
  char out[1536];
  TEST_ASSERT_TRUE(mb_public_status_build_json(&d, out, sizeof(out)) > 0);

  const std::set<std::string> allowed = {
      "product", "hostname", "fw_version", "fw_build", "uptime_s", "heap_free_bytes", "reset_reason", "board_type",
      "active_channels", "board_mode", "expander", "wifi_connected", "ethernet", "ota_pending_confirm",
      "ota_last_update_rolled_back", "channels",
      // pr. kanal
      "channel", "status", "mode", "baudrate", "parity", "stop_bits", "total_requests", "successful_requests",
      "timeout_errors", "crc_errors", "exception_errors"};
  for (const std::string &k : keys_in(out)) {
    TEST_ASSERT_TRUE_MESSAGE(allowed.count(k) == 1, ("uventet noegle i offentlig JSON: " + k).c_str());
  }
}

void test_json_never_contains_secrets_or_network_identity(void) {
  // Fyld ALLE strengfelter med skuffe-værdier; felter vi ikke har (token osv.)
  // findes slet ikke i structen — testen sikrer også, at ingen af de
  // forbudte ORD optræder som nøgle- eller værdi-tekst.
  const mb_public_status_t d = sample(2);
  char out[1536];
  TEST_ASSERT_TRUE(mb_public_status_build_json(&d, out, sizeof(out)) > 0);
  const char *forbidden[] = {"token", "pass", "ssid", "secret", "plc", "syslog", "mac", "slave", "address", "bearer", "ip\":"};
  for (const char *word : forbidden) {
    TEST_ASSERT_NULL_MESSAGE(strstr(out, word), word);
  }
}

void test_two_channel_board(void) {
  const mb_public_status_t d = sample(2);
  char out[1536];
  TEST_ASSERT_TRUE(mb_public_status_build_json(&d, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"board_type\":\"2xRS485\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"channel\":\"B\""));
  TEST_ASSERT_NULL(strstr(out, "\"channel\":\"C\""));
}

void test_rs232_board_type(void) {
  mb_public_status_t d = sample(4);
  d.board_mode = MB_CHANNEL_MODE_RS232;
  char out[1536];
  TEST_ASSERT_TRUE(mb_public_status_build_json(&d, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"board_type\":\"4xRS232\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"board_mode\":\"rs232\""));
}

void test_escapes_hostile_strings(void) {
  mb_public_status_t d = sample(2);
  d.hostname = "a\"b\\c\nd";
  d.reset_reason = "x\"y";
  char out[1536];
  TEST_ASSERT_TRUE(mb_public_status_build_json(&d, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"hostname\":\"a\\\"b\\\\c d\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"reset_reason\":\"x\\\"y\""));
  TEST_ASSERT_NULL_MESSAGE(strchr(out, '\n'), "raa linjeskift maa ikke naa ud i JSON");
}

void test_null_strings_get_safe_defaults(void) {
  mb_public_status_t d = sample(2);
  d.hostname = nullptr;
  d.reset_reason = nullptr;
  d.expander = nullptr;
  d.eth_status = nullptr;
  d.channels[0].status = nullptr;
  char out[1536];
  TEST_ASSERT_TRUE(mb_public_status_build_json(&d, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"hostname\":\"\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"reset_reason\":\"unknown\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"expander\":\"not_fitted\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"channel\":\"A\",\"status\":\"unavailable\""));
}

void test_too_small_buffer_gives_empty_not_partial_json(void) {
  const mb_public_status_t d = sample(4);
  char full[1536];
  const size_t n = mb_public_status_build_json(&d, full, sizeof(full));
  TEST_ASSERT_TRUE(n > 0);
  // Prøv hver buffer-størrelse fra 1 byte op til én under det krævede.
  for (size_t cap = 1; cap <= n; cap++) {
    char small[1536];
    memset(small, 'X', sizeof(small));
    TEST_ASSERT_EQUAL_size_t(0, mb_public_status_build_json(&d, small, cap));
    TEST_ASSERT_EQUAL_CHAR('\0', small[0]);
  }
  char exact[1536];
  TEST_ASSERT_EQUAL_size_t(n, mb_public_status_build_json(&d, exact, n + 1));
}

void test_invalid_arguments(void) {
  mb_public_status_t d = sample(4);
  char out[1536];
  TEST_ASSERT_EQUAL_size_t(0, mb_public_status_build_json(nullptr, out, sizeof(out)));
  TEST_ASSERT_EQUAL_size_t(0, mb_public_status_build_json(&d, nullptr, sizeof(out)));
  TEST_ASSERT_EQUAL_size_t(0, mb_public_status_build_json(&d, out, 0));
  d.channel_count = 0;
  TEST_ASSERT_EQUAL_size_t(0, mb_public_status_build_json(&d, out, sizeof(out)));
  d.channel_count = MB_PUBLIC_STATUS_MAX_CHANNELS + 1;  // afkortes ikke stille
  TEST_ASSERT_EQUAL_size_t(0, mb_public_status_build_json(&d, out, sizeof(out)));
}

void test_realistic_size_fits_buffer_used_on_board(void) {
  // Boardet bruger en 2048-byte buffer (src/http_server.cpp) — worst case:
  // 4 kanaler med store tællere og lange tekster.
  mb_public_status_t d = sample(4);
  d.hostname = "abcdefghijklmnopqrstuvwxyz012345";  // 32 tegn = maks.
  d.reset_reason = "int_wdt";
  for (auto &c : d.channels) {
    c.total_requests = c.successful_requests = c.timeout_errors = c.crc_errors = c.exception_errors = 4294967295u;
    c.baudrate = 115200;
    c.parity = MB_CHANNEL_PARITY_EVEN;
    c.stop_bits = 2;
    c.status = "unavailable";
  }
  char out[2048];
  const size_t n = mb_public_status_build_json(&d, out, sizeof(out));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_TRUE_MESSAGE(n < 2048 - 300, "worst-case JSON skal have mindst 300 bytes margin under bufferen");
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_json_has_expected_fields);
  RUN_TEST(test_json_keys_are_exactly_the_public_allowlist);
  RUN_TEST(test_json_never_contains_secrets_or_network_identity);
  RUN_TEST(test_two_channel_board);
  RUN_TEST(test_rs232_board_type);
  RUN_TEST(test_escapes_hostile_strings);
  RUN_TEST(test_null_strings_get_safe_defaults);
  RUN_TEST(test_too_small_buffer_gives_empty_not_partial_json);
  RUN_TEST(test_invalid_arguments);
  RUN_TEST(test_realistic_size_fits_buffer_used_on_board);
  return UNITY_END();
}
