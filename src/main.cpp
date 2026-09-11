#include <Arduino.h>

#include "provisioning.h"

void setup() {
  Serial.begin(115200);
  provisioning_begin();
}

void loop() { provisioning_poll(); }
