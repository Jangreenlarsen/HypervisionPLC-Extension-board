#include <unity.h>

#include <cstring>

#include "rest_auth.h"

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// base64-decoding — vektorer verificeret uafhængigt med Pythons base64-modul
// ---------------------------------------------------------------------------

void test_base64_decode_admin_secret(void) {
  uint8_t out[32];
  const size_t len = mb_base64_decode("YWRtaW46c2VjcmV0MTIz", out, sizeof(out));
  TEST_ASSERT_EQUAL_size_t(strlen("admin:secret123"), len);
  TEST_ASSERT_EQUAL_STRING("admin:secret123", reinterpret_cast<char *>(out));
}

void test_base64_decode_single_char_each_side(void) {
  uint8_t out[8];
  const size_t len = mb_base64_decode("YTpi", out, sizeof(out));
  TEST_ASSERT_EQUAL_size_t(3, len);
  TEST_ASSERT_EQUAL_STRING("a:b", reinterpret_cast<char *>(out));
}

void test_base64_decode_password_containing_colon(void) {
  uint8_t out[16];
  const size_t len = mb_base64_decode("YWI6Y2Q6ZWY=", out, sizeof(out));
  TEST_ASSERT_EQUAL_size_t(strlen("ab:cd:ef"), len);
  TEST_ASSERT_EQUAL_STRING("ab:cd:ef", reinterpret_cast<char *>(out));
}

void test_base64_decode_rejects_invalid_length(void) {
  uint8_t out[16];
  TEST_ASSERT_EQUAL_size_t(0, mb_base64_decode("YWI", out, sizeof(out)));  // 3 tegn, ikke multiplum af 4
}

void test_base64_decode_rejects_invalid_character(void) {
  uint8_t out[16];
  TEST_ASSERT_EQUAL_size_t(0, mb_base64_decode("YWI!", out, sizeof(out)));
}

void test_base64_decode_rejects_undersized_output(void) {
  uint8_t out[2];  // "admin:secret123" er 15 bytes
  TEST_ASSERT_EQUAL_size_t(0, mb_base64_decode("YWRtaW46c2VjcmV0MTIz", out, sizeof(out)));
}

// ---------------------------------------------------------------------------
// mb_rest_auth_check — Bearer-token
// ---------------------------------------------------------------------------

void test_auth_bearer_accepts_matching_token(void) {
  const mb_rest_credentials_t creds = {"abc123token", true, nullptr, nullptr, false, MB_REST_AUTH_MODE_BOTH};
  TEST_ASSERT_EQUAL(MB_REST_AUTH_OK, mb_rest_auth_check("Bearer abc123token", &creds));
}

void test_auth_bearer_rejects_wrong_token(void) {
  const mb_rest_credentials_t creds = {"abc123token", true, nullptr, nullptr, false, MB_REST_AUTH_MODE_BOTH};
  TEST_ASSERT_EQUAL(MB_REST_AUTH_INVALID_CREDENTIALS, mb_rest_auth_check("Bearer wrongtoken", &creds));
}

void test_auth_bearer_rejects_when_no_token_configured(void) {
  const mb_rest_credentials_t creds = {nullptr, false, nullptr, nullptr, false, MB_REST_AUTH_MODE_BOTH};
  TEST_ASSERT_EQUAL(MB_REST_AUTH_INVALID_CREDENTIALS, mb_rest_auth_check("Bearer anything", &creds));
}

void test_auth_bearer_scheme_is_case_insensitive(void) {
  const mb_rest_credentials_t creds = {"abc123token", true, nullptr, nullptr, false, MB_REST_AUTH_MODE_BOTH};
  TEST_ASSERT_EQUAL(MB_REST_AUTH_OK, mb_rest_auth_check("bearer abc123token", &creds));
}

// ---------------------------------------------------------------------------
// mb_rest_auth_check — Basic Auth
// ---------------------------------------------------------------------------

void test_auth_basic_accepts_matching_credentials(void) {
  const mb_rest_credentials_t creds = {nullptr, false, "admin", "secret123", true, MB_REST_AUTH_MODE_BOTH};
  // "admin:secret123" base64-encoded
  TEST_ASSERT_EQUAL(MB_REST_AUTH_OK, mb_rest_auth_check("Basic YWRtaW46c2VjcmV0MTIz", &creds));
}

void test_auth_basic_rejects_wrong_password(void) {
  const mb_rest_credentials_t creds = {nullptr, false, "admin", "differentpass", true, MB_REST_AUTH_MODE_BOTH};
  TEST_ASSERT_EQUAL(MB_REST_AUTH_INVALID_CREDENTIALS, mb_rest_auth_check("Basic YWRtaW46c2VjcmV0MTIz", &creds));
}

void test_auth_basic_rejects_when_not_configured(void) {
  const mb_rest_credentials_t creds = {nullptr, false, nullptr, nullptr, false, MB_REST_AUTH_MODE_BOTH};
  TEST_ASSERT_EQUAL(MB_REST_AUTH_INVALID_CREDENTIALS, mb_rest_auth_check("Basic YWRtaW46c2VjcmV0MTIz", &creds));
}

void test_auth_basic_handles_password_containing_colon(void) {
  const mb_rest_credentials_t creds = {nullptr, false, "ab", "cd:ef", true, MB_REST_AUTH_MODE_BOTH};
  TEST_ASSERT_EQUAL(MB_REST_AUTH_OK, mb_rest_auth_check("Basic YWI6Y2Q6ZWY=", &creds));
}

