#include <unity.h>

#include <cstring>

#include "board_config.h"

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------

void test_defaults_are_unprovisioned(void) {
  mb_board_config_t config;
  mb_config_set_defaults(&config);
  TEST_ASSERT_EQUAL(MB_CONFIG_SCHEMA_VERSION, config.schema_version);
  TEST_ASSERT_FALSE(config.provisioned);
  TEST_ASSERT_FALSE(config.wifi_has_ssid);
  TEST_ASSERT_FALSE(config.wifi_has_password);
  TEST_ASSERT_FALSE(config.has_plc_ip);
  TEST_ASSERT_FALSE(config.has_mgmt_token);
  TEST_ASSERT_FALSE(config.has_rest_user);
  TEST_ASSERT_FALSE(config.has_rest_pass);
}

// ---------------------------------------------------------------------------
// Blob round-trip
// ---------------------------------------------------------------------------

void test_save_and_load_roundtrip(void) {
  mb_board_config_t config;
  mb_config_set_defaults(&config);
  strncpy(config.wifi_ssid, "MyNetwork", sizeof(config.wifi_ssid) - 1);
  config.wifi_has_ssid = true;
  strncpy(config.plc_ip, "192.168.1.10", sizeof(config.plc_ip) - 1);
  config.has_plc_ip = true;
  config.provisioned = true;
  strncpy(config.mgmt_token, "abcd1234abcd1234abcd1234abcd1234", sizeof(config.mgmt_token) - 1);
  config.has_mgmt_token = true;

  uint8_t blob[sizeof(mb_board_config_t)];
  const size_t written = mb_config_save_to_blob(&config, blob, sizeof(blob));
  TEST_ASSERT_EQUAL_size_t(sizeof(mb_board_config_t), written);

  mb_board_config_t loaded;
  mb_config_load_from_blob(blob, written, &loaded);

  TEST_ASSERT_EQUAL_STRING("MyNetwork", loaded.wifi_ssid);
  TEST_ASSERT_TRUE(loaded.wifi_has_ssid);
  TEST_ASSERT_EQUAL_STRING("192.168.1.10", loaded.plc_ip);
  TEST_ASSERT_TRUE(loaded.has_plc_ip);
  TEST_ASSERT_TRUE(loaded.provisioned);
  TEST_ASSERT_EQUAL_STRING("abcd1234abcd1234abcd1234abcd1234", loaded.mgmt_token);
  TEST_ASSERT_TRUE(loaded.has_mgmt_token);
}

void test_save_rejects_undersized_buffer(void) {
  mb_board_config_t config;
  mb_config_set_defaults(&config);
  uint8_t blob[4];  // alt for lille
  TEST_ASSERT_EQUAL_size_t(0, mb_config_save_to_blob(&config, blob, sizeof(blob)));
}

// ---------------------------------------------------------------------------
// Robusthed: intet gemt, forkert størrelse, korruption, "fremtidig" schema
// ---------------------------------------------------------------------------

void test_load_with_no_stored_data_gives_defaults(void) {
  mb_board_config_t loaded;
  mb_config_load_from_blob(nullptr, 0, &loaded);
  TEST_ASSERT_FALSE(loaded.provisioned);
  TEST_ASSERT_EQUAL(MB_CONFIG_SCHEMA_VERSION, loaded.schema_version);
}

void test_load_with_wrong_size_gives_defaults_not_garbage(void) {
  mb_board_config_t config;
  mb_config_set_defaults(&config);
  config.provisioned = true;

  uint8_t blob[sizeof(mb_board_config_t)];
  mb_config_save_to_blob(&config, blob, sizeof(blob));

  mb_board_config_t loaded;
  // Trunkeret blob (fx en delvis/afbrudt NVS-laesning) — skal IKKE tolkes
  // som gyldig, uanset hvor mange bytes der reelt matcher.
  mb_config_load_from_blob(blob, sizeof(blob) - 1, &loaded);
  TEST_ASSERT_FALSE(loaded.provisioned);
  TEST_ASSERT_EQUAL(MB_CONFIG_SCHEMA_VERSION, loaded.schema_version);
}

void test_load_detects_corruption_via_checksum(void) {
  mb_board_config_t config;
  mb_config_set_defaults(&config);
  strncpy(config.wifi_ssid, "MyNetwork", sizeof(config.wifi_ssid) - 1);
  config.wifi_has_ssid = true;
  config.provisioned = true;

  uint8_t blob[sizeof(mb_board_config_t)];
  const size_t written = mb_config_save_to_blob(&config, blob, sizeof(blob));

  blob[5] ^= 0xFF;  // vaelt en enkelt byte midt i configen (simulerer strømtab under skrivning)

  mb_board_config_t loaded;
  mb_config_load_from_blob(blob, written, &loaded);
  TEST_ASSERT_FALSE_MESSAGE(loaded.provisioned, "korrupt blob blev fejlagtigt accepteret som gyldig");
  TEST_ASSERT_EQUAL(MB_CONFIG_SCHEMA_VERSION, loaded.schema_version);
}

