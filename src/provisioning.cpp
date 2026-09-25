#include "provisioning.h"

#include <Arduino.h>
#include <WiFi.h>

#include <cerrno>
#include <cstring>
#include <strings.h>

#include "config.h"
#include "diagnostic_modbus.h"
#include "eth_driver.h"
#include "modbus_channel.h"
#include "ota_manager.h"
#include "provisioning_cli.h"
#include "syslog_sender.h"

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

// Menneskelæselig gengivelse af eth_driver_status() — samme princip som
// wifi_status_text() ovenfor. v0.18.0 (Jan: "vi skal have noget diag på det
// w5500 så vi kan se det fungere eller om det er link fejl") — skelner
// eksplicit mellem "modulet blev aldrig fundet på SPI-bussen" (tjek den
// fysiske Ethernet-tilslutning/loedning) og "modulet virker fint, det er
// bare kablet/linket der mangler" (en ren netværks-sag), i stedet for det
// tidligere udifferentierede "link nede (intet kabel/modul...)".
const char *eth_status_text(eth_driver_status_t status) {
  switch (status) {
    case ETH_STATUS_NOT_DETECTED:
      return "intet W5500-modul fundet (tjek fysisk tilslutning/loedning - eller boardet har ikke et monteret)";
    case ETH_STATUS_LINK_DOWN:
      return "modul fundet, men link nede (tjek netvaerkskabel/switch-port)";
    case ETH_STATUS_WAITING_DHCP:
      return "link op, venter paa DHCP";
    case ETH_STATUS_CONNECTED:
      return "forbundet";
    default:
      return "ukendt";
  }
}

