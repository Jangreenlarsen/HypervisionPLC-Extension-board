#pragma once

// Modbus TCP data-plan (EXPANSION_BOARD_DESIGN.md §4.1) — Variant A: 2 porte
// (502=kanal A, 503=kanal B). Starter to FreeRTOS-tasks (én pr. port), kaldes
// fra src/provisioning.cpp's attempt_connect() efter en vellykket WiFi-
// forbindelse (dataplanet er meningsløst uden netværk).
void modbus_tcp_server_begin();
