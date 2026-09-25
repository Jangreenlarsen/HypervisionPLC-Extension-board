#pragma once

#include <cstddef>
#include <cstdint>

#include "board_config.h"

// v0.31.0 — Lag 3 (hardware-abstraktion): én fysisk UART bag et fælles
// interface, så kanal-tasken (src/modbus_channel.cpp, Lag 2) kører samme
// RTU-transaktionslogik uanset om kanalen sidder på ESP32'ens egen UART
// (kanal A/B, src/uart_port_native.cpp) eller på SC16IS752 over I2C (kanal
// C/D, src/uart_expander.cpp). Kaldes KUN fra kanalens egen task.
class ChannelPort {
 public:
  virtual ~ChannelPort() = default;

  // Anvender baudrate/paritet/stopbit/mode. false = kan ikke (fx baudrate
  // over krystallens grænse, eller hardwaren svarer ikke).
  virtual bool configure(const mb_channel_config_t &config) = 0;

  // Antal byte klar til read() lige nu.
  virtual int available() = 0;
  // Én byte, eller -1 hvis ingen.
  virtual int read() = 0;
  // Returnerer antal byte faktisk overgivet til hardwaren.
  virtual size_t write(const uint8_t *data, size_t len) = 0;
  // Blokerer til sidste bit er sendt ud på linjen (eller timeout).
  virtual void flush_tx() = 0;

  // true = hardwaren styrer selv RS485-DE/RE (SC16IS752's auto-RTS);
  // false = kanal-tasken skal selv kalde set_direction_tx() omkring sendingen.
  virtual bool handles_direction() const = 0;
  virtual void set_direction_tx(bool tx) = 0;

  virtual void set_activity_led(bool on) = 0;

  // false = hardwaren blev ikke fundet (fx EXP_SEL-jumperen siger "monteret",
  // men SC16IS752 svarer ikke på I2C) — transaktioner afvises da med en fejl.
  virtual bool present() const = 0;

  // Højeste baudrate porten kan levere.
  virtual uint32_t max_baud() const = 0;
};
