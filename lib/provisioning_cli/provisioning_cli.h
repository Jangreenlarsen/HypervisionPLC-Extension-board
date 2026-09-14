#pragma once

#include <cstddef>

#include "rest_auth.h"

// Grænser hentet fra spec, ikke gættet: 802.11 SSID max 32 bytes; WPA2-PSK
// ASCII-passphrase er 8-63 tegn (kortere/længere er ugyldigt for WPA2-PSK).
constexpr size_t MB_PROV_SSID_MAX_LEN = 32;
constexpr size_t MB_PROV_PASSWORD_MIN_LEN = 8;
constexpr size_t MB_PROV_PASSWORD_MAX_LEN = 63;
constexpr size_t MB_PROV_IPV4_MAX_LEN = 15;  // "255.255.255.255"
constexpr size_t MB_PROV_CLI_MAX_LINE_LEN = 128;

// REST-API brugernavn/adgangskode (Basic Auth, §4.4 — ved siden af, ikke i
// stedet for, det auto-genererede Bearer-token). Samme længdefilosofi som
// WiFi-password (min. 8 tegn af sikkerhedshygiejne), men egne konstanter så
// de to credential-typer ikke er sammenblandede i koden.
constexpr size_t MB_PROV_REST_USER_MAX_LEN = 32;
constexpr size_t MB_PROV_REST_PASS_MIN_LEN = 8;
constexpr size_t MB_PROV_REST_PASS_MAX_LEN = 63;

// Multi-linje CLI-output (Jan: "alle [beskeder] skal IKKE komme på en
// linje") — hvert felt/hver kommando på sin egen linje (\r\n-separeret, for
// kompatibilitet med simple seriel-terminaler der ikke auto-CR'er på bar
// \n). v0.20.0: hævet 1024→2048 — de 3 nye "eth ..."-help-linjer (samt
// "eth.*"-felterne i "show") fik help-teksten til at overskride 1024 og
// blive stille afkortet (samme BUGS.md v0.4.0-klasse-bug). ESP32 har 320KB
// RAM, så det er billigere at have rigelig margin end at ramme denne
// afkortnings-bug en tredje gang.
constexpr size_t MB_PROV_MSG_MAX_LEN = 2048;

// Tilstanden CLI-kommandoerne bygger op, indtil "connect" eller
// "factory-reset confirm" udløser en handling i src/provisioning.cpp
// (EXPANSION_BOARD_DESIGN.md §3.4.1). Rent data — ingen hardware-afhængighed.
struct mb_provisioning_state_t {
  // v0.21.0 (Jan: "kan vi disable wifi også fra cli") — mirroring
  // eth_enabled nedenfor. Default true (mb_provisioning_state_init()) —
  // matcher hidtidig ubetinget adfærd (WiFi altid forsøgt genforbundet ved
  // boot). Sat via "wifi enable"/"wifi disable"; gælder KUN den automatiske
  // genforbindelse ved boot (src/provisioning.cpp) — en eksplicit "connect"
  // virker stadig uanset dette flag (samme "eksplicit kommando er altid en
  // override"-filosofi som eth_enabled). Kræver "save" + "reboot", ikke
  // live — bevidst samme mentale model som eth_enabled, Jan bekræftet.
  bool wifi_enabled;

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

  // REST-API Basic Auth-credentials (§4.4) — ved siden af det separate,
  // auto-genererede Bearer-token, som stadig etableres af src/provisioning.cpp
  // ved første "connect" og IKKE administreres via denne struct.
  char rest_user[MB_PROV_REST_USER_MAX_LEN + 1];
  bool has_rest_user;
  char rest_pass[MB_PROV_REST_PASS_MAX_LEN + 1];
  bool has_rest_pass;

  // Hvilke(n) auth-metode(r) REST-API'et accepterer (Jan: "auth-metoden vi
  // bruger skal kunne config'es i CLI'en") — sat via "rest auth
  // token|basic|both", default BOTH (mb_provisioning_state_init()).
  mb_rest_auth_mode_t rest_auth_mode;

  // v0.20.0 (Jan: "har vi kommando til at enable/disable eterhnet samt ip
  // config, modes m.m.") — valgfri W5500-Ethernet enable/disable +
  // static-IP-config, sat via "eth enable/disable/mode/ip/mask/gw". KUN
  // seriel CLI (bekræftet) — intet REST-endpoint, samme filosofi som resten
  // af netværksprovisionering (§3.4). Træder i kraft ved næste "reboot",
  // ikke live — se src/eth_driver.cpp. `eth_static_ip=false` betyder DHCP.
  bool eth_enabled;
  bool eth_static_ip;
  char eth_ip[MB_PROV_IPV4_MAX_LEN + 1];
  char eth_mask[MB_PROV_IPV4_MAX_LEN + 1];
  char eth_gw[MB_PROV_IPV4_MAX_LEN + 1];
};

void mb_provisioning_state_init(mb_provisioning_state_t *state);

enum mb_provisioning_result_t {
  PROV_OK = 0,                 // felt sat/opdateret i state — out_message har en kort bekræftelse
  PROV_ACTION_CONNECT,         // "connect", state var komplet — kaldstedet skal nu forsøge en rigtig forbindelse
  PROV_ACTION_FACTORY_RESET,   // "factory-reset confirm" — kaldstedet skal nu rydde NVS og genstarte
  PROV_ACTION_SHOW,            // "show" — out_message har allerede den formaterede (password-maskerede) status
  PROV_ACTION_HELP,            // "help" — out_message har allerede kommandolisten
  PROV_ACTION_VERSION,         // "version" — out_message har allerede firmware-version+build (fra version.json, §1)
  PROV_ACTION_STATUS,          // "status" — kaldstedet skal selv sammensætte+udskrive systemstatus (uptime/heap/WiFi er runtime-data lib/ ikke kender)
  PROV_ACTION_SAVE,            // "save" — kaldstedet skal gemme den aktuelle state til NVS uden at forsøge en WiFi-forbindelse
  // v0.19.0 (Jan, under W5500-hardware-fejlsøgning: "vi har ikke en reboot
  // kommando på board") — blødt, IKKE-destruktivt genstart-kald, samme
  // funktion som REST-API'ets `POST /api/reboot` (v0.12.0) men fra den
  // serielle CLI. Rydder INTET i NVS (modsat PROV_ACTION_FACTORY_RESET) —
  // kræver derfor ingen "confirm".
  PROV_ACTION_REBOOT,
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
