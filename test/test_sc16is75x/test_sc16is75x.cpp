#include <unity.h>

#include "sc16is75x.h"

void setUp(void) {}
void tearDown(void) {}

void test_subaddress_encodes_register_and_channel(void) {
  TEST_ASSERT_EQUAL_HEX8(0x00, sc16_i2c_subaddress(SC16_REG_RHR_THR, 0));
  TEST_ASSERT_EQUAL_HEX8(0x02, sc16_i2c_subaddress(SC16_REG_RHR_THR, 1));
  TEST_ASSERT_EQUAL_HEX8(0x18, sc16_i2c_subaddress(SC16_REG_LCR, 0));   // 0x03 << 3
  TEST_ASSERT_EQUAL_HEX8(0x1A, sc16_i2c_subaddress(SC16_REG_LCR, 1));
  TEST_ASSERT_EQUAL_HEX8(0x78, sc16_i2c_subaddress(SC16_REG_EFCR, 0));  // 0x0F << 3
  TEST_ASSERT_EQUAL_HEX8(0x48, sc16_i2c_subaddress(SC16_REG_RXLVL, 0));
}

// 1,8432 MHz giver eksakte divisorer for alle standard-baudrater op til 115200.
void test_divisors_for_1_8432_mhz_are_exact(void) {
  struct {
    uint32_t baud;
    uint16_t divisor;
  } cases[] = {{1200, 96}, {2400, 48}, {4800, 24}, {9600, 12}, {19200, 6}, {38400, 3}, {57600, 2}, {115200, 1}};
  for (const auto &c : cases) {
    uint16_t d = 0;
    TEST_ASSERT_TRUE(sc16_baud_divisor(SC16_XTAL_HZ, c.baud, &d));
    TEST_ASSERT_EQUAL_UINT16(c.divisor, d);
  }
}

void test_baud_above_crystal_limit_is_rejected(void) {
  uint16_t d = 0;
  TEST_ASSERT_EQUAL_UINT32(115200, sc16_max_baud(SC16_XTAL_HZ));
  TEST_ASSERT_FALSE(sc16_baud_divisor(SC16_XTAL_HZ, 230400, &d));
  TEST_ASSERT_FALSE(sc16_baud_divisor(SC16_XTAL_HZ, 460800, &d));
  TEST_ASSERT_FALSE(sc16_baud_divisor(SC16_XTAL_HZ, 921600, &d));
}

void test_inexact_baud_rejected_and_bad_args(void) {
  uint16_t d = 0;
  // 76800 med 1,8432 MHz: divisor 1,5 → 1 eller 2 giver 115200/57600 — langt fra 1 %.
  TEST_ASSERT_FALSE(sc16_baud_divisor(SC16_XTAL_HZ, 76800, &d));
  TEST_ASSERT_FALSE(sc16_baud_divisor(SC16_XTAL_HZ, 0, &d));
  TEST_ASSERT_FALSE(sc16_baud_divisor(0, 9600, &d));
  TEST_ASSERT_FALSE(sc16_baud_divisor(SC16_XTAL_HZ, 9600, nullptr));
  // En anden krystal (14,7456 MHz) virker også - beregningen er ikke hardkodet.
  TEST_ASSERT_TRUE(sc16_baud_divisor(14745600, 921600, &d));
  TEST_ASSERT_EQUAL_UINT16(1, d);
}

void test_lcr_values(void) {
  TEST_ASSERT_EQUAL_HEX8(0x03, sc16_lcr_value(MB_CHANNEL_PARITY_NONE, 1));
  TEST_ASSERT_EQUAL_HEX8(0x07, sc16_lcr_value(MB_CHANNEL_PARITY_NONE, 2));
  TEST_ASSERT_EQUAL_HEX8(0x1B, sc16_lcr_value(MB_CHANNEL_PARITY_EVEN, 1));
  TEST_ASSERT_EQUAL_HEX8(0x0B, sc16_lcr_value(MB_CHANNEL_PARITY_ODD, 1));
  TEST_ASSERT_EQUAL_HEX8(0x1F, sc16_lcr_value(MB_CHANNEL_PARITY_EVEN, 2));
}

void test_rs485_rs232_register_values(void) {
  TEST_ASSERT_EQUAL_HEX8(0x30, sc16_efcr_value(MB_CHANNEL_MODE_RS485));  // RTSCON | RTSINVER
  TEST_ASSERT_EQUAL_HEX8(0x00, sc16_efcr_value(MB_CHANNEL_MODE_RS232));
  TEST_ASSERT_EQUAL_HEX8(0x00, sc16_mcr_value(MB_CHANNEL_MODE_RS485));
  TEST_ASSERT_EQUAL_HEX8(0x02, sc16_mcr_value(MB_CHANNEL_MODE_RS232));   // RTS-ben lavt → RS485-DE slukket
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_subaddress_encodes_register_and_channel);
  RUN_TEST(test_divisors_for_1_8432_mhz_are_exact);
  RUN_TEST(test_baud_above_crystal_limit_is_rejected);
  RUN_TEST(test_inexact_baud_rejected_and_bad_args);
  RUN_TEST(test_lcr_values);
  RUN_TEST(test_rs485_rs232_register_values);
  return UNITY_END();
}
