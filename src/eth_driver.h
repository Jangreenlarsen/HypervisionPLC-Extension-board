#pragma once

#include <cstdint>

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
// har ingen "provisionering" i samme forstand som WiFi (intet "connect"-
// forsøg/timeout) — den henter blot en IP via DHCP (eller anvender en
// konfigureret statisk IP, v0.20.0) så snart et kabel + link er til stede,
// uafhængigt af WiFi-forbindelsens status.
//
// GPIO-allokering: EXPANSION_BOARD_DESIGN.md §2.0.1 (aftalt 2026-09-13).
// Fejler BEVIDST stille (logger en fejlbesked, rører IKKE resten af
// boardet) hvis intet W5500-modul er fysisk tilsluttet — sikkert at kalde
// uforandret selv på et board hvor Ethernet-hardwaren endnu ikke er
// monteret.
//
// v0.20.0 (Jan: "har vi kommando til at enable/disable eterhnet samt ip
// config, modes m.m.") — `enabled=false` springer ALT over (ingen SPI-/
// GPIO-initialisering forsøgt overhovedet, hverken link, DHCP eller
// static-IP anvendes). `static_ip=true` bruger `ip`/`mask`/`gw` (skal alle
// tre være gyldige IPv4-strenge) i stedet for DHCP; ugyldige/tomme værdier
// falder sikkert tilbage til DHCP (advarsel logges) i stedet for udefineret
// adfærd. Kaldt ÉN gang ved boot (src/main.cpp) med værdier fra
// config_get() — ændringer via "eth ..."-CLI-kommandoerne kræver derfor et
// `reboot` for at træde i kraft, INGEN forsøg på at live-reinitialisere
// den SPI-baserede esp_eth-driver under kørsel.
//
// `mac` (6 bytes, ALDRIG nullptr — se config_ensure_eth_mac(), src/config.h)
// er en tilfældig, NVS-persisteret, lokalt-administreret unicast-MAC (Jan:
// "vi skal også have en random MAC adr brændt ind i NVS ved start") — sat
// via `esp_eth_ioctl(ETH_CMD_S_MAC_ADDR)` efter `esp_eth_driver_install()`.
// Uden dette ville W5500'en køre med MAC 00:00:00:00:00:00 (chippens egen
// default, ingen fabriks-MAC modsat ESP32'ens interne EMAC), hvilket giver
// MAC-kollisioner på netværket hvis flere boards er tilsluttet samtidig —
// samme rodårsag Modbus_API_Gateway (søsterprojekt) fandt og rettede, dog
// med `esp_read_mac(ESP_MAC_ETH)` i stedet for en NVS-persisteret tilfældig
// værdi (se board_config.h's kommentar for hvorfor denne board bevidst
// vælger den anden tilgang).
// `hostname` (ALDRIG nullptr, se mb_config_build_hostname(),
// lib/board_config/) sættes via `esp_netif_set_hostname()` FØR
// `esp_eth_start()`, så det indgår i DHCP-forespørgslen (Option 12) fra
// selve den første pakke (Jan: "vi skal lige have en hostname på kan jeg
// se da dhcp server bare har et espressif name nu" — Ethernet-interfacet
// fik hidtil INTET hostname overhovedet).
void eth_driver_begin(bool enabled, bool static_ip, const char *ip, const char *mask, const char *gw,
                       const uint8_t *mac, const char *hostname);

// Boardets aktuelle W5500-MAC (6 bytes, ALTID gyldig — samme værdi som blev
// givet til `eth_driver_begin()`, uanset om selve driveren rent faktisk fik
// talt med hardwaren) — bruges af `show`/`status`-CLI'en (Jan: "MAC skal så
// ved en show status i cli") til at vise den, uafhængigt af om et fysisk
// modul er tilsluttet.
void eth_driver_get_mac(uint8_t out_mac[6]);

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
