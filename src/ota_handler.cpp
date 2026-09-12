#include "ota_handler.h"

#include <Arduino.h>
#include <Update.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

#include <cstdio>
#include <cstring>

#include "http_helpers.h"
#include "ota_validation.h"

namespace {

enum class OtaState : uint8_t { kIdle, kInProgress, kSuccess, kFailed };

struct OtaStatus {
  OtaState state = OtaState::kIdle;
  size_t received = 0;
  size_t total = 0;
  char error_message[128] = {0};
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
}

// POST /api/ota — rå binær body. Skriver til den inaktive OTA-partition via
// Arduino-corets Update-bibliotek (wrapper omkring esp_ota_ops, inkl.
// checksum-verifikation i Update.end(true)). Ved succes er den nye firmware
// sat som boot-partition, men boardet genstarter IKKE af sig selv — kræver
// et eksplicit POST /api/reboot (§4.2's designbeslutning: en operatør skal
// bevidst aktivere en ny firmware, ikke overraskes af en automatisk reboot).
esp_err_t ota_upload_handler(httpd_req_t *req) {
  if (!require_auth(req)) return ESP_OK;

  if (g_ota_in_progress) {
    send_json_error(req, "409 Conflict", -1, "ota_in_progress", "En anden OTA-upload er allerede i gang");
    return ESP_OK;
  }

  const size_t content_len = req->content_len;
  if (content_len == 0) {
    send_json_error(req, "400 Bad Request", -1, "bad_request", "Tom request-body");
    return ESP_OK;
  }

  g_ota_in_progress = true;
  g_ota_status.state = OtaState::kInProgress;
  g_ota_status.received = 0;
  g_ota_status.total = content_len;
  g_ota_status.error_message[0] = '\0';

  if (!Update.begin(content_len)) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Update.begin() fejlede (passer firmwaren i den ledige OTA-partition?): %s",
             Update.errorString());
    set_failed(msg);
    g_ota_in_progress = false;
    send_json_error(req, "400 Bad Request", -1, "ota_begin_failed", msg);
    return ESP_OK;
  }

  struct timeval recv_timeout = {.tv_sec = kOtaSocketTimeoutS, .tv_usec = 0};
  setsockopt(httpd_req_to_sockfd(req), SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));

  uint8_t chunk[kOtaChunkSize];
  size_t received_total = 0;
  bool first_chunk = true;
  bool ok = true;

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
        ok = false;
        break;
      }
    }

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

  if (!ok) {
    Update.abort();
    g_ota_in_progress = false;
    send_json_error(req, "500 Internal Server Error", -1, "ota_failed", g_ota_status.error_message);
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

  g_ota_status.state = OtaState::kSuccess;
  g_ota_in_progress = false;

  char resp[192];
  const int written = snprintf(
      resp, sizeof(resp),
      "{\"ok\":true,\"message\":\"Firmware uploadet og verificeret - kald POST /api/reboot for at aktivere\",\"bytes\":%u}",
      static_cast<unsigned>(received_total));
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, resp, written > 0 ? written : HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

esp_err_t ota_status_handler(httpd_req_t *req) {
  if (!require_auth(req)) return ESP_OK;

  unsigned percent = 0;
  if (g_ota_status.total > 0) {
    percent = static_cast<unsigned>((static_cast<uint64_t>(g_ota_status.received) * 100) / g_ota_status.total);
  }

  char resp[256];
  const int written =
      snprintf(resp, sizeof(resp), "{\"state\":\"%s\",\"received\":%u,\"total\":%u,\"percent\":%u,\"error\":\"%s\"}",
               state_name(g_ota_status.state), static_cast<unsigned>(g_ota_status.received),
               static_cast<unsigned>(g_ota_status.total), percent, g_ota_status.error_message);
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

  const httpd_uri_t reboot_uri = {
      .uri = "/api/reboot",
      .method = HTTP_POST,
      .handler = reboot_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &reboot_uri);
}
