#include <Arduino.h>

#include "config.h"
#include "eth_driver.h"
#include "modbus_channel.h"
#include "provisioning.h"

void setup() {
  Serial.begin(115200);
  // config_begin() SKAL kaldes foer modbus_channel_init_all() - kanalerne
  // laeser deres persisterede baudrate/mode (§4.2) fra config_get() ved
  // opstart, og en ukaldt config_begin() giver et nul-initialiseret
  // (baudrate=0!) g_config, som faar HardwareSerial::begin() til at forsoege
  // baud-auto-detektion og haenge.
  config_begin();
  modbus_channel_init_all();  // uafhaengigt af WiFi-status, se modbus_channel.h
  eth_driver_begin();         // valgfri W5500-Ethernet, dual-stack med WiFi - fejler stille uden hardware tilsluttet
  provisioning_begin();
}

void loop() { provisioning_poll(); }
