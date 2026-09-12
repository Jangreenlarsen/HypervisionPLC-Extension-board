#pragma once

#include <cstddef>
#include <cstdint>

// §4.2's `POST /api/ota`: en hurtig, tidlig sundhedstjek af den uploadede
// binærs FØRSTE byte, FØR noget skrives til flash — en ESP32-firmware-image
// starter altid med magic byte 0xE9 (ESP-IDF's image-header-format). Fanger
// et forkert/korrupt upload med det samme i stedet for at bruge tid på at
// skrive hele filen til flash og fejle først ved `esp_ota_end()`s checksum-
// verifikation.
bool mb_ota_is_valid_firmware_magic(const uint8_t *first_bytes, size_t len);
