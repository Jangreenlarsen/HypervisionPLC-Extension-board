#pragma once

#include <cstddef>
#include <cstdint>

#include "board_config.h"

// §4.2's `PUT /api/channels/{n}/config`: boardet validerer stadig baudraten
// mod samme gyldige sæt som PLC-repoets `mb_is_valid_baudrate()`, selvom
// JSON'en selv ikke har registerlayoutets 16-bit-begrænsning.
bool mb_is_valid_baudrate(uint32_t baud);

// Runtime (IKKE-persisteret) statistik for én kanal — nulstilles ved reboot,
// ligesom uptime/heap. `last_error_type` er en `mb_error_code_t`-værdi.
struct mb_channel_stats_t {
  uint32_t total_requests;
  uint32_t successful_requests;
  uint32_t timeout_errors;
  uint32_t crc_errors;
  uint32_t exception_errors;
  bool has_last_error;
  uint8_t last_error_slave_id;
  uint16_t last_error_address;
  uint8_t last_error_type;
  uint32_t last_error_at_uptime_s;
};

// Bygger JSON for GET /api/channels/{n} (EXPANSION_BOARD_DESIGN.md §4.2's
// eksempel-payload). `channel_number` er 1-baseret (n=1..MB_CHANNEL_COUNT).
// Returnerer antal skrevne bytes, eller 0 hvis `out_capacity` er for lille.
size_t mb_channel_build_json(int channel_number, const mb_channel_config_t *config, const mb_channel_stats_t *stats,
                              char *out, size_t out_capacity);

// Parser en `PUT /api/channels/{n}/config`-JSON-body. §4.2: ATOMISK — ALLE
// felter skal være til stede og gyldige (ingen PATCH-semantik); mangler
// eller er blot ét ugyldigt, afvises HELE requestet (false, `*out_config`
// røres ikke), fremfor at anvende en delvis opdatering.
bool mb_channel_parse_config_json(const char *json, size_t len, mb_channel_config_t *out_config);
