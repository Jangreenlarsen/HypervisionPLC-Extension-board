#pragma once

#include <cstddef>
#include <cstdint>

#include "provisioning_cli.h"
#include "rest_auth.h"

// Schema-versionering (EXPANSION_BOARD_DESIGN.md §3.5) — en dyrekøbt lektion
// fra PLC-repoets egen historik: rul ALDRIG dette tal ned igen, heller ikke
// midlertidigt under udvikling. Skal et tilføjet felt fjernes efter at være
// testet på rigtig hardware, behold tallet og marker feltet
// "reserveret, ubrugt" i stedet for at sænke det.
//
// Schema 5 (denne version): tilføjede persisteret `wifi_enabled`
// (`wifi enable`/`disable`, v0.21.0) — se feltet nedenfor.
// `mb_board_config_v4_t`/`v3_t`/`v2_t`/`v1_t` nedenfor er FROSSNE kopier af
// ældre schema-layouts, udelukkende til migration af allerede-gemte blobs —
// ÆNDR DEM ALDRIG, de skal blive ved med at matche hvad der faktisk blev
// udgivet.
constexpr uint16_t MB_CONFIG_SCHEMA_VERSION = 5;

// §4.2: RS485/RS232-modevalg pr. kanal (§2.2.1) — styrer både
// MODE_SEL-GPIO'en og om kanal-tasken toggler DE/RE (kun RS485).
enum mb_channel_mode_t : uint8_t { MB_CHANNEL_MODE_RS485 = 0, MB_CHANNEL_MODE_RS232 = 1 };

// §4.2: paritet — "none"/"even"/"odd" i REST-JSON'en.
enum mb_channel_parity_t : uint8_t { MB_CHANNEL_PARITY_NONE = 0, MB_CHANNEL_PARITY_EVEN = 1, MB_CHANNEL_PARITY_ODD = 2 };

// Persisteret pr.-kanal-konfiguration (§4.2's `PUT /api/channels/{n}/config`
// — hele objektet skrives atomisk, aldrig felt-for-felt, jf. designdokumentet).
#pragma pack(push, 1)
struct mb_channel_config_t {
  bool enabled;
  mb_channel_mode_t mode;
  uint32_t baudrate;
  mb_channel_parity_t parity;
  uint8_t stop_bits;  // 1 eller 2
  uint32_t timeout_ms;
  uint32_t inter_frame_delay_ms;
};
#pragma pack(pop)

constexpr size_t MB_CHANNEL_COUNT = 2;  // Variant A (§2.0) — fast 2 kanaler

// Defaults der matcher v0.9.0's tidligere HARDKODEDE adfærd i
// modbus_channel.cpp — sikrer identisk opførsel for eksisterende boards
// umiddelbart efter migration til schema 3.
void mb_channel_config_set_defaults(mb_channel_config_t *config);

// 16 rå tilfældige bytes hex-encoded = 32 tegn, 128 bits entropi til
// management-API'ets Bearer-token (§4.4).
constexpr size_t MB_MGMT_TOKEN_LEN = 32;

// FROSSEN — schema 1's nøjagtige layout, kun til migration af allerede-gemte
// v1-blobs i mb_config_load_from_blob(). Ret ALDRIG denne struct.
#pragma pack(push, 1)
struct mb_board_config_v1_t {
  uint16_t schema_version;
  bool provisioned;
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
  char mgmt_token[MB_MGMT_TOKEN_LEN + 1];
  bool has_mgmt_token;
  char rest_user[MB_PROV_REST_USER_MAX_LEN + 1];
  bool has_rest_user;
  char rest_pass[MB_PROV_REST_PASS_MAX_LEN + 1];
  bool has_rest_pass;
  uint16_t checksum;
};
#pragma pack(pop)

// CRC16 over v1-structen (samme algoritme som mb_config_calc_checksum, blot
// over det ÆLDRE layout) — bruges KUN til at verificere en v1-blob under
// migration, se mb_config_load_from_blob().
uint16_t mb_config_calc_checksum_v1(const mb_board_config_v1_t *config);

