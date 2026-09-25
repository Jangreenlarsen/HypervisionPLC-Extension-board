#include "board_config.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

void mb_channel_config_set_defaults(mb_channel_config_t *config) {
  config->enabled = true;
  config->mode = MB_CHANNEL_MODE_RS485;
  config->baudrate = 9600;
  config->parity = MB_CHANNEL_PARITY_NONE;
  config->stop_bits = 1;
  config->timeout_ms = 500;
  config->inter_frame_delay_ms = 0;
}

void mb_config_set_defaults(mb_board_config_t *config) {
  memset(config, 0, sizeof(*config));
  config->schema_version = MB_CONFIG_SCHEMA_VERSION;
  config->provisioned = false;
  for (size_t i = 0; i < MB_CHANNEL_COUNT; i++) {
    mb_channel_config_set_defaults(&config->channel[i]);
  }
  // v0.20.0/v0.21.0: matcher den hidtidige, ubetingede adfærd FØR
  // enable/disable fandtes — Ethernet/WiFi var altid forsøgt startet.
  config->eth_enabled = true;
  config->eth_static_ip = false;
  config->wifi_enabled = true;
  // v0.22.0: has_hostname=false (allerede memset-default) betyder "brug
  // det auto-genererede default" (mb_config_build_hostname()) — IKKE
  // "intet hostname sat" (firmwaren har altid ET hostname).
}

// mb_config_load_from_blob() genkender et schema UDELUKKENDE på blob-
// størrelsen — to layouts med samme størrelse ville give forkert migration.
static_assert(sizeof(mb_board_config_t) != sizeof(mb_board_config_v7_t), "schema 8 og 7 skal have forskellig størrelse");
static_assert(sizeof(mb_board_config_t) != sizeof(mb_board_config_v6_t), "schema 8 og 6 skal have forskellig størrelse");
static_assert(sizeof(mb_board_config_t) != sizeof(mb_board_config_v5_t), "schema 8 og 5 skal have forskellig størrelse");
static_assert(sizeof(mb_board_config_t) != sizeof(mb_board_config_v4_t), "schema 8 og 4 skal have forskellig størrelse");
static_assert(sizeof(mb_board_config_t) != sizeof(mb_board_config_v3_t), "schema 8 og 3 skal have forskellig størrelse");
static_assert(sizeof(mb_board_config_t) != sizeof(mb_board_config_v2_t), "schema 8 og 2 skal have forskellig størrelse");
static_assert(sizeof(mb_board_config_t) != sizeof(mb_board_config_v1_t), "schema 8 og 1 skal have forskellig størrelse");
static_assert(sizeof(mb_board_config_v7_t) != sizeof(mb_board_config_v6_t), "schema 7 og 6 skal have forskellig størrelse");

