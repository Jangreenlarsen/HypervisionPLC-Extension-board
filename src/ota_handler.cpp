#include "ota_handler.h"

#include <Arduino.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

#include <cstdio>
#include <cstring>

#include "http_helpers.h"
#include "ota_manager.h"
#include "ota_validation.h"
#include "syslog_sender.h"

namespace {

enum class OtaState : uint8_t { kIdle, kInProgress, kSuccess, kFailed };

struct OtaStatus {
  OtaState state = OtaState::kIdle;
  size_t received = 0;
  size_t total = 0;
  char error_message[128] = {0};
  char new_version[MB_FWID_VERSION_MAX_LEN + 1] = {0};  // v0.30.0: markør-version i det senest uploadede image
};

OtaStatus g_ota_status;
bool g_ota_in_progress = false;  // konkurrence-lås — kun ét upload ad gangen

constexpr size_t kOtaChunkSize = 2048;
constexpr uint32_t kOtaSocketTimeoutS = 60;  // et fuldt firmware-upload kan tage betydeligt længere end en almindelig JSON-request

const char *state_name(OtaState state) {
  switch (state) {
    case OtaState::kIdle:
      return "idle";
    case OtaState::kInProgress:
      return "in_progress";
    case OtaState::kSuccess:
      return "success";
    case OtaState::kFailed:
      return "failed";
    default:
      return "?";
  }
}

void set_failed(const char *message) {
  g_ota_status.state = OtaState::kFailed;
  strncpy(g_ota_status.error_message, message, sizeof(g_ota_status.error_message) - 1);
  g_ota_status.error_message[sizeof(g_ota_status.error_message) - 1] = '\0';
  syslog_logf(MB_SYSLOG_FACILITY_SYSTEM, 1, "OTA: upload fejlede: %s", message);
}

// Klientens IP til syslog-audit ("hvem opdaterede boardet?").
void peer_ip(httpd_req_t *req, char *out, size_t out_size) {
  snprintf(out, out_size, "?");
  struct sockaddr_in6 addr;
  socklen_t addr_len = sizeof(addr);
  if (getpeername(httpd_req_to_sockfd(req), reinterpret_cast<struct sockaddr *>(&addr), &addr_len) != 0) return;
  if (addr.sin6_family == AF_INET6) {
    // IPv4-mapped IPv6 (::ffff:a.b.c.d) - de sidste 4 bytes er IPv4-adressen.
    const uint8_t *b = reinterpret_cast<const uint8_t *>(&addr.sin6_addr) + 12;
    snprintf(out, out_size, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
  } else if (addr.sin6_family == AF_INET) {
    const struct sockaddr_in *v4 = reinterpret_cast<const struct sockaddr_in *>(&addr);
    const uint8_t *b = reinterpret_cast<const uint8_t *>(&v4->sin_addr.s_addr);
    snprintf(out, out_size, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
  }
}

// POST /api/ota — rå binær body. Skriver til den inaktive OTA-partition via
// Arduino-corets Update-bibliotek (wrapper omkring esp_ota_ops, inkl.
// checksum-verifikation i Update.end(true)). Ved succes er den nye firmware
// sat som boot-partition, men boardet genstarter IKKE af sig selv — kræver
// et eksplicit POST /api/reboot (§4.2's designbeslutning: en operatør skal
// bevidst aktivere en ny firmware, ikke overraskes af en automatisk reboot).
//
// v0.30.0: afviser et image uden boardets firmware-identitets-markør (fx
// PLC'ens egen firmware), verificerer en valgfri X-Firmware-MD5-header, og
// afviser nye uploads mens den KØRENDE firmware afventer bekræftelse (se
// src/ota_manager.h) — et upload ville ellers overskrive netop den partition
// der er rollback-målet.
esp_err_t ota_upload_handler(httpd_req_t *req) {
  if (!require_auth(req)) return ESP_OK;

  if (g_ota_in_progress) {
    send_json_error(req, "409 Conflict", -1, "ota_in_progress", "En anden OTA-upload er allerede i gang");
    return ESP_OK;
  }

  if (ota_manager_pending_confirm(nullptr)) {
    send_json_error(req, "409 Conflict", -1, "ota_pending_confirm",
                    "Den koerende firmware afventer bekraeftelse - kald POST /api/ota/confirm (eller vent paa "
                    "automatisk rollback) foer en ny upload");
    return ESP_OK;
  }

  const size_t content_len = req->content_len;
  if (content_len == 0) {
    // esp_http_server understøtter ikke chunked transfer-encoding af en
    // request-body - længden SKAL kendes på forhånd.
    send_json_error(req, "411 Length Required", -1, "length_required",
                    "Content-Length med firmwarens stoerrelse kraeves (chunked transfer-encoding understoettes ikke)");
    return ESP_OK;
  }

  // Tydelig afvisning af en for stor fil FØR noget røres (Update.begin()'s
  // egen fejl er blot "Bad Size Given") — typisk en forkert fil, fx PLC'ens
  // egen, større firmware.
  const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
  if (target != nullptr && content_len > target->size) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Filen er for stor til OTA-partitionen (%u > %u bytes) - er det den rigtige firmware?",
             static_cast<unsigned>(content_len), static_cast<unsigned>(target->size));
    send_json_error(req, "413 Payload Too Large", -1, "ota_too_large", msg);
    return ESP_OK;
  }

  // Valgfri end-to-end-integritet: MD5 af hele .bin-filen, beregnet af
  // afsenderen (PLC'en). Verificeres af Update.end() mod det skrevne image.
  char md5[40] = {0};
  const size_t md5_len = httpd_req_get_hdr_value_len(req, "X-Firmware-MD5");
  const bool has_md5 = md5_len > 0;
  if (has_md5) {
    if (md5_len >= sizeof(md5) ||
        httpd_req_get_hdr_value_str(req, "X-Firmware-MD5", md5, sizeof(md5)) != ESP_OK ||
        !mb_ota_is_valid_md5_hex(md5)) {
      send_json_error(req, "400 Bad Request", -1, "bad_md5", "X-Firmware-MD5 skal vaere praecis 32 hex-tegn");
      return ESP_OK;
    }
  }

  char client_ip[20];
  peer_ip(req, client_ip, sizeof(client_ip));

  g_ota_in_progress = true;
  g_ota_status.state = OtaState::kInProgress;
  g_ota_status.received = 0;
  g_ota_status.total = content_len;
  g_ota_status.error_message[0] = '\0';
  g_ota_status.new_version[0] = '\0';
  syslog_logf(MB_SYSLOG_FACILITY_SYSTEM, 2, "OTA: upload startet fra %s (%u bytes, md5 %s)", client_ip,
              static_cast<unsigned>(content_len), has_md5 ? "angivet" : "ikke angivet");

  if (!Update.begin(content_len)) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Update.begin() fejlede (passer firmwaren i den ledige OTA-partition?): %s",
             Update.errorString());
    set_failed(msg);
    g_ota_in_progress = false;
    send_json_error(req, "400 Bad Request", -1, "ota_begin_failed", msg);
    return ESP_OK;
  }
  if (has_md5 && !Update.setMD5(md5)) {
    Update.abort();
    set_failed("X-Firmware-MD5 kunne ikke anvendes");
    g_ota_in_progress = false;
    send_json_error(req, "400 Bad Request", -1, "bad_md5", g_ota_status.error_message);
    return ESP_OK;
  }

  struct timeval recv_timeout = {.tv_sec = kOtaSocketTimeoutS, .tv_usec = 0};
  setsockopt(httpd_req_to_sockfd(req), SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));

  // static: 2048 bytes paa httpd-taskens stak er unoedvendigt (BUGS.md
  // v0.24.0-lektionen) - handleren koerer aldrig samtidigt med sig selv
  // (g_ota_in_progress-laasen ovenfor + httpd's ene worker-task).
  static uint8_t chunk[kOtaChunkSize];
  static mb_fwid_scanner_t scanner;
  mb_fwid_scanner_init(&scanner);
  size_t received_total = 0;
  bool first_chunk = true;
  bool ok = true;
  const char *fail_error = "ota_failed";
  const char *fail_status = "500 Internal Server Error";

  while (received_total < content_len) {
    const size_t remaining = content_len - received_total;
    const size_t to_read = remaining < kOtaChunkSize ? remaining : kOtaChunkSize;
    const int received = httpd_req_recv(req, reinterpret_cast<char *>(chunk), to_read);
    if (received <= 0) {
      if (received == HTTPD_SOCK_ERR_TIMEOUT) {
        continue;  // socket-timeout paa ét enkelt recv-kald - proev igen, ikke en fatal fejl i sig selv
      }
      char msg[128];
      snprintf(msg, sizeof(msg), "Modtagelsesfejl ved %u/%u bytes", static_cast<unsigned>(received_total),
               static_cast<unsigned>(content_len));
      set_failed(msg);
      ok = false;
      break;
    }

    if (first_chunk) {
      first_chunk = false;
      if (!mb_ota_is_valid_firmware_magic(chunk, static_cast<size_t>(received))) {
        set_failed("Ugyldig firmware - forkert magic byte (forventede 0xE9 foerst i filen)");
        fail_error = "ota_invalid_image";
        fail_status = "400 Bad Request";
        ok = false;
        break;
      }
    }

    mb_fwid_scanner_feed(&scanner, chunk, static_cast<size_t>(received));

    if (Update.write(chunk, static_cast<size_t>(received)) != static_cast<size_t>(received)) {
      char msg[128];
      snprintf(msg, sizeof(msg), "Flash-skrivning fejlede ved %u/%u bytes: %s", static_cast<unsigned>(received_total),
               static_cast<unsigned>(content_len), Update.errorString());
      set_failed(msg);
      ok = false;
      break;
    }

    received_total += static_cast<size_t>(received);
    g_ota_status.received = received_total;
  }

  if (ok && !scanner.found) {
    // Gyldig ESP32-firmware, men ikke DETTE boards (fx PLC'ens egen .bin) -
    // den ville aldrig komme paa nettet igen efter en reboot.
    set_failed("Forkert firmware - ikke en HypervisionPLC Extension board-firmware (identitets-markoer mangler)");
    fail_error = "ota_wrong_firmware";
    fail_status = "400 Bad Request";
    ok = false;
  }

  if (!ok) {
    Update.abort();
    g_ota_in_progress = false;
    send_json_error(req, fail_status, -1, fail_error, g_ota_status.error_message);
    return ESP_OK;
  }

  if (!Update.end(true)) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Firmware-verifikation fejlede: %s", Update.errorString());
    set_failed(msg);
    g_ota_in_progress = false;
    send_json_error(req, "500 Internal Server Error", -1, "ota_verify_failed", msg);
    return ESP_OK;
  }

  strncpy(g_ota_status.new_version, scanner.version, sizeof(g_ota_status.new_version) - 1);
  g_ota_status.state = OtaState::kSuccess;
  g_ota_in_progress = false;
  syslog_logf(MB_SYSLOG_FACILITY_SYSTEM, 2, "OTA: firmware %s uploadet og verificeret (%u bytes) - afventer reboot",
              g_ota_status.new_version, static_cast<unsigned>(received_total));

  char resp[256];
  const int written = snprintf(
      resp, sizeof(resp),
      "{\"ok\":true,\"message\":\"Firmware uploadet og verificeret - kald POST /api/reboot for at aktivere\","
      "\"bytes\":%u,\"new_version\":\"%s\",\"md5_verified\":%s}",
      static_cast<unsigned>(received_total), g_ota_status.new_version, has_md5 ? "true" : "false");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, resp, written > 0 ? written : HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

