#pragma once

#include <cstddef>

#include "diagnostic_modbus.h"
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

// DNS-hostname-label (RFC 1123): op til 63 tegn er teknisk lovligt, men 32
// er rigeligt til et meningsfuldt navn og holder samme stil som SSID
// ovenfor. v0.22.0 (Jan: "vi skal lige have en hostname på").
constexpr size_t MB_PROV_HOSTNAME_MAX_LEN = 32;

// Multi-linje CLI-output (Jan: "alle [beskeder] skal IKKE komme på en
// linje") — hvert felt/hver kommando på sin egen linje (\r\n-separeret, for
// kompatibilitet med simple seriel-terminaler der ikke auto-CR'er på bar
// \n). v0.20.0: hævet 1024→2048 — de 3 nye "eth ..."-help-linjer (samt
// "eth.*"-felterne i "show") fik help-teksten til at overskride 1024 og
// blive stille afkortet (samme BUGS.md v0.4.0-klasse-bug). ESP32 har 320KB
// RAM, så det er billigere at have rigelig margin end at ramme denne
// afkortnings-bug en tredje gang. v0.29.0: hævet 2048→4096 — den
// sektionsopdelte "help"-oversigt og de udførlige "help <emne>"-tekster
// overskred 2048. Kaldstedets buffer (src/provisioning.cpp) er derfor nu
// `static`, ikke på loopTask-stakken (BUGS.md v0.24.0's stack-overflow).
constexpr size_t MB_PROV_MSG_MAX_LEN = 4096;

// v0.25.0 (Jan: "lave en debug som outputer til console alt hvad der forgå
// på kanal A og B") — hvilke(n) kanal(er) en "debug modbus ..."-kommando
// gælder. Jans egen syntaks: "debug modbus a|b|all level 1-8".
enum class mb_debug_target_t : uint8_t { kA, kB, kAll };

constexpr uint8_t MB_PROV_DEBUG_LEVEL_MAX = 8;

// v0.26.0 (Jan: "kan vi lave en syslog funktion som vi kan sætte et target
// på som modtager af syslog" / "en eller flere target" / "vi skal have lave
// en level 1-8 samt local0-7 for syslog") — op til MB_SYSLOG_MAX_TARGETS
// samtidige UDP-syslog-modtagere (RFC 3164), hver med sin egen IP/port/tag/
// verbositets-loft. `tag` er RFC 3164's TAG/APP-NAME-felt — bevidst
// PR.-MODTAGER (ikke ét globalt tag for hele boardet), så en installatør kan
// give boardet forskellig identitet på forskellige syslog-servere. `level`
// GENBRUGER den allerede etablerede 1-8-skala fra `debug modbus ...`
// (v0.25.0) som en pr.-modtager verbositets-tærskel: kun beskeder med
// niveau <= denne værdi sendes til DEN modtager — mappes til RFC 3164-
// severity som `severity = level - 1` (niveau 1 = severity 0/mest
// kritisk/altid med, niveau 8 = severity 7/Debug), se lib/syslog_client.
// Facility (`local0`-`local7`) er IKKE en del af modtager-configuren — den
// er fast pr. delsystem i selve firmwaren (se mb_syslog_facility_t), så en
// syslog-server kan filtrere/route efter oprindelse uden brugerkonfiguration.
constexpr size_t MB_SYSLOG_MAX_TARGETS = 4;
constexpr size_t MB_SYSLOG_TAG_MAX_LEN = 24;
constexpr uint16_t MB_SYSLOG_DEFAULT_PORT = 514;