void test_load_migrates_v1_blob_without_data_loss(void) {
  // KRITISK: et rigtigt board kan allerede have en schema-1-blob gemt (det
  // gjorde det, under selve udviklingen af denne funktion) — en firmware-
  // opdatering til schema 2 maa IKKE nulstille den til fabriksdefaults.
  // v1-blobben konstrueres manuelt her, da mb_config_save_to_blob() altid
  // gemmer i det AKTUELLE (v2) format — der er ingen anden vej til at
  // producere en aegte v1-blob at teste migration imod.
  mb_board_config_v1_t v1{};
  v1.schema_version = 1;
  v1.provisioned = true;
  strncpy(v1.wifi_ssid, "OldNetwork", sizeof(v1.wifi_ssid) - 1);
  v1.wifi_has_ssid = true;
  strncpy(v1.wifi_password, "OldPassword1", sizeof(v1.wifi_password) - 1);
  v1.wifi_has_password = true;
  strncpy(v1.plc_ip, "10.1.1.153", sizeof(v1.plc_ip) - 1);
  v1.has_plc_ip = true;
  strncpy(v1.mgmt_token, "f4353305b96b5f1e61e5e70b49b18e1c", sizeof(v1.mgmt_token) - 1);
  v1.has_mgmt_token = true;
  strncpy(v1.rest_user, "testadmin", sizeof(v1.rest_user) - 1);
  v1.has_rest_user = true;
  strncpy(v1.rest_pass, "RestApiTest123", sizeof(v1.rest_pass) - 1);
  v1.has_rest_pass = true;
  v1.checksum = mb_config_calc_checksum_v1(&v1);

  mb_board_config_t migrated;
  mb_config_load_from_blob(reinterpret_cast<const uint8_t *>(&v1), sizeof(v1), &migrated);

  TEST_ASSERT_EQUAL_MESSAGE(MB_CONFIG_SCHEMA_VERSION, migrated.schema_version,
                             "migreret config skal have den AKTUELLE schema-version, ikke 1");
  TEST_ASSERT_TRUE_MESSAGE(migrated.provisioned, "provisioned-flag tabt under migration");
  TEST_ASSERT_EQUAL_STRING("OldNetwork", migrated.wifi_ssid);
  TEST_ASSERT_TRUE(migrated.wifi_has_ssid);
  TEST_ASSERT_EQUAL_STRING("OldPassword1", migrated.wifi_password);
  TEST_ASSERT_EQUAL_STRING("10.1.1.153", migrated.plc_ip);
  TEST_ASSERT_TRUE(migrated.has_plc_ip);
  TEST_ASSERT_EQUAL_STRING("f4353305b96b5f1e61e5e70b49b18e1c", migrated.mgmt_token);
  TEST_ASSERT_TRUE(migrated.has_mgmt_token);
  TEST_ASSERT_EQUAL_STRING("testadmin", migrated.rest_user);
  TEST_ASSERT_EQUAL_STRING("RestApiTest123", migrated.rest_pass);
  TEST_ASSERT_EQUAL_MESSAGE(MB_REST_AUTH_MODE_BOTH, migrated.rest_auth_mode,
                             "nyt felt skal faa default-vaerdien (BOTH), ikke vaere udefineret");
}

void test_load_rejects_corrupt_v1_blob(void) {
  mb_board_config_v1_t v1{};
  v1.schema_version = 1;
  v1.provisioned = true;
  strncpy(v1.wifi_ssid, "OldNetwork", sizeof(v1.wifi_ssid) - 1);
  v1.checksum = mb_config_calc_checksum_v1(&v1);

  uint8_t blob[sizeof(v1)];
  memcpy(blob, &v1, sizeof(blob));
  blob[10] ^= 0xFF;  // vaelt en byte midt i v1-blobben

  mb_board_config_t loaded;
  mb_config_load_from_blob(blob, sizeof(blob), &loaded);
  TEST_ASSERT_FALSE_MESSAGE(loaded.provisioned, "korrupt v1-blob blev fejlagtigt migreret");
  TEST_ASSERT_EQUAL(MB_CONFIG_SCHEMA_VERSION, loaded.schema_version);
}

