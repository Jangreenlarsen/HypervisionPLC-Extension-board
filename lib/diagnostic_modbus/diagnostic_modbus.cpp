#include "diagnostic_modbus.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// Samme minimale, bevidst ikke-generiske JSON-feltudtræk som
// lib/channel_config/channel_config.cpp — begge moduler har hvert sit
// lille, faste JSON-skema, så en delt generisk parser ville være en
// abstraktion uden reelt behov (jf. CLAUDE.md).
const char *find_value_start(const char *json, const char *key) {
  char needle[32];
  snprintf(needle, sizeof(needle), "\"%s\"", key);
  const char *pos = strstr(json, needle);
  if (pos == nullptr) return nullptr;
  pos = strchr(pos + strlen(needle), ':');
  if (pos == nullptr) return nullptr;
  pos++;
  while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') pos++;
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
  if (v == nullptr || *v < '0' || *v > '9') return false;
  char *end = nullptr;
  const unsigned long value = strtoul(v, &end, 10);
  if (end == v) return false;
  *out = static_cast<uint32_t>(value);
  return true;
}

// v0.27.1 — FC15's "values" er et array af BOOLEANS ([true,false,...]), IKKE
// tal — matcher PLC-udviklingsteamets forslag til kontrakten OG denne fils
// egen eksisterende konvention for coils (FC05's "value" er allerede en
// bool, se parse_bool_field()/FC05-grenen nedenfor). En tidligere udgave
// genbrugte parse_uint_array_field() (0/1-tal) for FC15 — inkonsistent med
// FC05 og ville have fejlet mod PLC-siden, hvis den implementerede sin egen
// foreslåede kontrakt.
bool parse_bool_array_field(const char *json, const char *key, uint16_t *out_values, uint16_t max_count,
                             uint16_t *out_count) {
  const char *v = find_value_start(json, key);
  if (v == nullptr || *v != '[') return false;
  v++;

  uint16_t count = 0;
  for (;;) {
    while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r') v++;
    if (*v == ']') {
      v++;
      break;
    }
    if (count >= max_count) return false;
    if (strncmp(v, "true", 4) == 0) {
      out_values[count++] = 1;
      v += 4;
    } else if (strncmp(v, "false", 5) == 0) {
      out_values[count++] = 0;
      v += 5;
    } else {
      return false;  // hverken "true" eller "false" — ikke et gyldigt boolsk element
    }
    while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r') v++;
    if (*v == ',') {
      v++;
      continue;
    }
    if (*v == ']') {
      v++;
      break;
    }
    return false;  // uventet tegn — hverken "," eller "]"
  }
  *out_count = count;
  return true;
}

bool parse_uint_array_field(const char *json, const char *key, uint16_t *out_values, uint16_t max_count,
                             uint16_t *out_count) {
  const char *v = find_value_start(json, key);
  if (v == nullptr || *v != '[') return false;
  v++;

  uint16_t count = 0;
  for (;;) {
    while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r') v++;
    if (*v == ']') {
      v++;
      break;
    }
    if (count >= max_count) return false;
    char *end = nullptr;
    const unsigned long value = strtoul(v, &end, 10);
    if (end == v || value > 0xFFFF) return false;
    out_values[count++] = static_cast<uint16_t>(value);
    v = end;
    while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r') v++;
    if (*v == ',') {
      v++;
      continue;
    }
    if (*v == ']') {
      v++;
      break;
    }
    return false;  // uventet tegn — hverken "," eller "]"
  }
  *out_count = count;
  return true;
}

}  // namespace

bool mb_diag_parse_read_request(const char *json, size_t len, mb_diag_read_request_t *out) {
  (void)len;  // json er null-termineret (fra HTTP-body-bufferen)

  uint32_t fc = 0, slave = 0, addr = 0, qty = 0;
  if (!parse_uint_field(json, "function_code", &fc)) return false;
  if (fc != 1 && fc != 2 && fc != 3 && fc != 4) return false;
  if (!parse_uint_field(json, "slave_id", &slave) || slave == 0 || slave > 247) return false;
  if (!parse_uint_field(json, "address", &addr) || addr > 0xFFFF) return false;
  if (!parse_uint_field(json, "quantity", &qty) || qty == 0 || qty > MB_DIAG_MAX_READ_QUANTITY) return false;

  out->function_code = static_cast<uint8_t>(fc);
  out->slave_id = static_cast<uint8_t>(slave);
  out->address = static_cast<uint16_t>(addr);
  out->quantity = static_cast<uint16_t>(qty);
  return true;
}

