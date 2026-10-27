#pragma once

// Watchdog-/heartbeat-LED (v0.37.0, Jan: "tilfoer en LED til status paa
// watchdog timer som skal blink 1 gang i sek"). Egen FreeRTOS-task blinker
// LED'en kort 1 gang i sekundet, men KUN saa laenge hovedloekken (loop())
// jevnligt melder "i live" via heartbeat_led_alive(). Hænger loop() i mere end
// kHeartbeatTimeoutMs, stopper blinket (LED slukket) - synligt i skabet uden
// seriel adgang. Ingen reset-handling: LED'en er ren status.
void heartbeat_led_begin();
void heartbeat_led_alive();
