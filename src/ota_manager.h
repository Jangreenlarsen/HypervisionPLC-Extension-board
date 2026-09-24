#pragma once

#include <cstdint>

// v0.30.0 — tværgående OTA-tilstand (samme placering som config.cpp: bruges
// af BÅDE REST-laget (ota_handler.cpp) og den serielle CLI (provisioning.cpp),
// så de to front-ends aldrig kalder hinanden på tværs).
//
// Bekræftelse + automatisk rollback: bootloaderen er bygget med
// CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE, og denne fil overstyrer Arduino-
// corens verifyRollbackLater(), så en NY firmware (første opstart efter OTA,
// ESP_OTA_IMG_PENDING_VERIFY) IKKE automatisk godkendes. Den skal bekræftes
// via ota_manager_confirm() (POST /api/ota/confirm eller CLI "ota confirm")
// inden kOtaConfirmTimeoutS — ellers ruller boardet selv tilbage til den
// forrige firmware. Crasher/genstarter den nye firmware før bekræftelse,
// ruller bootloaderen også tilbage.

constexpr uint32_t kOtaConfirmTimeoutS = 600;

// Kaldes tidligt i setup() (efter syslog_sender_begin()). Afgør om den
// kørende firmware afventer bekræftelse (starter i så fald deadline-tasken),
// og om den seneste OTA blev rullet tilbage.
void ota_manager_begin();

// true hvis den kørende firmware afventer bekræftelse; `remaining_s` (valgfri)
// får antal sekunder til automatisk rollback.
bool ota_manager_pending_confirm(uint32_t *remaining_s);

// Bekræfter den kørende firmware. kNothingPending er IKKE en fejl (idempotent
// - firmwaren var allerede bekræftet); kFailed = esp_ota_mark_app_valid_...()
// fejlede, firmwaren afventer stadig bekræftelse.
enum class OtaConfirmResult : uint8_t { kConfirmed, kNothingPending, kFailed };
OtaConfirmResult ota_manager_confirm();

// true hvis den seneste OTA-opdatering blev rullet tilbage (ubekræftet eller crash).
bool ota_manager_last_update_rolled_back();

// Den kørende firmwares identitets-version, fx "0.30.0-b0047" (se lib/ota_validation).
const char *ota_manager_running_version();
