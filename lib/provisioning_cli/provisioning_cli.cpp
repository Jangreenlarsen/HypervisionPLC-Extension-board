#include "provisioning_cli.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

void mb_provisioning_state_init(mb_provisioning_state_t *state) {
  memset(state, 0, sizeof(*state));
  // v0.20.0/v0.21.0: matcher den hidtidige, ubetingede adfærd FØR
  // enable/disable fandtes (Ethernet/WiFi altid forsøgt startet) - modsat
  // rest_auth_mode (MB_REST_AUTH_MODE_BOTH == 0) er "enabled" IKKE
  // zero-value'en, så skal sættes eksplicit her for at et fabriksnyt board
  // (der endnu ikke har kørt "eth/wifi enable"/en persisteret config) ikke
  // utilsigtet starter uden netværksadgang.
  state->eth_enabled = true;
  state->wifi_enabled = true;
}

bool mb_provisioning_validate_ssid(const char *ssid) {
  if (ssid == nullptr) return false;
  const size_t len = strlen(ssid);
  return len >= 1 && len <= MB_PROV_SSID_MAX_LEN;
}

bool mb_provisioning_validate_password(const char *password) {
  if (password == nullptr) return false;
  const size_t len = strlen(password);
  return len >= MB_PROV_PASSWORD_MIN_LEN && len <= MB_PROV_PASSWORD_MAX_LEN;
}

static bool validate_rest_user(const char *user) {
  if (user == nullptr) return false;
  const size_t len = strlen(user);
  return len >= 1 && len <= MB_PROV_REST_USER_MAX_LEN;
}

static bool validate_rest_pass(const char *pass) {
  if (pass == nullptr) return false;
  const size_t len = strlen(pass);
  return len >= MB_PROV_REST_PASS_MIN_LEN && len <= MB_PROV_REST_PASS_MAX_LEN;
}

bool mb_provisioning_validate_ipv4(const char *ip) {
  if (ip == nullptr) return false;

  int octets = 0;
  int digits_in_octet = 0;
  int value = 0;

  for (const char *p = ip;; p++) {
    if (*p >= '0' && *p <= '9') {
      value = value * 10 + (*p - '0');
      digits_in_octet++;
      if (digits_in_octet > 3 || value > 255) return false;
    } else if (*p == '.' || *p == '\0') {
      if (digits_in_octet == 0) return false;  // tomt oktet: ledende/dobbelt/afsluttende punktum
      octets++;
      digits_in_octet = 0;
      value = 0;
      if (*p == '\0') break;
    } else {
      return false;  // ugyldigt tegn
    }
  }

  return octets == 4;
}

