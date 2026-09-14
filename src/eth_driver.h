#pragma once

// §1.3/§2.2 (valgfri Ethernet, W5500 over SPI, EXPANSION_BOARD_DESIGN.md) —
// bringer W5500'en op som en almindelig lwIP-netværksinterface via
// ESP-IDF's native `esp_eth`-komponent — IKKE Arduino's klassiske
// `Ethernet`-bibliotek (som har sin EGEN private TCP/IP-stack med
// EthernetServer/EthernetClient og derfor IKKE ville virke sammen med den
// WiFiServer/WiFiClient-baserede kode modbus_tcp_server.cpp/http_server.cpp
// allerede bruger). Fordi esp_eth registrerer sig som en rigtig netif, virker
// ALT eksisterende netværkskode (Modbus TCP, REST-API'et) UÆNDRET, uanset om
// trafikken reelt kommer ind på WiFi eller Ethernet.
//
// Kører SIDELØBENDE med WiFi (dual-stack) — ikke et enten-eller. Ethernet
// har ingen "provisionering": den henter blot en IP via DHCP så snart et
// kabel + DHCP-server er til stede, uafhængigt af WiFi-forbindelsens status.
//
// GPIO-allokering: EXPANSION_BOARD_DESIGN.md §2.0.1 (aftalt 2026-09-13).
// Fejler BEVIDST stille (logger en fejlbesked, rører IKKE resten af
// boardet) hvis intet W5500-modul er fysisk tilsluttet — sikkert at kalde
// uforandret selv på et board hvor Ethernet-hardwaren endnu ikke er
// monteret.
void eth_driver_begin();

// True hvis Ethernet-linket er oppe (kabel tilsluttet + PHY-link-detekteret)
// — IKKE det samme som at have fået en IP endnu (det tager DHCP et øjeblik
// længere). Bruges af GET /api/status (§4.2) til at rapportere Ethernet-
// status ved siden af WiFi-status.
bool eth_driver_link_up();

// Boardets Ethernet-IP, eller en tom streng hvis intet link/ingen IP endnu.
// Bufferen er statisk internt i eth_driver.cpp — pointeren forbliver gyldig,
// men INDHOLDET kan ændre sig ved næste DHCP-lease/link-skift, så kaldstedet
// bør ikke gemme pointeren på tværs af kald.
const char *eth_driver_ip_string();
