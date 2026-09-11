#pragma once

#include <cstddef>

// Grænser hentet fra spec, ikke gættet: 802.11 SSID max 32 bytes; WPA2-PSK
// ASCII-passphrase er 8-63 tegn (kortere/længere er ugyldigt for WPA2-PSK).
constexpr size_t MB_PROV_SSID_MAX_LEN = 32;
constexpr size_t MB_PROV_PASSWORD_MIN_LEN = 8;
constexpr size_t MB_PROV_PASSWORD_MAX_LEN = 63;
constexpr size_t MB_PROV_IPV4_MAX_LEN = 15;  // "255.255.255.255"
constexpr size_t MB_PROV_CLI_MAX_LINE_LEN = 128;
constexpr size_t MB_PROV_MSG_MAX_LEN = 96;

// Tilstanden CLI-kommandoerne bygger op, indtil "connect" eller
// "factory-reset confirm" udløser en handling i src/provisioning.cpp
// (EXPANSION_BOARD_DESIGN.md §3.4.1). Rent data — ingen hardware-afhængighed.
struct mb_provisioning_state_t {
  char ssid[MB_PROV_SSID_MAX_LEN + 1];
  bool has_ssid;

  char password[MB_PROV_PASSWORD_MAX_LEN + 1];
  bool has_password;
  bool open_network;  // sat via "wifi open" — udelukker has_password

  bool static_ip;  // false = dhcp (default)
  char ip[MB_PROV_IPV4_MAX_LEN + 1];
  char mask[MB_PROV_IPV4_MAX_LEN + 1];
  char gw[MB_PROV_IPV4_MAX_LEN + 1];
  bool has_ip;
  bool has_mask;
  bool has_gw;

  char plc_ip[MB_PROV_IPV4_MAX_LEN + 1];
  bool has_plc_ip;
};

void mb_provisioning_state_init(mb_provisioning_state_t *state);

enum mb_provisioning_result_t {
  PROV_OK = 0,                 // felt sat/opdateret i state — out_message har en kort bekræftelse
  PROV_ACTION_CONNECT,         // "connect", state var komplet — kaldstedet skal nu forsøge en rigtig forbindelse
  PROV_ACTION_FACTORY_RESET,   // "factory-reset confirm" — kaldstedet skal nu rydde NVS og genstarte
  PROV_ACTION_SHOW,            // "show" — out_message har allerede den formaterede (password-maskerede) status
  PROV_ACTION_HELP,            // "help" — out_message har allerede kommandolisten
  PROV_EMPTY_LINE,             // tomt/whitespace-only input — kaldstedet kan ignorere stille
  PROV_UNKNOWN_COMMAND,
  PROV_MISSING_ARGUMENT,       // out_message forklarer hvilket felt der mangler
  PROV_INVALID_VALUE           // out_message forklarer hvorfor værdien blev afvist
};

// Fortolker ÉN linje seriel input (EXPANSION_BOARD_DESIGN.md §3.4.1's
// kommandosæt), opdaterer `state`, og skriver altid en null-termineret
// menneskelæselig besked til out_message. Password EKKOES ALDRIG i
// klartekst i out_message eller i "show"-output (kun *-maskeret) — samme
// hemmeligheds-disciplin som management-API-tokenet, jf. CLAUDE.md regel 6.
mb_provisioning_result_t mb_provisioning_apply_line(mb_provisioning_state_t *state, const char *line,
                                                     char *out_message, size_t out_message_capacity);

bool mb_provisioning_validate_ssid(const char *ssid);
bool mb_provisioning_validate_password(const char *password);
bool mb_provisioning_validate_ipv4(const char *ip);
