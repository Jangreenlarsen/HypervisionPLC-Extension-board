#include "uart_port_native.h"

#include <Arduino.h>

namespace {

// Mapper mb_channel_parity_t/stop_bits til Arduino/ESP32's SERIAL_8xx-config.
// Altid 8 databits — hverken §4.2 eller Modbus RTU-praksis eksponerer andet.
uint32_t serial_config_for(mb_channel_parity_t parity, uint8_t stop_bits) {
  if (stop_bits == 2) {
    switch (parity) {
      case MB_CHANNEL_PARITY_EVEN: return SERIAL_8E2;
      case MB_CHANNEL_PARITY_ODD: return SERIAL_8O2;
      case MB_CHANNEL_PARITY_NONE:
      default: return SERIAL_8N2;
    }
  }
  switch (parity) {
    case MB_CHANNEL_PARITY_EVEN: return SERIAL_8E1;
    case MB_CHANNEL_PARITY_ODD: return SERIAL_8O1;
    case MB_CHANNEL_PARITY_NONE:
    default: return SERIAL_8N1;
  }
}

}  // namespace

NativeUartPort::NativeUartPort(HardwareSerial &serial, int tx_pin, int rx_pin, int dir_pin, int led_pin)
    : serial_(serial), tx_pin_(tx_pin), rx_pin_(rx_pin), dir_pin_(dir_pin), led_pin_(led_pin) {}

void NativeUartPort::init_pins() {
  pinMode(dir_pin_, OUTPUT);
  digitalWrite(dir_pin_, LOW);
  pinMode(led_pin_, OUTPUT);
  digitalWrite(led_pin_, LOW);
}

bool NativeUartPort::configure(const mb_channel_config_t &config) {
  digitalWrite(dir_pin_, LOW);
  serial_.end();
  serial_.begin(config.baudrate, serial_config_for(config.parity, config.stop_bits), rx_pin_, tx_pin_);
  return true;
}

int NativeUartPort::available() { return serial_.available(); }

int NativeUartPort::read() { return serial_.read(); }

size_t NativeUartPort::write(const uint8_t *data, size_t len) { return serial_.write(data, len); }

void NativeUartPort::flush_tx() { serial_.flush(); }

void NativeUartPort::set_direction_tx(bool tx) { digitalWrite(dir_pin_, tx ? HIGH : LOW); }

void NativeUartPort::set_activity_led(bool on) { digitalWrite(led_pin_, on ? HIGH : LOW); }
