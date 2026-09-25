#include "sc16is75x.h"

uint8_t sc16_i2c_subaddress(uint8_t reg, uint8_t channel) {
  return static_cast<uint8_t>(((reg & 0x0F) << 3) | ((channel & 0x03) << 1));
}

bool sc16_baud_divisor(uint32_t xtal_hz, uint32_t baud, uint16_t *out_divisor) {
  if (out_divisor == nullptr || baud == 0 || xtal_hz == 0) return false;
  const uint64_t denom = 16ULL * baud;
  uint64_t divisor = (static_cast<uint64_t>(xtal_hz) + denom / 2) / denom;  // afrundet
  if (divisor == 0 || divisor > 0xFFFF) return false;

  // Tjek den faktiske afvigelse: |xtal/(16*div) - baud| <= 1 % af baud.
  const uint64_t actual_x100 = (static_cast<uint64_t>(xtal_hz) * 100) / (16ULL * divisor);
  const uint64_t wanted_x100 = static_cast<uint64_t>(baud) * 100;
  const uint64_t diff = actual_x100 > wanted_x100 ? actual_x100 - wanted_x100 : wanted_x100 - actual_x100;
  if (diff * 100 > wanted_x100) return false;

  *out_divisor = static_cast<uint16_t>(divisor);
  return true;
}

uint32_t sc16_max_baud(uint32_t xtal_hz) { return xtal_hz / 16; }

uint8_t sc16_lcr_value(mb_channel_parity_t parity, uint8_t stop_bits) {
  uint8_t lcr = 0x03;  // 8 databit
  if (stop_bits == 2) lcr |= 0x04;
  if (parity == MB_CHANNEL_PARITY_EVEN) {
    lcr |= 0x08 | 0x10;  // paritet til + lige
  } else if (parity == MB_CHANNEL_PARITY_ODD) {
    lcr |= 0x08;  // paritet til, ulige
  }
  return lcr;
}

uint8_t sc16_efcr_value(mb_channel_mode_t mode) { return mode == MB_CHANNEL_MODE_RS485 ? 0x30 : 0x00; }

uint8_t sc16_mcr_value(mb_channel_mode_t mode) { return mode == MB_CHANNEL_MODE_RS232 ? 0x02 : 0x00; }