// FROSSEN — schema 2's nøjagtige layout (identisk med `mb_board_config_t`
// FØR schema 3 tilføjede `channel[]`), kun til migration af allerede-gemte
// v2-blobs. Ret ALDRIG denne struct.
#pragma pack(push, 1)
struct mb_board_config_v2_t {
  uint16_t schema_version;
  bool provisioned;
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
  char mgmt_token[MB_MGMT_TOKEN_LEN + 1];
  bool has_mgmt_token;
  char rest_user[MB_PROV_REST_USER_MAX_LEN + 1];
  bool has_rest_user;
  char rest_pass[MB_PROV_REST_PASS_MAX_LEN + 1];
  bool has_rest_pass;
  mb_rest_auth_mode_t rest_auth_mode;
  uint16_t checksum;
};
#pragma pack(pop)

// CRC16 over v2-structen — bruges KUN til at verificere en v2-blob under
// migration, se mb_config_load_from_blob().
uint16_t mb_config_calc_checksum_v2(const mb_board_config_v2_t *config);

// FROSSEN — schema 3's nøjagtige layout (identisk med `mb_board_config_t`
// FØR schema 4 tilføjede Ethernet-felterne), kun til migration af
// allerede-gemte v3-blobs. Ret ALDRIG denne struct.
#pragma pack(push, 1)
struct mb_board_config_v3_t {
  uint16_t schema_version;
  bool provisioned;
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
  char mgmt_token[MB_MGMT_TOKEN_LEN + 1];
  bool has_mgmt_token;
  char rest_user[MB_PROV_REST_USER_MAX_LEN + 1];
  bool has_rest_user;
  char rest_pass[MB_PROV_REST_PASS_MAX_LEN + 1];
  bool has_rest_pass;
  mb_rest_auth_mode_t rest_auth_mode;
  mb_channel_config_t channel[MB_CHANNEL_COUNT];
  uint16_t checksum;
};
#pragma pack(pop)

// CRC16 over v3-structen — bruges KUN til at verificere en v3-blob under
// migration, se mb_config_load_from_blob().
uint16_t mb_config_calc_checksum_v3(const mb_board_config_v3_t *config);

// FROSSEN — schema 4's nøjagtige layout (identisk med `mb_board_config_t`
// FØR schema 5 tilføjede `wifi_enabled`), kun til migration af allerede-
// gemte v4-blobs. Ret ALDRIG denne struct.
#pragma pack(push, 1)
struct mb_board_config_v4_t {
  uint16_t schema_version;
  bool provisioned;
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
  char mgmt_token[MB_MGMT_TOKEN_LEN + 1];
  bool has_mgmt_token;
  char rest_user[MB_PROV_REST_USER_MAX_LEN + 1];
  bool has_rest_user;
  char rest_pass[MB_PROV_REST_PASS_MAX_LEN + 1];
  bool has_rest_pass;
  mb_rest_auth_mode_t rest_auth_mode;
  mb_channel_config_t channel[MB_CHANNEL_COUNT];
  bool eth_enabled;
  bool eth_static_ip;
  char eth_ip[MB_PROV_IPV4_MAX_LEN + 1];
  char eth_mask[MB_PROV_IPV4_MAX_LEN + 1];
  char eth_gw[MB_PROV_IPV4_MAX_LEN + 1];
  uint8_t eth_mac[6];
  bool has_eth_mac;
  uint16_t checksum;
};
#pragma pack(pop)

// CRC16 over v4-structen — bruges KUN til at verificere en v4-blob under
// migration, se mb_config_load_from_blob().
uint16_t mb_config_calc_checksum_v4(const mb_board_config_v4_t *config);

// Persisteret board-konfiguration (NVS, via src/config.cpp), schema 5. Rent
// data — ingen hardware-afhængighed, se board_config.cpp for hvorfor det kan
// native-testes. `schema_version` er bevidst FØRSTE felt (kan altid læses
// uanset hvordan resten af structen ændrer sig i en senere schema-version),
// `checksum` er bevidst SIDSTE felt (dækker alle felter før den selv).
#pragma pack(push, 1)
struct mb_board_config_t {
  uint16_t schema_version;

  bool provisioned;  // true first efter et vellykket "connect" (§3.4.1)

