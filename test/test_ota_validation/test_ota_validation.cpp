#include <unity.h>

#include <cstring>
#include <string>

#include "ota_validation.h"

void setUp(void) {}
void tearDown(void) {}

void test_accepts_valid_esp32_magic_byte(void) {
  const uint8_t data[] = {0xE9, 0x00, 0x00, 0x00};
  TEST_ASSERT_TRUE(mb_ota_is_valid_firmware_magic(data, sizeof(data)));
}

void test_rejects_wrong_magic_byte(void) {
  const uint8_t data[] = {0x00, 0x01, 0x02, 0x03};
  TEST_ASSERT_FALSE(mb_ota_is_valid_firmware_magic(data, sizeof(data)));
}

void test_rejects_empty_buffer(void) {
  const uint8_t data[] = {0xE9};
  TEST_ASSERT_FALSE(mb_ota_is_valid_firmware_magic(data, 0));
}

void test_rejects_null_pointer(void) {
  TEST_ASSERT_FALSE(mb_ota_is_valid_firmware_magic(nullptr, 4));
}

// --- v0.30.0: firmware-identitets-scanner -----------------------------------

// Et "image" med binært fyld omkring markøren, fodret i bidder af `chunk`.
static mb_fwid_scanner_t scan(const std::string &image, size_t chunk) {
  mb_fwid_scanner_t s;
  mb_fwid_scanner_init(&s);
  for (size_t off = 0; off < image.size(); off += chunk) {
    const size_t n = (image.size() - off < chunk) ? image.size() - off : chunk;
    mb_fwid_scanner_feed(&s, reinterpret_cast<const uint8_t *>(image.data() + off), n);
  }
  return s;
}

static std::string board_image(const char *version) {
  std::string img("\xE9\x03\x02\x20junk\x00\xff\x10", 12);
  img += "HVEXT-FW";  // falsk start - maa ikke forvirre matchet
  img += std::string(300, '\x00');
  img += MB_FWID_PREFIX;
  img += version;
  img += ";";
  img += std::string(200, '\xAA');
  return img;
}

void test_fwid_found_in_single_chunk(void) {
  const mb_fwid_scanner_t s = scan(board_image("0.30.0-b0047"), 4096);
  TEST_ASSERT_TRUE(s.found);
  TEST_ASSERT_EQUAL_STRING("0.30.0-b0047", s.version);
}

void test_fwid_found_across_every_chunk_boundary(void) {
  // Bid-stoerrelser 1..64 placerer markoeren over alle mulige graenser.
  const std::string img = board_image("1.2.3-b0099");
  for (size_t chunk = 1; chunk <= 64; chunk++) {
    const mb_fwid_scanner_t s = scan(img, chunk);
    TEST_ASSERT_TRUE_MESSAGE(s.found, "markoer ikke fundet ved en bid-graense");
    TEST_ASSERT_EQUAL_STRING("1.2.3-b0099", s.version);
  }
}

void test_fwid_missing_in_foreign_firmware(void) {
  // Fx PLC'ens egen firmware: gyldig ESP32-magic, men ingen markoer.
  std::string img("\xE9\x05\x02\x20", 4);
  img += std::string(1000, '\x5A');
  img += "HVEXT-FWID:some-other-product:1.0.0;";
  const mb_fwid_scanner_t s = scan(img, 128);
  TEST_ASSERT_FALSE(s.found);
}

void test_fwid_prefix_literal_alone_is_not_a_marker(void) {
  // Scannerens EGEN kode indeholder prefix-literalen efterfulgt af '\0' -
  // det maa ikke tælle som en markoer, og den rigtige markoer bagefter skal
  // stadig findes.
  std::string img(MB_FWID_PREFIX);
  img.push_back('\0');
  img += "xyz";
  const mb_fwid_scanner_t lone = scan(img, 16);
  TEST_ASSERT_FALSE(lone.found);

  img += MB_FWID_PREFIX "0.30.0-b0047;";
  const mb_fwid_scanner_t both = scan(img, 7);
  TEST_ASSERT_TRUE(both.found);
  TEST_ASSERT_EQUAL_STRING("0.30.0-b0047", both.version);
}

void test_fwid_rejects_empty_and_overlong_version(void) {
  TEST_ASSERT_FALSE(scan(std::string(MB_FWID_PREFIX ";"), 8).found);
  const std::string overlong = std::string(MB_FWID_PREFIX) + std::string(MB_FWID_VERSION_MAX_LEN + 1, '9') + ";";
  TEST_ASSERT_FALSE(scan(overlong, 8).found);
  const std::string maxlen = std::string(MB_FWID_PREFIX) + std::string(MB_FWID_VERSION_MAX_LEN, '9') + ";";
  TEST_ASSERT_TRUE(scan(maxlen, 8).found);
}

