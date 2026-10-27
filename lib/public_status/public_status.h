#pragma once

#include <cstddef>
#include <cstdint>

#include "board_config.h"

// v0.36.0 (Jan: "der skal ikke være login og data skal kunne tåle at være
// public på den status side" / "det skal også kun være en read only side") —
// JSON til den OFFENTLIGE, uautentificerede statusside (`GET /status.json` og
// `GET /`, src/http_server.cpp).
//
// Dette er en ALLOWLIST: JSON'en indeholder UDELUKKENDE de felter, der står i
// structerne nedenfor. Der findes bevidst intet felt til token, adgangskoder,
// SSID, IP-adresser (heller ikke PLC-IP eller syslog-modtagere), MAC-adresse
// eller slave-ID/adresse fra seneste fejl — så en senere udvidelse af
// mb_board_config_t eller kanal-statistikken ikke ved et uheld kan "sive" ud
// på en side uden login. test_public_status/ verificerer det.

constexpr size_t MB_PUBLIC_STATUS_MAX_CHANNELS = 4;

struct mb_public_channel_t {
  char letter;           // 'A'..'D'
  const char *status;    // "ok" | "error" | "disabled" | "unavailable"
  mb_channel_mode_t mode;
  uint32_t baudrate;
  mb_channel_parity_t parity;
  uint8_t stop_bits;
  uint32_t total_requests;
  uint32_t successful_requests;
  uint32_t timeout_errors;
  uint32_t crc_errors;
  uint32_t exception_errors;
};

struct mb_public_status_t {
  const char *hostname;      // validerede tegn (RFC 1123), men escapes alligevel
  const char *fw_version;
  const char *fw_build;
  uint32_t uptime_s;
  uint32_t heap_free_bytes;
  const char *reset_reason;  // mb_status_reset_reason_string()
  mb_channel_mode_t board_mode;
  const char *expander;      // "not_fitted" | "ok" | "not_found"
  bool wifi_connected;
  const char *eth_status;    // "not_detected" | "link_down" | "waiting_dhcp" | "connected"
  bool ota_pending_confirm;
  bool ota_last_update_rolled_back;
  uint8_t channel_count;     // 2..MB_PUBLIC_STATUS_MAX_CHANNELS
  mb_public_channel_t channels[MB_PUBLIC_STATUS_MAX_CHANNELS];
};

// Bygger JSON'en. Returnerer antal skrevne bytes (ekskl. '\0'), eller 0 (og
// en tom streng i `out`) hvis bufferen er for lille / data er ugyldigt —
// aldrig halv JSON. `channel_count` afkortes ikke stille: over maks. = 0.
size_t mb_public_status_build_json(const mb_public_status_t *data, char *out, size_t out_capacity);
