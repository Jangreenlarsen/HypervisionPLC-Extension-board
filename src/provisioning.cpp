#include "provisioning.h"

#include <Arduino.h>
#include <WiFi.h>

#include <cstring>
#include <strings.h>

#include "config.h"
#include "http_server.h"
#include "modbus_tcp_server.h"
#include "provisioning_cli.h"

namespace {

mb_provisioning_state_t g_state;
char g_line_buf[MB_PROV_CLI_MAX_LINE_LEN];
size_t g_line_len = 0;

// §3.4.1: "rimelig timeout (fx 30 sek.)" for WiFi-forbindelsesforsøget.
constexpr uint32_t kConnectTimeoutMs = 30000;

// --- Kommando-historik (op/ned-piletaster, Jan) -----------------------------
// En seriel terminal sender piletaster som en 3-byte ANSI-escape-sekvens:
// ESC '[' 'A' (op) eller ESC '[' 'B' (ned). Kun historik-navigation
// understøttes — venstre/højre-pil, Home/End m.fl. ignoreres bevidst (ingen
// cursor-inde-i-linjen-redigering, kun hele linjer frem/tilbage i historikken).
constexpr size_t kHistorySize = 8;
char g_history[kHistorySize][MB_PROV_CLI_MAX_LINE_LEN];
size_t g_history_count = 0;  // antal gyldige entries (op til kHistorySize)
size_t g_history_next = 0;   // ringbuffer-skriveposition
// -1 = redigerer en frisk linje (ikke i historik-browse-tilstand);
// 0..count-1 = hvor mange skridt tilbage i historikken (0 = nyeste)
int g_history_browse = -1;

enum class EscState { kNone, kGotEsc, kGotBracket };
EscState g_esc_state = EscState::kNone;

void history_push(const char *line) {
  if (line[0] == '\0') return;
  if (g_history_count > 0) {
    const size_t last_idx = (g_history_next + kHistorySize - 1) % kHistorySize;
    if (strcmp(g_history[last_idx], line) == 0) return;  // spring konsekutive dubletter over
  }
  strncpy(g_history[g_history_next], line, MB_PROV_CLI_MAX_LINE_LEN - 1);
  g_history[g_history_next][MB_PROV_CLI_MAX_LINE_LEN - 1] = '\0';
  g_history_next = (g_history_next + 1) % kHistorySize;
  if (g_history_count < kHistorySize) g_history_count++;
}

const char *history_get(size_t steps_back) {
  const size_t idx = (g_history_next + kHistorySize - 1 - steps_back) % kHistorySize;
  return g_history[idx];
}

// Sletter den aktuelt viste linje på skærmen (backspace+mellemrum+backspace
// pr. tegn) og erstatter den med `new_content` — bruges til historik-recall.
void redraw_line(const char *new_content) {
  for (size_t i = 0; i < g_line_len; i++) {
    Serial.print("\b \b");
  }
  const size_t new_len = strlen(new_content);
  Serial.print(new_content);
  strncpy(g_line_buf, new_content, sizeof(g_line_buf) - 1);
  g_line_buf[sizeof(g_line_buf) - 1] = '\0';
  g_line_len = (new_len < sizeof(g_line_buf) - 1) ? new_len : (sizeof(g_line_buf) - 1);
}

void history_recall_older() {
  if (g_history_browse + 1 >= static_cast<int>(g_history_count)) return;  // allerede ved aeldste
  g_history_browse++;
  redraw_line(history_get(static_cast<size_t>(g_history_browse)));
}

void history_recall_newer() {
  if (g_history_browse < 0) return;  // allerede paa en frisk linje
  g_history_browse--;
  redraw_line(g_history_browse >= 0 ? history_get(static_cast<size_t>(g_history_browse)) : "");
}
// -----------------------------------------------------------------------

// Menneskelæselig gengivelse af WiFi.status() — dækker de tilstande der
// reelt forekommer under normal drift eksplicit, i stedet for at samle det
// meste under et uinformativt "ukendt/fejl" (Jan: "viser ikke connect
// status" — WL_NO_SHIELD o.lign. blev tidligere vist som "ukendt/fejl").
const char *wifi_status_text(wl_status_t status) {
  switch (status) {
    case WL_CONNECTED:
      return "forbundet";
    case WL_IDLE_STATUS:
      return "forbinder...";
    case WL_NO_SSID_AVAIL:
      return "fejl: SSID ikke fundet";
    case WL_CONNECT_FAILED:
      return "fejl: forbindelse fejlede (forkert password?)";
    case WL_CONNECTION_LOST:
      return "forbindelse tabt";
    case WL_DISCONNECTED:
      return "ikke forbundet";
    case WL_NO_SHIELD:
      return "wifi ikke initialiseret (ingen 'connect' forsoegt endnu)";
    default:
      return "ukendt";
  }
}

// Udskriver forbindelsesstatus — bruges af BÅDE "status" og "show", så de to
// kommandoer ikke kan komme til at modsige hinanden (Jan: "show status eller
// wifi viser ikke connect status").
void print_wifi_connection_status() {
  const wl_status_t status = WiFi.status();
  Serial.print("wifi.connection: ");
  Serial.println(wifi_status_text(status));
  if (status == WL_CONNECTED) {
    Serial.print("wifi.ip: ");
    Serial.println(WiFi.localIP());
    Serial.print("wifi.rssi_dbm: ");
    Serial.println(WiFi.RSSI());
  }
}

bool attempt_connect() {
  Serial.println("Forbinder til WiFi...");

  WiFi.mode(WIFI_STA);

  if (g_state.static_ip) {
    IPAddress ip, mask, gw;
    if (!ip.fromString(g_state.ip) || !mask.fromString(g_state.mask) || !gw.fromString(g_state.gw)) {
      Serial.println("FEJL: kunne ikke fortolke wifi ip/mask/gw som gyldige IPv4-adresser.");
      return false;
    }
    WiFi.config(ip, gw, mask);
  }

  if (g_state.open_network) {
    WiFi.begin(g_state.ssid);
  } else {
    WiFi.begin(g_state.ssid, g_state.password);
  }

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > kConnectTimeoutMs) {
      // §3.4.1: fald tilbage til CLI'en med en fejlbesked i stedet for at
      // haenge paa "forbinder..." for evigt — CLI'en forbliver tilgaengelig
      // til et nyt forsoeg.
      Serial.println();
      Serial.println("FEJL: WiFi-forbindelse timede ud efter 30 sekunder. Tjek ssid/pass og proev 'connect' igen.");
      WiFi.disconnect(true);
      return false;
    }
    delay(250);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Forbundet. Boardets IP er nu: ");
  Serial.println(WiFi.localIP());