bool mb_provisioning_validate_syslog_tag(const char *tag) {
  if (tag == nullptr) return false;
  const size_t len = strlen(tag);
  if (len == 0 || len > MB_SYSLOG_TAG_MAX_LEN) return false;
  for (size_t i = 0; i < len; i++) {
    const char c = tag[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}

bool mb_provisioning_validate_hostname(const char *hostname) {
  if (hostname == nullptr) return false;
  const size_t len = strlen(hostname);
  if (len == 0 || len > MB_PROV_HOSTNAME_MAX_LEN) return false;
  if (hostname[0] == '-' || hostname[len - 1] == '-') return false;  // RFC 1123: ikke start/slut med '-'
  for (size_t i = 0; i < len; i++) {
    const char c = hostname[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
    if (!ok) return false;
  }
  return true;
}

// Splitter `buf` (muteres in-place) i op til `max_tokens` tokens. Et token er
// enten et "citeret" segment (mellemrum tilladt indeni — nødvendigt for SSID'er
// som "My Home Network", jf. §3.4.1) eller et almindeligt whitespace-afgrænset ord.
static size_t tokenize(char *buf, char *tokens[], size_t max_tokens) {
  size_t count = 0;
  char *p = buf;

  while (*p != '\0' && count < max_tokens) {
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') break;

    if (*p == '"') {
      p++;
      tokens[count++] = p;
      while (*p != '\0' && *p != '"') p++;
      if (*p == '"') {
        *p = '\0';
        p++;
      }
    } else {
      tokens[count++] = p;
      while (*p != '\0' && *p != ' ' && *p != '\t') p++;
      if (*p != '\0') {
        *p = '\0';
        p++;
      }
    }
  }

  return count;
}

// Versalfølsomheds-uafhængigt strengsammenlign — bruges KUN på kommando-ord
// (wifi/ssid/pass/dhcp/...), aldrig på værdier (SSID/password er case-sensitive).
static bool ieq(const char *a, const char *b) {
  while (*a != '\0' && *b != '\0') {
    if (tolower(static_cast<unsigned char>(*a)) != tolower(static_cast<unsigned char>(*b))) return false;
    a++;
    b++;
  }
  return *a == '\0' && *b == '\0';
}

static void set_ipv4_field(char *target, const char *value) {
  strncpy(target, value, MB_PROV_IPV4_MAX_LEN);
  target[MB_PROV_IPV4_MAX_LEN] = '\0';
}

// v0.24.0: parser ét CLI-token som et heltal (afviser tomme/ikke-numeriske
// tokens og forkortede kald som "5abc" — HELE tokenet skal være cifre,
// ikke kun et præfiks af det, modsat en rå strtoul()). Bruges af "test ...".
static bool parse_uint_token(const char *token, uint32_t *out) {
  if (token == nullptr || *token == '\0') return false;
  char *end = nullptr;
  const unsigned long value = strtoul(token, &end, 10);
  if (end == token || *end != '\0') return false;
  *out = static_cast<uint32_t>(value);
  return true;
}

// Lille hjælper til multi-linje-output: tilføjer "<label>: <value>\r\n" til
// `buf` (via `pos`, som opdateres) uden at kunne overskride `capacity` —
// snprintf'ens returværdi bruges IKKE direkte som ny `pos` (den kan angive
// hvor mange bytes der VILLE være skrevet ved uendelig plads, hvilket ville
// kunne få `pos` til at overstige `capacity` og efterfølgende kald til at
// skrive udenfor bufferen).
static void append_line(char *buf, size_t capacity, size_t *pos, const char *label, const char *value) {
  if (*pos >= capacity) return;
  const int written = snprintf(buf + *pos, capacity - *pos, "%s: %s\r\n", label, value);
  if (written > 0) {
    const size_t advance = static_cast<size_t>(written);
    *pos += (advance < capacity - *pos) ? advance : (capacity - *pos - 1);
  }
}

static void mb_provisioning_format_status(const mb_provisioning_state_t *state, char *out_buffer,
                                           size_t out_buffer_capacity) {
  // Jan (bekræftet): CLI'en kræver fysisk USB-adgang (§3.4 — samme
  // tillidsniveau som selve boardet/en factory-reset), så maskering her
  // giver ingen reel beskyttelse, kun friktion — al config, INKL.
  // adgangskoder og management-tokenet, vises derfor i klartekst. Dette
  // gælder KUN den serielle CLI — REST-API'et (§4.2/§4.4, netværksvendt)
  // returnerer fortsat ALDRIG tokenet, uanset auth-metode.
  const char *password_display = "(ikke sat)";
  if (state->open_network) {
    password_display = "(aabent netvaerk)";
  } else if (state->has_password) {
    password_display = state->password;
  }
  const char *rest_pass_display = state->has_rest_pass ? state->rest_pass : "(ikke sat)";

  size_t pos = 0;
  out_buffer[0] = '\0';
  // Jan: "show status paa serie cli skal vise version og build" — vis det
  // ogsaa her, ikke kun i den separate "version"/"status"-kommando.
#ifdef FW_VERSION
  append_line(out_buffer, out_buffer_capacity, &pos, "firmware", FW_VERSION " build " FW_BUILD);
#else
  append_line(out_buffer, out_buffer_capacity, &pos, "firmware", "(version ukendt)");
#endif
  // v0.22.0: konfigureret (staged/persisteret) override - IKKE den faktiske
  // hostname der bruges hvis "auto" (den afhænger af MAC'en, som denne
  // hardware-uafhængige funktion ikke kender) - se src/provisioning.cpp's
  // "status" for den reelt anvendte streng.
  append_line(out_buffer, out_buffer_capacity, &pos, "hostname",
              state->has_hostname ? state->hostname : "(auto-genereret, se 'status')");
  // v0.21.0: konfigureret (staged/persisteret) - IKKE live-status (det
  // kommer fra src/provisioning.cpp's print_wifi_connection_status()),
  // samme adskillelse som eth.enabled vs eth.connection nedenfor.
  append_line(out_buffer, out_buffer_capacity, &pos, "wifi.enabled", state->wifi_enabled ? "true" : "false");
  append_line(out_buffer, out_buffer_capacity, &pos, "wifi.ssid", state->has_ssid ? state->ssid : "(ikke sat)");
  append_line(out_buffer, out_buffer_capacity, &pos, "wifi.pass", password_display);
  append_line(out_buffer, out_buffer_capacity, &pos, "wifi.mode", state->static_ip ? "static" : "dhcp");
  if (state->static_ip) {
    append_line(out_buffer, out_buffer_capacity, &pos, "wifi.ip", state->has_ip ? state->ip : "(ikke sat)");
    append_line(out_buffer, out_buffer_capacity, &pos, "wifi.mask", state->has_mask ? state->mask : "(ikke sat)");
    append_line(out_buffer, out_buffer_capacity, &pos, "wifi.gw", state->has_gw ? state->gw : "(ikke sat)");
  }
  append_line(out_buffer, out_buffer_capacity, &pos, "plc.ip", state->has_plc_ip ? state->plc_ip : "(ikke sat)");

  const char *auth_mode_display = "both";
  if (state->rest_auth_mode == MB_REST_AUTH_MODE_TOKEN_ONLY) {
    auth_mode_display = "token";
  } else if (state->rest_auth_mode == MB_REST_AUTH_MODE_BASIC_ONLY) {
    auth_mode_display = "basic";
  }
  append_line(out_buffer, out_buffer_capacity, &pos, "rest.auth_mode", auth_mode_display);

  // Jan: "hvis vi køre rest auth token så skal rest user og rest pass [kun]
  // være i config kun hvis rest auth both" — rest.user/rest.pass vises KUN
  // når rest_auth_mode er BOTH (præcis som formuleret — ikke også for
  // BASIC_ONLY, selvom Basic Auth teknisk set også bruges der). Selve
  // værdierne SLETTES ikke fra NVS ved et modeskift (kun visningen her) —
  // de er stadig gemt hvis man senere skifter tilbage til "both".
  if (state->rest_auth_mode == MB_REST_AUTH_MODE_BOTH) {
    append_line(out_buffer, out_buffer_capacity, &pos, "rest.user",
                state->has_rest_user ? state->rest_user : "(ikke sat)");
    append_line(out_buffer, out_buffer_capacity, &pos, "rest.pass", rest_pass_display);
  }

  // v0.20.0: konfigureret (staged/persisteret) Ethernet-opsætning — IKKE
  // live-status (link/IP, det kommer fra src/provisioning.cpp's
  // print_ethernet_status(), samme adskillelse som wifi.mode vs
  // wifi.connection ovenfor).
  append_line(out_buffer, out_buffer_capacity, &pos, "eth.enabled", state->eth_enabled ? "true" : "false");
  append_line(out_buffer, out_buffer_capacity, &pos, "eth.mode", state->eth_static_ip ? "static" : "dhcp");
  if (state->eth_static_ip) {
    append_line(out_buffer, out_buffer_capacity, &pos, "eth.ip", state->eth_ip[0] != '\0' ? state->eth_ip : "(ikke sat)");
    append_line(out_buffer, out_buffer_capacity, &pos, "eth.mask",
                state->eth_mask[0] != '\0' ? state->eth_mask : "(ikke sat)");
    append_line(out_buffer, out_buffer_capacity, &pos, "eth.gw", state->eth_gw[0] != '\0' ? state->eth_gw : "(ikke sat)");
  }

  // v0.26.0 — persisteret syslog-modtager-liste (§3.4.1). Ingen konfigureret
  // = én linje der siger det, i stedet for slet ingen "syslog.*"-linjer
  // (samme "gør fraværet eksplicit"-stil som resten af show).
  bool any_syslog_target = false;
  for (size_t i = 0; i < MB_SYSLOG_MAX_TARGETS; i++) {
    if (!state->syslog_targets[i].in_use) continue;
    any_syslog_target = true;
    char line[64];
    snprintf(line, sizeof(line), "%s:%u tag=%s level<=%u", state->syslog_targets[i].ip,
             static_cast<unsigned>(state->syslog_targets[i].port), state->syslog_targets[i].tag,
             static_cast<unsigned>(state->syslog_targets[i].max_level));
    char label[24];
    snprintf(label, sizeof(label), "syslog.target%u", static_cast<unsigned>(i + 1));
    append_line(out_buffer, out_buffer_capacity, &pos, label, line);
  }
  if (!any_syslog_target) {
    append_line(out_buffer, out_buffer_capacity, &pos, "syslog.targets", "(ingen konfigureret)");
  }
}

// v0.29.0 (Jan: "da det man se i cli ikke er det samme som de kommando man
// skal slå så skal vi bruge en help" / "i den help skal ting være opdelt i
// seksioner") — udførlig forklaring pr. emne til "help <emne>". Hvert emne
// har samme faste undersektioner (Kommandoer / Vises i 'show' som / Traeder
// i kraft / Eksempel), så feltnavnene i "show" (fx rest.auth_mode) altid
// kan føres tilbage til den kommando der sætter dem (fx "rest auth ...").
// Hver tekst skal kunne være i MB_PROV_MSG_MAX_LEN — test_help_topics_*
// verificerer at intet emne afkortes.
struct help_topic_t {
  const char *name;
  const char *text;
};

static const help_topic_t kHelpTopics[] = {
    {"wifi",
     "=== WIFI - forbindelse til produktionsnetvaerket ===\r\n"
     "--- Kommandoer ---\r\n"
     "  wifi ssid <navn>        netvaerkets navn (1-32 tegn, brug \"...\" ved mellemrum)\r\n"
     "  wifi pass <kode>        WPA2-adgangskode (8-63 tegn)\r\n"
     "  wifi open               aabent netvaerk uden adgangskode (rydder wifi pass)\r\n"
     "  wifi mode dhcp|static   dhcp = automatisk IP (default), static = fast IP\r\n"
     "  wifi ip <a.b.c.d>       fast IP - kun ved wifi mode static\r\n"
     "  wifi mask <a.b.c.d>     netmaske, fx 255.255.255.0 - kun ved wifi mode static\r\n"
     "  wifi gw <a.b.c.d>       gateway/router - kun ved wifi mode static\r\n"
     "  wifi enable|disable     forbind automatisk ved opstart eller ej (default enable)\r\n"
     "--- Vises i 'show' som ---\r\n"
     "  wifi.ssid, wifi.pass, wifi.mode, wifi.ip, wifi.mask, wifi.gw, wifi.enabled\r\n"
     "  wifi.connection, wifi.ip, wifi.rssi_dbm = LIVE status (kan ikke saettes)\r\n"
     "  (ved mode static staar wifi.ip der to gange: den indtastede og den faktiske)\r\n"
     "--- Traeder i kraft ---\r\n"
     "  ssid/pass/open/mode/ip/mask/gw: ved 'connect' (som ogsaa gemmer)\r\n"
     "  enable/disable: kraever 'save' + 'reboot'\r\n"
     "  'connect' virker altid - ogsaa naar WiFi er sat til disable\r\n"
     "--- Eksempel ---\r\n"
     "  wifi ssid \"Fabrik WiFi\"\r\n"
     "  wifi pass hemmelig123\r\n"
     "  plc ip 192.168.1.10\r\n"
     "  connect\r\n"},
    {"eth",
     "=== ETH - kablet Ethernet (W5500-modul) ===\r\n"
     "--- Kommandoer ---\r\n"
     "  eth enable|disable      slaa Ethernet til/fra (default enable)\r\n"
     "  eth mode dhcp|static    dhcp = automatisk IP (default), static = fast IP\r\n"
     "  eth ip <a.b.c.d>        fast IP - kun ved eth mode static\r\n"
     "  eth mask <a.b.c.d>      netmaske - kun ved eth mode static\r\n"
     "  eth gw <a.b.c.d>        gateway/router - kun ved eth mode static\r\n"
     "--- Vises i 'show' som ---\r\n"
     "  eth.enabled, eth.mode, eth.ip, eth.mask, eth.gw\r\n"
     "  eth.connection, eth.ip, eth.mac = LIVE status (kan ikke saettes)\r\n"
     "--- Traeder i kraft ---\r\n"
     "  alle eth-kommandoer kraever 'save' + 'reboot'\r\n"
     "  ADVARSEL: eth disable + wifi disable = ingen netvaerksadgang (kun USB-CLI)\r\n"
     "--- Eksempel ---\r\n"
     "  eth mode static\r\n"
     "  eth ip 192.168.1.50\r\n"
     "  eth mask 255.255.255.0\r\n"
     "  eth gw 192.168.1.1\r\n"
     "  save\r\n"
     "  reboot\r\n"},
    {"hostname",
     "=== HOSTNAME - boardets navn paa netvaerket (fx i routerens DHCP-liste) ===\r\n"
     "--- Kommandoer ---\r\n"
     "  hostname <navn>         1-32 tegn, kun bogstaver/tal/'-', ikke '-' forrest/bagerst\r\n"
     "  hostname auto           brug det automatiske navn udledt af MAC-adressen (default)\r\n"
     "--- Vises i 'show' som ---\r\n"
     "  hostname - show viser to linjer: den indtastede og den faktisk brugte\r\n"
     "--- Traeder i kraft ---\r\n"
     "  WiFi: ved naeste 'connect'.  Ethernet: kraever 'save' + 'reboot'\r\n"
     "--- Eksempel ---\r\n"
     "  hostname skab3-gateway\r\n"
     "  save\r\n"
     "  reboot\r\n"},
    {"plc",
     "=== PLC - PLC'ens IP-adresse (adgangskontrol til Modbus TCP) ===\r\n"
     "--- Kommandoer ---\r\n"
     "  plc ip <a.b.c.d>        den ENESTE IP der maa forbinde til Modbus TCP-port 502/503\r\n"
     "--- Vises i 'show' som ---\r\n"
     "  plc.ip\r\n"
     "--- Traeder i kraft ---\r\n"
     "  naar den er gemt ('save' eller 'connect') - gaelder nye forbindelser\r\n"
     "  uden plc ip afvises ALLE Modbus TCP-forbindelser. Kraeves foer 'connect'\r\n"
     "  REST-API'et (port 8080) begraenses IKKE af plc ip - det beskyttes af 'rest auth'\r\n"
     "--- Eksempel ---\r\n"
     "  plc ip 192.168.1.10\r\n"
     "  save\r\n"},
    {"rest",
     "=== REST - login til management-API'et (port 8080), som PLC'en bruger ===\r\n"
     "--- Kommandoer ---\r\n"
     "  rest auth token|basic|both   hvilken login-metode API'et accepterer (default both)\r\n"
     "      token = kun Bearer-token (mgmt.token)\r\n"
     "      basic = kun brugernavn/adgangskode\r\n"
     "      both  = begge accepteres\r\n"
     "  rest user <navn>        brugernavn til basic-login (1-32 tegn)\r\n"
     "  rest pass <kode>        adgangskode til basic-login (8-63 tegn)\r\n"
     "--- Vises i 'show' som ---\r\n"
     "  rest.auth_mode, rest.user, rest.pass\r\n"
     "  rest.user/rest.pass vises kun ved rest.auth_mode both (de er stadig gemt)\r\n"
     "  mgmt.token = tokenet til token-login - se 'help token'\r\n"
     "  rest.api (i 'status') = URL'en til API'et\r\n"
     "--- Traeder i kraft ---\r\n"
     "  med det samme - gemmes automatisk, 'save' er ikke noedvendig\r\n"
     "  ADVARSEL: saet rest user + rest pass FOER 'rest auth basic', ellers kan\r\n"
     "  ingen logge ind paa API'et (ret det i saa fald her i CLI'en)\r\n"
     "--- Eksempel ---\r\n"
     "  rest user admin\r\n"
     "  rest pass hemmelig123\r\n"
     "  rest auth both\r\n"},
    {"token",
     "=== TOKEN - management-API-tokenet som PLC'en logger ind med ===\r\n"
     "--- Kommandoer ---\r\n"
     "  token regenerate        lav et nyt token - det gamle holder op med at virke straks\r\n"
     "--- Vises i 'show' som ---\r\n"
     "  mgmt.token (ogsaa i 'status')\r\n"
     "--- Traeder i kraft ---\r\n"
     "  med det samme - gemmes automatisk. Roerer intet andet\r\n"
     "  tokenet oprettes automatisk ved foerste vellykkede 'connect'\r\n"
     "  HUSK: indsaet det nye token i PLC'ens System-side under 'Modbus Expansion\r\n"
     "  Boards' - ellers mister PLC'en adgang til boardet\r\n"
     "--- Eksempel ---\r\n"
     "  token regenerate\r\n"},
    {"syslog",
     "=== SYSLOG - send log til en eller flere syslog-servere (UDP, maks 4) ===\r\n"
     "--- Kommandoer ---\r\n"
     "  syslog add <ip> <port> <tag> <level>   tilfoej modtager (eller opdater samme tag)\r\n"
     "      port  = normalt 514\r\n"
     "      tag   = navnet boardet optraeder under paa DEN server (1-24 tegn, a-z 0-9 - _)\r\n"
     "      level = 1-8, hvor meget der sendes: 1 = kun det mest kritiske,\r\n"
     "              4 = + fejl, 5 = + advarsler, 7 = + info, 8 = alt inkl. debug\r\n"
     "  syslog remove <tag>     fjern en modtager\r\n"
     "  no syslog               fjern ALLE modtagere (samme som 'no syslog all')\r\n"
     "--- Vises i 'show' som ---\r\n"
     "  syslog.target1..4 (eller 'syslog.targets: (ingen konfigureret)')\r\n"
     "  i 'status': syslog.sent / syslog.failed / syslog.queue_dropped - stiger\r\n"
     "  syslog.failed, kan modtageren ikke naas (tjek IP og at serveren koerer)\r\n"
     "--- Traeder i kraft ---\r\n"
     "  efter 'save' - virker straks, ingen reboot\r\n"
     "--- Eksempel ---\r\n"
     "  syslog add 192.168.1.20 514 skab3 6\r\n"
     "  save\r\n"},
    {"debug",
     "=== DEBUG - vis Modbus-trafikken paa kanal A/B live i denne konsol ===\r\n"
     "--- Kommandoer ---\r\n"
     "  debug modbus <a|b|all> level <1-8>   slaa debug til, hoejere level = flere\r\n"
     "                                       detaljer, 8 = ogsaa raa hex-dump\r\n"
     "  no debug modbus         slaa debug fra paa begge kanaler (= 'no debug all')\r\n"
     "--- Vises i 'status' som ---\r\n"
     "  debug.channel_a, debug.channel_b (0 = fra) - IKKE i 'show'\r\n"
     "--- Traeder i kraft ---\r\n"
     "  med det samme. Gemmes IKKE - debug er altid slaaet fra efter en reboot\r\n"
     "  kanalfejl vises kun i konsollen naar debug er slaaet til (syslog faar dem altid)\r\n"
     "--- Eksempel ---\r\n"
     "  debug modbus a level 8\r\n"
     "  no debug modbus\r\n"},
    {"test",
     "=== TEST - diagnostisk Modbus-laesning direkte fra CLI'en ===\r\n"
     "--- Kommandoer ---\r\n"
     "  test <kanal> <slave_id> <fc> <adresse> <antal>\r\n"
     "      kanal    = 1 (kanal A) eller 2 (kanal B)\r\n"
     "      slave_id = 1-247\r\n"
     "      fc       = 1 coils, 2 diskrete input, 3 holding-registre, 4 input-registre\r\n"
     "      adresse  = 0-65535 (0-baseret)\r\n"
     "      antal    = 1-2000 (Modbus-standarden tillader max 125 registre ved fc 3/4)\r\n"
     "--- Traeder i kraft ---\r\n"
     "  udfoeres straks. Kun laesning - der kan ikke skrives fra CLI'en\r\n"
     "  CLI'en venter paa svar/timeout; den anden kanal koerer uforstyrret videre\r\n"
     "  kanalens opsaetning (baud, paritet, timeout) saettes fra PLC'en, ikke her\r\n"
     "--- Eksempel ---\r\n"
     "  test 1 9 3 0 10         laes 10 holding-registre fra slave 9 paa kanal A\r\n"},
    {"ota",
     "=== OTA - firmwareopdatering (styres normalt fra PLC'en) ===\r\n"
     "--- Kommandoer ---\r\n"
     "  ota confirm             bekraeft en ny firmware manuelt (normalt goer PLC'en det)\r\n"
     "--- Saadan virker det ---\r\n"
     "  PLC'en uploader firmwaren (POST /api/ota) og genstarter boardet. Foerste\r\n"
     "  opstart afventer bekraeftelse: bekraeftes den ikke inden 10 minutter, eller\r\n"
     "  crasher den, ruller boardet selv tilbage til den forrige firmware\r\n"
     "  kun boardets egen firmware accepteres (identitets-markoer i filen)\r\n"
     "--- Vises i 'status' som ---\r\n"
     "  ota.firmware_id, ota.pending_confirm (sekunder til rollback), ota.last_rolled_back\r\n"
     "--- Eksempel ---\r\n"
     "  status\r\n"
     "  ota confirm\r\n"},
    {"show",
     "=== SHOW - vis al konfiguration (klartekst, inkl. adgangskoder og token) ===\r\n"
     "Viser det du har indtastet - ogsaa aendringer der endnu ikke er gemt.\r\n"
     "--- Felt i 'show' <- kommando der saetter det ---\r\n"
     "  hostname            <- hostname <navn> | hostname auto\r\n"
     "  wifi.enabled        <- wifi enable|disable\r\n"
     "  wifi.ssid           <- wifi ssid <navn>\r\n"
     "  wifi.pass           <- wifi pass <kode> | wifi open\r\n"
     "  wifi.mode           <- wifi mode dhcp|static\r\n"
     "  wifi.ip/mask/gw     <- wifi ip|mask|gw <a.b.c.d>\r\n"
     "  plc.ip              <- plc ip <a.b.c.d>\r\n"
     "  rest.auth_mode      <- rest auth token|basic|both\r\n"
     "  rest.user           <- rest user <navn>\r\n"
     "  rest.pass           <- rest pass <kode>\r\n"
     "  eth.enabled         <- eth enable|disable\r\n"
     "  eth.mode            <- eth mode dhcp|static\r\n"
     "  eth.ip/mask/gw      <- eth ip|mask|gw <a.b.c.d>\r\n"
     "  syslog.targetN      <- syslog add ... | syslog remove <tag> | no syslog\r\n"
     "  mgmt.token          <- token regenerate\r\n"
     "--- Kun visning (kan ikke saettes her) ---\r\n"
     "  firmware            firmware-version (se 'help version')\r\n"
     "  board_mode          rs232/rs485 - fysisk jumper (se 'help board_mode')\r\n"
     "  wifi.connection, wifi.rssi_dbm, eth.connection, eth.mac = live status\r\n"
     "--- Mere hjaelp ---\r\n"
     "  'help <felt>' forklarer et felt, fx 'help rest.auth_mode'\r\n"},
    {"status",
     "=== STATUS - hvad boardet koerer med lige nu ===\r\n"
     "--- Viser ---\r\n"
     "  firmware, uptime_s (sekunder siden opstart), heap_free_bytes (fri RAM),\r\n"
     "  hostname, board_mode, WiFi-/Ethernet-forbindelse, debug.channel_a/b,\r\n"
     "  rest.api (URL), provisioned, mgmt.token, rest.auth_mode, modbus_tcp-porte,\r\n"
     "  ota.firmware_id, ota.pending_confirm, ota.last_rolled_back (se 'help ota')\r\n"
     "--- Forskel til 'show' ---\r\n"
     "  status viser den GEMTE konfiguration boardet koerer med,\r\n"
     "  show viser det du har indtastet - ogsaa det der endnu ikke er gemt\r\n"},
    {"save",
     "=== SAVE - gem de indtastede indstillinger i flash (NVS) ===\r\n"
     "--- Bruges efter ---\r\n"
     "  eth-, hostname-, syslog-, plc ip- og wifi enable/disable-aendringer\r\n"
     "--- Godt at vide ---\r\n"
     "  forbinder IKKE til WiFi (det goer 'connect')\r\n"
     "  ikke-gemte aendringer er vaek efter en reboot\r\n"
     "  rest user/pass/auth og token regenerate gemmes automatisk\r\n"},
    {"connect",
     "=== CONNECT - forbind til WiFi med de indtastede felter, og gem dem ===\r\n"
     "--- Kraever ---\r\n"
     "  wifi ssid, wifi pass (eller wifi open) og plc ip\r\n"
     "  ved wifi mode static ogsaa wifi ip, wifi mask og wifi gw\r\n"
     "--- Godt at vide ---\r\n"
     "  venter op til 30 sekunder paa forbindelse\r\n"
     "  foerste vellykkede connect opretter mgmt.token og viser det\r\n"
     "  virker ogsaa selvom 'wifi disable' er sat\r\n"},
    {"reboot",
     "=== REBOOT - genstart boardet ===\r\n"
     "--- Godt at vide ---\r\n"
     "  rydder INTET - gemt konfiguration bevares (modsat factory-reset)\r\n"
     "  ikke-gemte aendringer (uden 'save') gaar tabt, og debug slaas fra\r\n"
     "  noedvendig efter eth-, wifi enable/disable- og board_mode-aendringer\r\n"},
    {"factory-reset",
     "=== FACTORY-RESET - slet AL konfiguration og genstart ===\r\n"
     "--- Kommandoer ---\r\n"
     "  factory-reset confirm   'confirm' er paakraevet, for at undgaa uheld\r\n"
     "--- Godt at vide ---\r\n"
     "  sletter WiFi, plc ip, REST-login, token, Ethernet, hostname, syslog og\r\n"
     "  kanal-opsaetning. Boardet skal derefter provisioneres forfra, og PLC'en\r\n"
     "  skal have det nye token\r\n"},
    {"version",
     "=== VERSION - vis firmware-version og build-nummer ===\r\n"
     "  samme oplysning som feltet 'firmware' i 'show' og 'status'\r\n"},
    {"board_mode",
     "=== BOARD_MODE - om kanalerne koerer RS232 eller RS485 ===\r\n"
     "--- Saettes IKKE i CLI'en ---\r\n"
     "  fysisk jumper paa GPIO4 (MODE_SEL), laest ved opstart:\r\n"
     "  GND = rs232,  3.3V eller ingen jumper = rs485\r\n"
     "--- Godt at vide ---\r\n"
     "  gaelder begge kanaler samtidig - kan ikke blandes\r\n"
     "  genstart boardet efter at jumperen er flyttet\r\n"
     "--- Vises i ---\r\n"
     "  'show' og 'status' som board_mode\r\n"},
    {"no",
     "=== NO - slaa noget fra eller ryd det igen ===\r\n"
     "--- Kommandoer ---\r\n"
     "  no debug modbus         slaa debug fra paa begge kanaler (= 'no debug all')\r\n"
     "  no syslog               fjern alle syslog-modtagere (= 'no syslog all'), kraever 'save'\r\n"},
    {"help",
     "=== HELP - hjaelp til CLI'en ===\r\n"
     "--- Kommandoer ---\r\n"
     "  help                    oversigt over alle kommandoer, opdelt i sektioner\r\n"
     "  help <emne>             udfoerlig forklaring, fx 'help wifi'\r\n"
     "  help <felt>             forklaring af et felt fra 'show'/'status', fx 'help rest.auth_mode'\r\n"
     "--- Emner ---\r\n"
     "  wifi eth hostname plc rest token syslog debug test ota show status\r\n"
     "  save connect reboot factory-reset version board_mode no help\r\n"},
};

// Felter fra "show"/"status" hvis navn IKKE følger "<emne>.<noget>"-mønsteret
// (de øvrige, fx wifi.ssid/rest.auth_mode/syslog.target1, slås op via delen
// før punktummet i find_help_topic()).
struct help_alias_t {
  const char *field;
  const char *topic;
};

static const help_alias_t kHelpAliases[] = {
    {"firmware", "version"},  {"mgmt", "token"},       {"uptime_s", "status"},
    {"heap_free_bytes", "status"}, {"provisioned", "status"}, {"modbus_tcp", "status"},
};

static const help_topic_t *find_topic_by_name(const char *name) {
  for (const help_topic_t &topic : kHelpTopics) {
    if (ieq(topic.name, name)) return &topic;
  }
  for (const help_alias_t &alias : kHelpAliases) {
    if (ieq(alias.field, name)) return find_topic_by_name(alias.topic);
  }
  return nullptr;
}

// Slår et emne op — enten direkte ("rest", "board_mode") eller via et
// feltnavn fra show/status ("rest.auth_mode" -> "rest", "mgmt.token" ->
// "token"), så man kan skrive præcis det man ser i "show".
static const help_topic_t *find_help_topic(const char *query) {
  const help_topic_t *topic = find_topic_by_name(query);
  if (topic != nullptr) return topic;

  const char *dot = strchr(query, '.');
  if (dot == nullptr || dot == query) return nullptr;
  char prefix[24];
  const size_t len = static_cast<size_t>(dot - query);
  if (len >= sizeof(prefix)) return nullptr;
  memcpy(prefix, query, len);
  prefix[len] = '\0';
  return find_topic_by_name(prefix);
}

// Sektionsoverskrift i "help"-oversigten.
static void append_section(char *buf, size_t capacity, size_t *pos, const char *title) {
  if (*pos >= capacity) return;
  const int written = snprintf(buf + *pos, capacity - *pos, "--- %s ---\r\n", title);
  if (written > 0) {
    const size_t advance = static_cast<size_t>(written);
    *pos += (advance < capacity - *pos) ? advance : (capacity - *pos - 1);
  }
}

// Tjekker om `state` har alle påkrævede felter til et "connect"-forsøg.
// Skriver en besked der navngiver det FØRSTE manglende felt (ikke en samlet
// liste) — installatøren retter ét felt ad gangen, samme filosofi som
// §3.4.1's fejlhåndtering ("ret kun ét felt").
static bool is_ready_to_connect(const mb_provisioning_state_t *state, char *out_message, size_t capacity) {
  if (!state->has_ssid) {
    snprintf(out_message, capacity, "mangler: wifi ssid <navn>");
    return false;
  }
  if (!state->has_password && !state->open_network) {
    snprintf(out_message, capacity, "mangler: wifi pass <kode> (eller 'wifi open' for aabent netvaerk)");
    return false;
  }
  if (!state->has_plc_ip) {
    snprintf(out_message, capacity, "mangler: plc ip <a.b.c.d>");
    return false;
  }
  if (state->static_ip && (!state->has_ip || !state->has_mask || !state->has_gw)) {
    snprintf(out_message, capacity, "mangler: wifi ip/mask/gw (mode er sat til static)");
    return false;
  }
  return true;
}

mb_provisioning_result_t mb_provisioning_apply_line(mb_provisioning_state_t *state, const char *line,
                                                     char *out_message, size_t out_message_capacity) {
  if (out_message != nullptr && out_message_capacity > 0) out_message[0] = '\0';
  if (state == nullptr || line == nullptr || out_message == nullptr || out_message_capacity == 0) {
    return PROV_INVALID_VALUE;
  }

  char buf[MB_PROV_CLI_MAX_LINE_LEN];
  size_t line_len = strlen(line);
  // Beskaer bevidst i stedet for at afvise en for lang linje — en trailing
  // CR/LF fra en seriel terminal skal ikke kunne goere en ellers gyldig
  // kommando til en fejl.
  if (line_len >= sizeof(buf)) line_len = sizeof(buf) - 1;
  memcpy(buf, line, line_len);
  buf[line_len] = '\0';
  while (line_len > 0 && (buf[line_len - 1] == '\r' || buf[line_len - 1] == '\n' || buf[line_len - 1] == ' ' ||
                          buf[line_len - 1] == '\t')) {
    buf[--line_len] = '\0';
  }

  // v0.24.0: hævet 4->6 for at rumme "test <n> <slave_id> <fc> <adresse>
  // <antal>" (6 tokens i alt, inkl. selve "test"-ordet).
  char *tokens[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
  const size_t token_count = tokenize(buf, tokens, 6);

  if (token_count == 0) {
    return PROV_EMPTY_LINE;
  }

  if (ieq(tokens[0], "help")) {
    // v0.29.0: "help <emne>" / "help <show-felt>" — udførlig forklaring.
    if (token_count >= 2) {
      const help_topic_t *topic = find_help_topic(tokens[1]);
      if (topic == nullptr) {
        snprintf(out_message, out_message_capacity,
                 "ukendt hjaelpe-emne '%s'. Emner: wifi eth hostname plc rest token syslog debug test ota show "
                 "status save connect reboot factory-reset version board_mode no help - eller et felt fra "
                 "'show', fx 'help rest.auth_mode'",
                 tokens[1]);
        return PROV_INVALID_VALUE;
      }
      snprintf(out_message, out_message_capacity, "%s", topic->text);
      return PROV_ACTION_HELP;
    }

    // v0.29.0 (Jan: "i den help skal ting være opdelt i seksioner") —
    // oversigten er grupperet i sektioner, hver med henvisning til sit
    // udførlige "help <emne>".
    size_t pos = 0;
    char *m = out_message;
    const size_t cap = out_message_capacity;
    m[0] = '\0';
    append_section(m, cap, &pos, "WiFi (help wifi)");
    append_line(m, cap, &pos, "wifi ssid <navn>", "netvaerkets navn");
    append_line(m, cap, &pos, "wifi pass <kode>", "WPA2-adgangskode (8-63 tegn)");
    append_line(m, cap, &pos, "wifi open", "aabent netvaerk uden adgangskode");
    append_line(m, cap, &pos, "wifi mode dhcp|static", "automatisk eller fast IP, default dhcp");
    append_line(m, cap, &pos, "wifi ip|mask|gw <a.b.c.d>", "fast IP - kun ved wifi mode static");
    append_line(m, cap, &pos, "wifi enable|disable", "forbind automatisk ved opstart, default enable");
    append_section(m, cap, &pos, "Ethernet (help eth)");
    append_line(m, cap, &pos, "eth enable|disable", "slaa W5500-Ethernet til/fra, default enable");
    append_line(m, cap, &pos, "eth mode dhcp|static", "automatisk eller fast IP, default dhcp");
    append_line(m, cap, &pos, "eth ip|mask|gw <a.b.c.d>", "fast IP - kun ved eth mode static");
    append_section(m, cap, &pos, "Netvaerk og adgang (help hostname / plc)");
    append_line(m, cap, &pos, "hostname <navn>|auto", "boardets navn paa netvaerket, default auto (fra MAC)");
    append_line(m, cap, &pos, "plc ip <a.b.c.d>", "den eneste IP der maa bruge Modbus TCP (port 502/503)");
    append_section(m, cap, &pos, "REST-API login (help rest / token)");
    append_line(m, cap, &pos, "rest auth token|basic|both", "hvilken login-metode API'et accepterer, default both");
    append_line(m, cap, &pos, "rest user <navn>", "brugernavn til basic-login");
    append_line(m, cap, &pos, "rest pass <kode>", "adgangskode til basic-login (8-63 tegn)");
    append_line(m, cap, &pos, "token regenerate", "nyt API-token til PLC'en - det gamle holder op med at virke");
    append_section(m, cap, &pos, "Logging og diagnose (help syslog / debug / test)");
    append_line(m, cap, &pos, "syslog add <ip> <port> <tag> <level 1-8>", "tilfoej syslog-modtager (maks 4)");
    append_line(m, cap, &pos, "syslog remove <tag>", "fjern en syslog-modtager");
    append_line(m, cap, &pos, "no syslog", "fjern ALLE syslog-modtagere");
    append_line(m, cap, &pos, "debug modbus <a|b|all> level <1-8>", "vis Modbus-trafik live i konsollen");
    append_line(m, cap, &pos, "no debug modbus", "slaa debug fra (begge kanaler)");
    append_line(m, cap, &pos, "test <kanal> <slave_id> <fc> <adresse> <antal>", "laes fra en Modbus-slave (fc 1-4)");
    append_section(m, cap, &pos, "Vis, gem og system");
    append_line(m, cap, &pos, "show", "vis al konfiguration - 'help show' forklarer felterne");
    append_line(m, cap, &pos, "status", "hvad boardet koerer med nu (forbindelse, uptime, heap)");
    append_line(m, cap, &pos, "save", "gem i flash uden at forbinde WiFi");
    append_line(m, cap, &pos, "connect", "forbind til WiFi og gem");
    append_line(m, cap, &pos, "reboot", "genstart - rydder INTET");
    append_line(m, cap, &pos, "ota confirm", "bekraeft ny firmware efter OTA - 'help ota'");
    append_line(m, cap, &pos, "factory-reset confirm", "slet AL konfiguration og genstart");
    append_line(m, cap, &pos, "version", "vis firmware-version+build");
    append_section(m, cap, &pos, "Hjaelp");
    append_line(m, cap, &pos, "help <emne>", "udfoerlig forklaring, fx 'help rest'");
    append_line(m, cap, &pos, "help <felt fra show>", "hvilken kommando saetter feltet, fx 'help rest.auth_mode'");
    append_line(m, cap, &pos, "help", "denne kommandoliste");
    return PROV_ACTION_HELP;
  }

  if (ieq(tokens[0], "version")) {
    // FW_VERSION/FW_BUILD injiceres af extra_scripts/extract_version.py fra
    // version.json (CLAUDE.md regel 1: version.json er den ENESTE kilde) —
    // kun defineret for esp32dev-target'et. native-miljøet (unit-tests) har
    // dem bevidst IKKE sat, så testen ikke skal opdateres ved hver
    // versionsbump — fallback-teksten er hvad testene faktisk verificerer.
#ifdef FW_VERSION
    snprintf(out_message, out_message_capacity, "HypervisionPLC Extension board v%s build %s", FW_VERSION,
             FW_BUILD);
#else
    snprintf(out_message, out_message_capacity,
             "HypervisionPLC Extension board (version ukendt - bygget uden FW_VERSION/FW_BUILD build-flags)");
#endif
    return PROV_ACTION_VERSION;
  }

  if (ieq(tokens[0], "show")) {
    mb_provisioning_format_status(state, out_message, out_message_capacity);
    return PROV_ACTION_SHOW;
  }

  if (ieq(tokens[0], "status")) {
    // Uptime/heap/WiFi-forbindelsesstatus er runtime-data denne
    // hardware-uafhængige funktion ikke har adgang til — kaldstedet
    // (src/provisioning.cpp) sammensætter selve status-teksten.
    return PROV_ACTION_STATUS;
  }

  if (ieq(tokens[0], "save")) {
    // Ren "gem hvad der er indtastet indtil nu"-handling — forsøger IKKE en
    // WiFi-forbindelse (det gør "connect"). Nyttig til fx at gemme REST-
    // credentials uden at ville (gen)forbinde WiFi lige nu.
    snprintf(out_message, out_message_capacity, "ok - gemmer til NVS");
    return PROV_ACTION_SAVE;
  }

  if (ieq(tokens[0], "connect")) {
    if (!is_ready_to_connect(state, out_message, out_message_capacity)) {
      return PROV_MISSING_ARGUMENT;
    }
    snprintf(out_message, out_message_capacity, "ok - forsoeger forbindelse");
    return PROV_ACTION_CONNECT;
  }

  if (ieq(tokens[0], "reboot")) {
    // Ikke-destruktiv, ingen confirm noedvendig — modsat "factory-reset"
    // rydder denne INTET i NVS (samme filosofi som REST-udgaven, v0.12.0).
    snprintf(out_message, out_message_capacity, "ok - genstarter");
    return PROV_ACTION_REBOOT;
  }

  // v0.23.0 (Jan: "hvordan generare vi ny token") — ikke-destruktiv, ingen
  // confirm noedvendig (Jan bekraeftet - roerer KUN tokenet, intet andet).
  // Selve genereringen (hardware-RNG) og persisteringen sker i kaldstedet
  // (src/provisioning.cpp/src/config.cpp), som ogsaa printer det nye token.
  if (ieq(tokens[0], "token")) {
    if (token_count < 2 || !ieq(tokens[1], "regenerate")) {
      snprintf(out_message, out_message_capacity, "brug 'token regenerate'");
      return PROV_MISSING_ARGUMENT;
    }
    snprintf(out_message, out_message_capacity, "ok - genererer nyt management-API-token");
    return PROV_ACTION_TOKEN_REGENERATE;
  }

  // v0.30.0 — manuel bekræftelse af en ny OTA-firmware (normalt gør PLC'en
  // det via POST /api/ota/confirm, se PLC_OTA_INTEGRATION_PLAN.md).
  if (ieq(tokens[0], "ota")) {
    if (token_count < 2 || !ieq(tokens[1], "confirm")) {
      snprintf(out_message, out_message_capacity, "brug 'ota confirm' (se 'help ota')");
      return PROV_MISSING_ARGUMENT;
    }
    snprintf(out_message, out_message_capacity, "ok - bekraefter den koerende firmware");
    return PROV_ACTION_OTA_CONFIRM;
  }

  if (ieq(tokens[0], "factory-reset")) {
    if (token_count < 2 || !ieq(tokens[1], "confirm")) {
      snprintf(out_message, out_message_capacity, "brug 'factory-reset confirm' for at bekraefte");
      return PROV_MISSING_ARGUMENT;
    }
    snprintf(out_message, out_message_capacity, "ok - rydder konfiguration og genstarter");
    return PROV_ACTION_FACTORY_RESET;
  }

  if (ieq(tokens[0], "wifi")) {
    if (token_count < 2) {
      snprintf(out_message, out_message_capacity,
               "wifi kraever en underkommando (enable/disable/ssid/pass/open/mode/ip/mask/gw)");
      return PROV_MISSING_ARGUMENT;
    }

    if (ieq(tokens[1], "open")) {
      state->open_network = true;
      state->has_password = false;
      state->password[0] = '\0';
      snprintf(out_message, out_message_capacity, "ok - aabent netvaerk (intet password)");
      return PROV_OK;
    }

    // v0.21.0 (Jan: "kan vi disable wifi også fra cli") — mirroring "eth
    // enable"/"eth disable". Gælder KUN boot-tids-auto-genforbindelsen
    // (src/provisioning.cpp) - en eksplicit "connect" virker stadig uanset
    // dette flag. Kræver "save" + "reboot", ikke live (Jan, bekræftet).
    if (ieq(tokens[1], "enable")) {
      state->wifi_enabled = true;
      snprintf(out_message, out_message_capacity, "ok - wifi.enabled=true (kraever 'save' + 'reboot')");
      return PROV_OK;
    }

    if (ieq(tokens[1], "disable")) {
      state->wifi_enabled = false;
      // Lockout-advarsel (Jan, bekræftet): blokerer IKKE kommandoen, men
      // gør det tydeligt at boardet mister AL netværksadgang hvis eth
      // OGSÅ er deaktiveret på dette tidspunkt - fysisk USB-adgang er
      // stadig en udvej (samme tillidsmodel som resten af §3.4), men det
      // er trods alt et driftsmæssigt uheld værd at undgå ubevidst.
      if (!state->eth_enabled) {
        snprintf(out_message, out_message_capacity,
                 "ok - wifi.enabled=false (kraever 'save' + 'reboot')\r\n"
                 "ADVARSEL: eth er OGSAA deaktiveret - boardet vil INGEN netvaerksadgang "
                 "have efter reboot (kun seriel CLI over USB)");
      } else {
        snprintf(out_message, out_message_capacity, "ok - wifi.enabled=false (kraever 'save' + 'reboot')");
      }
      return PROV_OK;
    }

    if (ieq(tokens[1], "mode")) {
      if (token_count < 3) {
        snprintf(out_message, out_message_capacity, "wifi mode kraever 'dhcp' eller 'static'");
        return PROV_MISSING_ARGUMENT;
      }
      if (ieq(tokens[2], "dhcp")) {
        state->static_ip = false;
        snprintf(out_message, out_message_capacity, "ok - mode=dhcp");
        return PROV_OK;
      }
      if (ieq(tokens[2], "static")) {
        state->static_ip = true;
        snprintf(out_message, out_message_capacity, "ok - mode=static (kraever ip/mask/gw)");
        return PROV_OK;
      }
      snprintf(out_message, out_message_capacity, "ugyldig mode '%s' - brug 'dhcp' eller 'static'", tokens[2]);
      return PROV_INVALID_VALUE;
    }

    if (token_count < 3) {
      snprintf(out_message, out_message_capacity, "wifi %s kraever en vaerdi", tokens[1]);
      return PROV_MISSING_ARGUMENT;
    }

    if (ieq(tokens[1], "ssid")) {
      if (!mb_provisioning_validate_ssid(tokens[2])) {
        snprintf(out_message, out_message_capacity, "ugyldigt ssid (1-%u tegn)",
                 static_cast<unsigned>(MB_PROV_SSID_MAX_LEN));
        return PROV_INVALID_VALUE;
      }
      strncpy(state->ssid, tokens[2], MB_PROV_SSID_MAX_LEN);
      state->ssid[MB_PROV_SSID_MAX_LEN] = '\0';
      state->has_ssid = true;
      snprintf(out_message, out_message_capacity, "ok - ssid sat");
      return PROV_OK;
    }

    if (ieq(tokens[1], "pass")) {
      if (!mb_provisioning_validate_password(tokens[2])) {
        snprintf(out_message, out_message_capacity, "ugyldigt password (%u-%u tegn - brug 'wifi open' for aabent net)",
                 static_cast<unsigned>(MB_PROV_PASSWORD_MIN_LEN), static_cast<unsigned>(MB_PROV_PASSWORD_MAX_LEN));
        return PROV_INVALID_VALUE;
      }
      strncpy(state->password, tokens[2], MB_PROV_PASSWORD_MAX_LEN);
      state->password[MB_PROV_PASSWORD_MAX_LEN] = '\0';
      state->has_password = true;
      state->open_network = false;
      snprintf(out_message, out_message_capacity, "ok - password sat: %s", state->password);
      return PROV_OK;
    }

    if (ieq(tokens[1], "ip") || ieq(tokens[1], "mask") || ieq(tokens[1], "gw")) {
      if (!mb_provisioning_validate_ipv4(tokens[2])) {
        snprintf(out_message, out_message_capacity, "ugyldig IPv4-adresse: %s", tokens[2]);
        return PROV_INVALID_VALUE;
      }
      if (ieq(tokens[1], "ip")) {
        set_ipv4_field(state->ip, tokens[2]);
        state->has_ip = true;
      } else if (ieq(tokens[1], "mask")) {
        set_ipv4_field(state->mask, tokens[2]);
        state->has_mask = true;
      } else {
        set_ipv4_field(state->gw, tokens[2]);
        state->has_gw = true;
      }
      snprintf(out_message, out_message_capacity, "ok - wifi %s sat", tokens[1]);
      return PROV_OK;
    }

    snprintf(out_message, out_message_capacity, "ukendt wifi-underkommando: %s", tokens[1]);
    return PROV_UNKNOWN_COMMAND;
  }

  if (ieq(tokens[0], "plc")) {
    if (token_count < 3 || !ieq(tokens[1], "ip")) {
      snprintf(out_message, out_message_capacity, "brug 'plc ip <a.b.c.d>'");
      return PROV_MISSING_ARGUMENT;
    }
    if (!mb_provisioning_validate_ipv4(tokens[2])) {
      snprintf(out_message, out_message_capacity, "ugyldig IPv4-adresse: %s", tokens[2]);
      return PROV_INVALID_VALUE;
    }
    set_ipv4_field(state->plc_ip, tokens[2]);
    state->has_plc_ip = true;
    snprintf(out_message, out_message_capacity, "ok - plc.ip sat");
    return PROV_OK;
  }

  // v0.22.0 (Jan: "vi skal lige have en hostname på kan jeg se da dhcp
  // server bare har et espressif name nu") — "hostname auto" rydder en
  // eksplicit override og falder tilbage til det MAC-udledte default
  // (mb_config_build_hostname(), lib/board_config/). WiFi anvender det
  // live ved næste "connect"; Ethernet kræver "reboot" (se eth_driver.h).
  if (ieq(tokens[0], "hostname")) {
    if (token_count < 2) {
      snprintf(out_message, out_message_capacity, "brug 'hostname <navn>' eller 'hostname auto'");
      return PROV_MISSING_ARGUMENT;
    }
    if (ieq(tokens[1], "auto")) {
      state->has_hostname = false;
      state->hostname[0] = '\0';
      snprintf(out_message, out_message_capacity, "ok - hostname=auto (MAC-udledt default)");
      return PROV_OK;
    }
    if (!mb_provisioning_validate_hostname(tokens[1])) {
      snprintf(out_message, out_message_capacity,
               "ugyldigt hostname (1-%u tegn, kun bogstaver/tal/'-', maa ikke starte/slutte med '-')",
               static_cast<unsigned>(MB_PROV_HOSTNAME_MAX_LEN));
      return PROV_INVALID_VALUE;
    }
    strncpy(state->hostname, tokens[1], MB_PROV_HOSTNAME_MAX_LEN);
    state->hostname[MB_PROV_HOSTNAME_MAX_LEN] = '\0';
    state->has_hostname = true;
    snprintf(out_message, out_message_capacity, "ok - hostname sat (wifi: live ved naeste 'connect', eth: kraever 'reboot')");
    return PROV_OK;
  }

  if (ieq(tokens[0], "rest")) {
    if (token_count < 3) {
      snprintf(out_message, out_message_capacity,
               "brug 'rest user <navn>', 'rest pass <kode>' eller 'rest auth token|basic|both' (se 'help rest')");
      return PROV_MISSING_ARGUMENT;
    }

    if (ieq(tokens[1], "user")) {
      if (!validate_rest_user(tokens[2])) {
        snprintf(out_message, out_message_capacity, "ugyldigt brugernavn (1-%u tegn)",
                 static_cast<unsigned>(MB_PROV_REST_USER_MAX_LEN));
        return PROV_INVALID_VALUE;
      }
      strncpy(state->rest_user, tokens[2], MB_PROV_REST_USER_MAX_LEN);
      state->rest_user[MB_PROV_REST_USER_MAX_LEN] = '\0';
      state->has_rest_user = true;
      snprintf(out_message, out_message_capacity, "ok - rest.user sat");
      return PROV_OK;
    }

    if (ieq(tokens[1], "pass")) {
      if (!validate_rest_pass(tokens[2])) {
        snprintf(out_message, out_message_capacity, "ugyldigt password (%u-%u tegn)",
                 static_cast<unsigned>(MB_PROV_REST_PASS_MIN_LEN), static_cast<unsigned>(MB_PROV_REST_PASS_MAX_LEN));
        return PROV_INVALID_VALUE;
      }
      strncpy(state->rest_pass, tokens[2], MB_PROV_REST_PASS_MAX_LEN);
      state->rest_pass[MB_PROV_REST_PASS_MAX_LEN] = '\0';
      state->has_rest_pass = true;
      snprintf(out_message, out_message_capacity, "ok - rest.pass sat: %s", state->rest_pass);
      return PROV_OK;
    }

    if (ieq(tokens[1], "auth")) {
      if (ieq(tokens[2], "token")) {
        state->rest_auth_mode = MB_REST_AUTH_MODE_TOKEN_ONLY;
        snprintf(out_message, out_message_capacity, "ok - rest.auth_mode=token (kun Bearer-token accepteres)");
        return PROV_OK;
      }
      if (ieq(tokens[2], "basic")) {
        state->rest_auth_mode = MB_REST_AUTH_MODE_BASIC_ONLY;
        snprintf(out_message, out_message_capacity, "ok - rest.auth_mode=basic (kun brugernavn/adgangskode accepteres)");
        return PROV_OK;
      }
      if (ieq(tokens[2], "both")) {
        state->rest_auth_mode = MB_REST_AUTH_MODE_BOTH;
        snprintf(out_message, out_message_capacity, "ok - rest.auth_mode=both (begge metoder accepteres)");
        return PROV_OK;
      }
      snprintf(out_message, out_message_capacity, "ugyldig rest auth-vaerdi '%s' - brug 'token', 'basic' eller 'both'",
               tokens[2]);
      return PROV_INVALID_VALUE;
    }

    snprintf(out_message, out_message_capacity, "ukendt rest-underkommando: %s", tokens[1]);
    return PROV_UNKNOWN_COMMAND;
  }

  // v0.20.0 (Jan: "har vi kommando til at enable/disable eterhnet samt ip
  // config, modes m.m.") — mirroring "wifi ..."-moenstret. Aendringer her
  // saettes kun i in-memory state (samme som "wifi ssid"/"rest user" osv.)
  // - kraever "save" for at persistere, og traeder foerst i kraft ved naeste
  // "reboot" (eth_driver_begin() koeres kun EN gang, ved boot).
  if (ieq(tokens[0], "eth")) {
    if (token_count < 2) {
      snprintf(out_message, out_message_capacity, "eth kraever en underkommando (enable/disable/mode/ip/mask/gw)");
      return PROV_MISSING_ARGUMENT;
    }

    if (ieq(tokens[1], "enable")) {
      state->eth_enabled = true;
      snprintf(out_message, out_message_capacity, "ok - eth.enabled=true (kraever 'save' + 'reboot')");
      return PROV_OK;
    }

    if (ieq(tokens[1], "disable")) {
      state->eth_enabled = false;
      // Samme lockout-advarsel som "wifi disable" ovenfor, symmetrisk.
      if (!state->wifi_enabled) {
        snprintf(out_message, out_message_capacity,
                 "ok - eth.enabled=false (kraever 'save' + 'reboot')\r\n"
                 "ADVARSEL: wifi er OGSAA deaktiveret - boardet vil INGEN netvaerksadgang "
                 "have efter reboot (kun seriel CLI over USB)");
      } else {
        snprintf(out_message, out_message_capacity, "ok - eth.enabled=false (kraever 'save' + 'reboot')");
      }
      return PROV_OK;
    }

    if (ieq(tokens[1], "mode")) {
      if (token_count < 3) {
        snprintf(out_message, out_message_capacity, "eth mode kraever 'dhcp' eller 'static'");
        return PROV_MISSING_ARGUMENT;
      }
      if (ieq(tokens[2], "dhcp")) {
        state->eth_static_ip = false;
        snprintf(out_message, out_message_capacity, "ok - eth.mode=dhcp (kraever 'save' + 'reboot')");
        return PROV_OK;
      }
      if (ieq(tokens[2], "static")) {
        state->eth_static_ip = true;
        snprintf(out_message, out_message_capacity, "ok - eth.mode=static (kraever 'save' + 'reboot')");
        return PROV_OK;
      }
      snprintf(out_message, out_message_capacity, "ugyldig eth mode '%s' - brug 'dhcp' eller 'static'", tokens[2]);
      return PROV_INVALID_VALUE;
    }

    if (ieq(tokens[1], "ip") || ieq(tokens[1], "mask") || ieq(tokens[1], "gw")) {
      if (token_count < 3) {
        snprintf(out_message, out_message_capacity, "brug 'eth %s <a.b.c.d>'", tokens[1]);
        return PROV_MISSING_ARGUMENT;
      }
      if (!mb_provisioning_validate_ipv4(tokens[2])) {
        snprintf(out_message, out_message_capacity, "ugyldig IPv4-adresse: %s", tokens[2]);
        return PROV_INVALID_VALUE;
      }
      if (ieq(tokens[1], "ip")) {
        set_ipv4_field(state->eth_ip, tokens[2]);
      } else if (ieq(tokens[1], "mask")) {
        set_ipv4_field(state->eth_mask, tokens[2]);
      } else {
        set_ipv4_field(state->eth_gw, tokens[2]);
      }
      snprintf(out_message, out_message_capacity, "ok - eth.%s sat (kraever 'save' + 'reboot')", tokens[1]);
      return PROV_OK;
    }

    snprintf(out_message, out_message_capacity, "ukendt eth-underkommando: %s", tokens[1]);
    return PROV_UNKNOWN_COMMAND;
  }

  // v0.26.0 (Jan: "kan vi lave en syslog funktion som vi kan sætte et
  // target på som modtager af syslog" / "en eller flere target") — "syslog
  // add <ip> <port> <tag> <level 1-8>" / "syslog remove <tag>". Persisteret
  // (§3.4.1-mønsteret, samme som eth/hostname ovenfor) — kræver 'save' for
  // at overleve en reboot. `level` genbruger v0.25.0's 1-8-verbositetsskala
  // som denne ENE modtagers loft (se provisioning_cli.h).
  if (ieq(tokens[0], "syslog")) {
    if (token_count < 2) {
      snprintf(out_message, out_message_capacity, "syslog kraever en underkommando (add/remove)");
      return PROV_MISSING_ARGUMENT;
    }

    if (ieq(tokens[1], "add")) {
      if (token_count < 6) {
        snprintf(out_message, out_message_capacity, "brug 'syslog add <ip> <port> <tag> <level 1-8>'");
        return PROV_MISSING_ARGUMENT;
      }
      if (!mb_provisioning_validate_ipv4(tokens[2])) {
        snprintf(out_message, out_message_capacity, "ugyldig IPv4-adresse: %s", tokens[2]);
        return PROV_INVALID_VALUE;
      }
      uint32_t port = 0;
      if (!parse_uint_token(tokens[3], &port) || port == 0 || port > 65535) {
        snprintf(out_message, out_message_capacity, "ugyldig port '%s' - skal vaere 1-65535", tokens[3]);
        return PROV_INVALID_VALUE;
      }
      if (!mb_provisioning_validate_syslog_tag(tokens[4])) {
        snprintf(out_message, out_message_capacity, "ugyldigt tag (1-%u tegn, kun bogstaver/tal/'-'/'_')",
                 static_cast<unsigned>(MB_SYSLOG_TAG_MAX_LEN));
        return PROV_INVALID_VALUE;
      }
      uint32_t level = 0;
      if (!parse_uint_token(tokens[5], &level) || level == 0 || level > MB_PROV_DEBUG_LEVEL_MAX) {
        snprintf(out_message, out_message_capacity, "ugyldigt level '%s' - skal vaere 1-%u", tokens[5],
                 static_cast<unsigned>(MB_PROV_DEBUG_LEVEL_MAX));
        return PROV_INVALID_VALUE;
      }

      // Genbrug en eksisterende slot med samme tag (opdatering), ellers
      // foerste ledige slot - ellers afvis (listen er fuld).
      int slot = -1;
      for (size_t i = 0; i < MB_SYSLOG_MAX_TARGETS; i++) {
        if (state->syslog_targets[i].in_use && ieq(state->syslog_targets[i].tag, tokens[4])) {
          slot = static_cast<int>(i);
          break;
        }
      }
      if (slot < 0) {
        for (size_t i = 0; i < MB_SYSLOG_MAX_TARGETS; i++) {
          if (!state->syslog_targets[i].in_use) {
            slot = static_cast<int>(i);
            break;
          }
        }
      }
      if (slot < 0) {
        snprintf(out_message, out_message_capacity,
                 "syslog-modtager-listen er fuld (maks %u) - fjern en foerst med 'syslog remove <tag>'",
                 static_cast<unsigned>(MB_SYSLOG_MAX_TARGETS));
        return PROV_INVALID_VALUE;
      }

      mb_syslog_target_t &target = state->syslog_targets[slot];
      target.in_use = true;
      set_ipv4_field(target.ip, tokens[2]);
      target.port = static_cast<uint16_t>(port);
      strncpy(target.tag, tokens[4], MB_SYSLOG_TAG_MAX_LEN);
      target.tag[MB_SYSLOG_TAG_MAX_LEN] = '\0';
      target.max_level = static_cast<uint8_t>(level);
      snprintf(out_message, out_message_capacity, "ok - syslog-modtager '%s' sat (%s:%u, level<=%u) - kraever 'save'",
               target.tag, target.ip, static_cast<unsigned>(target.port), static_cast<unsigned>(target.max_level));
      return PROV_OK;
    }

    if (ieq(tokens[1], "remove")) {
      if (token_count < 3) {
        snprintf(out_message, out_message_capacity, "brug 'syslog remove <tag>'");
        return PROV_MISSING_ARGUMENT;
      }
      for (size_t i = 0; i < MB_SYSLOG_MAX_TARGETS; i++) {
        if (state->syslog_targets[i].in_use && ieq(state->syslog_targets[i].tag, tokens[2])) {
          state->syslog_targets[i] = mb_syslog_target_t{};
          snprintf(out_message, out_message_capacity, "ok - syslog-modtager '%s' fjernet - kraever 'save'", tokens[2]);
          return PROV_OK;
        }
      }
      snprintf(out_message, out_message_capacity, "ukendt syslog-tag: %s", tokens[2]);
      return PROV_INVALID_VALUE;
    }

    snprintf(out_message, out_message_capacity, "ukendt syslog-underkommando: %s", tokens[1]);
    return PROV_UNKNOWN_COMMAND;
  }

  // v0.24.0 (Jan: "kan vi lave test fra cli") — "test <n> <slave_id> <fc>
  // <adresse> <antal>", CLI-udgaven af §4.2's diagnostiske
  // POST /api/channels/{n}/read. Samme grænser som lib/diagnostic_modbus's
  // JSON-udgave (mb_diag_parse_read_request()): fc 1-4, slave_id 1-247,
  // adresse 0-65535, antal 1-2000. KUN læsning - ingen "test write".
  if (ieq(tokens[0], "test")) {
    if (token_count < 6) {
      snprintf(out_message, out_message_capacity, "brug 'test <kanal 1|2> <slave_id> <fc 1-4> <adresse> <antal>'");
      return PROV_MISSING_ARGUMENT;
    }

    uint32_t n = 0, slave = 0, fc = 0, addr = 0, qty = 0;
    if (!parse_uint_token(tokens[1], &n) || (n != 1 && n != 2)) {
      snprintf(out_message, out_message_capacity, "ugyldig kanal '%s' - brug 1 (kanal A) eller 2 (kanal B)", tokens[1]);
      return PROV_INVALID_VALUE;
    }
    if (!parse_uint_token(tokens[2], &slave) || slave == 0 || slave > 247) {
      snprintf(out_message, out_message_capacity, "ugyldigt slave_id '%s' - skal vaere 1-247", tokens[2]);
      return PROV_INVALID_VALUE;
    }
    if (!parse_uint_token(tokens[3], &fc) || fc < 1 || fc > 4) {
      snprintf(out_message, out_message_capacity, "ugyldig function code '%s' - skal vaere 1-4 (kun laesning)", tokens[3]);
      return PROV_INVALID_VALUE;
    }
    if (!parse_uint_token(tokens[4], &addr) || addr > 0xFFFF) {
      snprintf(out_message, out_message_capacity, "ugyldig adresse '%s' - skal vaere 0-65535", tokens[4]);
      return PROV_INVALID_VALUE;
    }
    if (!parse_uint_token(tokens[5], &qty) || qty == 0 || qty > 2000) {
      snprintf(out_message, out_message_capacity, "ugyldigt antal '%s' - skal vaere 1-2000", tokens[5]);
      return PROV_INVALID_VALUE;
    }

    state->test_channel_number = static_cast<uint8_t>(n);
    state->test_read.function_code = static_cast<uint8_t>(fc);
    state->test_read.slave_id = static_cast<uint8_t>(slave);
    state->test_read.address = static_cast<uint16_t>(addr);
    state->test_read.quantity = static_cast<uint16_t>(qty);
    snprintf(out_message, out_message_capacity, "ok - udfoerer diagnostisk laesning paa kanal %u...",
             static_cast<unsigned>(n));
    return PROV_ACTION_TEST_READ;
  }

  // v0.25.0 (Jan: "lave en debug som outputer til console alt hvad der
  // forgå på kanal A og B") — "debug modbus <a|b|all> level <1-8>". Jans
  // egen Cisco-inspirerede syntaks. IKKE persisteret (kaldstedet saetter
  // den kun live via modbus_channel_set_debug_level(), rører aldrig NVS).
  if (ieq(tokens[0], "debug")) {
    if (token_count < 4 || !ieq(tokens[1], "modbus") || !ieq(tokens[3], "level") || token_count < 5) {
      snprintf(out_message, out_message_capacity, "brug 'debug modbus <a|b|all> level <1-8>'");
      return PROV_MISSING_ARGUMENT;
    }

    mb_debug_target_t target;
    if (ieq(tokens[2], "a")) {
      target = mb_debug_target_t::kA;
    } else if (ieq(tokens[2], "b")) {
      target = mb_debug_target_t::kB;
    } else if (ieq(tokens[2], "all")) {
      target = mb_debug_target_t::kAll;
    } else {
      snprintf(out_message, out_message_capacity, "ugyldig kanal '%s' - brug 'a', 'b' eller 'all'", tokens[2]);
      return PROV_INVALID_VALUE;
    }

    uint32_t level = 0;
    if (!parse_uint_token(tokens[4], &level) || level == 0 || level > MB_PROV_DEBUG_LEVEL_MAX) {
      snprintf(out_message, out_message_capacity, "ugyldigt level '%s' - skal vaere 1-%u", tokens[4],
               static_cast<unsigned>(MB_PROV_DEBUG_LEVEL_MAX));
      return PROV_INVALID_VALUE;
    }

    state->debug_target = target;
    state->debug_level = static_cast<uint8_t>(level);
    snprintf(out_message, out_message_capacity, "ok - modbus-debug level %u for kanal %s",
             static_cast<unsigned>(level), ieq(tokens[2], "all") ? "A+B" : tokens[2]);
    return PROV_ACTION_DEBUG_SET;
  }

  // "no debug modbus" / "no debug all" — begge er synonymer for FULD
  // deaktivering, paa BEGGE kanaler (Jan: "man skal kunne disable debug fra
  // cli også").
  if (ieq(tokens[0], "no")) {
    if (token_count < 2) {
      snprintf(out_message, out_message_capacity, "brug 'no debug modbus'/'no debug all' eller 'no syslog'/'no syslog all'");
      return PROV_MISSING_ARGUMENT;
    }

    if (ieq(tokens[1], "debug")) {
      if (token_count < 3 || (!ieq(tokens[2], "modbus") && !ieq(tokens[2], "all"))) {
        snprintf(out_message, out_message_capacity, "brug 'no debug modbus' eller 'no debug all'");
        return PROV_MISSING_ARGUMENT;
      }
      state->debug_target = mb_debug_target_t::kAll;
      state->debug_level = 0;
      snprintf(out_message, out_message_capacity, "ok - modbus-debug slaaet fra (begge kanaler)");
      return PROV_ACTION_DEBUG_SET;
    }

    // v0.26.1 (Jan: "har vi også no syslog som mulighed for at slette
    // config for syslog") — "no syslog"/"no syslog all" (synonymer, samme
    // "all"-mønster som "no debug ...") fjerner ALLE konfigurerede
    // modtagere på én gang, i stedet for at skulle "syslog remove <tag>"
    // dem én ad gangen. Persisteret (ligesom "syslog add"/"syslog remove")
    // — kræver 'save'.
    if (ieq(tokens[1], "syslog")) {
      if (token_count >= 3 && !ieq(tokens[2], "all")) {
        snprintf(out_message, out_message_capacity, "brug 'no syslog' eller 'no syslog all'");
        return PROV_INVALID_VALUE;
      }
      size_t cleared = 0;
      for (size_t i = 0; i < MB_SYSLOG_MAX_TARGETS; i++) {
        if (state->syslog_targets[i].in_use) cleared++;
        state->syslog_targets[i] = mb_syslog_target_t{};
      }
      snprintf(out_message, out_message_capacity, "ok - %u syslog-modtager(e) fjernet - kraever 'save'",
               static_cast<unsigned>(cleared));
      return PROV_OK;
    }

    snprintf(out_message, out_message_capacity, "brug 'no debug modbus'/'no debug all' eller 'no syslog'/'no syslog all'");
    return PROV_UNKNOWN_COMMAND;
  }

  snprintf(out_message, out_message_capacity, "ukendt kommando: %s (proev 'help')", tokens[0]);
  return PROV_UNKNOWN_COMMAND;
}
