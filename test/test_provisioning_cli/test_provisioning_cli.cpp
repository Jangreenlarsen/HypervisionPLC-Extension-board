#include <unity.h>

#include <cstring>

#include "provisioning_cli.h"

static mb_provisioning_state_t state;
static char msg[MB_PROV_MSG_MAX_LEN];

void setUp(void) { mb_provisioning_state_init(&state); }
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Validators
// ---------------------------------------------------------------------------

void test_validate_ssid_bounds(void) {
  TEST_ASSERT_FALSE(mb_provisioning_validate_ssid(""));
  TEST_ASSERT_TRUE(mb_provisioning_validate_ssid("a"));
  TEST_ASSERT_TRUE(mb_provisioning_validate_ssid("12345678901234567890123456789012"));  // 32 tegn
  TEST_ASSERT_FALSE(mb_provisioning_validate_ssid("123456789012345678901234567890123"));  // 33 tegn
}

void test_validate_password_bounds(void) {
  TEST_ASSERT_FALSE(mb_provisioning_validate_password("1234567"));   // 7 tegn - for kort til WPA2
  TEST_ASSERT_TRUE(mb_provisioning_validate_password("12345678"));   // 8 tegn - mindste gyldige
  TEST_ASSERT_TRUE(mb_provisioning_validate_password(
      "123456789012345678901234567890123456789012345678901234567890123"));  // 63 tegn (trunkeret i test til 63)
  TEST_ASSERT_FALSE(mb_provisioning_validate_password(
      "1234567890123456789012345678901234567890123456789012345678901234"));  // 64 tegn - for langt
}

void test_validate_ipv4(void) {
  TEST_ASSERT_TRUE(mb_provisioning_validate_ipv4("192.168.1.1"));
  TEST_ASSERT_TRUE(mb_provisioning_validate_ipv4("0.0.0.0"));
  TEST_ASSERT_TRUE(mb_provisioning_validate_ipv4("255.255.255.255"));
  TEST_ASSERT_FALSE(mb_provisioning_validate_ipv4("256.1.1.1"));      // oktet > 255
  TEST_ASSERT_FALSE(mb_provisioning_validate_ipv4("1.2.3"));          // for faa oktetter
  TEST_ASSERT_FALSE(mb_provisioning_validate_ipv4("1.2.3.4.5"));      // for mange oktetter
  TEST_ASSERT_FALSE(mb_provisioning_validate_ipv4("1..2.3"));         // tomt oktet
  TEST_ASSERT_FALSE(mb_provisioning_validate_ipv4(".1.2.3"));         // ledende punktum
  TEST_ASSERT_FALSE(mb_provisioning_validate_ipv4("1.2.3.4."));       // afsluttende punktum
  TEST_ASSERT_FALSE(mb_provisioning_validate_ipv4("1.2.3.a"));        // ikke-numerisk
  TEST_ASSERT_FALSE(mb_provisioning_validate_ipv4("1.2.3.1234"));     // oktet med 4 cifre
}

// ---------------------------------------------------------------------------
// wifi/plc kommandoer - opdaterer state
// ---------------------------------------------------------------------------

void test_wifi_ssid_sets_state(void) {
  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "wifi ssid MyNetwork", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_OK, r);
  TEST_ASSERT_TRUE(state.has_ssid);
  TEST_ASSERT_EQUAL_STRING("MyNetwork", state.ssid);
}

void test_wifi_ssid_with_spaces_via_quotes(void) {
  const mb_provisioning_result_t r =
      mb_provisioning_apply_line(&state, "wifi ssid \"My Home Network\"", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_OK, r);
  TEST_ASSERT_EQUAL_STRING("My Home Network", state.ssid);
}

void test_wifi_ssid_rejects_invalid(void) {
  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "wifi ssid \"\"", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_INVALID_VALUE, r);
  TEST_ASSERT_FALSE(state.has_ssid);
}

void test_wifi_pass_sets_state_and_clears_open(void) {
  mb_provisioning_apply_line(&state, "wifi open", msg, sizeof(msg));
  TEST_ASSERT_TRUE(state.open_network);

  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "wifi pass MySecretPass1", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_OK, r);
  TEST_ASSERT_TRUE(state.has_password);
  TEST_ASSERT_FALSE(state.open_network);  // "wifi pass" ophaever en tidligere "wifi open"
  TEST_ASSERT_EQUAL_STRING("MySecretPass1", state.password);
}