  // Persistér til NVS (§3.5) og udsted et management-API-token FØRSTE gang
  // boardet nogensinde forbinder. Jan (bekræftet): CLI'en kræver fysisk
  // USB-adgang, så tokenet er IKKE write-only her (modsat REST-API'et,
  // §4.4, som fortsat aldrig returnerer det) — det kan altid hentes igen
  // via 'status'.
  const bool had_token_already = config_get().has_mgmt_token;
  config_apply_and_save(&g_state);
  config_mark_provisioned();

  char token[MB_MGMT_TOKEN_LEN + 1];
  config_ensure_mgmt_token(token, sizeof(token));
  if (!had_token_already) {
    Serial.println();
    Serial.println("Management-API-token (kan altid ses igen med 'status'):");
    Serial.println(token);
    Serial.println("Indsæt det i PLC'ens System-side under 'Modbus Expansion Boards'.");
  }

  http_server_begin();
  modbus_tcp_server_begin();

  return true;
}

void print_status() {
  Serial.println("--- Systemstatus ---");

  Serial.print("firmware: ");
#ifdef FW_VERSION
  Serial.print("v");
  Serial.print(FW_VERSION);
  Serial.print(" build ");
  Serial.println(FW_BUILD);
#else
  Serial.println("(version ukendt)");
#endif

  Serial.print("uptime_s: ");
  Serial.println(millis() / 1000);

  Serial.print("heap_free_bytes: ");
  Serial.println(ESP.getFreeHeap());

  print_wifi_connection_status();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("rest.api: http://");
    Serial.print(WiFi.localIP());
    Serial.println(":8080/api/status");
  }

  Serial.print("provisioned: ");
  Serial.println(config_get().provisioned ? "ja" : "nej");
  // Jan (bekræftet): al config skal være synlig i CLI'en — fysisk USB-adgang
  // er allerede den reelle tillidsgrænse (§3.4), maskering her giver ingen
  // ekstra beskyttelse. Gælder KUN CLI'en — REST-API'et (§4.2/§4.4) returnerer
  // fortsat aldrig tokenet.
  Serial.print("mgmt.token: ");
  Serial.println(config_get().has_mgmt_token ? config_get().mgmt_token : "(ikke sat)");
  Serial.print("rest.user: ");
  Serial.println(config_get().has_rest_user ? config_get().rest_user : "(ikke sat)");
  Serial.print("rest.pass: ");
  Serial.println(config_get().has_rest_pass ? config_get().rest_pass : "(ikke sat)");
  Serial.print("rest.auth_mode: ");
  switch (config_get().rest_auth_mode) {
    case MB_REST_AUTH_MODE_TOKEN_ONLY:
      Serial.println("token");
      break;
    case MB_REST_AUTH_MODE_BASIC_ONLY:
      Serial.println("basic");
      break;
    default:
      Serial.println("both");
      break;
  }

  Serial.print("modbus_tcp: ");
  Serial.println("port 502 (kanal A) / 503 (kanal B) - se 'help' for oevrige kommandoer");
}

