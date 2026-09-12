#pragma once

#include "board_config.h"
#include "provisioning_cli.h"

// NVS-persisteret board-konfiguration (EXPANSION_BOARD_DESIGN.md §3.5) —
// ESP32/Preferences-laget omkring lib/board_config/'s hardware-uafhængige
// serialisering/schema-håndtering. Kaldes fra src/provisioning.cpp.
void config_begin();
const mb_board_config_t &config_get();

// Overfører WiFi/PLC-IP/REST-felter fra `state` og gemmer til NVS.
void config_apply_and_save(const mb_provisioning_state_t *state);

// Rydder hele NVS-namespacet og nulstiller in-memory-configen til defaults.
void config_factory_reset();

// Sikrer at et management-API-token findes (genererer ét via en rigtig
// hardware-RNG hvis der endnu ikke er ét) og gemmer. `out_token` (hvis ikke
// nullptr) får tokenet kopieret — kaldstedet bruger det til at afgøre om
// tokenet er NYT (skal vises én gang, §4.4) ved at sammenligne med
// `config_get().has_mgmt_token` FØR kaldet.
void config_ensure_mgmt_token(char *out_token, size_t out_capacity);

void config_mark_provisioned();

// §4.2: persisterer ét kanals config (index 0=kanal A, 1=kanal B) — kaldes
// efter en vellykket `PUT /api/channels/{n}/config` (src/http_server.cpp),
// EFTER kanalen selv er live-omkonfigureret (modbus_channel_apply_config()),
// så et strømudfald midt i et PUT-kald ikke kan efterlade flash og den
// faktisk kørende UART-konfiguration i to forskellige tilstande.
void config_set_channel(size_t index, const mb_channel_config_t &cfg);