void test_wifi_pass_rejects_too_short(void) {
  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "wifi pass short", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_INVALID_VALUE, r);
  TEST_ASSERT_FALSE(state.has_password);
}

void test_wifi_open_clears_password(void) {
  mb_provisioning_apply_line(&state, "wifi pass MySecretPass1", msg, sizeof(msg));
  TEST_ASSERT_TRUE(state.has_password);

  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "wifi open", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_OK, r);
  TEST_ASSERT_TRUE(state.open_network);
  TEST_ASSERT_FALSE(state.has_password);
  TEST_ASSERT_EQUAL_STRING("", state.password);
}

void test_wifi_mode_static_and_dhcp(void) {
  TEST_ASSERT_EQUAL(PROV_OK, mb_provisioning_apply_line(&state, "wifi mode static", msg, sizeof(msg)));
  TEST_ASSERT_TRUE(state.static_ip);

  TEST_ASSERT_EQUAL(PROV_OK, mb_provisioning_apply_line(&state, "wifi mode dhcp", msg, sizeof(msg)));
  TEST_ASSERT_FALSE(state.static_ip);
}

void test_wifi_mode_rejects_unknown(void) {
  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "wifi mode bogus", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_INVALID_VALUE, r);
}

void test_wifi_ip_mask_gw_set_state(void) {
  TEST_ASSERT_EQUAL(PROV_OK, mb_provisioning_apply_line(&state, "wifi ip 10.0.0.5", msg, sizeof(msg)));
  TEST_ASSERT_EQUAL(PROV_OK, mb_provisioning_apply_line(&state, "wifi mask 255.255.255.0", msg, sizeof(msg)));
  TEST_ASSERT_EQUAL(PROV_OK, mb_provisioning_apply_line(&state, "wifi gw 10.0.0.1", msg, sizeof(msg)));
  TEST_ASSERT_TRUE(state.has_ip && state.has_mask && state.has_gw);
  TEST_ASSERT_EQUAL_STRING("10.0.0.5", state.ip);
  TEST_ASSERT_EQUAL_STRING("255.255.255.0", state.mask);
  TEST_ASSERT_EQUAL_STRING("10.0.0.1", state.gw);
}

void test_wifi_ip_rejects_invalid(void) {
  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "wifi ip not.an.ip", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_INVALID_VALUE, r);
  TEST_ASSERT_FALSE(state.has_ip);
}

void test_plc_ip_sets_state(void) {
  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "plc ip 192.168.1.10", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_OK, r);
  TEST_ASSERT_TRUE(state.has_plc_ip);
  TEST_ASSERT_EQUAL_STRING("192.168.1.10", state.plc_ip);
}

void test_plc_ip_missing_argument(void) {
  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "plc ip", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_MISSING_ARGUMENT, r);
}

void test_wifi_missing_subcommand(void) {
  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "wifi", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_MISSING_ARGUMENT, r);
}

void test_wifi_unknown_subcommand(void) {
  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "wifi bogus value", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_UNKNOWN_COMMAND, r);
}

// ---------------------------------------------------------------------------
// Kommando-ord er versalfoelsomheds-uafhaengige; vaerdier er DET (SSID/password)
// ---------------------------------------------------------------------------

void test_command_words_are_case_insensitive(void) {
  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "WIFI SSID MyNetwork", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_OK, r);
  TEST_ASSERT_EQUAL_STRING("MyNetwork", state.ssid);  // vaerdien er IKKE lowercased
}

// ---------------------------------------------------------------------------
// show / help / tom linje
// ---------------------------------------------------------------------------

void test_show_action(void) {
  mb_provisioning_apply_line(&state, "wifi ssid Foo", msg, sizeof(msg));
  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "show", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_ACTION_SHOW, r);
  TEST_ASSERT_NOT_NULL(strstr(msg, "ssid=Foo"));
  TEST_ASSERT_NOT_NULL(strstr(msg, "mode=dhcp"));
}