#pragma pack(push, 1)
struct mb_syslog_target_t {
  bool in_use;
  char ip[MB_PROV_IPV4_MAX_LEN + 1];
  uint16_t port;
  char tag[MB_SYSLOG_TAG_MAX_LEN + 1];
  uint8_t max_level;  // 1-8, se MB_PROV_DEBUG_LEVEL_MAX ovenfor
};
#pragma pack(pop)

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

  // v0.22.0 (Jan: "vi skal lige have en hostname på kan jeg se da dhcp
  // server bare har et espressif name nu") — eksplicit hostname-override,
  // sat via "hostname <navn>" / ryddet via "hostname auto". `has_hostname
  // == false` betyder "brug det auto-genererede default"
  // (mb_config_build_hostname(), lib/board_config/ — udledt af boardets
  // persisterede MAC, IKKE en zero-value her).
  char hostname[MB_PROV_HOSTNAME_MAX_LEN + 1];
  bool has_hostname;

  // v0.24.0 (Jan: "kan vi lave test fra cli") — SCRATCH-felter, IKKE en del
  // af den persisterede config (mb_config_apply_provisioning_state()/
  // mb_config_to_provisioning_state() rører dem aldrig): bærer blot
  // parametrene for det ENE, netop udførte "test ..."-kald videre fra
  // parseren (mb_provisioning_apply_line()) til udførelsen (kaldstedet,
  // src/provisioning.cpp — den eneste der reelt kan tale Modbus).
  // `test_channel_number` er 1-baseret (1=kanal A, 2=kanal B), samme
  // konvention som REST-API'ets `PUT/GET /api/channels/{n}`.
  uint8_t test_channel_number;
  mb_diag_read_request_t test_read;

  // v0.25.0 (Jan: "lave en debug som outputer til console alt hvad der
  // forgå på kanal A og B") — SCRATCH-felter (samme filosofi som
  // test_channel_number/test_read ovenfor), IKKE en del af den persisterede
  // config — nulstilles altid til FRA ved reboot (Jan bekræftet: bevidst
  // ikke persisteret, så det aldrig utilsigtet efterlades kørende).
  // `debug_level == 0` betyder FRA. Sat via "debug modbus <a|b|all> level
  // <1-8>", ryddet via "no debug modbus"/"no debug all" (begge synonymer,
  // Jan: "man skal kunne disable debug fra cli også").
  mb_debug_target_t debug_target;
  uint8_t debug_level;

  // v0.26.0 (Jan: syslog-funktion med "en eller flere target") — IKKE et
  // scratch-felt (modsat debug_target/debug_level ovenfor) — dette ER den
  // persisterede config (mirroring wifi/eth/hostname-mønsteret), overført
  // til/fra mb_board_config_t via mb_config_apply_provisioning_state()/
  // mb_config_to_provisioning_state() og skrevet til NVS ved "save".
  mb_syslog_target_t syslog_targets[MB_SYSLOG_MAX_TARGETS];
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
  // v0.23.0 (Jan: "hvordan generare vi ny token") — "token regenerate".
  // Management-API-tokenet blev hidtil KUN genereret ÉN gang (ved første
  // "connect") — eneste vej til et nyt var "factory-reset confirm", som
  // også rydder WiFi/firewall/alt andet. Ikke-destruktiv (rører KUN
  // tokenet), ingen "confirm" krævet (Jan bekræftet). Kaldstedet skal
  // generere+persistere et helt nyt token (hardware-RNG, ikke en del af
  // denne hardware-uafhængige lib) og vise det.
  PROV_ACTION_TOKEN_REGENERATE,
  // v0.24.0 (Jan: "kan vi lave test fra cli") — "test <n> <slave_id> <fc>
  // <adresse> <antal>", CLI-udgaven af §4.2's diagnostiske
  // `POST /api/channels/{n}/read` (samme lib/diagnostic_modbus-parametre/
  // -grænser). KUN læsning (FC01/02/03/04) — ingen skrivning fra CLI'en,
  // bevidst lavere risiko end en fuld read/write-parallel. Udløser en
  // RIGTIG Modbus-transaktion (og dermed kanalens aktivitets-LED, v0.23.1)
  // — kaldstedet skal bruge `state->test_channel_number`/`state->test_read`.
  PROV_ACTION_TEST_READ,
  // v0.25.0 (Jan: "lave en debug som outputer til console alt hvad der
  // forgå på kanal A og B") — "debug modbus <a|b|all> level <1-8>" ELLER
  // "no debug modbus"/"no debug all" (sidstnævnte to sætter blot
  // `state->debug_level = 0`, samme action). Kaldstedet skal kalde
  // modbus_channel_set_debug_level() for den/de valgte kanal(er)
  // (`state->debug_target`) — ren runtime-tilstand, ikke persisteret.
  PROV_ACTION_DEBUG_SET,
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

// RFC 1123-hostname-label: 1-MB_PROV_HOSTNAME_MAX_LEN tegn, kun
// [A-Za-z0-9-], må ikke starte eller slutte med '-' (DHCP-/DNS-servere
// afviser eller mistolker ellers navnet).
bool mb_provisioning_validate_hostname(const char *hostname);

// RFC 3164 TAG-felt: 1-MB_SYSLOG_TAG_MAX_LEN tegn, kun [A-Za-z0-9_-] (ingen
// mellemrum/kolon — kolonet er selve feltets afgrænser i RFC 3164-formatet).
bool mb_provisioning_validate_syslog_tag(const char *tag);
