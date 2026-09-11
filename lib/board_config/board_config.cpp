#include "board_config.h"

#include <cstddef>
#include <cstring>

void mb_config_set_defaults(mb_board_config_t *config) {
  memset(config, 0, sizeof(*config));
  config->schema_version = MB_CONFIG_SCHEMA_VERSION;
  config->provisioned = false;
}

uint16_t mb_config_calc_checksum(const mb_board_config_t *config) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(config);
  const size_t len = offsetof(mb_board_config_t, checksum);

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

void mb_config_load_from_blob(const uint8_t *stored_blob, size_t stored_len, mb_board_config_t *out_config) {
  if (stored_blob == nullptr || stored_len == 0) {
    mb_config_set_defaults(out_config);
    return;
  }

  // En størrelse der ikke matcher NOGEN kendt schema-version er pr.
  // definition korrupt eller fra en fremtidig, ukendt schema — bump ALDRIG
  // MB_CONFIG_SCHEMA_VERSION ned (§3.5), så dette er ikke en gyldig,
  // ældre version vi bare ikke har set før.
  if (stored_len != sizeof(mb_board_config_t)) {
    mb_config_set_defaults(out_config);
    return;
  }

  mb_board_config_t candidate;
  memcpy(&candidate, stored_blob, sizeof(candidate));

  if (candidate.checksum != mb_config_calc_checksum(&candidate)) {
    // Korrupt blob (fx afbrudt skrivning ved strømtab) — fald tilbage til
    // fabriksdefaults i stedet for at bruge skrabede felter.
    mb_config_set_defaults(out_config);
    return;
  }

  if (candidate.schema_version == MB_CONFIG_SCHEMA_VERSION) {
    *out_config = candidate;
    return;
  }

  if (candidate.schema_version < MB_CONFIG_SCHEMA_VERSION) {
    // Migrationstrin indsættes HER når en schema 2 introduceres — endnu
    // uden effekt, da kun schema 1 nogensinde er udgivet. Se §3.5: en
    // migrationsfunktion må KUN køre denne vej (ældre -> nyere), aldrig
    // omvendt.
  }

  // candidate.schema_version > MB_CONFIG_SCHEMA_VERSION (koden er nedgraderet
  // i forhold til en enhed der allerede har gemt nyere data — skulle ikke
  // kunne ske ved korrekt efterlevelse af §3.5, men håndteres defensivt) —
  // fald tilbage til defaults i stedet for at fejlfortolke ukendte felter.
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

void mb_config_apply_provisioning_state(mb_board_config_t *config, const mb_provisioning_state_t *state) {
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
}

void mb_config_to_provisioning_state(const mb_board_config_t *config, mb_provisioning_state_t *out_state) {
  mb_provisioning_state_init(out_state);

  strncpy(out_state->ssid, config->wifi_ssid, sizeof(out_state->ssid) - 1);
  out_state->has_ssid = config->wifi_has_ssid;

  strncpy(out_state->password, config->wifi_password, sizeof(out_state->password) - 1);
  out_state->has_password = config->wifi_has_password;
  out_state->open_network = config->wifi_open_network;

  out_state->static_ip = config->wifi_static_ip;
  strncpy(out_state->ip, config->wifi_ip, sizeof(out_state->ip) - 1);
  strncpy(out_state->mask, config->wifi_mask, sizeof(out_state->mask) - 1);
  strncpy(out_state->gw, config->wifi_gw, sizeof(out_state->gw) - 1);
  // ip/mask/gw persisteres kun ved en vellykket "connect", som selv kræver
  // (via is_ready_to_connect() i lib/provisioning_cli) at alle tre var sat
  // når wifi_static_ip er true — derfor er wifi_static_ip alene en
  // pålidelig proxy for "disse tre felter er reelt udfyldt".
  out_state->has_ip = config->wifi_static_ip;
  out_state->has_mask = config->wifi_static_ip;
  out_state->has_gw = config->wifi_static_ip;

  strncpy(out_state->plc_ip, config->plc_ip, sizeof(out_state->plc_ip) - 1);
  out_state->has_plc_ip = config->has_plc_ip;

  strncpy(out_state->rest_user, config->rest_user, sizeof(out_state->rest_user) - 1);
  out_state->has_rest_user = config->has_rest_user;
  strncpy(out_state->rest_pass, config->rest_pass, sizeof(out_state->rest_pass) - 1);
  out_state->has_rest_pass = config->has_rest_pass;
}
