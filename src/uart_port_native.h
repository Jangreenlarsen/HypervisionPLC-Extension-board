#pragma once

#include <HardwareSerial.h>

#include "channel_port.h"

// Kanal A/B: ESP32'ens egne UART-periferier (§2.0.1) + ESP32-GPIO til
// RS485-retning og aktivitets-LED. Samme opførsel som før v0.31.0.
class NativeUartPort : public ChannelPort {
 public:
  NativeUartPort(HardwareSerial &serial, int tx_pin, int rx_pin, int dir_pin, int led_pin);

  // Sætter GPIO-retninger op — kaldes én gang før første configure().
  void init_pins();

  bool configure(const mb_channel_config_t &config) override;
  int available() override;
  int read() override;
  size_t write(const uint8_t *data, size_t len) override;
  void flush_tx() override;
  bool handles_direction() const override { return false; }
  void set_direction_tx(bool tx) override;
  void set_activity_led(bool on) override;
  bool present() const override { return true; }
  uint32_t max_baud() const override { return 0xFFFFFFFFu; }  // ingen ekstra grænse ud over mb_is_valid_baudrate()

 private:
  HardwareSerial &serial_;
  int tx_pin_;
  int rx_pin_;
  int dir_pin_;
  int led_pin_;
};
