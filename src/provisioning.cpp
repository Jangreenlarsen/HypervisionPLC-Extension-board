#include "provisioning.h"

#include <Arduino.h>
#include <WiFi.h>

#include <cstring>

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

void attempt_connect() {
  Serial.println("Forbinder til WiFi...");

  WiFi.mode(WIFI_STA);

  if (g_state.static_ip) {
    IPAddress ip, mask, gw;
    if (!ip.fromString(g_state.ip) || !mask.fromString(g_state.mask) || !gw.fromString(g_state.gw)) {
      Serial.println("FEJL: kunne ikke fortolke wifi ip/mask/gw som gyldige IPv4-adresser.");
      return;
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
      return;
    }
    delay(250);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Forbundet. Boardets IP er nu: ");
  Serial.println(WiFi.localIP());
  Serial.println(
      "BEMAERK: management-API-token, NVS-persistering og firewall-seed (paragraf 4.3) er IKKE "
      "implementeret endnu (config.cpp mangler, senere i Fase 3) - forbindelsen overlever "
      "IKKE en genstart af boardet.");
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

  Serial.print("wifi.status: ");
  switch (WiFi.status()) {
    case WL_CONNECTED:
      Serial.println("forbundet");
      Serial.print("wifi.ip: ");
      Serial.println(WiFi.localIP());
      Serial.print("wifi.rssi_dbm: ");
      Serial.println(WiFi.RSSI());
      break;
    case WL_IDLE_STATUS:
      Serial.println("ikke forsoegt (ingen 'connect' kaldt endnu)");
      break;
    case WL_DISCONNECTED:
      Serial.println("ikke forbundet");
      break;
    default:
      Serial.println("ukendt/fejl");
      break;
  }

  Serial.println(
      "BEMAERK: kanal-/modbus-status er ikke relevant endnu - UART-kanaler, REST-API og "
      "config.cpp er ikke implementeret (Fase 1/3 fortsaetter).");
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
  print_boot_banner();
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
        Serial.println(
            "BEMAERK: NVS-persistering findes ikke endnu, saa der er intet gemt at rydde. Genstarter alligevel.");
        delay(500);
        ESP.restart();
      } else if (result == PROV_ACTION_STATUS) {
        print_status();
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
