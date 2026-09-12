#include "rest_auth.h"

#include <cctype>
#include <cstring>

namespace {

int8_t base64_value(char c) {
  if (c >= 'A' && c <= 'Z') return static_cast<int8_t>(c - 'A');
  if (c >= 'a' && c <= 'z') return static_cast<int8_t>(c - 'a' + 26);
  if (c >= '0' && c <= '9') return static_cast<int8_t>(c - '0' + 52);
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

// Undgår at responstiden lækker HVOR i strengen en credential-sammenligning
// fejlede — laengde-forskel er bevidst IKKE skjult (kraever at bygge hele
// afkodningen paa faste laengder, unødig kompleksitet for denne trussel-
// model: embedded management-API bag netvaerkssegmentering, §4.4).
bool constant_time_equals(const char *a, const char *b) {
  const size_t len_a = strlen(a);
  const size_t len_b = strlen(b);
  if (len_a != len_b) return false;

  uint8_t diff = 0;
  for (size_t i = 0; i < len_a; i++) {
    diff |= static_cast<uint8_t>(a[i]) ^ static_cast<uint8_t>(b[i]);
  }
  return diff == 0;
}

bool starts_with_ci(const char *str, const char *prefix) {
  const size_t prefix_len = strlen(prefix);
  for (size_t i = 0; i < prefix_len; i++) {
    if (str[i] == '\0') return false;
    const char a = static_cast<char>(tolower(static_cast<unsigned char>(str[i])));
    const char b = static_cast<char>(tolower(static_cast<unsigned char>(prefix[i])));
    if (a != b) return false;
  }
  return true;
}

}  // namespace

size_t mb_base64_decode(const char *input, uint8_t *out, size_t out_capacity) {
  if (input == nullptr || out == nullptr) return 0;

  const size_t input_len = strlen(input);
  if (input_len == 0 || input_len % 4 != 0) return 0;

  size_t out_len = 0;
  for (size_t i = 0; i < input_len; i += 4) {
    uint32_t chunk = 0;
    int pad = 0;

    for (int j = 0; j < 4; j++) {
      const char c = input[i + static_cast<size_t>(j)];
      if (c == '=') {
        pad++;
        chunk <<= 6;
      } else {
        if (pad > 0) return 0;  // '=' skal vaere sidst i chunken, ikke efterfulgt af rigtige tegn
        const int8_t v = base64_value(c);
        if (v < 0) return 0;
        chunk = (chunk << 6) | static_cast<uint32_t>(v);
      }
    }

    if (pad > 0 && i + 4 != input_len) return 0;  // padding maa kun forekomme i SIDSTE chunk

    const int bytes_this_chunk = 3 - pad;
    if (out_len + static_cast<size_t>(bytes_this_chunk) > out_capacity) return 0;

    out[out_len++] = static_cast<uint8_t>((chunk >> 16) & 0xFF);
    if (bytes_this_chunk >= 2) out[out_len++] = static_cast<uint8_t>((chunk >> 8) & 0xFF);
    if (bytes_this_chunk >= 3) out[out_len++] = static_cast<uint8_t>(chunk & 0xFF);
  }

  if (out_len < out_capacity) out[out_len] = '\0';
  return out_len;
}

mb_rest_auth_result_t mb_rest_auth_check(const char *auth_header, const mb_rest_credentials_t *credentials) {
  if (auth_header == nullptr || auth_header[0] == '\0') {
    return MB_REST_AUTH_MISSING_HEADER;
  }

  if (starts_with_ci(auth_header, "Bearer ")) {
    if (credentials->auth_mode == MB_REST_AUTH_MODE_BASIC_ONLY) return MB_REST_AUTH_METHOD_DISABLED;
    const char *token = auth_header + 7;
    if (!credentials->has_mgmt_token) return MB_REST_AUTH_INVALID_CREDENTIALS;
    return constant_time_equals(token, credentials->mgmt_token) ? MB_REST_AUTH_OK : MB_REST_AUTH_INVALID_CREDENTIALS;
  }

  if (starts_with_ci(auth_header, "Basic ")) {
    if (credentials->auth_mode == MB_REST_AUTH_MODE_TOKEN_ONLY) return MB_REST_AUTH_METHOD_DISABLED;
    const char *b64 = auth_header + 6;
    uint8_t decoded[96];  // rigeligt til MB_PROV_REST_USER_MAX_LEN(32) + ':' + MB_PROV_REST_PASS_MAX_LEN(63) + '\0'
    const size_t decoded_len = mb_base64_decode(b64, decoded, sizeof(decoded));
    if (decoded_len == 0) return MB_REST_AUTH_MALFORMED;

    // Brugernavnet maa (per RFC 7617) ikke indeholde ':', men adgangskoden
    // MAA — split derfor kun paa det FØRSTE ':'.
    const char *colon = static_cast<const char *>(memchr(decoded, ':', decoded_len));
    if (colon == nullptr) return MB_REST_AUTH_MALFORMED;

    char user[64];
    char pass[80];
    const size_t user_len = static_cast<size_t>(colon - reinterpret_cast<const char *>(decoded));
    const size_t pass_len = decoded_len - user_len - 1;
    if (user_len >= sizeof(user) || pass_len >= sizeof(pass)) return MB_REST_AUTH_MALFORMED;

    memcpy(user, decoded, user_len);
    user[user_len] = '\0';
    memcpy(pass, colon + 1, pass_len);
    pass[pass_len] = '\0';

    if (!credentials->has_rest_auth) return MB_REST_AUTH_INVALID_CREDENTIALS;
    const bool user_ok = constant_time_equals(user, credentials->rest_user);
    const bool pass_ok = constant_time_equals(pass, credentials->rest_pass);
    return (user_ok && pass_ok) ? MB_REST_AUTH_OK : MB_REST_AUTH_INVALID_CREDENTIALS;
  }

  return MB_REST_AUTH_UNSUPPORTED_SCHEME;
}
