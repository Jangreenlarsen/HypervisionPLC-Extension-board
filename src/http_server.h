#pragma once

// REST management-API (EXPANSION_BOARD_DESIGN.md §4.2), port 8080. Startes
// ÉN gang efter en vellykket WiFi-forbindelse (kaldes fra
// src/provisioning.cpp's attempt_connect()). Bruger ESP-IDF's
// esp_http_server, samme stil som PLC-repoets http_server.h/cpp-plan.
void http_server_begin();