void print_boot_banner() {
  Serial.println();
#ifdef FW_VERSION
  Serial.print("=== HypervisionPLC Extension board v");
  Serial.print(FW_VERSION);
  Serial.print(" build ");
  Serial.print(FW_BUILD);
  Serial.println(" - seriel provisioning-CLI ===");
#else
  Serial.println("=== HypervisionPLC Extension board (version ukendt) - seriel provisioning-CLI ===");
#endif
  Serial.println("Skriv 'help' for kommandoer, 'status' for systemstatus. Op/ned-pil = kommando-historik.");
  Serial.print("> ");
}

}  // namespace

void provisioning_begin() {
  mb_provisioning_state_init(&g_state);
  g_line_len = 0;
  g_history_count = 0;
  g_history_next = 0;
  g_history_browse = -1;
  g_esc_state = EscState::kNone;

  config_begin();
  print_boot_banner();

  // Automatisk genforbindelse ved boot, hvis boardet allerede er
  // provisioneret (§3.4's "CLI'en er altid tilgængelig"-princip — en
  // genstart skal ikke kræve at et menneske genindtaster credentials).
  if (config_get().provisioned && config_get().wifi_has_ssid) {
    mb_config_to_provisioning_state(&config_get(), &g_state);
    Serial.print("Gemt WiFi-config fundet (");
    Serial.print(g_state.ssid);
    Serial.println(") - forsoeger automatisk genforbindelse...");
    attempt_connect();
    Serial.print("> ");
  }
}

void provisioning_poll() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());

    // ANSI-escape-sekvens for piletaster (op/ned = historik) — se note ved
    // kHistorySize ovenfor for hvorfor kun disse to genkendes.
    if (g_esc_state == EscState::kGotEsc) {
      g_esc_state = (c == '[') ? EscState::kGotBracket : EscState::kNone;
      continue;
    }
    if (g_esc_state == EscState::kGotBracket) {
      g_esc_state = EscState::kNone;
      if (c == 'A') {
        history_recall_older();
      } else if (c == 'B') {
        history_recall_newer();
      }
      // Andre sekvenser (venstre/hoejre-pil, Home/End, ...) ignoreres bevidst.
      continue;
    }
    if (c == 0x1B) {  // ESC
      g_esc_state = EscState::kGotEsc;
      continue;
    }

    if (c == '\n') {
      Serial.println();
      g_line_buf[g_line_len] = '\0';
      history_push(g_line_buf);
      g_history_browse = -1;

      char message[MB_PROV_MSG_MAX_LEN];
      const mb_provisioning_result_t result =
          mb_provisioning_apply_line(&g_state, g_line_buf, message, sizeof(message));

      if (result != PROV_EMPTY_LINE) {
        Serial.print(message);  // beskeder er selv \r\n-termineret pr. linje (multi-linje-format)
        Serial.println();
      }

      if (result == PROV_ACTION_CONNECT) {
        attempt_connect();
      } else if (result == PROV_ACTION_FACTORY_RESET) {
        Serial.println("Rydder NVS-konfiguration og genstarter.");
        config_factory_reset();
        delay(500);
        ESP.restart();
      } else if (result == PROV_ACTION_STATUS) {
        print_status();
      } else if (result == PROV_ACTION_SAVE) {
        config_apply_and_save(&g_state);
        Serial.println("Gemt til NVS (WiFi-forbindelse IKKE forsoegt).");
      } else if (result == PROV_ACTION_SHOW) {
        // "show" (lib/provisioning_cli) kender hverken live
        // WiFi-forbindelsesstatus eller det persisterede management-token
        // (som slet ikke er en del af mb_provisioning_state_t — kun
        // config.cpp/NVS) — begge tilføjes her, saa 'show' reelt viser ALT
        // config-data (Jan: "vi kan ikke se ... hvad token key er sat til").
        Serial.print("mgmt.token: ");
        Serial.println(config_get().has_mgmt_token ? config_get().mgmt_token : "(ikke sat)");
        print_wifi_connection_status();
      } else if (result == PROV_OK && strncasecmp(g_line_buf, "rest", 4) == 0) {
        // REST-credentials/auth-mode ("rest user/pass/auth") persisteres
        // uafhængigt af WiFi-forbindelsesstatus (§4.4) — installatøren skal
        // kunne rotere dem uden en fuld "connect"-cyklus.
        config_apply_and_save(&g_state);
      }

      g_line_len = 0;
      Serial.print("> ");
    } else if (c == '\r') {
      // ignoreres — '\n' afslutter linjen (haandterer baade CRLF- og LF-terminaler)
    } else if (c == 0x08 || c == 0x7F) {  // backspace/delete
      if (g_line_len > 0) {
        g_line_len--;
        Serial.print("\b \b");
      }
    } else if (g_line_len < sizeof(g_line_buf) - 1) {
      g_line_buf[g_line_len++] = c;
      Serial.write(c);  // lokal echo
    }
  }
}
