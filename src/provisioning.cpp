#include "provisioning.h"

#include <Arduino.h>
#include <WiFi.h>

#include "provisioning_cli.h"

namespace {

mb_provisioning_state_t g_state;
char g_line_buf[MB_PROV_CLI_MAX_LINE_LEN];
size_t g_line_len = 0;

// §3.4.1: "rimelig timeout (fx 30 sek.)" for WiFi-forbindelsesforsøget.
constexpr uint32_t kConnectTimeoutMs = 30000;

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
  Serial.println("Skriv 'help' for kommandoer, 'version' for firmware-version.");
  Serial.print("> ");
}

}  // namespace

void provisioning_begin() {
  mb_provisioning_state_init(&g_state);
  g_line_len = 0;
  print_boot_banner();
}

void provisioning_poll() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());

    if (c == '\n') {
      Serial.println();
      g_line_buf[g_line_len] = '\0';

      char message[MB_PROV_MSG_MAX_LEN];
      const mb_provisioning_result_t result =
          mb_provisioning_apply_line(&g_state, g_line_buf, message, sizeof(message));

      if (result != PROV_EMPTY_LINE) {
        Serial.println(message);
      }

      if (result == PROV_ACTION_CONNECT) {
        attempt_connect();
      } else if (result == PROV_ACTION_FACTORY_RESET) {
        Serial.println(
            "BEMAERK: NVS-persistering findes ikke endnu, saa der er intet gemt at rydde. Genstarter alligevel.");
        delay(500);
        ESP.restart();
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
