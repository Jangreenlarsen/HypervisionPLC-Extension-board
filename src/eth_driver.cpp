#include "eth_driver.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_eth.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_netif_defaults.h>

namespace {

// GPIO-allokering — EXPANSION_BOARD_DESIGN.md §2.0.1, aftalt 2026-09-13,
// RST tilføjet 2026-09-14. Bevidst IKKE brugt: GPIO21/22 (holdt fri til
// fremtidig I2C), GPIO26/33 (kanal-aktivitets-LED'er).
constexpr int kEthSckPin = 14;
constexpr int kEthMosiPin = 13;
constexpr int kEthMisoPin = 35;
constexpr int kEthCsPin = 32;
constexpr int kEthIntPin = 39;
// Hardware-revision 2026-09-14: MODE_SEL blev samlet til ÉN delt GPIO for
// hele boardet (kanal A/B kan ikke længere have forskellig RS232/RS485-mode,
// se modbus_channel.cpp) — det frigav GPIO23 (kanal B's tidligere
// dedikerede MODE_SEL), som nu bruges til en RIGTIG, software-styret
// RST-pin for W5500'en i stedet for kun at stole på modulets eget
// power-on-reset.
constexpr int kEthRstPin = 23;

// HSPI (SPI2_HOST) — bevidst IKKE VSPI (SPI3_HOST), hvis default-pins
// (MOSI=23/MISO=19/SCK=18) overlapper direkte med kanal B's UART-pins.
// Her bruges GPIO-matrixen til fuldstændig frie pin-valg uanset, men
// SPI2_HOST holder navngivningen fri af den forvirring.
constexpr spi_host_device_t kEthSpiHost = SPI2_HOST;
constexpr int kEthSpiClockHz = 8 * 1000 * 1000;  // 8 MHz — forsigtigt for et eksternt modul (længere ledninger end onboard)

esp_eth_handle_t g_eth_handle = nullptr;
volatile bool g_link_up = false;
char g_ip_string[16] = "";  // "255.255.255.255\0"

void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  (void)arg;
  (void)event_base;
  (void)event_data;
  switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
      g_link_up = true;
      Serial.println("Ethernet: link op.");
      break;
    case ETHERNET_EVENT_DISCONNECTED:
      g_link_up = false;
      g_ip_string[0] = '\0';
      Serial.println("Ethernet: link nede.");
      break;
    case ETHERNET_EVENT_START:
      Serial.println("Ethernet: driver startet, venter paa link...");
      break;
    case ETHERNET_EVENT_STOP:
      g_link_up = false;
      g_ip_string[0] = '\0';
      Serial.println("Ethernet: driver stoppet.");
      break;
    default:
      break;
  }
}

void got_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  (void)arg;
  (void)event_base;
  (void)event_id;
  const ip_event_got_ip_t *event = static_cast<const ip_event_got_ip_t *>(event_data);
  snprintf(g_ip_string, sizeof(g_ip_string), IPSTR, IP2STR(&event->ip_info.ip));
  Serial.print("Ethernet: fik IP ");
  Serial.println(g_ip_string);
}

}  // namespace

void eth_driver_begin() {
  // WiFi.mode()/WiFi.begin() (provisioning.cpp) initialiserer allerede
  // esp_netif/event-loopet ved foerste brug — begge kald her er derfor
  // idempotente og trygge uanset raekkefoelgen mellem Ethernet- og
  // WiFi-opstart (ESP_ERR_INVALID_STATE = "allerede gjort", ikke en fejl).
  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    Serial.printf("Ethernet: esp_netif_init() fejlede: 0x%x\n", err);
    return;
  }
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    Serial.printf("Ethernet: esp_event_loop_create_default() fejlede: 0x%x\n", err);
    return;
  }

  // W5500-MAC-driveren bruger gpio_isr_handler_add() internt for INT-pinden
  // (interrupt-drevet, ikke polling) — kræver at ISR-servicen er installeret
  // FØRST, ellers logger den en ESP-IDF-fejl og selve interrupt-registreringen
  // fejler stille (boardet crasher ikke, men Ethernet-driveren virker ikke
  // korrekt). Global, én gang — ESP_ERR_INVALID_STATE = allerede installeret.
  err = gpio_install_isr_service(0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    Serial.printf("Ethernet: gpio_install_isr_service() fejlede: 0x%x\n", err);
    return;
  }

  esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
  esp_netif_t *eth_netif = esp_netif_new(&netif_config);
  if (eth_netif == nullptr) {
    Serial.println("Ethernet: kunne ikke oprette netif.");
    return;
  }

  spi_bus_config_t bus_cfg = {};
  bus_cfg.mosi_io_num = kEthMosiPin;
  bus_cfg.miso_io_num = kEthMisoPin;
  bus_cfg.sclk_io_num = kEthSckPin;
  bus_cfg.quadwp_io_num = -1;
  bus_cfg.quadhd_io_num = -1;
  if (spi_bus_initialize(kEthSpiHost, &bus_cfg, SPI_DMA_CH_AUTO) != ESP_OK) {
    Serial.println("Ethernet: spi_bus_initialize() fejlede (SPI-bus allerede i brug af noget andet?).");
    return;
  }

  spi_device_interface_config_t spi_devcfg = {};
  spi_devcfg.mode = 0;
  spi_devcfg.clock_speed_hz = kEthSpiClockHz;
  spi_devcfg.queue_size = 20;
  spi_devcfg.spics_io_num = kEthCsPin;

  spi_device_handle_t spi_handle = nullptr;
  if (spi_bus_add_device(kEthSpiHost, &spi_devcfg, &spi_handle) != ESP_OK) {
    Serial.println("Ethernet: spi_bus_add_device() fejlede.");
    return;
  }

  eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(spi_handle);
  w5500_config.int_gpio_num = kEthIntPin;  // interrupt-drevet, ikke polling — se §2.0.1's INT-reservation

  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
  if (mac == nullptr) {
    Serial.println("Ethernet: esp_eth_mac_new_w5500() fejlede.");
    return;
  }

  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
  phy_config.phy_addr = 1;                  // W5500's interne PHY — fast adresse 1, ESP-IDF-konvention
  phy_config.reset_gpio_num = kEthRstPin;   // software-styret RST (hardware-revision 2026-09-14)
  esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
  if (phy == nullptr) {
    Serial.println("Ethernet: esp_eth_phy_new_w5500() fejlede.");
    return;
  }

  esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
  if (esp_eth_driver_install(&eth_config, &g_eth_handle) != ESP_OK) {
    Serial.println("Ethernet: esp_eth_driver_install() fejlede — intet W5500-modul tilsluttet, eller forkert forbundet?");
    return;
  }

  if (esp_netif_attach(eth_netif, esp_eth_new_netif_glue(g_eth_handle)) != ESP_OK) {
    Serial.println("Ethernet: esp_netif_attach() fejlede.");
    return;
  }

  esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, nullptr);

  if (esp_eth_start(g_eth_handle) != ESP_OK) {
    Serial.println("Ethernet: esp_eth_start() fejlede.");
    return;
  }

  Serial.println("Ethernet: W5500-driver startet (DHCP naar kabel + link er til stede).");
}

bool eth_driver_link_up() { return g_link_up; }

const char *eth_driver_ip_string() { return g_ip_string; }
