#pragma once

#include <cstddef>
#include <cstdint>

#include "modbus_pdu.h"

// Lag 2 (ARCHITECTURE.md) — kanal-eksekvering. ÉN FreeRTOS-task pr. kanal
// (§3.2), egen kø, ingen delt tilstand mellem kanaler. GPIO-allokering:
// EXPANSION_BOARD_DESIGN.md §2.0.1.
enum class ModbusChannelId : uint8_t { kA = 0, kB = 1 };

// Opsætter begge kanalers UART/GPIO og starter deres FreeRTOS-tasks. Kaldes
// én gang ved boot (fra main.cpp), uafhængigt af WiFi-status — kanalerne er
// klar til brug så snart Modbus TCP-serveren selv starter.
void modbus_channel_init_all();

// Den ENESTE tilladte indgang fra Lag 1 (netværk/protokol) til Lag 2 —
// lægger forespørgslen i kanalens kø og venter (blokerende, med timeout) på
// resultatet, jf. ARCHITECTURE.md regel 2: netværks-laget må aldrig selv
// røre UART'en. `pdu` er requestets PDU (function code + data, UDEN
// RTU-adresse/CRC — de tilføjes internt). `out_pdu` modtager svarets PDU —
// også ved en Modbus-exception (§4's exception-format er stadig en gyldig
// PDU at relaye, ikke en gateway-fejl i sig selv).
mb_error_code_t modbus_channel_submit(ModbusChannelId channel, uint8_t slave_id, const uint8_t *pdu, size_t pdu_len,
                                       uint8_t *out_pdu, size_t *out_pdu_len, size_t out_pdu_capacity);
