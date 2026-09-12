#include "modbus_tcp_server.h"

#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "config.h"
#include "modbus_channel.h"
#include "modbus_pdu.h"
#include "modbus_tcp.h"

namespace {

constexpr uint16_t kPortChannelA = 502;
constexpr uint16_t kPortChannelB = 503;
constexpr uint32_t kSocketReadTimeoutMs = 1000;

// Standard Modbus-gateway-exceptions (§4.1) — sendes til PLC'en når selve
// TCP/MBAP-laget er i orden, men RTU-transaktionen mod feltbus-slaven
// fejlede. Dette er IKKE en almindelig Modbus-exception (som kommer fra
// slaven selv og relayes uændret via out_pdu) — det er gatewayens egen måde
// at sige "din forespørgsel er gyldig, men jeg kunne ikke levere den".
constexpr uint8_t kGatewayPathUnavailable = 0x0A;         // kanalen findes ikke/ugyldig forespørgsel til den
constexpr uint8_t kGatewayTargetFailedToRespond = 0x0B;   // slaven svarede ikke/svarede forkert (CRC, adresse, timeout)

struct ServerContext {
  uint16_t port;
  ModbusChannelId channel;
};

ServerContext g_channel_a_ctx{kPortChannelA, ModbusChannelId::kA};
ServerContext g_channel_b_ctx{kPortChannelB, ModbusChannelId::kB};
bool g_started = false;

// §4.3 (revideret): ÉT fast PLC-IP-permit, håndhævet FØR noget Modbus-indhold
// overhovedet parses. Intet `plc_ip` sat → data-planet er fejl-lukket for
// ALLE, ikke fejl-åbent.
bool is_permitted_peer(const IPAddress &peer) {
  const mb_board_config_t &cfg = config_get();
  if (!cfg.has_plc_ip) {
    return false;
  }
  IPAddress permitted;
  if (!permitted.fromString(cfg.plc_ip)) {
    return false;
  }
  return peer == permitted;
}

uint8_t gateway_exception_for(mb_error_code_t error) {
  switch (error) {
    case MB_CHANNEL_UNREACHABLE:
    case MB_BUS_BUSY:
    case MB_INVALID_ADDRESS:
    case MB_NOT_ENABLED:  // §4.2: en deaktiveret kanal er en util-gaengelig sti, ikke en tavs slave
      return kGatewayPathUnavailable;
    case MB_TIMEOUT:
    case MB_CRC_ERROR:
    case MB_INVALID_SLAVE:
    default:
      return kGatewayTargetFailedToRespond;
  }
}

// BUGS.md v0.9.0.1/.2: `WiFiClient::readBytes()`s indbyggede `setTimeout()`
// viste sig IKKE pålideligt at give tasken sit tidsbudget tilbage på ESP32
// (en klient der forbinder og derefter aldrig sender mere kunne blokere
// denne funktion langt ud over `timeout_ms`, og dermed hele portens
// lyttetask permanent). Denne erstatning bruger UDELUKKENDE
// `client.available()`/`client.read()` (ikke-blokerende på ESP32's
// WiFiClient) i en løkke vi selv tidsbegrænser via `millis()` — garanterer
// at funktionen ALTID returnerer inden for `timeout_ms` (+ få ms), uanset
// hvordan det underliggende bibliotek håndterer sin egen timeout.
bool read_exact(WiFiClient &client, uint8_t *buf, size_t len, uint32_t timeout_ms) {
  size_t received = 0;
  const uint32_t start = millis();
  while (received < len) {
    if (client.available()) {
      const int c = client.read();
      if (c < 0) {
        return false;
      }
      buf[received++] = static_cast<uint8_t>(c);
      continue;
    }
    if (!client.connected() || millis() - start > timeout_ms) {
      return false;
    }
    delay(1);
  }
  return true;
}

// Læser og besvarer ÉN Modbus TCP-forespørgsel på en allerede-accepteret,
// permit-godkendt forbindelse. Returnerer false hvis forbindelsen skal
// lukkes (framing-fejl, timeout på selve socket-læsningen, eller klienten er
// lukket) — true hvis den kan blive åben til flere forespørgsler (langt de
// fleste Modbus TCP-mastere genbruger forbindelsen, §4.1).
bool handle_one_request(WiFiClient &client, ModbusChannelId channel) {
  uint8_t header_buf[MB_MBAP_HEADER_LEN];
  if (!read_exact(client, header_buf, sizeof(header_buf), kSocketReadTimeoutMs)) {
    return false;  // klienten lukkede, eller intet nåede frem inden read-timeout
  }

  mb_mbap_header_t header;
  if (mb_mbap_parse_header(header_buf, sizeof(header_buf), &header) != MB_MBAP_OK) {
    return false;  // forkert protocol_id — ikke Modbus, ingen mening i at fortsætte på denne forbindelse
  }

  if (header.length <= 1 || header.length - 1 > MB_PDU_MAX_LEN) {
    return false;  // <=1 => tom PDU (kun function code mangler), eller urealistisk stor længde — luk frem for at gætte videre på strømmen
  }
  const size_t pdu_len = header.length - 1;

  uint8_t request_pdu[MB_PDU_MAX_LEN];
  if (!read_exact(client, request_pdu, pdu_len, kSocketReadTimeoutMs)) {
    return false;
  }

  uint8_t response_pdu[MB_PDU_MAX_LEN];
  size_t response_pdu_len = 0;
  const mb_error_code_t result =
      modbus_channel_submit(channel, header.unit_id, request_pdu, pdu_len, response_pdu, &response_pdu_len, sizeof(response_pdu));

  uint8_t final_pdu[2];
  const uint8_t *pdu_to_send = response_pdu;
  size_t pdu_to_send_len = response_pdu_len;
  if (result != MB_OK) {
    final_pdu[0] = static_cast<uint8_t>(request_pdu[0] | 0x80);
    final_pdu[1] = gateway_exception_for(result);
    pdu_to_send = final_pdu;
    pdu_to_send_len = sizeof(final_pdu);
  }

  uint8_t adu[MB_MBAP_HEADER_LEN + MB_PDU_MAX_LEN];
  const size_t adu_len =
      mb_mbap_build_response(header.transaction_id, header.unit_id, pdu_to_send, pdu_to_send_len, adu, sizeof(adu));
  if (adu_len == 0) {
    return false;
  }

  client.write(adu, adu_len);
  return true;
}

void tcp_server_task(void *param) {
  ServerContext *ctx = static_cast<ServerContext *>(param);

  WiFiServer server(ctx->port);
  server.begin();
  server.setNoDelay(true);

  for (;;) {
    WiFiClient client = server.available();
    if (!client) {
      delay(20);
      continue;
    }

    if (!is_permitted_peer(client.remoteIP())) {
      // §4.3: afvises FØR noget Modbus-indhold parses — ingen respons, blot luk.
      client.stop();
      continue;
    }

    while (client.connected()) {
      if (!handle_one_request(client, ctx->channel)) {
        break;
      }
    }
    client.stop();
  }
}

}  // namespace

void modbus_tcp_server_begin() {
  if (g_started) return;  // undgår dobbelt-lyttesockets ved gen-forbindelse (samme moenster som http_server_begin())
  g_started = true;

  xTaskCreate(tcp_server_task, "mb_tcp_a", 4096, &g_channel_a_ctx, tskIDLE_PRIORITY + 1, nullptr);
  xTaskCreate(tcp_server_task, "mb_tcp_b", 4096, &g_channel_b_ctx, tskIDLE_PRIORITY + 1, nullptr);

  Serial.println("Modbus TCP-server startet: port 502 (kanal A), port 503 (kanal B).");
}
