#include "ota_manager.h"

#include <Arduino.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#include "ota_validation.h"
#include "syslog_sender.h"

// Firmware-identitets-markøren (lib/ota_validation, MB_FWID_PREFIX). Skal
// findes i ethvert image POST /api/ota accepterer — derfor også i DENNE
// firmware, så den selv kan opdateres til en senere version. `used` +
// læsningen via en volatile pointer nedenfor forhindrer at compiler/linker
// folder den væk (den læses ellers kun ét sted).
extern "C" __attribute__((used)) const char g_mb_firmware_id[] = MB_FWID_PREFIX FW_VERSION "-b" FW_BUILD ";";

// Arduino-coren (esp32-hal-misc.c) godkender ellers automatisk en ny
// firmware ved opstart. true = "vi afgør det selv" (ota_manager_begin()).
extern "C" bool verifyRollbackLater() { return true; }

namespace {

volatile bool g_pending_confirm = false;
uint32_t g_confirm_deadline_ms = 0;
bool g_rolled_back = false;
char g_running_version[MB_FWID_VERSION_MAX_LEN + 1] = "";

void extract_running_version() {
  const char *volatile marker = g_mb_firmware_id;
  const char *start = marker + (sizeof(MB_FWID_PREFIX) - 1);
  size_t n = 0;
  while (start[n] != '\0' && start[n] != ';' && n < MB_FWID_VERSION_MAX_LEN) {
    g_running_version[n] = start[n];
    n++;
  }
  g_running_version[n] = '\0';
}

void confirm_deadline_task(void *param) {
  (void)param;
  while (g_pending_confirm) {
    if (static_cast<int32_t>(millis() - g_confirm_deadline_ms) >= 0) {
      syslog_logf(MB_SYSLOG_FACILITY_SYSTEM, 1,
                  "OTA: firmware %s blev IKKE bekraeftet inden %u s - ruller tilbage til forrige firmware",
                  g_running_version, static_cast<unsigned>(kOtaConfirmTimeoutS));
      Serial.println("OTA: ny firmware ikke bekraeftet i tide - ruller tilbage og genstarter.");
      vTaskDelay(pdMS_TO_TICKS(500));  // lad syslog-pakken/seriel-linjen naa ud
      esp_ota_mark_app_invalid_rollback_and_reboot();
      // Returnerer kun hvis der ingen gyldig forrige firmware er (fx kun
      // flashet via USB én gang) - behold saa den kørende, frem for intet.
      syslog_log(MB_SYSLOG_FACILITY_SYSTEM, 1, "OTA: rollback umulig (ingen gyldig forrige firmware) - beholder den koerende");
      g_pending_confirm = false;
      esp_ota_mark_app_valid_cancel_rollback();
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  vTaskDelete(nullptr);
}

}  // namespace

void ota_manager_begin() {
  extract_running_version();

  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  const bool pending = running != nullptr && esp_ota_get_state_partition(running, &state) == ESP_OK &&
                       state == ESP_OTA_IMG_PENDING_VERIFY;

  // En partition markeret ugyldig/afbrudt = den seneste OTA blev rullet
  // tilbage (enten af deadline-tasken nedenfor eller af bootloaderen efter
  // et crash under "afventer bekræftelse").
  g_rolled_back = esp_ota_get_last_invalid_partition() != nullptr;

  Serial.print("Firmware-identitet: ");
  Serial.println(g_running_version);

  if (pending) {
    g_confirm_deadline_ms = millis() + kOtaConfirmTimeoutS * 1000UL;
    g_pending_confirm = true;
    Serial.printf("OTA: ny firmware afventer bekraeftelse (POST /api/ota/confirm eller 'ota confirm') - "
                  "automatisk rollback om %u s\r\n",
                  static_cast<unsigned>(kOtaConfirmTimeoutS));
    syslog_logf(MB_SYSLOG_FACILITY_SYSTEM, 2, "OTA: firmware %s startet - afventer bekraeftelse inden %u s",
                g_running_version, static_cast<unsigned>(kOtaConfirmTimeoutS));
    xTaskCreate(confirm_deadline_task, "ota_confirm", 3072, nullptr, 1, nullptr);
  } else {
    // Samme som Arduino-coren ville have gjort uden verifyRollbackLater()-
    // overstyringen - ufarligt for en allerede gyldig firmware.
    esp_ota_mark_app_valid_cancel_rollback();
  }

  if (g_rolled_back) {
    Serial.println("OTA: den seneste opdatering blev rullet tilbage - koerer den forrige firmware.");
    syslog_logf(MB_SYSLOG_FACILITY_SYSTEM, 1, "OTA: seneste opdatering blev rullet tilbage - koerer %s",
                g_running_version);
  }
}

bool ota_manager_pending_confirm(uint32_t *remaining_s) {
  const bool pending = g_pending_confirm;
  if (remaining_s != nullptr) {
    *remaining_s = 0;
    if (pending) {
      const int32_t left_ms = static_cast<int32_t>(g_confirm_deadline_ms - millis());
      *remaining_s = left_ms > 0 ? static_cast<uint32_t>(left_ms) / 1000U : 0;
    }
  }
  return pending;
}

OtaConfirmResult ota_manager_confirm() {
  if (!g_pending_confirm) return OtaConfirmResult::kNothingPending;
  const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
  if (err != ESP_OK) {
    // Forbliv "afventer bekræftelse" - deadline-tasken ruller stadig
    // tilbage, hvis et nyt forsøg heller ikke lykkes.
    syslog_logf(MB_SYSLOG_FACILITY_SYSTEM, 1, "OTA: bekraeftelse af %s fejlede (esp_err=%d)", g_running_version,
                static_cast<int>(err));
    return OtaConfirmResult::kFailed;
  }
  g_pending_confirm = false;  // deadline-tasken afslutter sig selv
  syslog_logf(MB_SYSLOG_FACILITY_SYSTEM, 2, "OTA: firmware %s bekraeftet - rollback annulleret", g_running_version);
  return OtaConfirmResult::kConfirmed;
}

bool ota_manager_last_update_rolled_back() { return g_rolled_back; }

const char *ota_manager_running_version() { return g_running_version; }