namespace {
uint16_t crc16(const uint8_t *bytes, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= bytes[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}
}  // namespace

uint16_t mb_config_calc_checksum(const mb_board_config_t *config) {
  return crc16(reinterpret_cast<const uint8_t *>(config), offsetof(mb_board_config_t, checksum));
}

uint16_t mb_config_calc_checksum_v1(const mb_board_config_v1_t *config) {
  return crc16(reinterpret_cast<const uint8_t *>(config), offsetof(mb_board_config_v1_t, checksum));
}

uint16_t mb_config_calc_checksum_v2(const mb_board_config_v2_t *config) {
  return crc16(reinterpret_cast<const uint8_t *>(config), offsetof(mb_board_config_v2_t, checksum));
}

uint16_t mb_config_calc_checksum_v3(const mb_board_config_v3_t *config) {
  return crc16(reinterpret_cast<const uint8_t *>(config), offsetof(mb_board_config_v3_t, checksum));
}

uint16_t mb_config_calc_checksum_v4(const mb_board_config_v4_t *config) {
  return crc16(reinterpret_cast<const uint8_t *>(config), offsetof(mb_board_config_v4_t, checksum));
}

uint16_t mb_config_calc_checksum_v5(const mb_board_config_v5_t *config) {
  return crc16(reinterpret_cast<const uint8_t *>(config), offsetof(mb_board_config_v5_t, checksum));
}

uint16_t mb_config_calc_checksum_v6(const mb_board_config_v6_t *config) {
  return crc16(reinterpret_cast<const uint8_t *>(config), offsetof(mb_board_config_v6_t, checksum));
}

uint16_t mb_config_calc_checksum_v7(const mb_board_config_v7_t *config) {
  return crc16(reinterpret_cast<const uint8_t *>(config), offsetof(mb_board_config_v7_t, checksum));
}

// Migrerer en verificeret v1-kandidat til v2-layout. Nye felter får deres
// default — her `rest_auth_mode = MB_REST_AUTH_MODE_BOTH`, som matcher
// adfærden FØR denne indstilling fandtes (§3.5: migration tilføjer,
// nulstiller aldrig eksisterende data).
static void migrate_v1_to_v2(const mb_board_config_v1_t &v1, mb_board_config_v2_t *out_config) {
  memset(out_config, 0, sizeof(*out_config));
  out_config->schema_version = 2;
  out_config->provisioned = v1.provisioned;
  memcpy(out_config->wifi_ssid, v1.wifi_ssid, sizeof(out_config->wifi_ssid));
  out_config->wifi_has_ssid = v1.wifi_has_ssid;
  memcpy(out_config->wifi_password, v1.wifi_password, sizeof(out_config->wifi_password));
  out_config->wifi_has_password = v1.wifi_has_password;
  out_config->wifi_open_network = v1.wifi_open_network;
  out_config->wifi_static_ip = v1.wifi_static_ip;
  memcpy(out_config->wifi_ip, v1.wifi_ip, sizeof(out_config->wifi_ip));
  memcpy(out_config->wifi_mask, v1.wifi_mask, sizeof(out_config->wifi_mask));
  memcpy(out_config->wifi_gw, v1.wifi_gw, sizeof(out_config->wifi_gw));
  memcpy(out_config->plc_ip, v1.plc_ip, sizeof(out_config->plc_ip));
  out_config->has_plc_ip = v1.has_plc_ip;
  memcpy(out_config->mgmt_token, v1.mgmt_token, sizeof(out_config->mgmt_token));
  out_config->has_mgmt_token = v1.has_mgmt_token;
  memcpy(out_config->rest_user, v1.rest_user, sizeof(out_config->rest_user));
  out_config->has_rest_user = v1.has_rest_user;
  memcpy(out_config->rest_pass, v1.rest_pass, sizeof(out_config->rest_pass));
  out_config->has_rest_pass = v1.has_rest_pass;
  out_config->rest_auth_mode = MB_REST_AUTH_MODE_BOTH;  // nyt felt — default, ikke i v1
}

// Migrerer en verificeret v2-kandidat til v3-layout. Nyt felt: `channel[]`
// — får defaults der matcher v0.9.0's hidtidige HARDKODEDE adfærd (9600
// baud, RS485, 500 ms timeout), så et allerede-kørende board ikke ændrer
// adfærd blot ved firmware-opdateringen til schema 3.
static void migrate_v2_to_v3(const mb_board_config_v2_t &v2, mb_board_config_v3_t *out_config) {
  memset(out_config, 0, sizeof(*out_config));
  out_config->schema_version = 3;
  out_config->provisioned = v2.provisioned;
  memcpy(out_config->wifi_ssid, v2.wifi_ssid, sizeof(out_config->wifi_ssid));
  out_config->wifi_has_ssid = v2.wifi_has_ssid;
  memcpy(out_config->wifi_password, v2.wifi_password, sizeof(out_config->wifi_password));
  out_config->wifi_has_password = v2.wifi_has_password;
  out_config->wifi_open_network = v2.wifi_open_network;
  out_config->wifi_static_ip = v2.wifi_static_ip;
  memcpy(out_config->wifi_ip, v2.wifi_ip, sizeof(out_config->wifi_ip));
  memcpy(out_config->wifi_mask, v2.wifi_mask, sizeof(out_config->wifi_mask));
  memcpy(out_config->wifi_gw, v2.wifi_gw, sizeof(out_config->wifi_gw));
  memcpy(out_config->plc_ip, v2.plc_ip, sizeof(out_config->plc_ip));
  out_config->has_plc_ip = v2.has_plc_ip;
  memcpy(out_config->mgmt_token, v2.mgmt_token, sizeof(out_config->mgmt_token));
  out_config->has_mgmt_token = v2.has_mgmt_token;
  memcpy(out_config->rest_user, v2.rest_user, sizeof(out_config->rest_user));
  out_config->has_rest_user = v2.has_rest_user;
  memcpy(out_config->rest_pass, v2.rest_pass, sizeof(out_config->rest_pass));
  out_config->has_rest_pass = v2.has_rest_pass;
  out_config->rest_auth_mode = v2.rest_auth_mode;
  for (size_t i = 0; i < MB_CHANNEL_COUNT_SCHEMA_3_TO_7; i++) {
    mb_channel_config_set_defaults(&out_config->channel[i]);
  }
}

// Migrerer en verificeret v3-kandidat til v4-layout. Nye felter: Ethernet
// enable/disable + static-IP-config + MAC (v0.20.0) — `eth_enabled=true`/
// DHCP/`has_eth_mac=false` matcher den hidtidige, ubetingede adfærd FØR
// disse indstillinger fandtes, så et allerede-kørende board ikke ændrer
// adfærd ved firmware-opdateringen til schema 4 (MAC'en genereres ved
// næste boot, se config_ensure_eth_mac(), src/config.cpp).
static void migrate_v3_to_v4(const mb_board_config_v3_t &v3, mb_board_config_v4_t *out_config) {
  memset(out_config, 0, sizeof(*out_config));
  out_config->schema_version = 4;
  out_config->provisioned = v3.provisioned;
  memcpy(out_config->wifi_ssid, v3.wifi_ssid, sizeof(out_config->wifi_ssid));
  out_config->wifi_has_ssid = v3.wifi_has_ssid;
  memcpy(out_config->wifi_password, v3.wifi_password, sizeof(out_config->wifi_password));
  out_config->wifi_has_password = v3.wifi_has_password;
  out_config->wifi_open_network = v3.wifi_open_network;
  out_config->wifi_static_ip = v3.wifi_static_ip;
  memcpy(out_config->wifi_ip, v3.wifi_ip, sizeof(out_config->wifi_ip));
  memcpy(out_config->wifi_mask, v3.wifi_mask, sizeof(out_config->wifi_mask));
  memcpy(out_config->wifi_gw, v3.wifi_gw, sizeof(out_config->wifi_gw));
  memcpy(out_config->plc_ip, v3.plc_ip, sizeof(out_config->plc_ip));
  out_config->has_plc_ip = v3.has_plc_ip;
  memcpy(out_config->mgmt_token, v3.mgmt_token, sizeof(out_config->mgmt_token));
  out_config->has_mgmt_token = v3.has_mgmt_token;
  memcpy(out_config->rest_user, v3.rest_user, sizeof(out_config->rest_user));
  out_config->has_rest_user = v3.has_rest_user;
  memcpy(out_config->rest_pass, v3.rest_pass, sizeof(out_config->rest_pass));
  out_config->has_rest_pass = v3.has_rest_pass;
  out_config->rest_auth_mode = v3.rest_auth_mode;
  memcpy(out_config->channel, v3.channel, sizeof(out_config->channel));
  out_config->eth_enabled = true;
  out_config->eth_static_ip = false;
  // eth_ip/mask/gw/eth_mac/has_eth_mac forbliver nul-initialiserede (memset ovenfor).
}

// Migrerer en verificeret v4-kandidat til v5-layout. Nyt felt:
// `wifi_enabled` (v0.21.0) — `true` matcher den hidtidige, ubetingede
// adfærd FØR denne indstilling fandtes.
static void migrate_v4_to_v5(const mb_board_config_v4_t &v4, mb_board_config_v5_t *out_config) {
  memset(out_config, 0, sizeof(*out_config));
  out_config->schema_version = 5;
  out_config->provisioned = v4.provisioned;
  out_config->wifi_enabled = true;
  memcpy(out_config->wifi_ssid, v4.wifi_ssid, sizeof(out_config->wifi_ssid));
  out_config->wifi_has_ssid = v4.wifi_has_ssid;
  memcpy(out_config->wifi_password, v4.wifi_password, sizeof(out_config->wifi_password));
  out_config->wifi_has_password = v4.wifi_has_password;
  out_config->wifi_open_network = v4.wifi_open_network;
  out_config->wifi_static_ip = v4.wifi_static_ip;
  memcpy(out_config->wifi_ip, v4.wifi_ip, sizeof(out_config->wifi_ip));
  memcpy(out_config->wifi_mask, v4.wifi_mask, sizeof(out_config->wifi_mask));
  memcpy(out_config->wifi_gw, v4.wifi_gw, sizeof(out_config->wifi_gw));
  memcpy(out_config->plc_ip, v4.plc_ip, sizeof(out_config->plc_ip));
  out_config->has_plc_ip = v4.has_plc_ip;
  memcpy(out_config->mgmt_token, v4.mgmt_token, sizeof(out_config->mgmt_token));
  out_config->has_mgmt_token = v4.has_mgmt_token;
  memcpy(out_config->rest_user, v4.rest_user, sizeof(out_config->rest_user));
  out_config->has_rest_user = v4.has_rest_user;
  memcpy(out_config->rest_pass, v4.rest_pass, sizeof(out_config->rest_pass));
  out_config->has_rest_pass = v4.has_rest_pass;
  out_config->rest_auth_mode = v4.rest_auth_mode;
  memcpy(out_config->channel, v4.channel, sizeof(out_config->channel));
  out_config->eth_enabled = v4.eth_enabled;
  out_config->eth_static_ip = v4.eth_static_ip;
  memcpy(out_config->eth_ip, v4.eth_ip, sizeof(out_config->eth_ip));
  memcpy(out_config->eth_mask, v4.eth_mask, sizeof(out_config->eth_mask));
  memcpy(out_config->eth_gw, v4.eth_gw, sizeof(out_config->eth_gw));
  memcpy(out_config->eth_mac, v4.eth_mac, sizeof(out_config->eth_mac));
  out_config->has_eth_mac = v4.has_eth_mac;
}

// Migrerer en verificeret v5-kandidat til nuværende (v6) layout. Nye
// felter: `hostname`/`has_hostname` (v0.22.0) — `has_hostname=false`
// matcher den hidtidige, ubetingede adfærd FØR denne indstilling fandtes
// (intet eksplicit hostname sat — det auto-genererede default bruges).
static void migrate_v5_to_current(const mb_board_config_v5_t &v5, mb_board_config_t *out_config) {
  mb_config_set_defaults(out_config);  // saetter ogsaa channel[]-, eth-, wifi_enabled- og hostname-defaults

  out_config->provisioned = v5.provisioned;
  out_config->wifi_enabled = v5.wifi_enabled;
  memcpy(out_config->wifi_ssid, v5.wifi_ssid, sizeof(out_config->wifi_ssid));
  out_config->wifi_has_ssid = v5.wifi_has_ssid;
  memcpy(out_config->wifi_password, v5.wifi_password, sizeof(out_config->wifi_password));
  out_config->wifi_has_password = v5.wifi_has_password;
  out_config->wifi_open_network = v5.wifi_open_network;
  out_config->wifi_static_ip = v5.wifi_static_ip;
  memcpy(out_config->wifi_ip, v5.wifi_ip, sizeof(out_config->wifi_ip));
  memcpy(out_config->wifi_mask, v5.wifi_mask, sizeof(out_config->wifi_mask));
  memcpy(out_config->wifi_gw, v5.wifi_gw, sizeof(out_config->wifi_gw));
  memcpy(out_config->plc_ip, v5.plc_ip, sizeof(out_config->plc_ip));
  out_config->has_plc_ip = v5.has_plc_ip;
  memcpy(out_config->mgmt_token, v5.mgmt_token, sizeof(out_config->mgmt_token));
  out_config->has_mgmt_token = v5.has_mgmt_token;
  memcpy(out_config->rest_user, v5.rest_user, sizeof(out_config->rest_user));
  out_config->has_rest_user = v5.has_rest_user;
  memcpy(out_config->rest_pass, v5.rest_pass, sizeof(out_config->rest_pass));
  out_config->has_rest_pass = v5.has_rest_pass;
  out_config->rest_auth_mode = v5.rest_auth_mode;
  memcpy(out_config->channel, v5.channel, sizeof(v5.channel));  // schema 8: kun A+B; C+D beholder defaults
  out_config->eth_enabled = v5.eth_enabled;
  out_config->eth_static_ip = v5.eth_static_ip;
  memcpy(out_config->eth_ip, v5.eth_ip, sizeof(out_config->eth_ip));
  memcpy(out_config->eth_mask, v5.eth_mask, sizeof(out_config->eth_mask));
  memcpy(out_config->eth_gw, v5.eth_gw, sizeof(out_config->eth_gw));
  memcpy(out_config->eth_mac, v5.eth_mac, sizeof(out_config->eth_mac));
  out_config->has_eth_mac = v5.has_eth_mac;
  // out_config->hostname/has_hostname beholder de defaults mb_config_set_defaults() satte ovenfor.
}

// Migrerer en verificeret v6-kandidat til nuværende (v7) layout. Nyt felt:
// `syslog_targets[]` (v0.26.0) — ingen konfigureret (alle `in_use=false`)
// matcher hidtidig adfærd (intet syslog-output overhovedet fandtes før).
static void migrate_v6_to_current(const mb_board_config_v6_t &v6, mb_board_config_t *out_config) {
  mb_config_set_defaults(out_config);  // saetter ogsaa channel[]-, eth-, wifi_enabled-, hostname- og syslog-defaults

  out_config->provisioned = v6.provisioned;
  out_config->wifi_enabled = v6.wifi_enabled;
  memcpy(out_config->wifi_ssid, v6.wifi_ssid, sizeof(out_config->wifi_ssid));
  out_config->wifi_has_ssid = v6.wifi_has_ssid;
  memcpy(out_config->wifi_password, v6.wifi_password, sizeof(out_config->wifi_password));
  out_config->wifi_has_password = v6.wifi_has_password;
  out_config->wifi_open_network = v6.wifi_open_network;
  out_config->wifi_static_ip = v6.wifi_static_ip;
  memcpy(out_config->wifi_ip, v6.wifi_ip, sizeof(out_config->wifi_ip));
  memcpy(out_config->wifi_mask, v6.wifi_mask, sizeof(out_config->wifi_mask));
  memcpy(out_config->wifi_gw, v6.wifi_gw, sizeof(out_config->wifi_gw));
  memcpy(out_config->plc_ip, v6.plc_ip, sizeof(out_config->plc_ip));
  out_config->has_plc_ip = v6.has_plc_ip;
  memcpy(out_config->mgmt_token, v6.mgmt_token, sizeof(out_config->mgmt_token));
  out_config->has_mgmt_token = v6.has_mgmt_token;
  memcpy(out_config->rest_user, v6.rest_user, sizeof(out_config->rest_user));
  out_config->has_rest_user = v6.has_rest_user;
  memcpy(out_config->rest_pass, v6.rest_pass, sizeof(out_config->rest_pass));
  out_config->has_rest_pass = v6.has_rest_pass;
  out_config->rest_auth_mode = v6.rest_auth_mode;
  memcpy(out_config->channel, v6.channel, sizeof(v6.channel));  // schema 8: kun A+B; C+D beholder defaults
  out_config->eth_enabled = v6.eth_enabled;
  out_config->eth_static_ip = v6.eth_static_ip;
  memcpy(out_config->eth_ip, v6.eth_ip, sizeof(out_config->eth_ip));
  memcpy(out_config->eth_mask, v6.eth_mask, sizeof(out_config->eth_mask));
  memcpy(out_config->eth_gw, v6.eth_gw, sizeof(out_config->eth_gw));
  memcpy(out_config->eth_mac, v6.eth_mac, sizeof(out_config->eth_mac));
  out_config->has_eth_mac = v6.has_eth_mac;
  out_config->has_hostname = v6.has_hostname;
  memcpy(out_config->hostname, v6.hostname, sizeof(out_config->hostname));
  // out_config->syslog_targets beholder de defaults mb_config_set_defaults() satte ovenfor (ingen konfigureret).
}

// Migrerer en verificeret v7-kandidat til nuværende (v8) layout. Ændring:
// `channel[]` udvidet fra 2 til 4 (v0.31.0, kanal C+D via CJMCU-752) — A+B
// kopieres uændret, C+D får defaults (samme som et fabriksnyt board).
static void migrate_v7_to_current(const mb_board_config_v7_t &v7, mb_board_config_t *out_config) {
  mb_config_set_defaults(out_config);

  out_config->provisioned = v7.provisioned;
  out_config->wifi_enabled = v7.wifi_enabled;
  memcpy(out_config->wifi_ssid, v7.wifi_ssid, sizeof(out_config->wifi_ssid));
  out_config->wifi_has_ssid = v7.wifi_has_ssid;
  memcpy(out_config->wifi_password, v7.wifi_password, sizeof(out_config->wifi_password));
  out_config->wifi_has_password = v7.wifi_has_password;
  out_config->wifi_open_network = v7.wifi_open_network;
  out_config->wifi_static_ip = v7.wifi_static_ip;
  memcpy(out_config->wifi_ip, v7.wifi_ip, sizeof(out_config->wifi_ip));
  memcpy(out_config->wifi_mask, v7.wifi_mask, sizeof(out_config->wifi_mask));
  memcpy(out_config->wifi_gw, v7.wifi_gw, sizeof(out_config->wifi_gw));
  memcpy(out_config->plc_ip, v7.plc_ip, sizeof(out_config->plc_ip));
  out_config->has_plc_ip = v7.has_plc_ip;
  memcpy(out_config->mgmt_token, v7.mgmt_token, sizeof(out_config->mgmt_token));
  out_config->has_mgmt_token = v7.has_mgmt_token;
  memcpy(out_config->rest_user, v7.rest_user, sizeof(out_config->rest_user));
  out_config->has_rest_user = v7.has_rest_user;
  memcpy(out_config->rest_pass, v7.rest_pass, sizeof(out_config->rest_pass));
  out_config->has_rest_pass = v7.has_rest_pass;
  out_config->rest_auth_mode = v7.rest_auth_mode;
  memcpy(out_config->channel, v7.channel, sizeof(v7.channel));  // A+B; C+D beholder defaults
  out_config->eth_enabled = v7.eth_enabled;
  out_config->eth_static_ip = v7.eth_static_ip;
  memcpy(out_config->eth_ip, v7.eth_ip, sizeof(out_config->eth_ip));
  memcpy(out_config->eth_mask, v7.eth_mask, sizeof(out_config->eth_mask));
  memcpy(out_config->eth_gw, v7.eth_gw, sizeof(out_config->eth_gw));
  memcpy(out_config->eth_mac, v7.eth_mac, sizeof(out_config->eth_mac));
  out_config->has_eth_mac = v7.has_eth_mac;
  out_config->has_hostname = v7.has_hostname;
  memcpy(out_config->hostname, v7.hostname, sizeof(out_config->hostname));
  memcpy(out_config->syslog_targets, v7.syslog_targets, sizeof(out_config->syslog_targets));
}

void mb_config_load_from_blob(const uint8_t *stored_blob, size_t stored_len, mb_board_config_t *out_config) {
  if (stored_blob == nullptr || stored_len == 0) {
    mb_config_set_defaults(out_config);
    return;
  }

  if (stored_len == sizeof(mb_board_config_t)) {
    mb_board_config_t candidate;
    memcpy(&candidate, stored_blob, sizeof(candidate));
    if (candidate.checksum == mb_config_calc_checksum(&candidate) &&
        candidate.schema_version == MB_CONFIG_SCHEMA_VERSION) {
      *out_config = candidate;
      return;
    }
    // Størrelsen matcher AKTUEL schema, men checksum eller schema_version
    // gør ikke — korruption, eller en fremtidig schema-version koden (i
    // strid med §3.5) er blevet nedgraderet i forhold til. Fald sikkert
    // til defaults.
    mb_config_set_defaults(out_config);
    return;
  }

  if (stored_len == sizeof(mb_board_config_v7_t)) {
    mb_board_config_v7_t v7_candidate;
    memcpy(&v7_candidate, stored_blob, sizeof(v7_candidate));
    if (v7_candidate.checksum == mb_config_calc_checksum_v7(&v7_candidate) && v7_candidate.schema_version == 7) {
      migrate_v7_to_current(v7_candidate, out_config);
      return;
    }
    // Størrelsen matcher v7, men checksum eller schema_version gør ikke —
    // korrupt v7-blob, ikke en gyldig ældre version. Fald til defaults.
    mb_config_set_defaults(out_config);
    return;
  }

  if (stored_len == sizeof(mb_board_config_v6_t)) {
    mb_board_config_v6_t v6_candidate;
    memcpy(&v6_candidate, stored_blob, sizeof(v6_candidate));
    if (v6_candidate.checksum == mb_config_calc_checksum_v6(&v6_candidate) && v6_candidate.schema_version == 6) {
      migrate_v6_to_current(v6_candidate, out_config);
      return;
    }
    // Størrelsen matcher v6, men checksum eller schema_version gør ikke —
    // korrupt v6-blob, ikke en gyldig ældre version. Fald til defaults.
    mb_config_set_defaults(out_config);
    return;
  }

  if (stored_len == sizeof(mb_board_config_v5_t)) {
    mb_board_config_v5_t v5_candidate;
    memcpy(&v5_candidate, stored_blob, sizeof(v5_candidate));
    if (v5_candidate.checksum == mb_config_calc_checksum_v5(&v5_candidate) && v5_candidate.schema_version == 5) {
      // migrate_v5_to_current() kalder selv mb_config_set_defaults() først,
      // som allerede sætter syslog_targets-defaults (og hostname-defaults) —
      // ingen mellemtrin via v6 nødvendigt her.
      migrate_v5_to_current(v5_candidate, out_config);
      return;
    }
    // Størrelsen matcher v5, men checksum eller schema_version gør ikke —
    // korrupt v5-blob, ikke en gyldig ældre version. Fald til defaults.
    mb_config_set_defaults(out_config);
    return;
  }

  if (stored_len == sizeof(mb_board_config_v4_t)) {
    mb_board_config_v4_t v4_candidate;
    memcpy(&v4_candidate, stored_blob, sizeof(v4_candidate));
    if (v4_candidate.checksum == mb_config_calc_checksum_v4(&v4_candidate) && v4_candidate.schema_version == 4) {
      mb_board_config_v5_t v5_intermediate;
      migrate_v4_to_v5(v4_candidate, &v5_intermediate);
      v5_intermediate.checksum = mb_config_calc_checksum_v5(&v5_intermediate);
      migrate_v5_to_current(v5_intermediate, out_config);
      return;
    }
    // Størrelsen matcher v4, men checksum eller schema_version gør ikke —
    // korrupt v4-blob, ikke en gyldig ældre version. Fald til defaults.
    mb_config_set_defaults(out_config);
    return;
  }

  if (stored_len == sizeof(mb_board_config_v3_t)) {
    mb_board_config_v3_t v3_candidate;
    memcpy(&v3_candidate, stored_blob, sizeof(v3_candidate));
    if (v3_candidate.checksum == mb_config_calc_checksum_v3(&v3_candidate) && v3_candidate.schema_version == 3) {
      mb_board_config_v4_t v4_intermediate;
      migrate_v3_to_v4(v3_candidate, &v4_intermediate);
      v4_intermediate.checksum = mb_config_calc_checksum_v4(&v4_intermediate);
      mb_board_config_v5_t v5_intermediate;
      migrate_v4_to_v5(v4_intermediate, &v5_intermediate);
      v5_intermediate.checksum = mb_config_calc_checksum_v5(&v5_intermediate);
      migrate_v5_to_current(v5_intermediate, out_config);
      return;
    }
    // Størrelsen matcher v3, men checksum eller schema_version gør ikke —
    // korrupt v3-blob, ikke en gyldig ældre version. Fald til defaults.
    mb_config_set_defaults(out_config);
    return;
  }

  if (stored_len == sizeof(mb_board_config_v2_t)) {
    mb_board_config_v2_t v2_candidate;
    memcpy(&v2_candidate, stored_blob, sizeof(v2_candidate));
    if (v2_candidate.checksum == mb_config_calc_checksum_v2(&v2_candidate) && v2_candidate.schema_version == 2) {
      mb_board_config_v3_t v3_intermediate;
      migrate_v2_to_v3(v2_candidate, &v3_intermediate);
      v3_intermediate.checksum = mb_config_calc_checksum_v3(&v3_intermediate);
      mb_board_config_v4_t v4_intermediate;
      migrate_v3_to_v4(v3_intermediate, &v4_intermediate);
      v4_intermediate.checksum = mb_config_calc_checksum_v4(&v4_intermediate);
      mb_board_config_v5_t v5_intermediate;
      migrate_v4_to_v5(v4_intermediate, &v5_intermediate);
      v5_intermediate.checksum = mb_config_calc_checksum_v5(&v5_intermediate);
      migrate_v5_to_current(v5_intermediate, out_config);
      return;
    }
    // Størrelsen matcher v2, men checksum eller schema_version gør ikke —
    // korrupt v2-blob, ikke en gyldig ældre version. Fald til defaults.
    mb_config_set_defaults(out_config);
    return;
  }

  if (stored_len == sizeof(mb_board_config_v1_t)) {
    mb_board_config_v1_t v1_candidate;
    memcpy(&v1_candidate, stored_blob, sizeof(v1_candidate));
    if (v1_candidate.checksum == mb_config_calc_checksum_v1(&v1_candidate) && v1_candidate.schema_version == 1) {
      mb_board_config_v2_t v2_intermediate;
      migrate_v1_to_v2(v1_candidate, &v2_intermediate);
      v2_intermediate.checksum = mb_config_calc_checksum_v2(&v2_intermediate);
      mb_board_config_v3_t v3_intermediate;
      migrate_v2_to_v3(v2_intermediate, &v3_intermediate);
      v3_intermediate.checksum = mb_config_calc_checksum_v3(&v3_intermediate);
      mb_board_config_v4_t v4_intermediate;
      migrate_v3_to_v4(v3_intermediate, &v4_intermediate);
      v4_intermediate.checksum = mb_config_calc_checksum_v4(&v4_intermediate);
      mb_board_config_v5_t v5_intermediate;
      migrate_v4_to_v5(v4_intermediate, &v5_intermediate);
      v5_intermediate.checksum = mb_config_calc_checksum_v5(&v5_intermediate);
      migrate_v5_to_current(v5_intermediate, out_config);
      return;
    }
    // Størrelsen matcher v1, men checksum eller schema_version gør ikke —
    // korrupt v1-blob, ikke en gyldig ældre version. Fald til defaults.
    mb_config_set_defaults(out_config);
    return;
  }

  // Størrelsen matcher INGEN kendt schema-version — hverken korruption vi
  // kan reparere via checksum, eller en ældre version vi ved hvordan vi
  // migrerer. Fald sikkert til defaults.
  mb_config_set_defaults(out_config);
}

size_t mb_config_save_to_blob(mb_board_config_t *config, uint8_t *out_blob, size_t out_capacity) {
  if (out_capacity < sizeof(mb_board_config_t)) {
    return 0;
  }
  config->schema_version = MB_CONFIG_SCHEMA_VERSION;
  config->checksum = mb_config_calc_checksum(config);
  memcpy(out_blob, config, sizeof(*config));
  return sizeof(mb_board_config_t);
}

bool mb_config_token_from_random_bytes(const uint8_t *random_bytes, size_t random_len, char *out_token,
                                        size_t out_capacity) {
  if (random_bytes == nullptr || out_token == nullptr || out_capacity < random_len * 2 + 1) {
    return false;
  }
  static const char kHexDigits[] = "0123456789abcdef";
  for (size_t i = 0; i < random_len; i++) {
    out_token[i * 2] = kHexDigits[(random_bytes[i] >> 4) & 0x0F];
    out_token[i * 2 + 1] = kHexDigits[random_bytes[i] & 0x0F];
  }
  out_token[random_len * 2] = '\0';
  return true;
}

bool mb_config_mac_from_random_bytes(const uint8_t *random_bytes, size_t random_len, uint8_t *out_mac) {
  if (random_bytes == nullptr || out_mac == nullptr || random_len < 6) {
    return false;
  }
  memcpy(out_mac, random_bytes, 6);
  out_mac[0] = static_cast<uint8_t>((out_mac[0] & 0xFE) | 0x02);  // unicast + lokalt administreret
  return true;
}

void mb_config_build_hostname(bool has_hostname, const char *hostname, const uint8_t mac[6], char *out_hostname,
                               size_t out_capacity) {
  if (has_hostname) {
    strncpy(out_hostname, hostname, out_capacity - 1);
    out_hostname[out_capacity - 1] = '\0';
    return;
  }
  // Auto-genereret default: "hypervision-ext-" + sidste 3 MAC-bytes som
  // store hex-bogstaver — samme MAC som allerede er unik pr. board
  // (v0.20.0), genbrugt her i stedet for at kræve endnu en separat unik
  // identifikator. Store bogstaver for læsbarhed (DNS/hostnames er
  // case-insensitive i praksis).
  static const char kHexDigits[] = "0123456789ABCDEF";
  snprintf(out_hostname, out_capacity, "hypervision-ext-%c%c%c%c%c%c", kHexDigits[(mac[3] >> 4) & 0x0F],
           kHexDigits[mac[3] & 0x0F], kHexDigits[(mac[4] >> 4) & 0x0F], kHexDigits[mac[4] & 0x0F],
           kHexDigits[(mac[5] >> 4) & 0x0F], kHexDigits[mac[5] & 0x0F]);
}

void mb_config_apply_provisioning_state(mb_board_config_t *config, const mb_provisioning_state_t *state) {
  config->wifi_enabled = state->wifi_enabled;

  strncpy(config->wifi_ssid, state->ssid, sizeof(config->wifi_ssid) - 1);
  config->wifi_ssid[sizeof(config->wifi_ssid) - 1] = '\0';
  config->wifi_has_ssid = state->has_ssid;

  strncpy(config->wifi_password, state->password, sizeof(config->wifi_password) - 1);
  config->wifi_password[sizeof(config->wifi_password) - 1] = '\0';
  config->wifi_has_password = state->has_password;
  config->wifi_open_network = state->open_network;

  config->wifi_static_ip = state->static_ip;
  strncpy(config->wifi_ip, state->ip, sizeof(config->wifi_ip) - 1);
  config->wifi_ip[sizeof(config->wifi_ip) - 1] = '\0';
  strncpy(config->wifi_mask, state->mask, sizeof(config->wifi_mask) - 1);
  config->wifi_mask[sizeof(config->wifi_mask) - 1] = '\0';
  strncpy(config->wifi_gw, state->gw, sizeof(config->wifi_gw) - 1);
  config->wifi_gw[sizeof(config->wifi_gw) - 1] = '\0';

  strncpy(config->plc_ip, state->plc_ip, sizeof(config->plc_ip) - 1);
  config->plc_ip[sizeof(config->plc_ip) - 1] = '\0';
  config->has_plc_ip = state->has_plc_ip;

  strncpy(config->rest_user, state->rest_user, sizeof(config->rest_user) - 1);
  config->rest_user[sizeof(config->rest_user) - 1] = '\0';
  config->has_rest_user = state->has_rest_user;

  strncpy(config->rest_pass, state->rest_pass, sizeof(config->rest_pass) - 1);
  config->rest_pass[sizeof(config->rest_pass) - 1] = '\0';
  config->has_rest_pass = state->has_rest_pass;

  config->rest_auth_mode = state->rest_auth_mode;

  config->eth_enabled = state->eth_enabled;
  config->eth_static_ip = state->eth_static_ip;
  strncpy(config->eth_ip, state->eth_ip, sizeof(config->eth_ip) - 1);
  config->eth_ip[sizeof(config->eth_ip) - 1] = '\0';
  strncpy(config->eth_mask, state->eth_mask, sizeof(config->eth_mask) - 1);
  config->eth_mask[sizeof(config->eth_mask) - 1] = '\0';
  strncpy(config->eth_gw, state->eth_gw, sizeof(config->eth_gw) - 1);
  config->eth_gw[sizeof(config->eth_gw) - 1] = '\0';

  config->has_hostname = state->has_hostname;
  strncpy(config->hostname, state->hostname, sizeof(config->hostname) - 1);
  config->hostname[sizeof(config->hostname) - 1] = '\0';

  memcpy(config->syslog_targets, state->syslog_targets, sizeof(config->syslog_targets));
}

void mb_config_to_provisioning_state(const mb_board_config_t *config, mb_provisioning_state_t *out_state) {
  mb_provisioning_state_init(out_state);

  out_state->wifi_enabled = config->wifi_enabled;

  strncpy(out_state->ssid, config->wifi_ssid, sizeof(out_state->ssid) - 1);
  out_state->has_ssid = config->wifi_has_ssid;

  strncpy(out_state->password, config->wifi_password, sizeof(out_state->password) - 1);
  out_state->has_password = config->wifi_has_password;
  out_state->open_network = config->wifi_open_network;

  out_state->static_ip = config->wifi_static_ip;
  strncpy(out_state->ip, config->wifi_ip, sizeof(out_state->ip) - 1);
  strncpy(out_state->mask, config->wifi_mask, sizeof(out_state->mask) - 1);
  strncpy(out_state->gw, config->wifi_gw, sizeof(out_state->gw) - 1);
  // BUGS.md v0.29.1: udledt af om feltet reelt har indhold — IKKE af
  // wifi_static_ip. "save" persisterer også et delvist udfyldt static-sæt
  // (ikke kun en vellykket "connect"), så wifi_static_ip er ingen pålidelig
  // proxy for at alle tre felter er sat.
  out_state->has_ip = config->wifi_ip[0] != '\0';
  out_state->has_mask = config->wifi_mask[0] != '\0';
  out_state->has_gw = config->wifi_gw[0] != '\0';

  strncpy(out_state->plc_ip, config->plc_ip, sizeof(out_state->plc_ip) - 1);
  out_state->has_plc_ip = config->has_plc_ip;

  strncpy(out_state->rest_user, config->rest_user, sizeof(out_state->rest_user) - 1);
  out_state->has_rest_user = config->has_rest_user;
  strncpy(out_state->rest_pass, config->rest_pass, sizeof(out_state->rest_pass) - 1);
  out_state->has_rest_pass = config->has_rest_pass;

  out_state->rest_auth_mode = config->rest_auth_mode;

  out_state->eth_enabled = config->eth_enabled;
  out_state->eth_static_ip = config->eth_static_ip;
  strncpy(out_state->eth_ip, config->eth_ip, sizeof(out_state->eth_ip) - 1);
  strncpy(out_state->eth_mask, config->eth_mask, sizeof(out_state->eth_mask) - 1);
  strncpy(out_state->eth_gw, config->eth_gw, sizeof(out_state->eth_gw) - 1);

  out_state->has_hostname = config->has_hostname;
  strncpy(out_state->hostname, config->hostname, sizeof(out_state->hostname) - 1);

  memcpy(out_state->syslog_targets, config->syslog_targets, sizeof(out_state->syslog_targets));
}
