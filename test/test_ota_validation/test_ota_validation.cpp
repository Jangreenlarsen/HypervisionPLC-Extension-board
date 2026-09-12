#include <unity.h>

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

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_accepts_valid_esp32_magic_byte);
  RUN_TEST(test_rejects_wrong_magic_byte);
  RUN_TEST(test_rejects_empty_buffer);
  RUN_TEST(test_rejects_null_pointer);
  return UNITY_END();
}