// Samme princip som print_wifi_connection_status() — Jan bad om at kunne se
// Ethernet-status (§1.3/§2.2, W5500, v0.13.0) og board_mode (§2.0.1's delte
// MODE_SEL, v0.14.0/v0.15.0) direkte i den serielle CLI, ikke kun via REST.
void print_ethernet_status() {
  const eth_driver_status_t status = eth_driver_status();
  Serial.print("eth.connection: ");
  Serial.println(eth_status_text(status));
  if (status == ETH_STATUS_CONNECTED) {
    Serial.print("eth.ip: ");
    Serial.println(eth_driver_ip_string());
  }
  // v0.20.0 (Jan: "og MAC skal så ved en show status i cli") — vises
  // UAFHÆNGIGT af forbindelsesstatus (den NVS-persisterede MAC findes og er
  // relevant selv med intet modul tilsluttet/Ethernet slået fra).
  uint8_t mac[6];
  eth_driver_get_mac(mac);
  Serial.printf("eth.mac: %02X:%02X:%02X:%02X:%02X:%02X\r\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// v0.24.0 ("test ...") — samme mb_error_code_t-værdier som
// modbus_channel.cpp's (interne, ikke-eksporterede) error_name(), men egen
// lille kopi her — samme "hold moduler begrebsmæssigt adskilte fremfor at
// dele triviel formateringslogik"-princip som fx rest_status.cpp's
// mode_to_string().
const char *test_error_text(mb_error_code_t error) {
  switch (error) {
    case MB_TIMEOUT: return "timeout - intet svar fra slaven";
    case MB_CRC_ERROR: return "CRC-fejl i svaret";
    case MB_NOT_ENABLED: return "kanalen er deaktiveret (enabled:false)";
    case MB_INVALID_SLAVE: return "svar fra forkert slave-ID";
    case MB_BUS_BUSY: return "kanalen er optaget (koen var fuld)";
    case MB_CHANNEL_UNREACHABLE: return "ufuldstaendigt/ulaeseligt svar";
    default: return "ukendt fejl";
  }
}

void print_board_mode() {
  const mb_channel_config_t cfg_a = modbus_channel_get_config(ModbusChannelId::kA);
  const bool rs485 = cfg_a.mode == MB_CHANNEL_MODE_RS485;
  Serial.print("board_mode: ");
  Serial.println(rs485 ? "rs485" : "rs232");
  // v0.31.0 (Jan: "board skal signalere til plc at det er et 4 x rs232 eller
  // 4 x rs485, alt efter jumper") — samme streng som REST'ens board_type.
  Serial.printf("board_type: %ux%s\r\n", static_cast<unsigned>(modbus_channel_active_count()), rs485 ? "RS485" : "RS232");
  Serial.print("expander: ");
  switch (modbus_channel_expander_status()) {
    case ModbusExpanderStatus::kOk:
      Serial.println("CJMCU-752 fundet (kanal C+D aktive)");
      break;
    case ModbusExpanderStatus::kNotFound:
      Serial.println("FEJL: EXP_SEL-jumperen siger monteret, men CJMCU-752 svarer ikke paa I2C (GPIO21/22) - kanal C+D virker ikke");
      break;
    case ModbusExpanderStatus::kNotFitted:
    default:
      Serial.println("ikke monteret (EXP_SEL-jumper ikke sat)");
      break;
  }
}

// v0.31.0: kanal-nummer 1-4 → id; false (med besked) hvis kanalen ikke er aktiv.
bool active_channel_from_number(uint8_t n, ModbusChannelId *out) {
  if (n < 1 || n > modbus_channel_active_count()) {
    Serial.printf("FEJL: kanal %u er ikke aktiv - boardet har %u kanaler (kanal 3-4 kraever CJMCU-752 og EXP_SEL-jumperen)\r\n",
                  static_cast<unsigned>(n), static_cast<unsigned>(modbus_channel_active_count()));
    return false;
  }
  *out = static_cast<ModbusChannelId>(n - 1);
  return true;
}

// v0.22.0 (Jan: "vi skal lige have en hostname på") — viser den FAKTISK
// anvendte hostname (custom eller auto-genereret), til forskel fra "show"
// (lib/provisioning_cli), som kun kender den KONFIGUREREDE override-
// tilstand, ikke MAC'en der indgår i auto-defaultet.
void print_hostname() {
  char hostname[40];
  uint8_t mac[6];
  eth_driver_get_mac(mac);
  mb_config_build_hostname(g_state.has_hostname, g_state.hostname, mac, hostname, sizeof(hostname));
  Serial.print("hostname: ");
  Serial.println(hostname);
}

bool attempt_connect() {
  Serial.println("Forbinder til WiFi...");

  // v0.22.0 (Jan: "vi skal lige have en hostname på") — SKAL sættes FØR
  // WiFi.mode(WIFI_STA) (Arduino-WiFi-kernens WiFiGenericClass::mode()
  // anvender selv hostnamet på STA-netif'et NÅR mode kaldes, ikke senere —
  // et kald efter mode()/begin() virker IKKE). Læses fra g_state (ikke
  // config_get()) så et lige-sat, endnu ikke "save"'et "hostname ..." også
  // gælder med det samme, ligesom SSID/password gør ovenfor.
  char hostname[40];
  uint8_t mac[6];
  eth_driver_get_mac(mac);
  mb_config_build_hostname(g_state.has_hostname, g_state.hostname, mac, hostname, sizeof(hostname));
  WiFi.setHostname(hostname);

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
  syslog_sender_refresh();  // v0.26.0: en evt. "syslog add ..." skal virke straks, ikke foerst efter reboot
  config_mark_provisioned();

  char token[MB_MGMT_TOKEN_LEN + 1];
  config_ensure_mgmt_token(token, sizeof(token));
  if (!had_token_already) {
    Serial.println();
    Serial.println("Management-API-token (kan altid ses igen med 'status'):");
    Serial.println(token);
    Serial.println("Indsæt det i PLC'ens System-side under 'Modbus Expansion Boards'.");
  }

  // v0.21.0: http_server_begin()/modbus_tcp_server_begin() flyttet til
  // src/main.cpp::setup() (kaldes nu ubetinget, uafhængigt af WiFi-status -
  // se dens kommentar for hvorfor) - IKKE længere herfra, da et rent
  // Ethernet-board (WiFi deaktiveret) ellers aldrig ville faa dem startet.

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

  print_hostname();
  print_board_mode();

  print_wifi_connection_status();
  print_ethernet_status();

  // v0.25.0 (Jan: "lave en debug som outputer til console") — live
  // runtime-tilstand (IKKE persisteret, se modbus_channel_set_debug_level()),
  // derfor vist her i 'status', ikke i 'show' (som kun viser konfigureret,
  // persisteret data).
  for (size_t i = 0; i < modbus_channel_active_count(); i++) {
    Serial.printf("debug.channel_%c: %u\r\n", static_cast<char>('a' + i),
                  static_cast<unsigned>(modbus_channel_get_debug_level(static_cast<ModbusChannelId>(i))));
  }
  // v0.21.0-fund: viste hidtil KUN rest.api naar WiFi var forbundet - et
  // rent Ethernet-board (WiFi deaktiveret/aldrig konfigureret, se BUGS.md's
  // relaterede server-start-fund) fik derfor aldrig vist URL'en, selvom
  // REST-API'et rent faktisk er naaeligt via Ethernet-IP'en. Viser nu
  // BEGGE, hvis begge er oppe (dual-stack, samme server svarer paa begge).
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("rest.api: http://");
    Serial.print(WiFi.localIP());
    Serial.println(":8080/api/status");
  }
  if (eth_driver_status() == ETH_STATUS_CONNECTED) {
    Serial.print("rest.api (eth): http://");
    Serial.print(eth_driver_ip_string());
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
  // Jan: "hvis vi køre rest auth token så skal rest user og rest pass [kun]
  // være i config kun hvis rest auth both" — vises KUN i BOTH-mode (præcis
  // som formuleret, se samme regel i lib/provisioning_cli's "show"-
  // formatter). Værdierne slettes IKKE fra NVS ved et modeskift.
  if (config_get().rest_auth_mode == MB_REST_AUTH_MODE_BOTH) {
    Serial.print("rest.user: ");
    Serial.println(config_get().has_rest_user ? config_get().rest_user : "(ikke sat)");
    Serial.print("rest.pass: ");
    Serial.println(config_get().has_rest_pass ? config_get().rest_pass : "(ikke sat)");
  }

  // v0.30.1 (BUGS.md) — syslog-afsendelsens tællere: en modtager der ikke
  // kan naas, ses her i stedet for som fejllinjer paa konsollen.
  syslog_sender_stats_t syslog_stats;
  syslog_sender_get_stats(&syslog_stats);
  Serial.printf("syslog.sent: %lu, syslog.failed: %lu, syslog.queue_dropped: %lu",
                static_cast<unsigned long>(syslog_stats.sent), static_cast<unsigned long>(syslog_stats.failed),
                static_cast<unsigned long>(syslog_stats.queue_dropped));
  if (syslog_stats.last_errno != 0) {
    Serial.printf(" (sidste fejlkode: %d", syslog_stats.last_errno);
    // Live-verificeret (BUGS.md v0.30.1): en modtager der ikke svarer paa
    // netvaerket (ARP) giver netop ENOMEM, én pakke pr. burst.
    if (syslog_stats.last_errno == ENOMEM) {
      Serial.print(" - modtageren svarer ikke paa netvaerket: tjek syslog-serverens IP og at den koerer");
    }
    Serial.print(")");
  }
  Serial.println();

  // v0.30.0 — OTA-tilstand (se 'help ota').
  Serial.print("ota.firmware_id: ");
  Serial.println(ota_manager_running_version());
  uint32_t ota_remaining_s = 0;
  if (ota_manager_pending_confirm(&ota_remaining_s)) {
    Serial.printf("ota.pending_confirm: JA - automatisk rollback om %u s (bekraeft med 'ota confirm')\r\n",
                  static_cast<unsigned>(ota_remaining_s));
  } else {
    Serial.println("ota.pending_confirm: nej");
  }
  Serial.print("ota.last_rolled_back: ");
  Serial.println(ota_manager_last_update_rolled_back() ? "ja - seneste opdatering blev rullet tilbage" : "nej");

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

  // config_begin() kaldes nu fra main.cpp::setup() - FOER modbus_channel_init_all(),
  // som ogsaa afhaenger af den. Kaldes IKKE her igen (ville blot vaere overfloedigt).
  print_boot_banner();

  // BUGS.md v0.29.1: CLI'ens arbejdskopi indlæses ALTID fra den gemte
  // konfiguration — ikke kun efter en vellykket WiFi-"connect". Ellers
  // viste "show" tomme felter efter genstart på et board sat op med "save"
  // alene, og næste "save"/"rest ..." overskrev hele NVS med den tomme kopi.
  mb_config_to_provisioning_state(&config_get(), &g_state);

  // Automatisk genforbindelse ved boot, hvis boardet allerede er
  // provisioneret (§3.4's "CLI'en er altid tilgængelig"-princip — en
  // genstart skal ikke kræve at et menneske genindtaster credentials).
  if (config_get().provisioned && config_get().wifi_has_ssid) {
    // v0.21.0 (Jan: "kan vi disable wifi også fra cli") — "wifi disable"
    // springer KUN denne automatiske boot-tids-genforbindelse over; en
    // eksplicit "connect" fra CLI'en virker stadig uanset flaget.
    if (!g_state.wifi_enabled) {
      Serial.println("WiFi deaktiveret via config (wifi disable) - springer automatisk genforbindelse over.");
    } else {
      Serial.print("Gemt WiFi-config fundet (");
      Serial.print(g_state.ssid);
      Serial.println(") - forsoeger automatisk genforbindelse...");
      attempt_connect();
    }
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

      // v0.29.0: `static` (4096 bytes) — samme begrundelse som response_pdu/
      // resp_body nedenfor: loopTask-stakken er kun 8192 bytes (BUGS.md
      // v0.24.0), og provisioning_poll() er aldrig genindtrædende.
      static char message[MB_PROV_MSG_MAX_LEN];
      const mb_provisioning_result_t result =
          mb_provisioning_apply_line(&g_state, g_line_buf, message, sizeof(message));

      if (result != PROV_EMPTY_LINE) {
        Serial.print(message);  // beskeder er selv \r\n-termineret pr. linje (multi-linje-format)
        Serial.println();
      }

      if (result == PROV_ACTION_CONNECT) {
        attempt_connect();
      } else if (result == PROV_ACTION_REBOOT) {
        Serial.println("Genstarter (ingen konfiguration rørt).");
        delay(500);
        ESP.restart();
      } else if (result == PROV_ACTION_TOKEN_REGENERATE) {
        // v0.23.0 (Jan: "hvordan generare vi ny token") — ikke-destruktiv,
        // roerer KUN tokenet. Vises i klartekst (samme tillidsmodel som
        // resten af CLI'en, §3.4/CLAUDE.md regel 6) - kaldstedet er den
        // ENESTE plads et nyt token nogensinde kan ses, REST-API'et
        // returnerer det aldrig.
        char new_token[MB_MGMT_TOKEN_LEN + 1];
        config_regenerate_mgmt_token(new_token, sizeof(new_token));
        Serial.println();
        Serial.println("Nyt management-API-token:");
        Serial.println(new_token);
        Serial.println("ADVARSEL: det GAMLE token virker IKKE laengere - opdater det med det samme i PLC'ens System-side under 'Modbus Expansion Boards'.");
      } else if (result == PROV_ACTION_TEST_READ) {
        // v0.24.0 (Jan: "kan vi lave test fra cli") — CLI-udgaven af §4.2's
        // diagnostiske POST /api/channels/{n}/read, samme
        // lib/diagnostic_modbus-byggeklodser som REST-handleren
        // (src/http_server.cpp) bruger. Udløser en RIGTIG transaktion —
        // tænder derfor ogsaa kanalens aktivitets-LED (v0.23.1).
        ModbusChannelId test_id;
        if (!active_channel_from_number(g_state.test_channel_number, &test_id)) {
          g_line_len = 0;
          Serial.print("> ");
          continue;
        }

        // Jan (afklaret efter at have oplevet dette som "kanal B goer ingenting"
        // under et "test"-kald mod kanal A, som timede ud): "test" er BEVIDST
        // synkron/blokerende — den serielle CLI (ÉN tekst-terminal, ÉN
        // kommando ad gangen) venter selv på svaret/timeout, ligesom "connect"
        // allerede gør ved WiFi. De to kanalers FreeRTOS-tasks kører uændret
        // fuldstændig uafhængigt af hinanden i baggrunden (konkret målt: et
        // REST-kald til kanal B svarede på 140ms, MENS et samtidigt kald til
        // kanal A stadig ventede på sin 762ms-timeout) — det er kun CLI'ens
        // EGEN prompt der er optaget, ikke kanalerne. Denne besked gør det
        // tydeligt FØR ventetiden, i stedet for at det ligner et hængende board.
        Serial.print("(CLI'en venter nu op til ");
        Serial.print(modbus_channel_get_config(test_id).timeout_ms);
        Serial.println("ms paa svar/timeout - den ANDEN kanal koerer uforstyrret videre i baggrunden)");

        uint8_t request_pdu[8];
        const size_t request_pdu_len = mb_diag_build_read_pdu(&g_state.test_read, request_pdu, sizeof(request_pdu));

        // BUGS.md v0.24.0: `static` her (og for err_body/resp_body nedenfor)
        // er IKKE stilistisk — response_pdu (253 bytes) + resp_body (op til
        // 4608 bytes) som stak-lokale variable i denne funktion (der dengang
        // ALLEREDE havde en 2048-byte `message`-buffer paa stakken - nu static)
        // overskred faktisk Arduino-kernens loopTask-stak (8192 bytes) og gav
        // et rigtigt, live-observeret stack-overflow-nedbrud. `provisioning_poll()`
        // kører udelukkende sekventielt på ÉN task (aldrig genindtrædende), så
        // `static` her introducerer ingen samtidigheds-risiko.
        static uint8_t response_pdu[MB_PDU_MAX_LEN];
        size_t response_pdu_len = 0;
        const mb_error_code_t test_result =
            modbus_channel_submit(test_id, g_state.test_read.slave_id, request_pdu, request_pdu_len, response_pdu,
                                   &response_pdu_len, sizeof(response_pdu));

        if (test_result != MB_OK) {
          Serial.print("FEJL: ");
          Serial.println(test_error_text(test_result));
        } else if (mb_diag_is_exception(response_pdu, response_pdu_len)) {
          static char err_body[192];
          const size_t err_len = mb_diag_build_exception_json(g_state.test_read.slave_id, response_pdu,
                                                                response_pdu_len, err_body, sizeof(err_body));
          Serial.println(err_len > 0 ? err_body : "FEJL: kunne ikke bygge exception-svaret");
        } else {
          static char resp_body[4608];
          const size_t resp_len = mb_diag_build_read_values_json(&g_state.test_read, response_pdu, response_pdu_len,
                                                                   resp_body, sizeof(resp_body));
          Serial.println(resp_len > 0 ? resp_body : "FEJL: kunne ikke bygge svaret");
        }
      } else if (result == PROV_ACTION_DEBUG_SET) {
        // v0.25.0 (Jan: "lave en debug som outputer til console alt hvad der
        // forgå på kanal A og B") — ren runtime-tilstand, roerer INTET i NVS
        // (state->debug_target/debug_level er scratch-felter, se
        // provisioning_cli.h). "all" saetter begge kanaler til samme level
        // (ogsaa 0, for "no debug modbus"/"no debug all").
        // v0.31.0: kanal C/D. "all" = alle AKTIVE kanaler.
        if (g_state.debug_target == mb_debug_target_t::kAll) {
          for (size_t i = 0; i < modbus_channel_active_count(); i++) {
            modbus_channel_set_debug_level(static_cast<ModbusChannelId>(i), g_state.debug_level);
          }
        } else {
          uint8_t n = 1;
          switch (g_state.debug_target) {
            case mb_debug_target_t::kB: n = 2; break;
            case mb_debug_target_t::kC: n = 3; break;
            case mb_debug_target_t::kD: n = 4; break;
            case mb_debug_target_t::kA:
            default: n = 1; break;
          }
          ModbusChannelId id;
          if (active_channel_from_number(n, &id)) {
            modbus_channel_set_debug_level(id, g_state.debug_level);
          }
        }
      } else if (result == PROV_ACTION_OTA_CONFIRM) {
        switch (ota_manager_confirm()) {
          case OtaConfirmResult::kConfirmed:
            Serial.print("Firmware ");
            Serial.print(ota_manager_running_version());
            Serial.println(" bekraeftet - automatisk rollback annulleret.");
            break;
          case OtaConfirmResult::kNothingPending:
            Serial.println("Intet at bekraefte - den koerende firmware er allerede bekraeftet.");
            break;
          case OtaConfirmResult::kFailed:
          default:
            Serial.println("FEJL: bekraeftelse fejlede - firmwaren afventer stadig bekraeftelse. Proev igen.");
            break;
        }
      } else if (result == PROV_ACTION_FACTORY_RESET) {
        Serial.println("Rydder NVS-konfiguration og genstarter.");
        config_factory_reset();
        delay(500);
        ESP.restart();
      } else if (result == PROV_ACTION_STATUS) {
        print_status();
      } else if (result == PROV_ACTION_SAVE) {
        config_apply_and_save(&g_state);
        syslog_sender_refresh();  // v0.26.0: en evt. "syslog add/remove ..." skal virke straks
        Serial.println("Gemt til NVS (WiFi-forbindelse IKKE forsoegt).");
      } else if (result == PROV_ACTION_SHOW) {
        // "show" (lib/provisioning_cli) kender hverken live
        // WiFi/Ethernet-forbindelsesstatus, board_mode eller det
        // persisterede management-token (som slet ikke er en del af
        // mb_provisioning_state_t — kun config.cpp/NVS) — alle tilføjes
        // her, saa 'show' reelt viser ALT config-data (Jan: "vi kan ikke
        // se ... hvad token key er sat til").
        Serial.print("mgmt.token: ");
        Serial.println(config_get().has_mgmt_token ? config_get().mgmt_token : "(ikke sat)");
        print_hostname();
        print_board_mode();
        print_wifi_connection_status();
        print_ethernet_status();
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