void test_show_never_leaks_password(void) {
  mb_provisioning_apply_line(&state, "wifi pass SuperSecret123", msg, sizeof(msg));
  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "show", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_ACTION_SHOW, r);
  TEST_ASSERT_NULL_MESSAGE(strstr(msg, "SuperSecret123"), "password laakkede i klartekst i show-output");
  TEST_ASSERT_NOT_NULL(strstr(msg, "********"));
}

void test_wifi_pass_confirmation_never_echoes_password(void) {
  const mb_provisioning_result_t r =
      mb_provisioning_apply_line(&state, "wifi pass SuperSecret123", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_OK, r);
  TEST_ASSERT_NULL_MESSAGE(strstr(msg, "SuperSecret123"),
                            "password laakkede i klartekst i bekraeftelsesbeskeden");
}

void test_help_action(void) {
  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "help", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_ACTION_HELP, r);
  // Tjek at ALLE kommandoer reelt fremgår, ikke kun at strengen er ikke-tom —
  // en for lille MB_PROV_MSG_MAX_LEN ville snprintf-afkorte teksten midt i en
  // sætning uden at gøre strlen(msg)==0 (fanget ved manuel test mod rigtig
  // hardware, ikke af en tidligere, svagere version af denne test).
  TEST_ASSERT_NOT_NULL(strstr(msg, "wifi ssid"));
  TEST_ASSERT_NOT_NULL(strstr(msg, "wifi pass"));
  TEST_ASSERT_NOT_NULL(strstr(msg, "wifi open"));
  TEST_ASSERT_NOT_NULL(strstr(msg, "wifi mode"));
  TEST_ASSERT_NOT_NULL(strstr(msg, "plc ip"));
  TEST_ASSERT_NOT_NULL(strstr(msg, "show"));
  TEST_ASSERT_NOT_NULL(strstr(msg, "connect"));
  TEST_ASSERT_NOT_NULL(strstr(msg, "factory-reset confirm"));
  TEST_ASSERT_NOT_NULL(strstr(msg, "version"));
  TEST_ASSERT_NOT_NULL(strstr(msg, "help"));
}

void test_version_action_without_build_flags(void) {
  // native-miljøet faar bevidst IKKE FW_VERSION/FW_BUILD injiceret (kun
  // esp32dev-target'et faar dem fra extract_version.py) — denne test
  // verificerer derfor fallback-stien, ikke et hardkodet versionsnummer der
  // ville skulle opdateres ved hver versionsbump.
  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "version", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_ACTION_VERSION, r);
  TEST_ASSERT_NOT_NULL(strstr(msg, "HypervisionPLC Extension board"));
}

void test_empty_line(void) {
  TEST_ASSERT_EQUAL(PROV_EMPTY_LINE, mb_provisioning_apply_line(&state, "", msg, sizeof(msg)));
  TEST_ASSERT_EQUAL(PROV_EMPTY_LINE, mb_provisioning_apply_line(&state, "   ", msg, sizeof(msg)));
}

void test_trailing_crlf_is_stripped(void) {
  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "wifi ssid Foo\r\n", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_OK, r);
  TEST_ASSERT_EQUAL_STRING("Foo", state.ssid);
}

void test_unknown_command(void) {
  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "bogus", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_UNKNOWN_COMMAND, r);
}

// ---------------------------------------------------------------------------
// connect - kraever alle paakraevede felter foerst
// ---------------------------------------------------------------------------

void test_connect_rejected_when_incomplete(void) {
  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "connect", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_MISSING_ARGUMENT, r);
  TEST_ASSERT_NOT_NULL(strstr(msg, "ssid"));
}

void test_connect_succeeds_dhcp_with_password(void) {
  mb_provisioning_apply_line(&state, "wifi ssid MyNetwork", msg, sizeof(msg));
  mb_provisioning_apply_line(&state, "wifi pass MySecretPass1", msg, sizeof(msg));
  mb_provisioning_apply_line(&state, "plc ip 192.168.1.10", msg, sizeof(msg));

  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "connect", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_ACTION_CONNECT, r);
}

void test_connect_succeeds_open_network(void) {
  mb_provisioning_apply_line(&state, "wifi ssid MyNetwork", msg, sizeof(msg));
  mb_provisioning_apply_line(&state, "wifi open", msg, sizeof(msg));
  mb_provisioning_apply_line(&state, "plc ip 192.168.1.10", msg, sizeof(msg));

  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "connect", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_ACTION_CONNECT, r);
}

