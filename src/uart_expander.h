#pragma once

#include <cstdint>

#include "channel_port.h"

// v0.31.0 — Lag 3: SC16IS752 (CJMCU-752-modulet) over I2C → kanal C og D.
// Tilslutning (GPIO_MAPPING.md): SDA=GPIO21, SCL=GPIO22, 400 kHz, modulet
// forsynet med 3,3 V. RS485-retning styres af chippens egne RTS-ben
// (RTSA → kanal C's DE/RE, RTSB → kanal D's DE/RE). Aktivitets-LED'er på
// chippens GPIO0 (kanal C) og GPIO1 (kanal D).

// Starter I2C og leder efter chippen på alle 16 mulige adresser (0x48-0x57).
// Returnerer true hvis den blev fundet og nulstillet.
bool uart_expander_begin();

bool uart_expander_found();
uint8_t uart_expander_i2c_address();  // 0 hvis ikke fundet

// Porten for chippens UART 0 (→ kanal C) eller 1 (→ kanal D). Findes altid —
// present() fortæller om chippen reelt svarer.
ChannelPort &uart_expander_port(uint8_t uart_index);
