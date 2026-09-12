#include "ota_validation.h"

bool mb_ota_is_valid_firmware_magic(const uint8_t *first_bytes, size_t len) {
  return first_bytes != nullptr && len >= 1 && first_bytes[0] == 0xE9;
}
