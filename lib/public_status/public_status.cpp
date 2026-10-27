#include "public_status.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {

// Skriver `value` JSON-escapet (", \ og kontroltegn) ind i buffer[pos..].
// Returnerer ny pos, eller (size_t)-1 ved overløb.
size_t append_escaped(char *buf, size_t cap, size_t pos, const char *value) {
  if (value == nullptr) value = "";
  for (const char *p = value; *p != '\0'; p++) {
    const unsigned char c = static_cast<unsigned char>(*p);
    const bool needs_escape = c == '"' || c == '\\';
    if (pos + (needs_escape ? 2 : 1) + 1 > cap) return static_cast<size_t>(-1);
    if (needs_escape) {
      buf[pos++] = '\\';
      buf[pos++] = static_cast<char>(c);
    } else if (c < 0x20 || c == 0x7F) {
      buf[pos++] = ' ';
    } else {
      buf[pos++] = static_cast<char>(c);
    }
  }
  buf[pos] = '\0';
  return pos;
}

// Skriver printf-formateret tekst; ved overløb returneres (size_t)-1.
size_t append_fmt(char *buf, size_t cap, size_t pos, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
size_t append_fmt(char *buf, size_t cap, size_t pos, const char *fmt, ...) {
  if (pos >= cap) return static_cast<size_t>(-1);
  va_list args;
  va_start(args, fmt);
  const int n = vsnprintf(buf + pos, cap - pos, fmt, args);
  va_end(args);
  if (n < 0 || static_cast<size_t>(n) >= cap - pos) return static_cast<size_t>(-1);
  return pos + static_cast<size_t>(n);
}

const char *mode_name(mb_channel_mode_t mode) { return mode == MB_CHANNEL_MODE_RS232 ? "rs232" : "rs485"; }

const char *parity_name(mb_channel_parity_t parity) {
  switch (parity) {
    case MB_CHANNEL_PARITY_EVEN: return "even";
    case MB_CHANNEL_PARITY_ODD: return "odd";
    case MB_CHANNEL_PARITY_NONE:
    default: return "none";
  }
}

#define CHECK_POS(p)                 \
  do {                               \
    if ((p) == static_cast<size_t>(-1)) goto overflow; \
  } while (0)

}  // namespace

size_t mb_public_status_build_json(const mb_public_status_t *d, char *out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  if (d == nullptr || d->channel_count == 0 || d->channel_count > MB_PUBLIC_STATUS_MAX_CHANNELS) return 0;

  size_t pos = 0;

  pos = append_fmt(out, cap, pos, "{\"product\":\"HypervisionPLC Extension Board\",\"hostname\":\"");
  CHECK_POS(pos);
  pos = append_escaped(out, cap, pos, d->hostname);
  CHECK_POS(pos);
  pos = append_fmt(out, cap, pos, "\",\"fw_version\":\"");
  CHECK_POS(pos);
  pos = append_escaped(out, cap, pos, d->fw_version);
  CHECK_POS(pos);
  pos = append_fmt(out, cap, pos, "\",\"fw_build\":\"");
  CHECK_POS(pos);
  pos = append_escaped(out, cap, pos, d->fw_build);
  CHECK_POS(pos);
  pos = append_fmt(out, cap, pos, "\",\"uptime_s\":%lu,\"heap_free_bytes\":%lu,\"reset_reason\":\"",
                   static_cast<unsigned long>(d->uptime_s), static_cast<unsigned long>(d->heap_free_bytes));
  CHECK_POS(pos);
  pos = append_escaped(out, cap, pos, d->reset_reason != nullptr ? d->reset_reason : "unknown");
  CHECK_POS(pos);
  pos = append_fmt(out, cap, pos, "\",\"board_type\":\"%ux%s\",\"active_channels\":%u,\"board_mode\":\"%s\",\"expander\":\"",
                   static_cast<unsigned>(d->channel_count), d->board_mode == MB_CHANNEL_MODE_RS232 ? "RS232" : "RS485",
                   static_cast<unsigned>(d->channel_count), mode_name(d->board_mode));
  CHECK_POS(pos);
  pos = append_escaped(out, cap, pos, d->expander != nullptr ? d->expander : "not_fitted");
  CHECK_POS(pos);
  pos = append_fmt(out, cap, pos, "\",\"wifi_connected\":%s,\"ethernet\":\"", d->wifi_connected ? "true" : "false");
  CHECK_POS(pos);
  pos = append_escaped(out, cap, pos, d->eth_status != nullptr ? d->eth_status : "not_detected");
  CHECK_POS(pos);
  pos = append_fmt(out, cap, pos, "\",\"ota_pending_confirm\":%s,\"ota_last_update_rolled_back\":%s,\"channels\":[",
                   d->ota_pending_confirm ? "true" : "false", d->ota_last_update_rolled_back ? "true" : "false");
  CHECK_POS(pos);

  for (size_t i = 0; i < d->channel_count; i++) {
    const mb_public_channel_t &c = d->channels[i];
    pos = append_fmt(out, cap, pos, "%s{\"channel\":\"%c\",\"status\":\"", i > 0 ? "," : "",
                     (c.letter >= 'A' && c.letter <= 'Z') ? c.letter : '?');
    CHECK_POS(pos);
    pos = append_escaped(out, cap, pos, c.status != nullptr ? c.status : "unavailable");
    CHECK_POS(pos);
    pos = append_fmt(out, cap, pos,
                     "\",\"mode\":\"%s\",\"baudrate\":%lu,\"parity\":\"%s\",\"stop_bits\":%u,\"total_requests\":%lu,"
                     "\"successful_requests\":%lu,\"timeout_errors\":%lu,\"crc_errors\":%lu,\"exception_errors\":%lu}",
                     mode_name(c.mode), static_cast<unsigned long>(c.baudrate), parity_name(c.parity),
                     static_cast<unsigned>(c.stop_bits), static_cast<unsigned long>(c.total_requests),
                     static_cast<unsigned long>(c.successful_requests), static_cast<unsigned long>(c.timeout_errors),
                     static_cast<unsigned long>(c.crc_errors), static_cast<unsigned long>(c.exception_errors));
    CHECK_POS(pos);
  }

  pos = append_fmt(out, cap, pos, "]}");
  CHECK_POS(pos);
  return pos;

overflow:
  out[0] = '\0';
  return 0;
}
