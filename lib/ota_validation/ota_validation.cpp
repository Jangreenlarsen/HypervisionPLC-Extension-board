#include "ota_validation.h"

#include <cstdio>
#include <cstring>

bool mb_ota_is_valid_firmware_magic(const uint8_t *first_bytes, size_t len) {
  return first_bytes != nullptr && len >= 1 && first_bytes[0] == 0xE9;
}

void mb_fwid_scanner_init(mb_fwid_scanner_t *scanner) {
  if (scanner == nullptr) return;
  memset(scanner, 0, sizeof(*scanner));
}

static bool is_version_char(uint8_t c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '.' || c == '-' ||
         c == '_' || c == '+';
}

static void reset_match(mb_fwid_scanner_t *s) {
  s->match_len = 0;
  s->collecting = false;
  s->version_len = 0;
  s->version[0] = '\0';
}

void mb_fwid_scanner_feed(mb_fwid_scanner_t *scanner, const uint8_t *data, size_t len) {
  if (scanner == nullptr || data == nullptr || scanner->found) return;

  static const char kPrefix[] = MB_FWID_PREFIX;
  const size_t prefix_len = sizeof(kPrefix) - 1;

  for (size_t i = 0; i < len && !scanner->found; i++) {
    const uint8_t c = data[i];

    if (scanner->collecting) {
      if (c == ';' && scanner->version_len > 0) {
        scanner->version[scanner->version_len] = '\0';
        scanner->found = true;
      } else if (is_version_char(c) && scanner->version_len < MB_FWID_VERSION_MAX_LEN) {
        scanner->version[scanner->version_len++] = static_cast<char>(c);
      } else {
        // Ikke en gyldig markør (fx selve MB_FWID_PREFIX-literalen i
        // scannerens egen kode, efterfulgt af '\0') — søg videre.
        reset_match(scanner);
        if (c == static_cast<uint8_t>(kPrefix[0])) scanner->match_len = 1;
      }
      continue;
    }

    // Naiv genstart ved mismatch er korrekt her, fordi prefixets første tegn
    // ('H', versal) ikke forekommer andre steder i prefixet — et delvist
    // match kan derfor aldrig indeholde starten på et andet match.
    if (c == static_cast<uint8_t>(kPrefix[scanner->match_len])) {
      scanner->match_len++;
      if (scanner->match_len == prefix_len) {
        scanner->collecting = true;
        scanner->version_len = 0;
      }
    } else {
      scanner->match_len = (c == static_cast<uint8_t>(kPrefix[0])) ? 1 : 0;
    }
  }
}

bool mb_ota_is_valid_md5_hex(const char *md5) {
  if (md5 == nullptr) return false;
  size_t n = 0;
  for (; md5[n] != '\0'; n++) {
    const char c = md5[n];
    const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    if (!hex) return false;
  }
  return n == 32;
}

// Minimal JSON-streng-escaping (", \ og kontroltegn) — fejlbeskeder kan i
// princippet komme fra Update.errorString(), som vi ikke selv styrer.
static size_t append_json_string(char *out, size_t cap, size_t pos, const char *value) {
  if (value == nullptr) value = "";
  for (const char *p = value; *p != '\0' && pos + 2 < cap; p++) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c == '"' || c == '\\') {
      if (pos + 3 >= cap) break;
      out[pos++] = '\\';
      out[pos++] = static_cast<char>(c);
    } else if (c < 0x20) {
      out[pos++] = ' ';
    } else {
      out[pos++] = static_cast<char>(c);
    }
  }
  out[pos] = '\0';
  return pos;
}

size_t mb_ota_build_status_json(const mb_ota_status_data_t *d, char *out, size_t out_capacity) {
  if (out == nullptr || out_capacity == 0) return 0;
  out[0] = '\0';
  if (d == nullptr) return 0;

  const unsigned percent =
      d->total > 0 ? static_cast<unsigned>((static_cast<uint64_t>(d->received) * 100) / d->total) : 0;

  // Opbygges i trin, så de tre fritekst-felter kan escapes.
  size_t pos = 0;
  int n = snprintf(out, out_capacity, "{\"state\":\"%s\",\"received\":%u,\"total\":%u,\"percent\":%u,\"error\":\"",
                   d->state != nullptr ? d->state : "idle", static_cast<unsigned>(d->received),
                   static_cast<unsigned>(d->total), percent);
  if (n < 0 || static_cast<size_t>(n) >= out_capacity) goto overflow;
  pos = static_cast<size_t>(n);
  pos = append_json_string(out, out_capacity, pos, d->error);

  n = snprintf(out + pos, out_capacity - pos, "\",\"running_version\":\"");
  if (n < 0 || static_cast<size_t>(n) >= out_capacity - pos) goto overflow;
  pos += static_cast<size_t>(n);
  pos = append_json_string(out, out_capacity, pos, d->running_version);

  n = snprintf(out + pos, out_capacity - pos, "\",\"new_version\":\"");
  if (n < 0 || static_cast<size_t>(n) >= out_capacity - pos) goto overflow;
  pos += static_cast<size_t>(n);
  pos = append_json_string(out, out_capacity, pos, d->new_version);

  n = snprintf(out + pos, out_capacity - pos,
               "\",\"pending_confirm\":%s,\"confirm_remaining_s\":%u,\"last_update_rolled_back\":%s}",
               d->pending_confirm ? "true" : "false",
               static_cast<unsigned>(d->pending_confirm ? d->confirm_remaining_s : 0),
               d->last_update_rolled_back ? "true" : "false");
  if (n < 0 || static_cast<size_t>(n) >= out_capacity - pos) goto overflow;
  pos += static_cast<size_t>(n);
  return pos;

overflow:
  out[0] = '\0';
  return 0;
}
