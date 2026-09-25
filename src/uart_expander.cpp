#include "uart_expander.h"

#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "sc16is75x.h"

namespace {

constexpr int kI2cSdaPin = 21;
constexpr int kI2cSclPin = 22;
constexpr uint32_t kI2cClockHz = 400000;

uint8_t g_address = 0;
bool g_found = false;
// Én I2C-bus og ét sæt delte registre (IOSTATE) for begge UART'er — kanal C's
// og D's tasks kører samtidigt, så hver registersekvens skal være atomisk.
SemaphoreHandle_t g_bus_mutex = nullptr;
uint8_t g_io_state = 0;  // skygge af IOSTATE (LED-bits), så to kanaler ikke overskriver hinanden

class BusLock {
 public:
  BusLock() { xSemaphoreTake(g_bus_mutex, portMAX_DELAY); }
  ~BusLock() { xSemaphoreGive(g_bus_mutex); }
};

bool write_reg_raw(uint8_t address, uint8_t channel, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(sc16_i2c_subaddress(reg, channel));
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool read_reg_raw(uint8_t address, uint8_t channel, uint8_t reg, uint8_t *out) {
  Wire.beginTransmission(address);
  Wire.write(sc16_i2c_subaddress(reg, channel));
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(address, static_cast<uint8_t>(1)) != 1) return false;
  *out = static_cast<uint8_t>(Wire.read());
  return true;
}

bool write_reg(uint8_t channel, uint8_t reg, uint8_t value) { return write_reg_raw(g_address, channel, reg, value); }
bool read_reg(uint8_t channel, uint8_t reg, uint8_t *out) { return read_reg_raw(g_address, channel, reg, out); }

// Scratch-register-test: skriv to forskellige mønstre og læs dem tilbage.
// Skelner en SC16IS75x fra en tilfældig anden I2C-enhed på samme adresse.
bool probe(uint8_t address) {
  uint8_t v = 0;
  if (!write_reg_raw(address, 0, SC16_REG_SPR, 0xA5) || !read_reg_raw(address, 0, SC16_REG_SPR, &v) || v != 0xA5) {
    return false;
  }
  return write_reg_raw(address, 0, SC16_REG_SPR, 0x5A) && read_reg_raw(address, 0, SC16_REG_SPR, &v) && v == 0x5A;
}

class Sc16Port : public ChannelPort {
 public:
  explicit Sc16Port(uint8_t channel) : channel_(channel) {}

  bool configure(const mb_channel_config_t &config) override {
    if (!g_found) return false;
    uint16_t divisor = 0;
    if (!sc16_baud_divisor(SC16_XTAL_HZ, config.baudrate, &divisor)) return false;

    BusLock lock;
    const bool ok = write_reg(channel_, SC16_REG_LCR, SC16_LCR_DIVISOR_LATCH) &&
                    write_reg(channel_, SC16_REG_DLL, static_cast<uint8_t>(divisor & 0xFF)) &&
                    write_reg(channel_, SC16_REG_DLH, static_cast<uint8_t>(divisor >> 8)) &&
                    write_reg(channel_, SC16_REG_LCR, sc16_lcr_value(config.parity, config.stop_bits)) &&
                    write_reg(channel_, SC16_REG_FCR, SC16_FCR_ENABLE_AND_RESET) &&
                    write_reg(channel_, SC16_REG_IER, 0x00) &&  // polling, ingen interrupts
                    write_reg(channel_, SC16_REG_MCR, sc16_mcr_value(config.mode)) &&
                    write_reg(channel_, SC16_REG_EFCR, sc16_efcr_value(config.mode));
    rx_len_ = 0;
    rx_pos_ = 0;
    return ok;
  }

  int available() override {
    if (rx_pos_ < rx_len_) return static_cast<int>(rx_len_ - rx_pos_);
    if (!g_found) return 0;
    // Tøm hele den ventende del af chippens FIFO i ÉN I2C-transaktion — én
    // byte pr. transaktion er for langsomt til at følge med ved 115200 baud.
    BusLock lock;
    uint8_t level = 0;
    if (!read_reg(channel_, SC16_REG_RXLVL, &level) || level == 0) return 0;
    if (level > sizeof(rx_buf_)) level = sizeof(rx_buf_);
    Wire.beginTransmission(g_address);
    Wire.write(sc16_i2c_subaddress(SC16_REG_RHR_THR, channel_));
    if (Wire.endTransmission(false) != 0) return 0;
    const uint8_t got = Wire.requestFrom(g_address, level);
    for (uint8_t i = 0; i < got; i++) rx_buf_[i] = static_cast<uint8_t>(Wire.read());
    rx_len_ = got;
    rx_pos_ = 0;
    return got;
  }