size_t mb_diag_build_read_pdu(const mb_diag_read_request_t *req, uint8_t *out_pdu, size_t out_capacity) {
  if (out_capacity < 5) return 0;
  out_pdu[0] = req->function_code;
  out_pdu[1] = static_cast<uint8_t>(req->address >> 8);
  out_pdu[2] = static_cast<uint8_t>(req->address & 0xFF);
  out_pdu[3] = static_cast<uint8_t>(req->quantity >> 8);
  out_pdu[4] = static_cast<uint8_t>(req->quantity & 0xFF);
  return 5;
}

size_t mb_diag_build_read_values_json(const mb_diag_read_request_t *req, const uint8_t *response_pdu,
                                       size_t response_pdu_len, char *out, size_t out_capacity) {
  if (response_pdu_len < 2) return 0;
  const uint8_t byte_count = response_pdu[1];
  if (response_pdu_len < static_cast<size_t>(2 + byte_count)) return 0;

  // Skriver DIREKTE ind i `out` (ingen mellemliggende buffer for selve
  // values-arrayet) — et diagnostisk read kan i teorien indeholde op til
  // ~2000 bit-værdier, og en ekstra kopi af den størrelsesorden ville lægge
  // unødigt pres på en ESP32 HTTP-worker-tasks (begrænsede) stak.
  const int header_written =
      snprintf(out, out_capacity, "{\"ok\":true,\"function_code\":%u,\"slave_id\":%u,\"address\":%u,\"quantity\":%u,\"values\":[",
               static_cast<unsigned>(req->function_code), static_cast<unsigned>(req->slave_id),
               static_cast<unsigned>(req->address), static_cast<unsigned>(req->quantity));
  if (header_written <= 0 || static_cast<size_t>(header_written) >= out_capacity) return 0;
  size_t offset = static_cast<size_t>(header_written);

  const bool is_register_read = (req->function_code == 3 || req->function_code == 4);
  const size_t value_count = is_register_read ? static_cast<size_t>(byte_count) / 2 : req->quantity;

  for (size_t i = 0; i < value_count; i++) {
    int value;
    if (is_register_read) {
      value = (response_pdu[2 + i * 2] << 8) | response_pdu[3 + i * 2];
    } else {
      const size_t byte_idx = i / 8;
      const size_t bit_idx = i % 8;
      value = (response_pdu[2 + byte_idx] >> bit_idx) & 1;
    }
    const int written = snprintf(out + offset, out_capacity - offset, "%s%d", i > 0 ? "," : "", value);
    if (written <= 0 || static_cast<size_t>(written) >= out_capacity - offset) return 0;
    offset += static_cast<size_t>(written);
  }

  if (offset + 2 >= out_capacity) return 0;
  out[offset++] = ']';
  out[offset++] = '}';
  return offset;
}

bool mb_diag_parse_write_request(const char *json, size_t len, mb_diag_write_request_t *out) {
  (void)len;

  mb_diag_write_request_t parsed{};
  uint32_t fc = 0, slave = 0, addr = 0;
  if (!parse_uint_field(json, "function_code", &fc)) return false;
  if (fc != 5 && fc != 6 && fc != 15 && fc != 16) return false;
  if (!parse_uint_field(json, "slave_id", &slave) || slave == 0 || slave > 247) return false;
  if (!parse_uint_field(json, "address", &addr) || addr > 0xFFFF) return false;

  parsed.function_code = static_cast<uint8_t>(fc);
  parsed.slave_id = static_cast<uint8_t>(slave);
  parsed.address = static_cast<uint16_t>(addr);

  if (fc == 16) {
    uint16_t count = 0;
    if (!parse_uint_array_field(json, "values", parsed.values, MB_DIAG_MAX_WRITE_VALUES, &count) || count == 0) {
      return false;
    }
    parsed.value_count = count;
  } else if (fc == 15) {
    uint16_t count = 0;
    if (!parse_bool_array_field(json, "values", parsed.values, MB_DIAG_MAX_WRITE_VALUES, &count) || count == 0) {
      return false;
    }
    parsed.value_count = count;
  } else if (fc == 5) {
    bool value = false;
    if (!parse_bool_field(json, "value", &value)) return false;
    parsed.values[0] = value ? 0xFF00 : 0x0000;  // Modbus-spec: coil-ON/OFF på "trådniveau"
    parsed.value_count = 1;
  } else {  // fc == 6
    uint32_t value = 0;
    if (!parse_uint_field(json, "value", &value) || value > 0xFFFF) return false;
    parsed.values[0] = static_cast<uint16_t>(value);
    parsed.value_count = 1;
  }

  *out = parsed;
  return true;
}

