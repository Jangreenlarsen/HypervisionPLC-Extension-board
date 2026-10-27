#include "heartbeat_led.h"

#include <Arduino.h>

namespace {

// GPIO2: ledig (ikke brugt af kanaler, W5500 eller I2C) og samme ben som
// DevKit'ets indbyggede LED. Strapping-ben, men LED + 1k til GND forstyrrer
// ikke boot (hoejt-Z/lav ved reset).
constexpr int kHeartbeatLedPin = 2;
constexpr uint32_t kBlinkPeriodMs = 1000;  // 1 blink i sekundet
constexpr uint32_t kBlinkOnMs = 100;
constexpr uint32_t kHeartbeatTimeoutMs = 30000;  // loop() skal melde sig oftere end dette

volatile uint32_t g_last_alive_ms = 0;

void heartbeat_task(void *) {
  for (;;) {
    const bool alive = (millis() - g_last_alive_ms) < kHeartbeatTimeoutMs;
    if (alive) {
      digitalWrite(kHeartbeatLedPin, HIGH);
      vTaskDelay(pdMS_TO_TICKS(kBlinkOnMs));
      digitalWrite(kHeartbeatLedPin, LOW);
      vTaskDelay(pdMS_TO_TICKS(kBlinkPeriodMs - kBlinkOnMs));
    } else {
      digitalWrite(kHeartbeatLedPin, LOW);  // hovedloekken haenger: ingen blink
      vTaskDelay(pdMS_TO_TICKS(250));
    }
  }
}

}  // namespace

void heartbeat_led_begin() {
  pinMode(kHeartbeatLedPin, OUTPUT);
  digitalWrite(kHeartbeatLedPin, LOW);
  g_last_alive_ms = millis();
  xTaskCreate(heartbeat_task, "heartbeat", 2048, nullptr, 1, nullptr);
}

void heartbeat_led_alive() { g_last_alive_ms = millis(); }