esp_err_t ota_status_handler(httpd_req_t *req) {
  if (!require_auth(req)) return ESP_OK;

  uint32_t remaining_s = 0;
  const bool pending = ota_manager_pending_confirm(&remaining_s);
  const mb_ota_status_data_t data = {
      state_name(g_ota_status.state),
      static_cast<uint32_t>(g_ota_status.received),
      static_cast<uint32_t>(g_ota_status.total),
      g_ota_status.error_message,
      ota_manager_running_version(),
      g_ota_status.new_version,
      pending,
      remaining_s,
      ota_manager_last_update_rolled_back(),
  };

  char resp[512];
  const size_t len = mb_ota_build_status_json(&data, resp, sizeof(resp));
  if (len == 0) {
    send_json_error(req, "500 Internal Server Error", -1, "internal", "Kunne ikke bygge OTA-status");
    return ESP_OK;
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, resp, len);
  return ESP_OK;
}

// POST /api/ota/confirm (v0.30.0) — PLC'en kalder dette EFTER at have
// verificeret at den nye firmware kører korrekt (se PLC_OTA_INTEGRATION_PLAN.md).
// Idempotent: et kald uden noget at bekræfte svarer også 200, så PLC'en
// altid trygt kan kalde det efter en opdatering.
esp_err_t ota_confirm_handler(httpd_req_t *req) {
  if (!require_auth(req)) return ESP_OK;

  char resp[192];
  int written = 0;
  switch (ota_manager_confirm()) {
    case OtaConfirmResult::kConfirmed:
      written = snprintf(resp, sizeof(resp),
                         "{\"ok\":true,\"confirmed\":true,\"running_version\":\"%s\","
                         "\"message\":\"Firmware bekraeftet - rollback annulleret\"}",
                         ota_manager_running_version());
      break;
    case OtaConfirmResult::kNothingPending:
      written = snprintf(resp, sizeof(resp),
                         "{\"ok\":true,\"confirmed\":false,\"running_version\":\"%s\","
                         "\"message\":\"Intet at bekraefte - firmwaren er allerede bekraeftet\"}",
                         ota_manager_running_version());
      break;
    case OtaConfirmResult::kFailed:
    default:
      send_json_error(req, "500 Internal Server Error", -1, "ota_confirm_failed",
                      "Bekraeftelse fejlede - firmwaren afventer stadig bekraeftelse, proev igen");
      return ESP_OK;
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, resp, written > 0 ? written : HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

void reboot_task(void *param) {
  (void)param;
  vTaskDelay(pdMS_TO_TICKS(500));  // lad HTTP-svaret naa at blive sendt foerst
  ESP.restart();
}

// POST /api/reboot — blødt, kontrolleret reboot. Ikke kun til OTA-aktivering
// (§4.2's tabel lister den som et generelt endpoint) — men det ER den
// eneste måde en uploadet, verificeret firmware (ovenfor) rent faktisk
// bliver aktiveret.
esp_err_t reboot_handler(httpd_req_t *req) {
  if (!require_auth(req)) return ESP_OK;

  char client_ip[20];
  peer_ip(req, client_ip, sizeof(client_ip));
  syslog_logf(MB_SYSLOG_FACILITY_SYSTEM, 2, "Reboot anmodet via REST fra %s", client_ip);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, "{\"ok\":true,\"message\":\"Genstarter...\"}", HTTPD_RESP_USE_STRLEN);

  xTaskCreate(reboot_task, "reboot", 2048, nullptr, 5, nullptr);
  return ESP_OK;
}

}  // namespace

void ota_handler_register(httpd_handle_t server) {
  const httpd_uri_t ota_upload_uri = {
      .uri = "/api/ota",
      .method = HTTP_POST,
      .handler = ota_upload_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &ota_upload_uri);

  const httpd_uri_t ota_status_uri = {
      .uri = "/api/ota/status",
      .method = HTTP_GET,
      .handler = ota_status_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &ota_status_uri);

  const httpd_uri_t ota_confirm_uri = {
      .uri = "/api/ota/confirm",
      .method = HTTP_POST,
      .handler = ota_confirm_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &ota_confirm_uri);

  const httpd_uri_t reboot_uri = {
      .uri = "/api/reboot",
      .method = HTTP_POST,
      .handler = reboot_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &reboot_uri);
}
