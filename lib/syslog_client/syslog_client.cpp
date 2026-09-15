#include "syslog_client.h"

#include <cstdio>

uint8_t mb_syslog_severity_from_level(uint8_t level) {
  if (level < 1) level = 1;
  if (level > 8) level = 8;
  return static_cast<uint8_t>(level - 1);
}

size_t mb_syslog_build_packet(mb_syslog_facility_t facility, uint8_t level, const char *tag, const char *hostname,
                               uint32_t uptime_s, const char *message, char *out_buf, size_t out_capacity) {
  if (tag == nullptr || hostname == nullptr || message == nullptr || out_buf == nullptr || out_capacity == 0) {
    return 0;
  }

  const uint8_t severity = mb_syslog_severity_from_level(level);
  const unsigned pri = static_cast<unsigned>(facility) * 8U + severity;

  const uint32_t hh = (uptime_s / 3600U) % 24U;
  const uint32_t mm = (uptime_s / 60U) % 60U;
  const uint32_t ss = uptime_s % 60U;

  const int written = snprintf(out_buf, out_capacity, "<%u>Jan  1 %02u:%02u:%02u %s %s: %s", pri,
                                static_cast<unsigned>(hh), static_cast<unsigned>(mm), static_cast<unsigned>(ss),
                                hostname, tag, message);
  if (written <= 0) {
    return 0;
  }
  const size_t written_len = static_cast<size_t>(written);
  return written_len < out_capacity ? written_len : out_capacity - 1;
}
