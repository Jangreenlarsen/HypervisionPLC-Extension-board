#include "provisioning_cli.h"

#include <cctype>
#include <cstdio>
#include <cstring>

void mb_provisioning_state_init(mb_provisioning_state_t *state) {
  memset(state, 0, sizeof(*state));
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
  const char *password_display = "(ikke sat)";
  if (state->open_network) {
    password_display = "(aabent netvaerk)";
  } else if (state->has_password) {
    password_display = "********";
  }
  const char *rest_pass_display = state->has_rest_pass ? "********" : "(ikke sat)";

  size_t pos = 0;
  out_buffer[0] = '\0';
  append_line(out_buffer, out_buffer_capacity, &pos, "wifi.ssid", state->has_ssid ? state->ssid : "(ikke sat)");
  append_line(out_buffer, out_buffer_capacity, &pos, "wifi.pass", password_display);
  append_line(out_buffer, out_buffer_capacity, &pos, "wifi.mode", state->static_ip ? "static" : "dhcp");
  if (state->static_ip) {
    append_line(out_buffer, out_buffer_capacity, &pos, "wifi.ip", state->has_ip ? state->ip : "(ikke sat)");
    append_line(out_buffer, out_buffer_capacity, &pos, "wifi.mask", state->has_mask ? state->mask : "(ikke sat)");
    append_line(out_buffer, out_buffer_capacity, &pos, "wifi.gw", state->has_gw ? state->gw : "(ikke sat)");
  }
  append_line(out_buffer, out_buffer_capacity, &pos, "plc.ip", state->has_plc_ip ? state->plc_ip : "(ikke sat)");
  append_line(out_buffer, out_buffer_capacity, &pos, "rest.user", state->has_rest_user ? state->rest_user : "(ikke sat)");
  append_line(out_buffer, out_buffer_capacity, &pos, "rest.pass", rest_pass_display);
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

  char *tokens[4] = {nullptr, nullptr, nullptr, nullptr};
  const size_t token_count = tokenize(buf, tokens, 4);

  if (token_count == 0) {
    return PROV_EMPTY_LINE;
  }

  if (ieq(tokens[0], "help")) {
    size_t pos = 0;
    out_message[0] = '\0';
    append_line(out_message, out_message_capacity, &pos, "wifi ssid <navn>", "SSID for produktionsnetvaerket");
    append_line(out_message, out_message_capacity, &pos, "wifi pass <kode>", "WPA2-adgangskode (8-63 tegn)");
    append_line(out_message, out_message_capacity, &pos, "wifi open", "marker netvaerket som aabent (intet password)");
    append_line(out_message, out_message_capacity, &pos, "wifi mode dhcp|static", "netvaerkstype, default dhcp");
    append_line(out_message, out_message_capacity, &pos, "wifi ip/mask/gw <a.b.c.d>", "kun ved mode static");
    append_line(out_message, out_message_capacity, &pos, "plc ip <a.b.c.d>", "PLC'ens IP - seedes i firewall-allowlist");
    append_line(out_message, out_message_capacity, &pos, "rest user <navn>", "brugernavn til REST-management-API'et");
    append_line(out_message, out_message_capacity, &pos, "rest pass <kode>", "adgangskode til REST-management-API'et (8-63 tegn)");
    append_line(out_message, out_message_capacity, &pos, "show", "vis alt der er sat (password maskeret)");
    append_line(out_message, out_message_capacity, &pos, "status", "systemstatus (uptime/heap/WiFi/tilstand)");
    append_line(out_message, out_message_capacity, &pos, "save", "gem nuvaerende felter til NVS uden at forsoege forbindelse");
    append_line(out_message, out_message_capacity, &pos, "connect", "anvend felterne og forsoeg WiFi-forbindelse");
    append_line(out_message, out_message_capacity, &pos, "factory-reset confirm", "ryd WiFi/token/firewall og genstart");
    append_line(out_message, out_message_capacity, &pos, "version", "vis firmware-version+build");
    append_line(out_message, out_message_capacity, &pos, "help", "denne kommandoliste");
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
      snprintf(out_message, out_message_capacity, "wifi kraever en underkommando (ssid/pass/open/mode/ip/mask/gw)");
      return PROV_MISSING_ARGUMENT;
    }

    if (ieq(tokens[1], "open")) {
      state->open_network = true;
      state->has_password = false;
      state->password[0] = '\0';
      snprintf(out_message, out_message_capacity, "ok - aabent netvaerk (intet password)");
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
      snprintf(out_message, out_message_capacity, "ok - password sat (%u tegn)",
               static_cast<unsigned>(strlen(tokens[2])));
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

  if (ieq(tokens[0], "rest")) {
    if (token_count < 3) {
      snprintf(out_message, out_message_capacity, "brug 'rest user <navn>' eller 'rest pass <kode>'");
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
      snprintf(out_message, out_message_capacity, "ok - rest.pass sat (%u tegn)",
               static_cast<unsigned>(strlen(tokens[2])));
      return PROV_OK;
    }

    snprintf(out_message, out_message_capacity, "ukendt rest-underkommando: %s", tokens[1]);
    return PROV_UNKNOWN_COMMAND;
  }

  snprintf(out_message, out_message_capacity, "ukendt kommando: %s (proev 'help')", tokens[0]);
  return PROV_UNKNOWN_COMMAND;
}
