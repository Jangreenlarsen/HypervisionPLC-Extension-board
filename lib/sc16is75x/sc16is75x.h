#pragma once

#include <cstddef>
#include <cstdint>

#include "board_config.h"

// v0.31.0 — hardware-uafhængig registerlogik for NXP SC16IS752 (CJMCU-752-
// modulet: 2× UART over I2C, kanal C+D). Kun beregninger og registerværdier
// — selve I2C-trafikken ligger i src/uart_expander.cpp (Lag 3), så denne del
// kan native-testes.

// Registre (generelt registersæt, LCR[7]=0) — SC16IS752-databladet tabel 10.
constexpr uint8_t SC16_REG_RHR_THR = 0x00;
constexpr uint8_t SC16_REG_IER = 0x01;
constexpr uint8_t SC16_REG_FCR = 0x02;  // kun skriv (læs = IIR)
constexpr uint8_t SC16_REG_LCR = 0x03;
constexpr uint8_t SC16_REG_MCR = 0x04;
constexpr uint8_t SC16_REG_LSR = 0x05;
constexpr uint8_t SC16_REG_SPR = 0x07;  // scratch pad — bruges til auto-detektion
constexpr uint8_t SC16_REG_TXLVL = 0x08;
constexpr uint8_t SC16_REG_RXLVL = 0x09;
constexpr uint8_t SC16_REG_IODIR = 0x0A;
constexpr uint8_t SC16_REG_IOSTATE = 0x0B;
constexpr uint8_t SC16_REG_IOCONTROL = 0x0E;
constexpr uint8_t SC16_REG_EFCR = 0x0F;
// Specielt registersæt (LCR = 0x80, divisor latch):
constexpr uint8_t SC16_REG_DLL = 0x00;
constexpr uint8_t SC16_REG_DLH = 0x01;

constexpr uint8_t SC16_LCR_DIVISOR_LATCH = 0x80;
constexpr uint8_t SC16_LSR_DATA_READY = 0x01;
constexpr uint8_t SC16_LSR_TX_EMPTY = 0x40;  // THR OG sende-shiftregister tomme — sidste bit er ude
constexpr uint8_t SC16_FCR_ENABLE_AND_RESET = 0x07;  // FIFO til + nulstil RX- og TX-FIFO
constexpr uint8_t SC16_IOCONTROL_SOFT_RESET = 0x08;
constexpr size_t SC16_FIFO_SIZE = 64;

// CJMCU-752-modulets krystal (Jan, bekræftet 2026-09-25: 1,8432 MHz).
constexpr uint32_t SC16_XTAL_HZ = 1843200;

// I2C-adresser (7-bit): A0/A1 kan hver forbindes til VDD/VSS/SCL/SDA → 16
// mulige adresser, 0x48-0x57 (databladets tabel 32). Firmwaren prøver dem
// alle ved boot, så modulets adresse-jumpere ikke skal kendes på forhånd.
constexpr uint8_t SC16_I2C_ADDR_FIRST = 0x48;
constexpr uint8_t SC16_I2C_ADDR_LAST = 0x57;

// I2C-subadresse: bit 6:3 = register, bit 2:1 = kanal (0=A, 1=B), bit 0 = 0.
uint8_t sc16_i2c_subaddress(uint8_t reg, uint8_t channel);

// Baud-divisor ved prescaler 1: divisor = xtal / (16 × baud). Returnerer
// false hvis baudraten ikke kan laves inden for ±1 % (for høj for krystallen,
// eller ingen heltalsdivisor tæt nok på). Med 1,8432 MHz er 1200-115200
// eksakte; alt over 115200 afvises.
bool sc16_baud_divisor(uint32_t xtal_hz, uint32_t baud, uint16_t *out_divisor);

// Højeste baudrate krystallen kan levere (divisor 1).
uint32_t sc16_max_baud(uint32_t xtal_hz);

// LCR: altid 8 databit; paritet og 1/2 stopbit fra kanal-config (§4.2).
uint8_t sc16_lcr_value(mb_channel_parity_t parity, uint8_t stop_bits);

// RS485: EFCR = RTSCON|RTSINVER — chippen styrer selv RTS-benet som DE/RE
// (HØJ under afsendelse, LAV ellers), så kanalen skal ikke bruge en
// ESP32-GPIO til retning. RS232: EFCR = 0 (fuld duplex, ingen retning).
uint8_t sc16_efcr_value(mb_channel_mode_t mode);

// RS232: MCR[1]=1 → RTS-benet LAVT, så en evt. monteret RS485-transceivers
// DE forbliver slukket. RS485: 0 (RTS styres af EFCR ovenfor).
uint8_t sc16_mcr_value(mb_channel_mode_t mode);
