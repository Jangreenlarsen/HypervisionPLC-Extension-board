#include <Arduino.h>

#include "modbus_channel.h"
#include "provisioning.h"

void setup() {
  Serial.begin(115200);
  modbus_channel_init_all();  // uafhaengigt af WiFi-status, se modbus_channel.h
  provisioning_begin();
}

void loop() { provisioning_poll(); }
