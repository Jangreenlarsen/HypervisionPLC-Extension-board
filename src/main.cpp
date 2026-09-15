#include <Arduino.h>

#include "config.h"
#include "eth_driver.h"
#include "http_server.h"
#include "modbus_channel.h"
#include "modbus_tcp_server.h"
#include "provisioning.h"
#include "syslog_sender.h"

void setup() {
  Serial.begin(115200);
  // config_begin() SKAL kaldes foer modbus_channel_init_all() - kanalerne
  // laeser deres persisterede baudrate/mode (§4.2) fra config_get() ved
  // opstart, og en ukaldt config_begin() giver et nul-initialiseret
  // (baudrate=0!) g_config, som faar HardwareSerial::begin() til at forsoege
  // baud-auto-detektion og haenge.
  config_begin();
  // v0.26.0 (Jan: "kan vi lave en syslog funktion...") — laeser den
  // persisterede modtager-liste/hostname. Uafhaengigt af WiFi/Ethernet-
  // status (samme filosofi som modbus_channel_init_all() nedenfor) -
  // UDP-afsendelse fejler blot stille indtil et interface reelt har en IP.
  syslog_sender_begin();
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
  // v0.22.0 (Jan: "vi skal lige have en hostname på") — beregnes EN gang
  // her (enten den eksplicit satte, eller MAC-udledt default) og bruges
  // for Ethernet nedenfor; WiFi'en genberegner selv sin ved hvert
  // "connect"-forsøg (src/provisioning.cpp), da den kan ændres live.
  char hostname[40];
  mb_config_build_hostname(boot_cfg.has_hostname, boot_cfg.hostname, boot_cfg.eth_mac, hostname, sizeof(hostname));
  eth_driver_begin(boot_cfg.eth_enabled, boot_cfg.eth_static_ip, boot_cfg.eth_ip, boot_cfg.eth_mask, boot_cfg.eth_gw,
                    eth_mac, hostname);
  // v0.21.0-fund (Jan: "kan vi disable wifi også fra cli"): disse blev
  // hidtil KUN startet fra attempt_connect() (src/provisioning.cpp), dvs.
  // udelukkende udløst af en vellykket WIFI-forbindelse - et rent
  // Ethernet-board (WiFi deaktiveret/aldrig konfigureret) ville derfor
  // ALDRIG have faaet REST-API'et eller Modbus TCP-serverne startet,
  // uanset hvor godt Ethernet-forbindelsen ellers virkede. Flyttet hertil,
  // ubetinget - begge er idempotente (g_server/g_started-tjek) og
  // httpd_start()/lytte-sockets kræver ikke at noget interface allerede
  // har en IP (WiFiServer/WiFiClient er interface-agnostiske, jf.
  // eth_driver.h's dual-stack-kommentar) - starter derfor korrekt uanset
  // om det bliver WiFi, Ethernet, begge eller (via "connect") en senere
  // WiFi-forbindelse der først giver reel netværksadgang.
  http_server_begin();
  modbus_tcp_server_begin();
  provisioning_begin();
}

void loop() { provisioning_poll(); }
