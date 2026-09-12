#include "channel_config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

bool mb_is_valid_baudrate(uint32_t baud) {
  switch (baud) {
    case 1200:
    case 2400:
    case 4800:
    case 9600:
    case 19200:
    case 38400:
    case 57600:
    case 115200:
      return true;
    default:
      return false;
  }
}

namespace {

const char *mode_to_string(mb_channel_mode_t mode) { return mode == MB_CHANNEL_MODE_RS232 ? "rs232" : "rs485"; }

const char *parity_to_string(mb_channel_parity_t parity) {
  switch (parity) {
    case MB_CHANNEL_PARITY_EVEN:
      return "even";
    case MB_CHANNEL_PARITY_ODD:
      return "odd";
    case MB_CHANNEL_PARITY_NONE:
    default:
      return "none";
  }
}

bool string_to_mode(const char *s, mb_channel_mode_t *out) {
  if (strcmp(s, "rs485") == 0) {
    *out = MB_CHANNEL_MODE_RS485;
    return true;
  }
  if (strcmp(s, "rs232") == 0) {
    *out = MB_CHANNEL_MODE_RS232;
    return true;
  }
  return false;
}

bool string_to_parity(const char *s, mb_channel_parity_t *out) {
  if (strcmp(s, "none") == 0) {
    *out = MB_CHANNEL_PARITY_NONE;
    return true;
  }
  if (strcmp(s, "even") == 0) {
    *out = MB_CHANNEL_PARITY_EVEN;
    return true;
  }
  if (strcmp(s, "odd") == 0) {
    *out = MB_CHANNEL_PARITY_ODD;
    return true;
  }
  return false;
}

// Minimal, bevidst ikke-generisk JSON-feltudtræk — §4.2's PUT-body har et
// FAST, kendt sæt felter (ingen indlejrede objekter/arrays), så en fuld
// rekursiv JSON-parser ville være overkill (jf. CLAUDE.md: undgå
// abstraktioner ud over hvad opgaven kræver). Finder `"key"` et vilkårligt
// sted i strengen, springer til efter det følgende `:`, og springer
// whitespace over — ingen håndtering af key'er der optræder som substrenge
// af værdier, hvilket er en acceptabel begrænsning for dette lille,
// kontrollerede skema.
const char *find_value_start(const char *json, const char *key) {
  char needle[40];
  snprintf(needle, sizeof(needle), "\"%s\"", key);
  const char *pos = strstr(json, needle);
  if (pos == nullptr) {
    return nullptr;
  }
  pos = strchr(pos + strlen(needle), ':');
  if (pos == nullptr) {
    return nullptr;
  }
  pos++;
  while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') {
    pos++;
  }
  return pos;
}

bool parse_bool_field(const char *json, const char *key, bool *out) {
  const char *v = find_value_start(json, key);
  if (v == nullptr) return false;
  if (strncmp(v, "true", 4) == 0) {
    *out = true;
    return true;
  }
  if (strncmp(v, "false", 5) == 0) {
    *out = false;
    return true;
  }
  return false;
}

bool parse_uint_field(const char *json, const char *key, uint32_t *out) {
  const char *v = find_value_start(json, key);
  if (v == nullptr || (*v < '0' || *v > '9')) return false;
  char *end = nullptr;
  const unsigned long value = strtoul(v, &end, 10);
  if (end == v) return false;
  *out = static_cast<uint32_t>(value);
  return true;
}

bool parse_string_field(const char *json, const char *key, char *out, size_t out_capacity) {
  const char *v = find_value_start(json, key);
  if (v == nullptr || *v != '"') return false;
  v++;
  const char *end = strchr(v, '"');
  if (end == nullptr) return false;
  const size_t len = static_cast<size_t>(end - v);
  if (len >= out_capacity) return false;
  memcpy(out, v, len);
  out[len] = '\0';
  return true;
}

}  // namespace

size_t mb_channel_build_json(int channel_number, const mb_channel_config_t *config, const mb_channel_stats_t *stats,
                              char *out, size_t out_capacity) {
  const char *status = !config->enabled ? "disabled" : stats->has_last_error ? "error" : "ok";

  int written = snprintf(
      out, out_capacity,
      "{"
      "\"channel\":%d,"
      "\"enabled\":%s,"
      "\"mode\":\"%s\","
      "\"baudrate\":%lu,"
      "\"parity\":\"%s\","
      "\"stop_bits\":%u,"
      "\"timeout_ms\":%lu,"
      "\"inter_frame_delay_ms\":%lu,"
      "\"status\":\"%s\","
      "\"total_requests\":%lu,"
      "\"successful_requests\":%lu,"
      "\"timeout_errors\":%lu,"
      "\"crc_errors\":%lu,"
      "\"exception_errors\":%lu,"
      "\"last_error_slave_id\":%u,"
      "\"last_error_address\":%u,"
      "\"last_error_type\":%u,"
      "\"last_error_at_uptime_s\":%lu"
      "}",
      channel_number, config->enabled ? "true" : "false", mode_to_string(config->mode),
      static_cast<unsigned long>(config->baudrate), parity_to_string(config->parity),
      static_cast<unsigned>(config->stop_bits), static_cast<unsigned long>(config->timeout_ms),
      static_cast<unsigned long>(config->inter_frame_delay_ms), status,
      static_cast<unsigned long>(stats->total_requests), static_cast<unsigned long>(stats->successful_requests),
      static_cast<unsigned long>(stats->timeout_errors), static_cast<unsigned long>(stats->crc_errors),
      static_cast<unsigned long>(stats->exception_errors), static_cast<unsigned>(stats->last_error_slave_id),
      static_cast<unsigned>(stats->last_error_address), static_cast<unsigned>(stats->last_error_type),
      static_cast<unsigned long>(stats->last_error_at_uptime_s));

  if (written <= 0 || static_cast<size_t>(written) >= out_capacity) {
    return 0;
  }
  return static_cast<size_t>(written);
}

bool mb_channel_parse_config_json(const char *json, size_t len, mb_channel_config_t *out_config) {
  (void)len;  // json er null-termineret (fra HTTP-body-bufferen) - strstr/strchr er derfor trygge at bruge direkte

  mb_channel_config_t parsed{};

  if (!parse_bool_field(json, "enabled", &parsed.enabled)) return false;

  char mode_str[8];
  if (!parse_string_field(json, "mode", mode_str, sizeof(mode_str))) return false;
  if (!string_to_mode(mode_str, &parsed.mode)) return false;

  if (!parse_uint_field(json, "baudrate", &parsed.baudrate)) return false;
  if (!mb_is_valid_baudrate(parsed.baudrate)) return false;

  char parity_str[8];
  if (!parse_string_field(json, "parity", parity_str, sizeof(parity_str))) return false;
  if (!string_to_parity(parity_str, &parsed.parity)) return false;

  uint32_t stop_bits = 0;
  if (!parse_uint_field(json, "stop_bits", &stop_bits)) return false;
  if (stop_bits != 1 && stop_bits != 2) return false;
  parsed.stop_bits = static_cast<uint8_t>(stop_bits);

  if (!parse_uint_field(json, "timeout_ms", &parsed.timeout_ms)) return false;
  if (parsed.timeout_ms == 0) return false;

  if (!parse_uint_field(json, "inter_frame_delay_ms", &parsed.inter_frame_delay_ms)) return false;

  *out_config = parsed;
  return true;
}
