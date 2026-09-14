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

void test_defaults_have_wifi_and_eth_enabled(void) {
  // v0.20.0/v0.21.0: matcher den hidtidige, ubetingede adfærd FØR
  // enable/disable fandtes for begge interfaces.
  mb_board_config_t config;
  mb_config_set_defaults(&config);
  TEST_ASSERT_TRUE(config.wifi_enabled);
  TEST_ASSERT_TRUE(config.eth_enabled);
  // v0.22.0: default = intet eksplicit hostname (auto-genereret bruges).
  TEST_ASSERT_FALSE(config.has_hostname);
}

void test_defaults_include_sane_channel_config(void) {
  // §4.2: defaults skal matche v0.9.0's tidligere HARDKODEDE adfaerd i
  // modbus_channel.cpp (9600 baud, RS485, 500 ms timeout) - ingen
  // eksisterende board maa aendre adfaerd blot ved schema-migrationen.
  mb_board_config_t config;
  mb_config_set_defaults(&config);
  for (size_t i = 0; i < MB_CHANNEL_COUNT; i++) {
    TEST_ASSERT_TRUE(config.channel[i].enabled);
    TEST_ASSERT_EQUAL(MB_CHANNEL_MODE_RS485, config.channel[i].mode);
    TEST_ASSERT_EQUAL_UINT32(9600, config.channel[i].baudrate);
    TEST_ASSERT_EQUAL(MB_CHANNEL_PARITY_NONE, config.channel[i].parity);
    TEST_ASSERT_EQUAL_UINT8(1, config.channel[i].stop_bits);
    TEST_ASSERT_EQUAL_UINT32(500, config.channel[i].timeout_ms);
    TEST_ASSERT_EQUAL_UINT32(0, config.channel[i].inter_frame_delay_ms);
  }
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

void test_save_and_load_roundtrip_channel_config(void) {
  mb_board_config_t config;
  mb_config_set_defaults(&config);
  config.channel[0].enabled = false;
  config.channel[0].mode = MB_CHANNEL_MODE_RS232;
  config.channel[0].baudrate = 19200;
  config.channel[0].parity = MB_CHANNEL_PARITY_EVEN;
  config.channel[0].stop_bits = 2;
  config.channel[0].timeout_ms = 1000;
  config.channel[0].inter_frame_delay_ms = 20;

  uint8_t blob[sizeof(mb_board_config_t)];
  const size_t written = mb_config_save_to_blob(&config, blob, sizeof(blob));

  mb_board_config_t loaded;
  mb_config_load_from_blob(blob, written, &loaded);

  TEST_ASSERT_FALSE(loaded.channel[0].enabled);
  TEST_ASSERT_EQUAL(MB_CHANNEL_MODE_RS232, loaded.channel[0].mode);
  TEST_ASSERT_EQUAL_UINT32(19200, loaded.channel[0].baudrate);
  TEST_ASSERT_EQUAL(MB_CHANNEL_PARITY_EVEN, loaded.channel[0].parity);
  TEST_ASSERT_EQUAL_UINT8(2, loaded.channel[0].stop_bits);
  TEST_ASSERT_EQUAL_UINT32(1000, loaded.channel[0].timeout_ms);
  TEST_ASSERT_EQUAL_UINT32(20, loaded.channel[0].inter_frame_delay_ms);
  // Kanal B er urørt - skal stadig staa paa defaults.
  TEST_ASSERT_TRUE(loaded.channel[1].enabled);
  TEST_ASSERT_EQUAL(MB_CHANNEL_MODE_RS485, loaded.channel[1].mode);
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
  TEST_ASSERT_TRUE_MESSAGE(migrated.channel[0].enabled, "schema-3-felt (channel[]) skal faa sin default via v1->v2->v3-kaeden");
  TEST_ASSERT_EQUAL(MB_CHANNEL_MODE_RS485, migrated.channel[0].mode);
  TEST_ASSERT_EQUAL_UINT32(9600, migrated.channel[0].baudrate);
}

void test_load_migrates_v2_blob_without_data_loss(void) {
  // Samme situation som v1-testen ovenfor, men for et board der allerede
  // naaede at opgradere til schema 2 (v0.8.0) foer schema 3 (kanal-config)
  // fandtes.
  mb_board_config_v2_t v2{};
  v2.schema_version = 2;
  v2.provisioned = true;
  strncpy(v2.wifi_ssid, "V2Network", sizeof(v2.wifi_ssid) - 1);
  v2.wifi_has_ssid = true;
  strncpy(v2.plc_ip, "10.1.1.153", sizeof(v2.plc_ip) - 1);
  v2.has_plc_ip = true;
  strncpy(v2.mgmt_token, "f4353305b96b5f1e61e5e70b49b18e1c", sizeof(v2.mgmt_token) - 1);
  v2.has_mgmt_token = true;
  strncpy(v2.rest_user, "testadmin", sizeof(v2.rest_user) - 1);
  v2.has_rest_user = true;
  strncpy(v2.rest_pass, "RestApiTest123", sizeof(v2.rest_pass) - 1);
  v2.has_rest_pass = true;
  v2.rest_auth_mode = MB_REST_AUTH_MODE_TOKEN_ONLY;
  v2.checksum = mb_config_calc_checksum_v2(&v2);

  mb_board_config_t migrated;
  mb_config_load_from_blob(reinterpret_cast<const uint8_t *>(&v2), sizeof(v2), &migrated);

  TEST_ASSERT_EQUAL_MESSAGE(MB_CONFIG_SCHEMA_VERSION, migrated.schema_version,
                             "migreret config skal have den AKTUELLE schema-version, ikke 2");
  TEST_ASSERT_TRUE(migrated.provisioned);
  TEST_ASSERT_EQUAL_STRING("V2Network", migrated.wifi_ssid);
  TEST_ASSERT_EQUAL_STRING("10.1.1.153", migrated.plc_ip);
  TEST_ASSERT_EQUAL_STRING("f4353305b96b5f1e61e5e70b49b18e1c", migrated.mgmt_token);
  TEST_ASSERT_EQUAL_STRING("testadmin", migrated.rest_user);
  TEST_ASSERT_EQUAL_STRING("RestApiTest123", migrated.rest_pass);
  TEST_ASSERT_EQUAL_MESSAGE(MB_REST_AUTH_MODE_TOKEN_ONLY, migrated.rest_auth_mode,
                             "eksisterende v2-felt maa ikke tabes/overskrives under migration");
  TEST_ASSERT_TRUE_MESSAGE(migrated.channel[0].enabled, "nyt schema-3-felt skal faa sin default");
  TEST_ASSERT_EQUAL_UINT32(9600, migrated.channel[0].baudrate);
  TEST_ASSERT_EQUAL_UINT32(9600, migrated.channel[1].baudrate);
}

void test_load_migrates_v3_blob_without_data_loss(void) {
  // v0.20.0: schema 4 tilfoejede Ethernet enable/disable + static-IP-config
  // - et board der naaede at opgradere til schema 3 (v0.10.0, kanal-config)
  // foer dette maa ikke tabe sin eksisterende config under migrationen.
  mb_board_config_v3_t v3{};
  v3.schema_version = 3;
  v3.provisioned = true;
  strncpy(v3.wifi_ssid, "V3Network", sizeof(v3.wifi_ssid) - 1);
  v3.wifi_has_ssid = true;
  strncpy(v3.plc_ip, "10.1.1.153", sizeof(v3.plc_ip) - 1);
  v3.has_plc_ip = true;
  strncpy(v3.mgmt_token, "f4353305b96b5f1e61e5e70b49b18e1c", sizeof(v3.mgmt_token) - 1);
  v3.has_mgmt_token = true;
  v3.rest_auth_mode = MB_REST_AUTH_MODE_BASIC_ONLY;
  v3.channel[0].enabled = true;
  v3.channel[0].baudrate = 19200;
  v3.channel[1].enabled = false;
  v3.channel[1].baudrate = 9600;
  v3.checksum = mb_config_calc_checksum_v3(&v3);

  mb_board_config_t migrated;
  mb_config_load_from_blob(reinterpret_cast<const uint8_t *>(&v3), sizeof(v3), &migrated);

  TEST_ASSERT_EQUAL_MESSAGE(MB_CONFIG_SCHEMA_VERSION, migrated.schema_version,
                             "migreret config skal have den AKTUELLE schema-version, ikke 3");
  TEST_ASSERT_TRUE(migrated.provisioned);
  TEST_ASSERT_EQUAL_STRING("V3Network", migrated.wifi_ssid);
  TEST_ASSERT_EQUAL_STRING("10.1.1.153", migrated.plc_ip);
  TEST_ASSERT_EQUAL_STRING("f4353305b96b5f1e61e5e70b49b18e1c", migrated.mgmt_token);
  TEST_ASSERT_EQUAL_MESSAGE(MB_REST_AUTH_MODE_BASIC_ONLY, migrated.rest_auth_mode,
                             "eksisterende v3-felt maa ikke tabes/overskrives under migration");
  TEST_ASSERT_EQUAL_UINT32(19200, migrated.channel[0].baudrate);
  TEST_ASSERT_FALSE_MESSAGE(migrated.channel[1].enabled,
                             "eksisterende schema-3 kanal-config maa ikke tabes under migration");
  TEST_ASSERT_TRUE_MESSAGE(migrated.eth_enabled,
                            "nyt schema-4-felt skal faa default 'true' (matcher hidtidig ubetinget adfaerd)");
  TEST_ASSERT_FALSE_MESSAGE(migrated.eth_static_ip, "nyt schema-4-felt skal faa default 'dhcp'");
}

void test_load_migrates_v4_blob_without_data_loss(void) {
  // v0.21.0: schema 5 tilfoejede wifi_enabled - et board der naaede at
  // opgradere til schema 4 (v0.20.0, Ethernet-config+MAC) foer dette maa
  // ikke tabe sin eksisterende config (INKL. den persisterede MAC) under
  // migrationen.
  mb_board_config_v4_t v4{};
  v4.schema_version = 4;
  v4.provisioned = true;
  strncpy(v4.wifi_ssid, "V4Network", sizeof(v4.wifi_ssid) - 1);
  v4.wifi_has_ssid = true;
  strncpy(v4.plc_ip, "10.1.1.153", sizeof(v4.plc_ip) - 1);
  v4.has_plc_ip = true;
  v4.eth_enabled = false;
  v4.eth_static_ip = true;
  strncpy(v4.eth_ip, "10.1.1.199", sizeof(v4.eth_ip) - 1);
  const uint8_t existing_mac[6] = {0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
  memcpy(v4.eth_mac, existing_mac, sizeof(v4.eth_mac));
  v4.has_eth_mac = true;
  v4.channel[0].enabled = true;
  v4.channel[0].baudrate = 19200;
  v4.checksum = mb_config_calc_checksum_v4(&v4);

  mb_board_config_t migrated;
  mb_config_load_from_blob(reinterpret_cast<const uint8_t *>(&v4), sizeof(v4), &migrated);

  TEST_ASSERT_EQUAL_MESSAGE(MB_CONFIG_SCHEMA_VERSION, migrated.schema_version,
                             "migreret config skal have den AKTUELLE schema-version, ikke 4");
  TEST_ASSERT_TRUE(migrated.provisioned);
  TEST_ASSERT_EQUAL_STRING("V4Network", migrated.wifi_ssid);
  TEST_ASSERT_FALSE_MESSAGE(migrated.eth_enabled, "eksisterende v4-felt maa ikke tabes/overskrives under migration");
  TEST_ASSERT_TRUE(migrated.eth_static_ip);
  TEST_ASSERT_EQUAL_STRING("10.1.1.199", migrated.eth_ip);
  TEST_ASSERT_TRUE_MESSAGE(migrated.has_eth_mac, "en allerede-genereret MAC maa ALDRIG glemmes/gen-genereres");
  TEST_ASSERT_EQUAL_UINT8_ARRAY(existing_mac, migrated.eth_mac, 6);
  TEST_ASSERT_EQUAL_UINT32(19200, migrated.channel[0].baudrate);
  TEST_ASSERT_TRUE_MESSAGE(migrated.wifi_enabled,
                            "nyt schema-5-felt skal faa default 'true' (matcher hidtidig ubetinget adfaerd)");
}

void test_load_rejects_corrupt_v4_blob(void) {
  mb_board_config_v4_t v4{};
  v4.schema_version = 4;
  v4.provisioned = true;
  strncpy(v4.wifi_ssid, "V4Network", sizeof(v4.wifi_ssid) - 1);
  v4.checksum = mb_config_calc_checksum_v4(&v4);

  uint8_t blob[sizeof(v4)];
  memcpy(blob, &v4, sizeof(blob));
  blob[10] ^= 0xFF;

  mb_board_config_t loaded;
  mb_config_load_from_blob(blob, sizeof(blob), &loaded);
  TEST_ASSERT_FALSE_MESSAGE(loaded.provisioned, "korrupt v4-blob blev fejlagtigt migreret");
  TEST_ASSERT_EQUAL(MB_CONFIG_SCHEMA_VERSION, loaded.schema_version);
}

void test_load_migrates_v5_blob_without_data_loss(void) {
  // v0.22.0: schema 6 tilfoejede hostname/has_hostname - et board der
  // naaede at opgradere til schema 5 (v0.21.0, wifi_enabled) foer dette maa
  // ikke tabe sin eksisterende config under migrationen.
  mb_board_config_v5_t v5{};
  v5.schema_version = 5;
  v5.provisioned = true;
  v5.wifi_enabled = false;
  strncpy(v5.wifi_ssid, "V5Network", sizeof(v5.wifi_ssid) - 1);
  v5.wifi_has_ssid = true;
  v5.eth_enabled = true;
  const uint8_t existing_mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
  memcpy(v5.eth_mac, existing_mac, sizeof(v5.eth_mac));
  v5.has_eth_mac = true;
  v5.checksum = mb_config_calc_checksum_v5(&v5);

  mb_board_config_t migrated;
  mb_config_load_from_blob(reinterpret_cast<const uint8_t *>(&v5), sizeof(v5), &migrated);

  TEST_ASSERT_EQUAL_MESSAGE(MB_CONFIG_SCHEMA_VERSION, migrated.schema_version,
                             "migreret config skal have den AKTUELLE schema-version, ikke 5");
  TEST_ASSERT_TRUE(migrated.provisioned);
  TEST_ASSERT_FALSE_MESSAGE(migrated.wifi_enabled, "eksisterende v5-felt maa ikke tabes/overskrives under migration");
  TEST_ASSERT_EQUAL_STRING("V5Network", migrated.wifi_ssid);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(existing_mac, migrated.eth_mac, 6);
  TEST_ASSERT_FALSE_MESSAGE(migrated.has_hostname,
                             "nyt schema-6-felt skal faa default 'false' (auto-genereret hostname)");
}

void test_load_rejects_corrupt_v5_blob(void) {
  mb_board_config_v5_t v5{};
  v5.schema_version = 5;
  v5.provisioned = true;
  strncpy(v5.wifi_ssid, "V5Network", sizeof(v5.wifi_ssid) - 1);
  v5.checksum = mb_config_calc_checksum_v5(&v5);

  uint8_t blob[sizeof(v5)];
  memcpy(blob, &v5, sizeof(blob));
  blob[10] ^= 0xFF;

  mb_board_config_t loaded;
  mb_config_load_from_blob(blob, sizeof(blob), &loaded);
  TEST_ASSERT_FALSE_MESSAGE(loaded.provisioned, "korrupt v5-blob blev fejlagtigt migreret");
  TEST_ASSERT_EQUAL(MB_CONFIG_SCHEMA_VERSION, loaded.schema_version);
}

void test_load_rejects_corrupt_v3_blob(void) {
  mb_board_config_v3_t v3{};
  v3.schema_version = 3;
  v3.provisioned = true;
  strncpy(v3.wifi_ssid, "V3Network", sizeof(v3.wifi_ssid) - 1);
  v3.checksum = mb_config_calc_checksum_v3(&v3);

  uint8_t blob[sizeof(v3)];
  memcpy(blob, &v3, sizeof(blob));
  blob[10] ^= 0xFF;

  mb_board_config_t loaded;
  mb_config_load_from_blob(blob, sizeof(blob), &loaded);
  TEST_ASSERT_FALSE_MESSAGE(loaded.provisioned, "korrupt v3-blob blev fejlagtigt migreret");
  TEST_ASSERT_EQUAL(MB_CONFIG_SCHEMA_VERSION, loaded.schema_version);
}

void test_load_rejects_corrupt_v2_blob(void) {
  mb_board_config_v2_t v2{};
  v2.schema_version = 2;
  v2.provisioned = true;
  strncpy(v2.wifi_ssid, "V2Network", sizeof(v2.wifi_ssid) - 1);
  v2.checksum = mb_config_calc_checksum_v2(&v2);

  uint8_t blob[sizeof(v2)];
  memcpy(blob, &v2, sizeof(blob));
  blob[10] ^= 0xFF;

  mb_board_config_t loaded;
  mb_config_load_from_blob(blob, sizeof(blob), &loaded);
  TEST_ASSERT_FALSE_MESSAGE(loaded.provisioned, "korrupt v2-blob blev fejlagtigt migreret");
  TEST_ASSERT_EQUAL(MB_CONFIG_SCHEMA_VERSION, loaded.schema_version);
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
// MAC-generering (v0.20.0)
// ---------------------------------------------------------------------------

void test_mac_from_random_bytes_sets_unicast_and_locally_administered_bits(void) {
  // 0x01 haver multicast-bit sat, 0x00 (2. bit) har lokalt-administreret-bit
  // ryddet - begge skal fikses af mb_config_mac_from_random_bytes(), uanset
  // hvad de "tilfaeldige" input-bytes tilfaeldigvis indeholder.
  const uint8_t random_bytes[6] = {0x01, 0xAD, 0xBE, 0xEF, 0x12, 0x34};
  uint8_t mac[6];
  TEST_ASSERT_TRUE(mb_config_mac_from_random_bytes(random_bytes, sizeof(random_bytes), mac));
  TEST_ASSERT_EQUAL_HEX8(0x02, mac[0]);  // (0x01 & 0xFE) | 0x02 = 0x00 | 0x02 = 0x02
  TEST_ASSERT_EQUAL_HEX8(0xAD, mac[1]);
  TEST_ASSERT_EQUAL_HEX8(0xBE, mac[2]);
  TEST_ASSERT_EQUAL_HEX8(0xEF, mac[3]);
  TEST_ASSERT_EQUAL_HEX8(0x12, mac[4]);
  TEST_ASSERT_EQUAL_HEX8(0x34, mac[5]);
  TEST_ASSERT_EQUAL_MESSAGE(0, mac[0] & 0x01, "unicast-bit (LSB) skal vaere 0");
  TEST_ASSERT_EQUAL_MESSAGE(0x02, mac[0] & 0x02, "lokalt-administreret-bit skal vaere 1");
}

void test_mac_from_random_bytes_rejects_undersized_input(void) {
  const uint8_t random_bytes[5] = {1, 2, 3, 4, 5};  // kun 5 - kraever 6
  uint8_t mac[6];
  TEST_ASSERT_FALSE(mb_config_mac_from_random_bytes(random_bytes, sizeof(random_bytes), mac));
}

// ---------------------------------------------------------------------------
// Hostname-generering (v0.22.0)
// ---------------------------------------------------------------------------

void test_build_hostname_auto_uses_last_3_mac_bytes(void) {
  const uint8_t mac[6] = {0x02, 0xAA, 0xBB, 0x6A, 0x8C, 0xAE};
  char out[40];
  mb_config_build_hostname(false, "", mac, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("hypervision-ext-6A8CAE", out);
}

void test_build_hostname_uses_explicit_override(void) {
  const uint8_t mac[6] = {0x02, 0xAA, 0xBB, 0x6A, 0x8C, 0xAE};
  char out[40];
  mb_config_build_hostname(true, "my-custom-name", mac, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("my-custom-name", out);
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
  state.eth_enabled = false;
  state.eth_static_ip = true;
  strncpy(state.eth_ip, "10.0.0.50", sizeof(state.eth_ip) - 1);
  strncpy(state.eth_mask, "255.255.255.0", sizeof(state.eth_mask) - 1);
  strncpy(state.eth_gw, "10.0.0.1", sizeof(state.eth_gw) - 1);
  state.wifi_enabled = false;
  state.has_hostname = true;
  strncpy(state.hostname, "custom-name", sizeof(state.hostname) - 1);

  mb_board_config_t config;
  mb_config_set_defaults(&config);
  mb_config_apply_provisioning_state(&config, &state);

  TEST_ASSERT_EQUAL_STRING("TransferNet", config.wifi_ssid);
  TEST_ASSERT_TRUE(config.wifi_has_ssid);
  TEST_ASSERT_EQUAL_STRING("10.0.0.1", config.plc_ip);
  TEST_ASSERT_TRUE(config.has_plc_ip);
  TEST_ASSERT_EQUAL_STRING("admin", config.rest_user);
  TEST_ASSERT_TRUE(config.has_rest_user);
  TEST_ASSERT_FALSE(config.eth_enabled);
  TEST_ASSERT_TRUE(config.eth_static_ip);
  TEST_ASSERT_EQUAL_STRING("10.0.0.50", config.eth_ip);
  TEST_ASSERT_EQUAL_STRING("255.255.255.0", config.eth_mask);
  TEST_ASSERT_EQUAL_STRING("10.0.0.1", config.eth_gw);
  TEST_ASSERT_FALSE(config.wifi_enabled);
  TEST_ASSERT_TRUE(config.has_hostname);
  TEST_ASSERT_EQUAL_STRING("custom-name", config.hostname);
}

void test_to_provisioning_state_roundtrips_eth_fields(void) {
  // Den omvendte overfoersel (bruges ved boot til at genopbygge state fra
  // persisteret config, §3.4.1) - v0.20.0's eth-felter skal med, ellers ville
  // en genstart stille "glemme" en gemt "eth disable"/static-IP-config.
  mb_board_config_t config;
  mb_config_set_defaults(&config);
  config.eth_enabled = false;
  config.eth_static_ip = true;
  strncpy(config.eth_ip, "192.168.5.9", sizeof(config.eth_ip) - 1);
  strncpy(config.eth_mask, "255.255.0.0", sizeof(config.eth_mask) - 1);
  strncpy(config.eth_gw, "192.168.5.1", sizeof(config.eth_gw) - 1);
  config.wifi_enabled = false;
  config.has_hostname = true;
  strncpy(config.hostname, "custom-name", sizeof(config.hostname) - 1);

  mb_provisioning_state_t state;
  mb_config_to_provisioning_state(&config, &state);

  TEST_ASSERT_FALSE(state.eth_enabled);
  TEST_ASSERT_TRUE(state.eth_static_ip);
  TEST_ASSERT_EQUAL_STRING("192.168.5.9", state.eth_ip);
  TEST_ASSERT_EQUAL_STRING("255.255.0.0", state.eth_mask);
  TEST_ASSERT_EQUAL_STRING("192.168.5.1", state.eth_gw);
  TEST_ASSERT_FALSE(state.wifi_enabled);
  TEST_ASSERT_TRUE(state.has_hostname);
  TEST_ASSERT_EQUAL_STRING("custom-name", state.hostname);
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
  RUN_TEST(test_defaults_have_wifi_and_eth_enabled);
  RUN_TEST(test_defaults_include_sane_channel_config);

  RUN_TEST(test_save_and_load_roundtrip);
  RUN_TEST(test_save_and_load_roundtrip_channel_config);
  RUN_TEST(test_save_rejects_undersized_buffer);

  RUN_TEST(test_load_with_no_stored_data_gives_defaults);
  RUN_TEST(test_load_with_wrong_size_gives_defaults_not_garbage);
  RUN_TEST(test_load_detects_corruption_via_checksum);
  RUN_TEST(test_load_migrates_v1_blob_without_data_loss);
  RUN_TEST(test_load_rejects_corrupt_v1_blob);
  RUN_TEST(test_load_migrates_v2_blob_without_data_loss);
  RUN_TEST(test_load_rejects_corrupt_v2_blob);
  RUN_TEST(test_load_migrates_v3_blob_without_data_loss);
  RUN_TEST(test_load_rejects_corrupt_v3_blob);
  RUN_TEST(test_load_migrates_v4_blob_without_data_loss);
  RUN_TEST(test_load_rejects_corrupt_v4_blob);
  RUN_TEST(test_load_migrates_v5_blob_without_data_loss);
  RUN_TEST(test_load_rejects_corrupt_v5_blob);
  RUN_TEST(test_load_rejects_future_schema_version);

  RUN_TEST(test_token_from_random_bytes);
  RUN_TEST(test_token_from_random_bytes_rejects_undersized_output);
  RUN_TEST(test_mac_from_random_bytes_sets_unicast_and_locally_administered_bits);
  RUN_TEST(test_mac_from_random_bytes_rejects_undersized_input);
  RUN_TEST(test_build_hostname_auto_uses_last_3_mac_bytes);
  RUN_TEST(test_build_hostname_uses_explicit_override);

  RUN_TEST(test_apply_provisioning_state_transfers_fields);
  RUN_TEST(test_apply_provisioning_state_never_touches_mgmt_token);
  RUN_TEST(test_to_provisioning_state_roundtrips_eth_fields);

  return UNITY_END();
}