  int read() override {
    if (rx_pos_ >= rx_len_ && available() == 0) return -1;
    return rx_buf_[rx_pos_++];
  }

  size_t write(const uint8_t *data, size_t len) override {
    if (!g_found) return 0;
    size_t sent = 0;
    const uint32_t start = millis();
    while (sent < len && millis() - start < 500) {
      BusLock lock;
      uint8_t space = 0;
      if (!read_reg(channel_, SC16_REG_TXLVL, &space)) break;
      if (space == 0) continue;
      size_t n = len - sent;
      if (n > space) n = space;
      if (n > 32) n = 32;  // Wire-bufferen (128 B) — og hold hver bus-låsning kort
      Wire.beginTransmission(g_address);
      Wire.write(sc16_i2c_subaddress(SC16_REG_RHR_THR, channel_));
      Wire.write(data + sent, n);
      if (Wire.endTransmission() != 0) break;
      sent += n;
    }
    return sent;
  }

  void flush_tx() override {
    if (!g_found) return;
    // Vent til THR OG sende-shiftregisteret er tomme — først da slipper
    // chippens auto-RTS DE, og svaret kan begynde at komme.
    const uint32_t start = millis();
    while (millis() - start < 500) {
      uint8_t lsr = 0;
      {
        BusLock lock;
        if (!read_reg(channel_, SC16_REG_LSR, &lsr)) return;
      }
      if (lsr & SC16_LSR_TX_EMPTY) return;
    }
  }

  bool handles_direction() const override { return true; }
  void set_direction_tx(bool) override {}  // auto-RTS (EFCR) — se sc16_efcr_value()

  void set_activity_led(bool on) override {
    if (!g_found) return;
    BusLock lock;
    const uint8_t bit = static_cast<uint8_t>(1u << channel_);  // GPIO0 = kanal C, GPIO1 = kanal D
    g_io_state = on ? static_cast<uint8_t>(g_io_state | bit) : static_cast<uint8_t>(g_io_state & ~bit);
    write_reg(0, SC16_REG_IOSTATE, g_io_state);
  }

  bool present() const override { return g_found; }
  uint32_t max_baud() const override { return sc16_max_baud(SC16_XTAL_HZ); }

 private:
  uint8_t channel_;
  uint8_t rx_buf_[SC16_FIFO_SIZE];
  size_t rx_len_ = 0;
  size_t rx_pos_ = 0;
};

Sc16Port g_ports[2] = {Sc16Port(0), Sc16Port(1)};

}  // namespace

bool uart_expander_begin() {
  if (g_bus_mutex == nullptr) g_bus_mutex = xSemaphoreCreateMutex();
  Wire.begin(kI2cSdaPin, kI2cSclPin, kI2cClockHz);

  g_found = false;
  g_address = 0;
  for (uint8_t address = SC16_I2C_ADDR_FIRST; address <= SC16_I2C_ADDR_LAST; address++) {
    if (probe(address)) {
      g_address = address;
      g_found = true;
      break;
    }
  }
  if (!g_found) return false;

  BusLock lock;
  // Software-reset af begge UART'er (IOControl bit 3 — chippen svarer ikke
  // på selve skrivningen, derfor ignoreres resultatet), derefter LED-GPIO'er.
  write_reg(0, SC16_REG_IOCONTROL, SC16_IOCONTROL_SOFT_RESET);
  delay(5);
  g_io_state = 0;
  write_reg(0, SC16_REG_IOSTATE, g_io_state);
  write_reg(0, SC16_REG_IODIR, 0x03);  // GPIO0+GPIO1 = udgange (LED'er)
  return true;
}

bool uart_expander_found() { return g_found; }

uint8_t uart_expander_i2c_address() { return g_found ? g_address : 0; }

ChannelPort &uart_expander_port(uint8_t uart_index) { return g_ports[uart_index > 1 ? 1 : uart_index]; }
