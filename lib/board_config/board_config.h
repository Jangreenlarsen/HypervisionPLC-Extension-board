#pragma once

#include <cstddef>
#include <cstdint>

#include "provisioning_cli.h"

// Schema-versionering (EXPANSION_BOARD_DESIGN.md §3.5) — en dyrekøbt lektion
// fra PLC-repoets egen historik: rul ALDRIG dette tal ned igen, heller ikke
// midlertidigt under udvikling. Skal et tilføjet felt fjernes efter at være
// testet på rigtig hardware, behold tallet og marker feltet
// "reserveret, ubrugt" i stedet for at sænke det.
constexpr uint16_t MB_CONFIG_SCHEMA_VERSION = 1;

// 16 rå tilfældige bytes hex-encoded = 32 tegn, 128 bits entropi til
// management-API'ets Bearer-token (§4.4).
constexpr size_t MB_MGMT_TOKEN_LEN = 32;

// Persisteret board-konfiguration (NVS, via src/config.cpp). Rent data —
// ingen hardware-afhængighed, se board_config.cpp for hvorfor det kan
// native-testes. `schema_version` er bevidst FØRSTE felt (kan altid læses
// uanset hvordan resten af structen ændrer sig i en senere schema-version),
// `checksum` er bevidst SIDSTE felt (dækker alle felter før den selv).
#pragma pack(push, 1)
struct mb_board_config_t {
  uint16_t schema_version;

  bool provisioned;  // true first efter et vellykket "connect" (§3.4.1)

  char wifi_ssid[MB_PROV_SSID_MAX_LEN + 1];
  bool wifi_has_ssid;
  char wifi_password[MB_PROV_PASSWORD_MAX_LEN + 1];
  bool wifi_has_password;
  bool wifi_open_network;
  bool wifi_static_ip;
  char wifi_ip[MB_PROV_IPV4_MAX_LEN + 1];
  char wifi_mask[MB_PROV_IPV4_MAX_LEN + 1];
  char wifi_gw[MB_PROV_IPV4_MAX_LEN + 1];

  char plc_ip[MB_PROV_IPV4_MAX_LEN + 1];
  bool has_plc_ip;

  // Management-API Bearer-token (§4.4) — write-only fra klientens synsvinkel
  // efter generering; kun provisioning-flowet (§3.4.1) viser det, én gang.
  char mgmt_token[MB_MGMT_TOKEN_LEN + 1];
  bool has_mgmt_token;

  // REST Basic Auth-credentials (§4.4, dual auth-model) — sat via "rest
  // user"/"rest pass", persisteres uafhængigt af WiFi-forbindelsesstatus.
  char rest_user[MB_PROV_REST_USER_MAX_LEN + 1];
  bool has_rest_user;
  char rest_pass[MB_PROV_REST_PASS_MAX_LEN + 1];
  bool has_rest_pass;

  uint16_t checksum;
};
#pragma pack(pop)

void mb_config_set_defaults(mb_board_config_t *config);

// CRC16 over alle felter FØR `checksum` — egen, lille implementering (ikke
// lib/modbus_pdu's mb_pdu_calc_crc16) for at holde de to moduler
// begrebsmæssigt adskilte, selvom algoritmen er identisk.
uint16_t mb_config_calc_checksum(const mb_board_config_t *config);

// Fortolker en rå byte-blob (som læst fra NVS) til `out_config`. Håndterer
// robust: intet gemt endnu (stored_len==0) → defaults; forkert
// størrelse/checksum-fejl (korruption, eller et ukendt/for-nyt schema) →
// defaults (ALDRIG udefineret adfærd på skrabede/trunkerede data); en
// gemt schema_version ÆLDRE end koden → migration (endnu ingen
// migrationstrin defineret — kun schema 1 eksisterer, se §3.5).
void mb_config_load_from_blob(const uint8_t *stored_blob, size_t stored_len, mb_board_config_t *out_config);

// Genberegner checksum og serialiserer `config` til `out_blob`. Returnerer
// antal skrevne bytes, eller 0 hvis `out_capacity` er for lille.
size_t mb_config_save_to_blob(mb_board_config_t *config, uint8_t *out_blob, size_t out_capacity);

// Hex-encoder `random_len` rå bytes til en null-termineret hex-streng i
// `out_token` (kræver out_capacity >= random_len*2 + 1). Selve
// tilfældighedskilden (esp_fill_random()) er hardware-specifik og ligger i
// src/config.cpp — denne funktion er ren, deterministisk formatering,
// testbar uden en rigtig RNG.
bool mb_config_token_from_random_bytes(const uint8_t *random_bytes, size_t random_len, char *out_token,
                                        size_t out_capacity);

// Overfører WiFi/PLC-IP/REST-felter fra en afsluttet mb_provisioning_state_t
// (§3.4.1) ind i `config` — kaldes når "connect" lykkes, ELLER når en
// REST-credential-kommando ("rest user"/"rest pass") skal persisteres
// uafhængigt af WiFi-status. Rører ALDRIG mgmt_token/has_mgmt_token — det er
// src/config.cpp's ansvar (kræver en rigtig RNG, ikke en del af denne
// hardware-uafhængige overførsel).
void mb_config_apply_provisioning_state(mb_board_config_t *config, const mb_provisioning_state_t *state);

// Den omvendte overførsel — bruges ved boot til at genopbygge en
// mb_provisioning_state_t fra en persisteret, allerede-provisioneret
// mb_board_config_t, så src/provisioning.cpp kan forsøge en automatisk
// genforbindelse uden at kræve en menneskelig "connect" igen efter en
// strømafbrydelse.
void mb_config_to_provisioning_state(const mb_board_config_t *config, mb_provisioning_state_t *out_state);