void test_connect_rejected_missing_plc_ip(void) {
  mb_provisioning_apply_line(&state, "wifi ssid MyNetwork", msg, sizeof(msg));
  mb_provisioning_apply_line(&state, "wifi pass MySecretPass1", msg, sizeof(msg));

  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "connect", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_MISSING_ARGUMENT, r);
  TEST_ASSERT_NOT_NULL(strstr(msg, "plc"));
}

void test_connect_static_mode_requires_ip_mask_gw(void) {
  mb_provisioning_apply_line(&state, "wifi ssid MyNetwork", msg, sizeof(msg));
  mb_provisioning_apply_line(&state, "wifi pass MySecretPass1", msg, sizeof(msg));
  mb_provisioning_apply_line(&state, "plc ip 192.168.1.10", msg, sizeof(msg));
  mb_provisioning_apply_line(&state, "wifi mode static", msg, sizeof(msg));

  // ip/mask/gw endnu ikke sat
  mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "connect", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_MISSING_ARGUMENT, r);

  mb_provisioning_apply_line(&state, "wifi ip 10.0.0.5", msg, sizeof(msg));
  mb_provisioning_apply_line(&state, "wifi mask 255.255.255.0", msg, sizeof(msg));
  mb_provisioning_apply_line(&state, "wifi gw 10.0.0.1", msg, sizeof(msg));

  r = mb_provisioning_apply_line(&state, "connect", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_ACTION_CONNECT, r);
}

// ---------------------------------------------------------------------------
// factory-reset
// ---------------------------------------------------------------------------

void test_factory_reset_requires_confirm(void) {
  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "factory-reset", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_MISSING_ARGUMENT, r);
}

void test_factory_reset_confirm_triggers_action(void) {
  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "factory-reset confirm", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_ACTION_FACTORY_RESET, r);
}

void test_factory_reset_rejects_wrong_argument(void) {
  const mb_provisioning_result_t r = mb_provisioning_apply_line(&state, "factory-reset yes", msg, sizeof(msg));
  TEST_ASSERT_EQUAL(PROV_MISSING_ARGUMENT, r);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();

  RUN_TEST(test_validate_ssid_bounds);
  RUN_TEST(test_validate_password_bounds);
  RUN_TEST(test_validate_ipv4);

  RUN_TEST(test_wifi_ssid_sets_state);
  RUN_TEST(test_wifi_ssid_with_spaces_via_quotes);
  RUN_TEST(test_wifi_ssid_rejects_invalid);
  RUN_TEST(test_wifi_pass_sets_state_and_clears_open);
  RUN_TEST(test_wifi_pass_rejects_too_short);
  RUN_TEST(test_wifi_open_clears_password);
  RUN_TEST(test_wifi_mode_static_and_dhcp);
  RUN_TEST(test_wifi_mode_rejects_unknown);
  RUN_TEST(test_wifi_ip_mask_gw_set_state);
  RUN_TEST(test_wifi_ip_rejects_invalid);
  RUN_TEST(test_plc_ip_sets_state);
  RUN_TEST(test_plc_ip_missing_argument);
  RUN_TEST(test_wifi_missing_subcommand);
  RUN_TEST(test_wifi_unknown_subcommand);

  RUN_TEST(test_command_words_are_case_insensitive);

  RUN_TEST(test_show_action);
  RUN_TEST(test_show_never_leaks_password);
  RUN_TEST(test_wifi_pass_confirmation_never_echoes_password);
  RUN_TEST(test_help_action);
  RUN_TEST(test_version_action_without_build_flags);
  RUN_TEST(test_empty_line);
  RUN_TEST(test_trailing_crlf_is_stripped);
  RUN_TEST(test_unknown_command);

  RUN_TEST(test_connect_rejected_when_incomplete);
  RUN_TEST(test_connect_succeeds_dhcp_with_password);
  RUN_TEST(test_connect_succeeds_open_network);
  RUN_TEST(test_connect_rejected_missing_plc_ip);
  RUN_TEST(test_connect_static_mode_requires_ip_mask_gw);

  RUN_TEST(test_factory_reset_requires_confirm);
  RUN_TEST(test_factory_reset_confirm_triggers_action);
  RUN_TEST(test_factory_reset_rejects_wrong_argument);

  return UNITY_END();
}
