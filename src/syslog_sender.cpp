#include "syslog_sender.h"

#include <Arduino.h>
#include <lwip/sockets.h>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "board_config.h"
#include "config.h"

// BUGS.md v0.30.1: syslog sendes ikke længere direkte fra den kaldende task
// (fx en Modbus-kanal-task midt i en transaktion) via Arduino's WiFiUDP.
// Kaldstedet lægger blot beskeden i en kø (aldrig blokerende); en egen,
// lav-prioritets task sender via en egen lwIP-socket, med genforsøg ved
// ENOMEM/EAGAIN. Fejl tælles (syslog_sender_get_stats(), vist i CLI'ens
// "status") i stedet for at give Arduino-corens ubetingede
// "[E][WiFiUdp.cpp] could not send data"-linje på konsollen.

namespace {

constexpr size_t kQueueDepth = 32;
constexpr size_t kMessageMaxLen = 159;  // = syslog_logf()s egen buffer; holder kanal-taskens stakforbrug nede
constexpr int kSendAttempts = 3;
constexpr TickType_t kRetryDelay = pdMS_TO_TICKS(10);

struct SyslogItem {
  uint8_t facility;
  uint8_t level;
  uint32_t uptime_s;
  char message[kMessageMaxLen + 1];
};

mb_syslog_target_t g_targets[MB_SYSLOG_MAX_TARGETS];
char g_hostname[MB_PROV_HOSTNAME_MAX_LEN + 1] = "";

// Beskytter g_targets/g_hostname mod en samtidig syslog_sender_refresh()
// (CLI-tasken) mens sender-tasken læser dem.
SemaphoreHandle_t g_config_mutex = nullptr;
QueueHandle_t g_queue = nullptr;
int g_sock = -1;

volatile uint32_t g_sent = 0;
volatile uint32_t g_failed = 0;
volatile uint32_t g_queue_dropped = 0;
volatile int g_last_errno = 0;

bool any_target_wants(uint8_t level) {
  for (size_t i = 0; i < MB_SYSLOG_MAX_TARGETS; i++) {
    if (g_targets[i].in_use && g_targets[i].max_level >= level) return true;
  }
  return false;
}

// Sender én pakke; genforsøger kun ved midlertidig ressourcemangel.
bool send_packet(const char *ip_str, uint16_t port, const char *packet, size_t len) {
  if (g_sock < 0) {
    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sock < 0) {
      g_last_errno = errno;
      return false;
    }
  }

  struct sockaddr_in to;
  memset(&to, 0, sizeof(to));
  to.sin_family = AF_INET;
  to.sin_port = htons(port);
  if (inet_aton(ip_str, &to.sin_addr) == 0) {
    g_last_errno = EINVAL;
    return false;
  }

  for (int attempt = 0; attempt < kSendAttempts; attempt++) {
    if (sendto(g_sock, packet, len, 0, reinterpret_cast<struct sockaddr *>(&to), sizeof(to)) >= 0) {
      return true;
    }
    g_last_errno = errno;
    if (errno != ENOMEM && errno != EAGAIN && errno != EWOULDBLOCK) break;
    vTaskDelay(kRetryDelay);
  }
  return false;
}

void sender_task(void *param) {
  (void)param;
  static SyslogItem item;
  static char packet[MB_SYSLOG_PACKET_MAX_LEN];

  for (;;) {
    if (xQueueReceive(g_queue, &item, portMAX_DELAY) != pdTRUE) continue;

    mb_syslog_target_t targets[MB_SYSLOG_MAX_TARGETS];
    char hostname[sizeof(g_hostname)];
    xSemaphoreTake(g_config_mutex, portMAX_DELAY);
    memcpy(targets, g_targets, sizeof(targets));
    memcpy(hostname, g_hostname, sizeof(hostname));
    xSemaphoreGive(g_config_mutex);

    for (size_t i = 0; i < MB_SYSLOG_MAX_TARGETS; i++) {
      if (!targets[i].in_use || targets[i].max_level < item.level) continue;
      const size_t len = mb_syslog_build_packet(static_cast<mb_syslog_facility_t>(item.facility), item.level,
                                                targets[i].tag, hostname, item.uptime_s, item.message, packet,
                                                sizeof(packet));
      if (len == 0) continue;
      if (send_packet(targets[i].ip, targets[i].port, packet, len)) {
        g_sent++;
      } else {
        g_failed++;
      }
    }
  }
}

}  // namespace

void syslog_sender_begin() {
  if (g_config_mutex == nullptr) {
    g_config_mutex = xSemaphoreCreateMutex();
  }
  if (g_queue == nullptr) {
    g_queue = xQueueCreate(kQueueDepth, sizeof(SyslogItem));
    if (g_queue != nullptr) {
      xTaskCreate(sender_task, "syslog_tx", 4096, nullptr, 1, nullptr);
    }
  }
  syslog_sender_refresh();
}

void syslog_sender_refresh() {
  if (g_config_mutex == nullptr) return;
  const mb_board_config_t &cfg = config_get();
  xSemaphoreTake(g_config_mutex, portMAX_DELAY);
  memcpy(g_targets, cfg.syslog_targets, sizeof(g_targets));
  mb_config_build_hostname(cfg.has_hostname, cfg.hostname, cfg.eth_mac, g_hostname, sizeof(g_hostname));
  xSemaphoreGive(g_config_mutex);
}

void syslog_log(mb_syslog_facility_t facility, uint8_t level, const char *message) {
  if (message == nullptr || g_queue == nullptr) return;

  // Hurtig, billig bail-out: ingen modtager vil have denne besked (langt det
  // almindelige tilfælde). Læses uden lås — en samtidig refresh giver i
  // værste fald én besked for meget eller for lidt, aldrig et nedbrud.
  if (!any_target_wants(level)) return;

  SyslogItem item;
  item.facility = static_cast<uint8_t>(facility);
  item.level = level;
  item.uptime_s = millis() / 1000;
  strncpy(item.message, message, kMessageMaxLen);
  item.message[kMessageMaxLen] = '\0';

  // Timeout 0: den kaldende task (fx en Modbus-kanal) må ALDRIG vente på syslog.
  if (xQueueSend(g_queue, &item, 0) != pdTRUE) {
    g_queue_dropped++;
  }
}

void syslog_logf(mb_syslog_facility_t facility, uint8_t level, const char *fmt, ...) {
  // Lille, begrænset lokal buffer (IKKE en af de store buffere §BUGS.md
  // advarer om) — sikkert selv på channel_task()'s 4096-byte stak.
  char message[160];
  va_list args;
  va_start(args, fmt);
  vsnprintf(message, sizeof(message), fmt, args);
  va_end(args);
  syslog_log(facility, level, message);
}

void syslog_sender_get_stats(syslog_sender_stats_t *out) {
  if (out == nullptr) return;
  out->sent = g_sent;
  out->failed = g_failed;
  out->queue_dropped = g_queue_dropped;
  out->last_errno = g_last_errno;
}