void test_fwid_null_args_are_safe(void) {
  mb_fwid_scanner_t s;
  mb_fwid_scanner_init(&s);
  mb_fwid_scanner_feed(&s, nullptr, 10);
  mb_fwid_scanner_feed(nullptr, reinterpret_cast<const uint8_t *>("x"), 1);
  mb_fwid_scanner_init(nullptr);
  TEST_ASSERT_FALSE(s.found);
}

// --- v0.30.0: X-Firmware-MD5 ---------------------------------------------------

void test_md5_hex_validation(void) {
  TEST_ASSERT_TRUE(mb_ota_is_valid_md5_hex("d41d8cd98f00b204e9800998ecf8427e"));
  TEST_ASSERT_TRUE(mb_ota_is_valid_md5_hex("D41D8CD98F00B204E9800998ECF8427E"));
  TEST_ASSERT_FALSE(mb_ota_is_valid_md5_hex("d41d8cd98f00b204e9800998ecf8427"));    // 31
  TEST_ASSERT_FALSE(mb_ota_is_valid_md5_hex("d41d8cd98f00b204e9800998ecf8427e0"));  // 33
  TEST_ASSERT_FALSE(mb_ota_is_valid_md5_hex("g41d8cd98f00b204e9800998ecf8427e"));   // ikke-hex
  TEST_ASSERT_FALSE(mb_ota_is_valid_md5_hex(""));
  TEST_ASSERT_FALSE(mb_ota_is_valid_md5_hex("   "));
  TEST_ASSERT_FALSE(mb_ota_is_valid_md5_hex(nullptr));
}

// --- v0.30.0: GET /api/ota/status-JSON -----------------------------------------

void test_status_json_pending_confirm(void) {
  const mb_ota_status_data_t d = {"idle", 0, 0, "", "0.30.0-b0047", "", true, 598, false};
  char out[512];
  const size_t n = mb_ota_build_status_json(&d, out, sizeof(out));
  TEST_ASSERT_EQUAL(strlen(out), n);
  TEST_ASSERT_EQUAL_STRING(
      "{\"state\":\"idle\",\"received\":0,\"total\":0,\"percent\":0,\"error\":\"\",\"running_version\":\"0.30.0-b0047\","
      "\"new_version\":\"\",\"pending_confirm\":true,\"confirm_remaining_s\":598,\"last_update_rolled_back\":false}",
      out);
}

void test_status_json_success_with_percent(void) {
  const mb_ota_status_data_t d = {"success", 890432, 890432, "", "0.30.0-b0047", "0.30.1-b0048", false, 123, true};
  char out[512];
  TEST_ASSERT_TRUE(mb_ota_build_status_json(&d, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"percent\":100"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"new_version\":\"0.30.1-b0048\""));
  // remaining_s rapporteres kun naar der rent faktisk afventes bekraeftelse
  TEST_ASSERT_NOT_NULL(strstr(out, "\"confirm_remaining_s\":0"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"last_update_rolled_back\":true"));
}

void test_status_json_escapes_error_text(void) {
  const mb_ota_status_data_t d = {"failed", 10, 100, "fejl \"x\" i C:\\sti\nny linje", "v", "", false, 0, false};
  char out[512];
  TEST_ASSERT_TRUE(mb_ota_build_status_json(&d, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"error\":\"fejl \\\"x\\\" i C:\\\\sti ny linje\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"percent\":10"));
}

void test_status_json_null_strings_and_small_buffer(void) {
  const mb_ota_status_data_t d = {nullptr, 0, 0, nullptr, nullptr, nullptr, false, 0, false};
  char out[512];
  TEST_ASSERT_TRUE(mb_ota_build_status_json(&d, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"state\":\"idle\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"running_version\":\"\""));

  char tiny[40];
  TEST_ASSERT_EQUAL(0, mb_ota_build_status_json(&d, tiny, sizeof(tiny)));
  TEST_ASSERT_EQUAL_STRING("", tiny);  // hellere tomt end halv/ugyldig JSON
  TEST_ASSERT_EQUAL(0, mb_ota_build_status_json(nullptr, out, sizeof(out)));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_accepts_valid_esp32_magic_byte);
  RUN_TEST(test_rejects_wrong_magic_byte);
  RUN_TEST(test_rejects_empty_buffer);
  RUN_TEST(test_rejects_null_pointer);
  RUN_TEST(test_fwid_found_in_single_chunk);
  RUN_TEST(test_fwid_found_across_every_chunk_boundary);
  RUN_TEST(test_fwid_missing_in_foreign_firmware);
  RUN_TEST(test_fwid_prefix_literal_alone_is_not_a_marker);
  RUN_TEST(test_fwid_rejects_empty_and_overlong_version);
  RUN_TEST(test_fwid_null_args_are_safe);
  RUN_TEST(test_md5_hex_validation);
  RUN_TEST(test_status_json_pending_confirm);
  RUN_TEST(test_status_json_success_with_percent);
  RUN_TEST(test_status_json_escapes_error_text);
  RUN_TEST(test_status_json_null_strings_and_small_buffer);
  return UNITY_END();
}
