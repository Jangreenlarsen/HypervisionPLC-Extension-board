#include "config.h"

#include <Preferences.h>
#include <cstring>
#include <esp_random.h>

namespace {

constexpr const char *kNamespace = "boardcfg";
constexpr const char *kBlobKey = "blob";

Preferences g_prefs;
mb_board_config_t g_config;

void save_current_config() {
  uint8_t blob[sizeof(mb_board_config_t)];
  const size_t len = mb_config_save_to_blob(&g_config, blob, sizeof(blob));
  if (len == 0) return;  // kan ikke ske i praksis — blob er praecis stor nok

  g_prefs.begin(kNamespace, /*readOnly=*/false);
  g_prefs.putBytes(kBlobKey, blob, len);
  g_prefs.end();
}

}  // namespace

void config_begin() {
  mb_config_set_defaults(&g_config);

  // readOnly=false ved begin(): et read-only begin() på et namespace der
  // endnu ikke findes (fabriksnyt board, aldrig gemt noget) logger en
  // ESP-IDF-fejl (NOT_FOUND) — read-write opretter det stille i stedet,
  // uden at noget rent faktisk skrives før et eksplicit putBytes()-kald.
  g_prefs.begin(kNamespace, /*readOnly=*/false);
  const size_t stored_len = g_prefs.isKey(kBlobKey) ? g_prefs.getBytesLength(kBlobKey) : 0;

  if (stored_len > 0 && stored_len <= sizeof(mb_board_config_t)) {
    uint8_t blob[sizeof(mb_board_config_t)];
    g_prefs.getBytes(kBlobKey, blob, stored_len);
    g_prefs.end();
    mb_config_load_from_blob(blob, stored_len, &g_config);
  } else {
    g_prefs.end();
    // stored_len == 0 (intet gemt endnu) ELLER > sizeof(...) (skulle ikke
    // kunne ske — defensivt, samme "ukendt data -> defaults"-princip som
    // mb_config_load_from_blob selv anvender for forkerte størrelser).
    mb_config_set_defaults(&g_config);
  }
}

const mb_board_config_t &config_get() { return g_config; }

void config_apply_and_save(const mb_provisioning_state_t *state) {
  mb_config_apply_provisioning_state(&g_config, state);
  save_current_config();
}

void config_factory_reset() {
  g_prefs.begin(kNamespace, /*readOnly=*/false);
  g_prefs.clear();
  g_prefs.end();
  mb_config_set_defaults(&g_config);
}

void config_ensure_mgmt_token(char *out_token, size_t out_capacity) {
  if (!g_config.has_mgmt_token) {
    uint8_t random_bytes[MB_MGMT_TOKEN_LEN / 2];
    esp_fill_random(random_bytes, sizeof(random_bytes));
    if (mb_config_token_from_random_bytes(random_bytes, sizeof(random_bytes), g_config.mgmt_token,
                                           sizeof(g_config.mgmt_token))) {
      g_config.has_mgmt_token = true;
    }
    save_current_config();
  }

  if (out_token != nullptr && out_capacity > 0) {
    strncpy(out_token, g_config.mgmt_token, out_capacity - 1);
    out_token[out_capacity - 1] = '\0';
  }
}

void config_ensure_eth_mac(uint8_t *out_mac) {
  if (!g_config.has_eth_mac) {
    uint8_t random_bytes[6];
    esp_fill_random(random_bytes, sizeof(random_bytes));
    if (mb_config_mac_from_random_bytes(random_bytes, sizeof(random_bytes), g_config.eth_mac)) {
      g_config.has_eth_mac = true;
    }
    save_current_config();
  }

  if (out_mac != nullptr) {
    memcpy(out_mac, g_config.eth_mac, sizeof(g_config.eth_mac));
  }
}

void config_mark_provisioned() {
  g_config.provisioned = true;
  save_current_config();
}

void config_set_channel(size_t index, const mb_channel_config_t &cfg) {
  if (index >= MB_CHANNEL_COUNT) return;
  g_config.channel[index] = cfg;
  save_current_config();
}
