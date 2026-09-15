#pragma once

#include <cstddef>
#include <cstdint>

#include "board_config.h"
#include "channel_config.h"
#include "modbus_pdu.h"

// Lag 2 (ARCHITECTURE.md) — kanal-eksekvering. ÉN FreeRTOS-task pr. kanal
// (§3.2), egen kø, ingen delt tilstand mellem kanaler. GPIO-allokering:
// EXPANSION_BOARD_DESIGN.md §2.0.1.
enum class ModbusChannelId : uint8_t { kA = 0, kB = 1 };

// Opsætter begge kanalers UART/GPIO (fra persisteret config, §4.2) og
// starter deres FreeRTOS-tasks. Kaldes én gang ved boot (fra main.cpp),
// uafhængigt af WiFi-status — kanalerne er klar til brug så snart Modbus
// TCP-serveren selv starter.
void modbus_channel_init_all();

// Den ENESTE tilladte indgang fra Lag 1 (netværk/protokol) til Lag 2 —
// lægger forespørgslen i kanalens kø og venter (blokerende, med timeout) på
// resultatet, jf. ARCHITECTURE.md regel 2: netværks-laget må aldrig selv
// røre UART'en. `pdu` er requestets PDU (function code + data, UDEN
// RTU-adresse/CRC — de tilføjes internt). `out_pdu` modtager svarets PDU —
// også ved en Modbus-exception (§4's exception-format er stadig en gyldig
// PDU at relaye, ikke en gateway-fejl i sig selv). Returnerer
// `MB_NOT_ENABLED` uden at røre UART'en hvis kanalen er deaktiveret
// (`enabled:false`, §4.2).
mb_error_code_t modbus_channel_submit(ModbusChannelId channel, uint8_t slave_id, const uint8_t *pdu, size_t pdu_len,
                                       uint8_t *out_pdu, size_t *out_pdu_len, size_t out_pdu_capacity);

// §4.2's `PUT /api/channels/{n}/config` — sendes gennem den SAMME kø som
// almindelige transaktioner, så en igangværende transaktion altid fuldføres
// på den GAMLE config før omkobling (designdokumentets eksplicitte krav),
// uden at kræve en separat lås omkring kanalens hardware-tilstand.
// Persisterer selv (config_set_channel()) ved succes — kaldstedet skal
// IKKE persistere igen. Returnerer false hvis kanalens task ikke kunne nås
// (fx kø fuld).
//
// Hardware-revision 2026-09-14 (2. ændring samme dag): MODE_SEL (GPIO4) er
// en INPUT sat ved fremstilling (fysisk jumper/strap), ikke et
// firmware-styret valg. `mode`-feltet i `new_config` IGNORERES derfor
// bevidst — den faktiske, hardware-udlæste mode (læst én gang ved boot,
// se `g_hardware_mode`/`read_board_mode_sel()` i modbus_channel.cpp)
// bruges altid i stedet, for BEGGE kanaler. `mode` er ikke længere et
// felt PLC-siden kan sætte via `PUT /api/channels/{n}/config` — kun en
// læseværdi (se `GET .../{n}` og `GET /api/status`s `board_mode`).
bool modbus_channel_apply_config(ModbusChannelId channel, const mb_channel_config_t &new_config);

// Nuværende config/statistik — kaldes fra REST-laget (GET /api/channels/{n})
// uden om kanalens kø (rene status-læsninger, samme lette tilgang som
// `config_get()` allerede bruges på tværs af tasks andetsteds i projektet).
mb_channel_config_t modbus_channel_get_config(ModbusChannelId channel);
mb_channel_stats_t modbus_channel_get_stats(ModbusChannelId channel);

// v0.25.0 (Jan: "lave en debug som outputer til console alt hvad der forgå
// på kanal A og B") — leveled debug-output til seriel konsol, sat pr. kanal.
// 0 = fra (default ved boot). 1-8 = stigende detaljeniveau, se
// execute_transaction() i modbus_channel.cpp for den fulde level-definition.
// Bevidst IKKE persisteret (§4.2 dækker kun rigtig kanal-config) — nulstilles
// altid til 0 ved reboot, sat direkte (uden om kanalens kø, ren
// runtime-tilstand, ingen hardware-adgang) fra den serielle CLI.
void modbus_channel_set_debug_level(ModbusChannelId channel, uint8_t level);
uint8_t modbus_channel_get_debug_level(ModbusChannelId channel);
