#pragma once

#include <esp_http_server.h>

// §4.2: firmware-opdatering, dual-partition. Registrerer POST /api/ota
// (rå binær body, IKKE multipart — samme `--data-binary @firmware.bin`-
// mønster som referenceimplementeringen i reference-plc-source/src/
// ota_handler.cpp), GET /api/ota/status og POST /api/reboot på en allerede-
// startet httpd-server. Kaldes fra src/http_server.cpp::http_server_begin().
//
// Bevidst UDEN reference-implementeringens GitHub-Releases-baserede
// auto-opdatering — det er en betydelig ekstra angrebsflade og
// kompleksitet (TLS-klient, CA-bundling, baggrundstasks) som hverken
// EXPANSION_BOARD_DESIGN.md §4.2 kræver eller Fase 5's scope dækker.
void ota_handler_register(httpd_handle_t server);