void test_auth_basic_rejects_malformed_base64(void) {
  const mb_rest_credentials_t creds = {nullptr, false, "admin", "secret123", true, MB_REST_AUTH_MODE_BOTH};
  TEST_ASSERT_EQUAL(MB_REST_AUTH_MALFORMED, mb_rest_auth_check("Basic not-valid-base64!", &creds));
}

void test_auth_basic_rejects_missing_colon(void) {
  const mb_rest_credentials_t creds = {nullptr, false, "admin", "secret123", true, MB_REST_AUTH_MODE_BOTH};
  // "HypervisionPLC" base64-encoded - gyldig base64, men intet ':' efter afkodning
  TEST_ASSERT_EQUAL(MB_REST_AUTH_MALFORMED, mb_rest_auth_check("Basic SHlwZXJ2aXNpb25QTEM=", &creds));
}

// ---------------------------------------------------------------------------
// auth_mode (Jan: "auth-metoden vi bruger skal kunne config'es i CLI'en")
// ---------------------------------------------------------------------------

void test_auth_mode_both_accepts_bearer_and_basic(void) {
  const mb_rest_credentials_t creds = {"abc123token", true, "admin", "secret123", true, MB_REST_AUTH_MODE_BOTH};
  TEST_ASSERT_EQUAL(MB_REST_AUTH_OK, mb_rest_auth_check("Bearer abc123token", &creds));
  TEST_ASSERT_EQUAL(MB_REST_AUTH_OK, mb_rest_auth_check("Basic YWRtaW46c2VjcmV0MTIz", &creds));
}

void test_auth_mode_token_only_rejects_basic_even_with_correct_credentials(void) {
  const mb_rest_credentials_t creds = {"abc123token", true, "admin", "secret123", true,
                                        MB_REST_AUTH_MODE_TOKEN_ONLY};
  TEST_ASSERT_EQUAL(MB_REST_AUTH_OK, mb_rest_auth_check("Bearer abc123token", &creds));
  TEST_ASSERT_EQUAL(MB_REST_AUTH_METHOD_DISABLED, mb_rest_auth_check("Basic YWRtaW46c2VjcmV0MTIz", &creds));
}

void test_auth_mode_basic_only_rejects_bearer_even_with_correct_token(void) {
  const mb_rest_credentials_t creds = {"abc123token", true, "admin", "secret123", true,
                                        MB_REST_AUTH_MODE_BASIC_ONLY};
  TEST_ASSERT_EQUAL(MB_REST_AUTH_METHOD_DISABLED, mb_rest_auth_check("Bearer abc123token", &creds));
  TEST_ASSERT_EQUAL(MB_REST_AUTH_OK, mb_rest_auth_check("Basic YWRtaW46c2VjcmV0MTIz", &creds));
}

// ---------------------------------------------------------------------------
// Header-niveau fejl
// ---------------------------------------------------------------------------

void test_auth_missing_header(void) {
  const mb_rest_credentials_t creds = {"token", true, nullptr, nullptr, false, MB_REST_AUTH_MODE_BOTH};
  TEST_ASSERT_EQUAL(MB_REST_AUTH_MISSING_HEADER, mb_rest_auth_check(nullptr, &creds));
  TEST_ASSERT_EQUAL(MB_REST_AUTH_MISSING_HEADER, mb_rest_auth_check("", &creds));
}

void test_auth_unsupported_scheme(void) {
  const mb_rest_credentials_t creds = {"token", true, nullptr, nullptr, false, MB_REST_AUTH_MODE_BOTH};
  TEST_ASSERT_EQUAL(MB_REST_AUTH_UNSUPPORTED_SCHEME, mb_rest_auth_check("Digest abc123", &creds));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();

  RUN_TEST(test_base64_decode_admin_secret);
  RUN_TEST(test_base64_decode_single_char_each_side);
  RUN_TEST(test_base64_decode_password_containing_colon);
  RUN_TEST(test_base64_decode_rejects_invalid_length);
  RUN_TEST(test_base64_decode_rejects_invalid_character);
  RUN_TEST(test_base64_decode_rejects_undersized_output);

  RUN_TEST(test_auth_bearer_accepts_matching_token);
  RUN_TEST(test_auth_bearer_rejects_wrong_token);
  RUN_TEST(test_auth_bearer_rejects_when_no_token_configured);
  RUN_TEST(test_auth_bearer_scheme_is_case_insensitive);

  RUN_TEST(test_auth_basic_accepts_matching_credentials);
  RUN_TEST(test_auth_basic_rejects_wrong_password);
  RUN_TEST(test_auth_basic_rejects_when_not_configured);
  RUN_TEST(test_auth_basic_handles_password_containing_colon);
  RUN_TEST(test_auth_basic_rejects_malformed_base64);
  RUN_TEST(test_auth_basic_rejects_missing_colon);

  RUN_TEST(test_auth_mode_both_accepts_bearer_and_basic);
  RUN_TEST(test_auth_mode_token_only_rejects_basic_even_with_correct_credentials);
  RUN_TEST(test_auth_mode_basic_only_rejects_bearer_even_with_correct_token);

  RUN_TEST(test_auth_missing_header);
  RUN_TEST(test_auth_unsupported_scheme);

  return UNITY_END();
}