  // Schema 5 (nyt felt, v0.21.0, Jan: "kan vi disable wifi også fra cli") —
  // mirroring eth_enabled nedenfor. Default true (matcher hidtidig
  // ubetinget adfærd). Gælder KUN boot-tids-auto-genforbindelsen
  // (src/provisioning.cpp) — en eksplicit "connect" virker stadig uanset.
  bool wifi_enabled;

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

  // Schema 2 (nyt felt): hvilke(n) REST-auth-metode(r) der accepteres.
  // Default MB_REST_AUTH_MODE_BOTH ved migration fra v1 (matcher den
  // hidtidige, ubetingede adfærd før denne indstilling fandtes).
  mb_rest_auth_mode_t rest_auth_mode;

  // Schema 3 (nyt felt): pr.-kanal-config (§4.2). index 0 = kanal A (n=1 i
  // REST-API'et), index 1 = kanal B (n=2) — se MB_CHANNEL_COUNT.
  mb_channel_config_t channel[MB_CHANNEL_COUNT];

  // Schema 4 (nye felter, v0.20.0): valgfri W5500-Ethernet enable/disable +
  // static-IP-config, sat via seriel CLI ("eth enable/disable/mode/ip/mask/
  // gw", §3.4.1) — KUN CLI, bevidst intet REST-endpoint (Jan, bekræftet:
  // en installations-/fremstillingstidsbeslutning, ikke noget PLC'en skal
  // fjernstyre). `eth_static_ip=false` betyder DHCP. Ændringer træder først
  // i kraft ved næste `reboot` — `eth_driver_begin()` (src/eth_driver.cpp)
  // kaldes kun én gang, ved boot; ingen forsøg på at live-reinitialisere
  // den SPI-baserede esp_eth-driver under kørsel.
  bool eth_enabled;
  bool eth_static_ip;
  char eth_ip[MB_PROV_IPV4_MAX_LEN + 1];
  char eth_mask[MB_PROV_IPV4_MAX_LEN + 1];
  char eth_gw[MB_PROV_IPV4_MAX_LEN + 1];

  // Schema 4 (nyt felt, samme v0.20.0-udgivelse som eth_enabled ovenfor):
  // tilfældig, persisteret MAC-adresse til W5500'en (Jan: "vi skal også have
  // en random MAC adr brændt ind i NVS ved start"). Genereres ÉN gang (§3.5-
  // mønsteret — samme hardware-RNG-tilgang som mgmt_token, src/config.cpp),
  // uafhængigt af ESP32'ens egen eFuse-udledte ESP_MAC_ETH — det giver
  // MAC'en mulighed for at overleve en fysisk WROOM-modul-udskiftning ved
  // reparation (en eFuse-baseret MAC ville ellers ændre sig sammen med selve
  // chippen, hvilket kan bryde en DHCP-reservation/allowlist keyed på den
  // gamle MAC). `has_eth_mac=false` betyder "ikke genereret endnu" — sat af
  // `config_ensure_eth_mac()` (src/config.cpp) ved boot, FØR eth_driver_begin().
  uint8_t eth_mac[6];
  bool has_eth_mac;

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
// defaults (ALDRIG udefineret adfærd på skrabede/trunkerede data); en gemt
// v1- eller v2-blob (ældre schema, §3.5) → migreres frem til AKTUEL schema
// (v1→v2→v3, nye felter får deres respektive default), IKKE nulstillet til
// defaults — dette er den egentlige pointe med schema-versionering: et
// board med allerede-gemt config må ikke miste den ved en firmware-
// opdatering.
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

// Formaterer 6 rå tilfældige bytes til en gyldig, lokalt-administreret
// unicast-MAC (IEEE 802-konvention): bit 0 af FØRSTE byte ryddes (unicast,
// ikke multicast — et krav for at hosten overhovedet kan bruge adressen
// normalt), bit 1 sættes (lokalt administreret — signalerer eksplicit at
// dette IKKE er en ægte vendor-tildelt OUI-adresse, undgår enhver
// (om end statistisk usandsynlig) kollision med en rigtig fabrikat-MAC).
// `out_mac` skal have plads til 6 bytes. Returnerer false hvis
// `random_len < 6` eller en pointer er nullptr.
bool mb_config_mac_from_random_bytes(const uint8_t *random_bytes, size_t random_len, uint8_t *out_mac);

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
