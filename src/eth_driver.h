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

// v0.18.0 (Jan: "vi skal have noget diag på det w5500 så vi kan se det
// fungere eller om det er link fejl") — `eth_driver_link_up()` alene kan
// IKKE skelne "intet W5500-modul fundet/forkert forbundet" (et
// firmware-/hardware-problem — tjek loedning/forbindelser) fra "modul
// fundet og virker fint, men netværkskablet mangler eller switch-porten er
// nede" (en ren netværks-/kabel-sag). Denne mere detaljerede status gør
// præcis det skel muligt.
enum eth_driver_status_t {
  // esp_eth_start() (den FØRSTE reelle SPI-samtale med W5500-chippen — se
  // src/eth_driver.cpp's kommentar ved kaldet, IKKE esp_eth_driver_install()
  // som kun allokerer driver-strukturer uden at røre hardwaren) er ALDRIG
  // lykkedes — enten intet modul fysisk tilsluttet, forkert forbundet,
  // eller en tidligere SPI-/GPIO-opsætningsfejl (alle ender her, da
  // handlingen for installatøren er den samme: tjek den fysiske Ethernet-
  // tilslutning).
  ETH_STATUS_NOT_DETECTED = 0,
  // Modulet ER fundet og driveren kører, men PHY'en rapporterer intet link
  // — netværkskabel ikke tilsluttet, eller den anden ende (switch) er nede.
  ETH_STATUS_LINK_DOWN = 1,
  // Link er oppe, men DHCP har endnu ikke tildelt en IP.
  ETH_STATUS_WAITING_DHCP = 2,
  // Link oppe OG en IP er modtaget via DHCP — fuldt funktionsdygtig.
  ETH_STATUS_CONNECTED = 3,
};

eth_driver_status_t eth_driver_status();

// Kort, stabil status-slug (samme stil som channel_config.cpp's
// "ok"/"error"/"disabled" for `status`-feltet) — bruges direkte i JSON
// (`GET /api/status`s `ethernet.status`) og som grundlag for CLI'ens
// danske visning (se src/provisioning.cpp).
const char *eth_driver_status_string();
