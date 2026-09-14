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
  // valgfri W5500-Ethernet, dual-stack med WiFi - fejler stille uden hardware
  // tilsluttet. enable/disable + static-IP (v0.20.0, "eth ..."-CLI) laeses
  // fra NVS her, ved boot - ændringer kraever et "reboot" for at traede i
  // kraft (se eth_driver.h). MAC'en (v0.20.0, "vi skal også have en random
  // MAC adr brændt ind i NVS ved start") sikres/genereres FØR
  // eth_driver_begin() kaldes, uanset om Ethernet er slaaet til.
  uint8_t eth_mac[6];
  config_ensure_eth_mac(eth_mac);
  const mb_board_config_t &boot_cfg = config_get();
  eth_driver_begin(boot_cfg.eth_enabled, boot_cfg.eth_static_ip, boot_cfg.eth_ip, boot_cfg.eth_mask, boot_cfg.eth_gw,
                    eth_mac);
  provisioning_begin();
}

void loop() { provisioning_poll(); }
