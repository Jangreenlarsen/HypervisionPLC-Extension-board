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

// --- v0.30.0: firmware-identitet ---------------------------------------------
// Magic byte 0xE9 siger kun "det er ESP32-firmware" — IKKE "det er DETTE
// boards firmware". PLC'en er selv en ESP32, så dens egen .bin ville bestå
// magic-tjekket og (efter en reboot) lægge en firmware med et helt andet
// pin-layout på boardet — som så aldrig kommer på nettet igen. Derfor bærer
// hver board-firmware en indlejret identitets-markør (src/ota_manager.cpp):
//
//   "HVEXT-FWID:hypervisionplc-extension-board:<version>;"
//
// og et upload afvises hvis markøren ikke findes nogen steder i imaget.
// <version> er "MAJOR.MINOR.PATCH-bNNNN" (fx "0.30.0-b0047").
#define MB_FWID_PREFIX "HVEXT-FWID:hypervisionplc-extension-board:"
constexpr size_t MB_FWID_VERSION_MAX_LEN = 31;

// Streaming-søgning (imaget modtages i bidder — markøren kan krydse en
// bid-grænse). Nul-initialiseres via mb_fwid_scanner_init().
struct mb_fwid_scanner_t {
  size_t match_len;      // antal tegn af MB_FWID_PREFIX matchet indtil nu
  bool collecting;       // prefix fundet — samler nu <version> op til ';'
  bool found;            // en komplet, gyldig markør er fundet
  char version[MB_FWID_VERSION_MAX_LEN + 1];
  size_t version_len;
};

void mb_fwid_scanner_init(mb_fwid_scanner_t *scanner);
void mb_fwid_scanner_feed(mb_fwid_scanner_t *scanner, const uint8_t *data, size_t len);
// Den FØRSTE komplette markør vinder; version er "" indtil found==true.

// Valgfri `X-Firmware-MD5`-header: præcis 32 hex-tegn (store eller små).
bool mb_ota_is_valid_md5_hex(const char *md5);

// --- v0.30.0: GET /api/ota/status ---------------------------------------------
struct mb_ota_status_data_t {
  const char *state;             // "idle" | "in_progress" | "success" | "failed"
  uint32_t received;
  uint32_t total;
  const char *error;             // "" hvis ingen fejl
  const char *running_version;   // den KØRENDE firmwares markør-version
  const char *new_version;       // version i det senest uploadede image, "" hvis intet
  bool pending_confirm;          // første opstart efter OTA — afventer POST /api/ota/confirm
  uint32_t confirm_remaining_s;  // sekunder til automatisk rollback (0 hvis !pending_confirm)
  bool last_update_rolled_back;  // seneste OTA blev rullet tilbage (ubekræftet eller crash)
};

// Bygger status-JSON'en. Returnerer antal skrevne bytes (ekskl. '\0'), eller
// 0 hvis `out_capacity` er for lille (out er da en tom streng).
size_t mb_ota_build_status_json(const mb_ota_status_data_t *data, char *out, size_t out_capacity);