size_t mb_diag_build_write_pdu(const mb_diag_write_request_t *req, uint8_t *out_pdu, size_t out_capacity) {
  if (req->function_code == 5 || req->function_code == 6) {
    if (out_capacity < 5) return 0;
    out_pdu[0] = req->function_code;
    out_pdu[1] = static_cast<uint8_t>(req->address >> 8);
    out_pdu[2] = static_cast<uint8_t>(req->address & 0xFF);
    out_pdu[3] = static_cast<uint8_t>(req->values[0] >> 8);
    out_pdu[4] = static_cast<uint8_t>(req->values[0] & 0xFF);
    return 5;
  }

  if (req->function_code == 15) {
    const size_t byte_count = (static_cast<size_t>(req->value_count) + 7) / 8;
    const size_t total = 6 + byte_count;
    if (out_capacity < total) return 0;
    out_pdu[0] = 15;
    out_pdu[1] = static_cast<uint8_t>(req->address >> 8);
    out_pdu[2] = static_cast<uint8_t>(req->address & 0xFF);
    out_pdu[3] = static_cast<uint8_t>(req->value_count >> 8);
    out_pdu[4] = static_cast<uint8_t>(req->value_count & 0xFF);
    out_pdu[5] = static_cast<uint8_t>(byte_count);
    for (size_t i = 0; i < byte_count; i++) out_pdu[6 + i] = 0;
    for (size_t i = 0; i < req->value_count; i++) {
      if (req->values[i] != 0) {
        out_pdu[6 + i / 8] = static_cast<uint8_t>(out_pdu[6 + i / 8] | (1 << (i % 8)));
      }
    }
    return total;
  }

  // FC16
  const size_t byte_count = static_cast<size_t>(req->value_count) * 2;
  const size_t total = 6 + byte_count;
  if (out_capacity < total) return 0;
  out_pdu[0] = 16;
  out_pdu[1] = static_cast<uint8_t>(req->address >> 8);
  out_pdu[2] = static_cast<uint8_t>(req->address & 0xFF);
  out_pdu[3] = static_cast<uint8_t>(req->value_count >> 8);
  out_pdu[4] = static_cast<uint8_t>(req->value_count & 0xFF);
  out_pdu[5] = static_cast<uint8_t>(byte_count);
  for (size_t i = 0; i < req->value_count; i++) {
    out_pdu[6 + i * 2] = static_cast<uint8_t>(req->values[i] >> 8);
    out_pdu[7 + i * 2] = static_cast<uint8_t>(req->values[i] & 0xFF);
  }
  return total;
}

size_t mb_diag_build_write_confirmation_json(const mb_diag_write_request_t *req, const uint8_t *response_pdu,
                                              size_t response_pdu_len, char *out, size_t out_capacity) {
  // Et gyldigt (ikke-exception) FC05/06/16-svar er altid et ekko af
  // function code + adresse (+ quantity for FC16) — lib/modbus_pdu's
  // mb_pdu_parse_rtu_response() har allerede valideret CRC/slave-adresse
  // FØR denne funktion kaldes, så vi stoler på ekkoet uden at genparse det.
  (void)response_pdu;
  (void)response_pdu_len;

  const int written = snprintf(out, out_capacity,
                                "{\"ok\":true,\"function_code\":%u,\"slave_id\":%u,\"address\":%u,\"quantity\":%u}",
                                static_cast<unsigned>(req->function_code), static_cast<unsigned>(req->slave_id),
                                static_cast<unsigned>(req->address), static_cast<unsigned>(req->value_count));
  if (written <= 0 || static_cast<size_t>(written) >= out_capacity) return 0;
  return static_cast<size_t>(written);
}

bool mb_diag_is_exception(const uint8_t *response_pdu, size_t response_pdu_len) {
  return response_pdu_len >= 1 && (response_pdu[0] & 0x80) != 0;
}

size_t mb_diag_build_exception_json(uint8_t slave_id, const uint8_t *response_pdu, size_t response_pdu_len, char *out,
                                     size_t out_capacity) {
  if (response_pdu_len < 2) return 0;
  const uint8_t exception_code = response_pdu[1];
  const int written =
      snprintf(out, out_capacity, "{\"ok\":false,\"error\":\"modbus_exception\",\"slave_id\":%u,\"exception_code\":%u}",
               static_cast<unsigned>(slave_id), static_cast<unsigned>(exception_code));
  if (written <= 0 || static_cast<size_t>(written) >= out_capacity) return 0;
  return static_cast<size_t>(written);
}