void test_load_rejects_future_schema_version(void) {
  // Simulerer at koden (fejlagtigt, i strid med §3.5) er nedgraderet i
  // forhold til data en enhed allerede har gemt i en NYERE schema-version —
  // manuelt konstrueret, da mb_config_save_to_blob() altid tvinger den
  // AKTUELLE schema_version ind, så denne tilstand ikke kan opstaas normalt.
  mb_board_config_t future_config;
  mb_config_set_defaults(&future_config);
  future_config.schema_version = MB_CONFIG_SCHEMA_VERSION + 1;
  future_config.provisioned = true;
  future_config.checksum = mb_config_calc_checksum(&future_config);

  uint8_t blob[sizeof(mb_board_config_t)];
  memcpy(blob, &future_config, sizeof(blob));

  mb_board_config_t loaded;
  mb_config_load_from_blob(blob, sizeof(blob), &loaded);
  TEST_ASSERT_FALSE_MESSAGE(loaded.provisioned, "en fremtidig/ukendt schema-version blev fejlagtigt tolket");
  TEST_ASSERT_EQUAL(MB_CONFIG_SCHEMA_VERSION, loaded.schema_version);
}

// ---------------------------------------------------------------------------
// Token-hex-encoding
// ---------------------------------------------------------------------------

void test_token_from_random_bytes(void) {
  const uint8_t random_bytes[4] = {0xDE, 0xAD, 0xBE, 0xEF};
  char token[9];
  const bool ok = mb_config_token_from_random_bytes(random_bytes, sizeof(random_bytes), token, sizeof(token));
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING("deadbeef", token);
}

void test_token_from_random_bytes_rejects_undersized_output(void) {
  const uint8_t random_bytes[4] = {0xDE, 0xAD, 0xBE, 0xEF};
  char token[4];  // kraever mindst 9 (4*2+1)
  TEST_ASSERT_FALSE(mb_config_token_from_random_bytes(random_bytes, sizeof(random_bytes), token, sizeof(token)));
}

// ---------------------------------------------------------------------------
// Overførsel fra mb_provisioning_state_t
// ---------------------------------------------------------------------------

void test_apply_provisioning_state_transfers_fields(void) {
  mb_provisioning_state_t state;
  mb_provisioning_state_init(&state);
  strncpy(state.ssid, "TransferNet", sizeof(state.ssid) - 1);
  state.has_ssid = true;
  strncpy(state.plc_ip, "10.0.0.1", sizeof(state.plc_ip) - 1);
  state.has_plc_ip = true;
  strncpy(state.rest_user, "admin", sizeof(state.rest_user) - 1);
  state.has_rest_user = true;

  mb_board_config_t config;
  mb_config_set_defaults(&config);
  mb_config_apply_provisioning_state(&config, &state);

  TEST_ASSERT_EQUAL_STRING("TransferNet", config.wifi_ssid);
  TEST_ASSERT_TRUE(config.wifi_has_ssid);
  TEST_ASSERT_EQUAL_STRING("10.0.0.1", config.plc_ip);
  TEST_ASSERT_TRUE(config.has_plc_ip);
  TEST_ASSERT_EQUAL_STRING("admin", config.rest_user);
  TEST_ASSERT_TRUE(config.has_rest_user);
}

void test_apply_provisioning_state_never_touches_mgmt_token(void) {
  mb_provisioning_state_t state;
  mb_provisioning_state_init(&state);
  strncpy(state.ssid, "TransferNet", sizeof(state.ssid) - 1);
  state.has_ssid = true;

  mb_board_config_t config;
  mb_config_set_defaults(&config);
  strncpy(config.mgmt_token, "existing-token-not-to-be-touched", sizeof(config.mgmt_token) - 1);
  config.has_mgmt_token = true;

  mb_config_apply_provisioning_state(&config, &state);

  TEST_ASSERT_EQUAL_STRING("existing-token-not-to-be-touched", config.mgmt_token);
  TEST_ASSERT_TRUE(config.has_mgmt_token);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();

  RUN_TEST(test_defaults_are_unprovisioned);

  RUN_TEST(test_save_and_load_roundtrip);
  RUN_TEST(test_save_rejects_undersized_buffer);

  RUN_TEST(test_load_with_no_stored_data_gives_defaults);
  RUN_TEST(test_load_with_wrong_size_gives_defaults_not_garbage);
  RUN_TEST(test_load_detects_corruption_via_checksum);
  RUN_TEST(test_load_migrates_v1_blob_without_data_loss);
  RUN_TEST(test_load_rejects_corrupt_v1_blob);
  RUN_TEST(test_load_rejects_future_schema_version);

  RUN_TEST(test_token_from_random_bytes);
  RUN_TEST(test_token_from_random_bytes_rejects_undersized_output);

  RUN_TEST(test_apply_provisioning_state_transfers_fields);
  RUN_TEST(test_apply_provisioning_state_never_touches_mgmt_token);

  return UNITY_END();
}
