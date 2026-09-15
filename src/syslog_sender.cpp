#include "syslog_sender.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "board_config.h"
#include "config.h"

namespace {

WiFiUDP g_udp;
mb_syslog_target_t g_targets[MB_SYSLOG_MAX_TARGETS];
char g_hostname[MB_PROV_HOSTNAME_MAX_LEN + 1] = "";

// g_send_mutex serialiserer selve pakke-byg+afsend (én delt, `static`
// pakke-buffer, se syslog_log() nedenfor) — nødvendigt fordi denne funktion
// kaldes samtidigt fra FLERE forskellige FreeRTOS-tasks (begge
// modbus_channel.cpp-kanal-tasks, potentielt senere REST-httpd-workere),
// modsat provisioning_poll()'s `static`-brug (BUGS.md v0.24.0), som er
// sikker netop fordi DEN kun kører på én task.
SemaphoreHandle_t g_send_mutex = nullptr;

}  // namespace

void syslog_sender_begin() {
  if (g_send_mutex == nullptr) {
    g_send_mutex = xSemaphoreCreateMutex();
  }
  syslog_sender_refresh();
}

void syslog_sender_refresh() {
  const mb_board_config_t &cfg = config_get();
  memcpy(g_targets, cfg.syslog_targets, sizeof(g_targets));
  mb_config_build_hostname(cfg.has_hostname, cfg.hostname, cfg.eth_mac, g_hostname, sizeof(g_hostname));
}

void syslog_log(mb_syslog_facility_t facility, uint8_t level, const char *message) {
  if (message == nullptr || g_send_mutex == nullptr) return;

  // Hurtig, billig bail-out FØR mutex/pakke-bygning: ingen konfigureret
  // modtager der overhovedet vil have denne besked (langt det almindelige
  // tilfælde, når syslog slet ikke er sat op) — undgår unødig ventetid/lås
  // i den KRITISK tidsbegrænsede sti (fx execute_transaction(), se
  // src/modbus_channel.cpp) selv når syslog er helt utilgængeligt.
  bool any_target = false;
  for (size_t i = 0; i < MB_SYSLOG_MAX_TARGETS; i++) {
    if (g_targets[i].in_use && g_targets[i].max_level >= level) {
      any_target = true;
      break;
    }
  }
  if (!any_target) return;

  // Kort timeout (ikke portMAX_DELAY) — et syslog-problem må ALDRIG kunne
  // blokere den kaldende task på ubestemt tid.
  if (xSemaphoreTake(g_send_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

  static char packet[MB_SYSLOG_PACKET_MAX_LEN];
  const uint32_t uptime_s = millis() / 1000;

  for (size_t i = 0; i < MB_SYSLOG_MAX_TARGETS; i++) {
    if (!g_targets[i].in_use || g_targets[i].max_level < level) continue;

    IPAddress ip;
    if (!ip.fromString(g_targets[i].ip)) continue;

    const size_t len =
        mb_syslog_build_packet(facility, level, g_targets[i].tag, g_hostname, uptime_s, message, packet, sizeof(packet));
    if (len == 0) continue;

    if (g_udp.beginPacket(ip, g_targets[i].port)) {
      g_udp.write(reinterpret_cast<const uint8_t *>(packet), len);
      g_udp.endPacket();
    }
  }

  xSemaphoreGive(g_send_mutex);
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
