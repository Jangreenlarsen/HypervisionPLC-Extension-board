#include "modbus_channel.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "config.h"

namespace {

// GPIO-allokering — EXPANSION_BOARD_DESIGN.md §2.0.1, ESP32-WROOM-32 DevKit.
// Disse er FYSISK faste (loddet på boardet) og derfor ikke en del af den
// konfigurerbare §4.2-config — kun baudrate/mode/parity/stop-bits/timeout/
// inter-frame-delay/enabled er det.
constexpr int kChannelATx = 17;
constexpr int kChannelARx = 16;
constexpr int kChannelADir = 27;

constexpr int kChannelBTx = 18;
constexpr int kChannelBRx = 19;
constexpr int kChannelBDir = 25;

// Hardware-revision 2026-09-14 (Jan, bekræftet): MODE_SEL er ÉN delt GPIO
// for HELE boardet, ikke længere én pr. kanal — kanal A og B kan derfor
// ALDRIG have forskellig RS232/RS485-mode, kun ét fast valg for hele
// boardet. Frigav GPIO23 (kanal B's tidligere MODE_SEL) til W5500's RST
// (se src/eth_driver.cpp).
//
// Hardware-revision 2026-09-14 (2. ændring samme dag, Jan bekræftet): dette
// er en INPUT, ikke en output — et fysisk jumper/strap SAT VED FREMSTILLING
// (installatøren/fabrikanten vælger RS232 eller RS485 for boardet, én gang,
// ved at forbinde GPIO4 til enten 3.3V eller GND), IKKE noget firmwaren selv
// driver ud fra en gemt config. Firmwaren LÆSER pinden ÉN gang ved boot
// (`modbus_channel_init_all()`) og bruger den udlæste værdi resten af
// levetiden — `mode` er derfor IKKE længere et felt PLC-siden kan sætte via
// `PUT /api/channels/{n}/config` (kun læseværdi, se `GET .../{n}` og
// `GET /api/status`s `board_mode`). `INPUT_PULLUP` sikrer en veldefineret
// default (RS485) hvis pinden mod forventning skulle stå uforbundet under
// bring-up, i stedet for at flyde og læse støj.
constexpr int kBoardModeSelPin = 4;

// Læses ÉN gang ved boot (modbus_channel_init_all()) — se kBoardModeSelPin's
// kommentar. `execute_transaction()` og REST-laget (via
// modbus_channel_get_config()) bruger denne, ALDRIG et gemt/PUT'et felt.
mb_channel_mode_t g_hardware_mode = MB_CHANNEL_MODE_RS485;

mb_channel_mode_t read_board_mode_sel() {
  pinMode(kBoardModeSelPin, INPUT_PULLUP);
  delayMicroseconds(10);  // lad evt. parasitkapacitans paa linjen naa at settle efter pinMode-skiftet
  return digitalRead(kBoardModeSelPin) == HIGH ? MB_CHANNEL_MODE_RS485 : MB_CHANNEL_MODE_RS232;
}

enum class ChannelRequestType : uint8_t { kTransaction, kReconfigure };

struct ChannelRequest {
  ChannelRequestType type;

  // kTransaction:
  uint8_t slave_id;
  const uint8_t *pdu;
  size_t pdu_len;
  uint8_t *out_pdu;
  size_t *out_pdu_len;
  size_t out_pdu_capacity;

  // kReconfigure:
  mb_channel_config_t new_config;

  mb_error_code_t result;
  SemaphoreHandle_t done;
};

struct ChannelContext {
  const char *name;
  size_t config_index;  // 0=A, 1=B — index ind i mb_board_config_t::channel[]
  HardwareSerial *serial;
  int tx_pin;
  int rx_pin;
  int dir_pin;
  mb_channel_config_t config;
  mb_channel_stats_t stats;
  QueueHandle_t queue;
};

HardwareSerial g_serialA(1);  // UART-periferi #1 (§2.0: udelukkende brugt her, ikke af CLI'en som ejer UART0)
HardwareSerial g_serialB(2);  // UART-periferi #2

ChannelContext g_channelA;
ChannelContext g_channelB;

ChannelContext &context_for(ModbusChannelId channel) { return (channel == ModbusChannelId::kA) ? g_channelA : g_channelB; }

// §BUGS.md v0.9.0.1: kanal-fejl skal logges struktureret via seriel konsol
// (CLAUDE.md regel 11) — uden dette er en fejlende RTU-transaktion usynlig
// for installatøren, der kun ser en generisk Modbus TCP-gateway-exception.
const char *error_name(mb_error_code_t error) {
  switch (error) {
    case MB_OK: return "MB_OK";
    case MB_TIMEOUT: return "MB_TIMEOUT";
    case MB_CRC_ERROR: return "MB_CRC_ERROR";
    case MB_EXCEPTION: return "MB_EXCEPTION";
    case MB_MAX_REQUESTS_EXCEEDED: return "MB_MAX_REQUESTS_EXCEEDED";
    case MB_NOT_ENABLED: return "MB_NOT_ENABLED";
    case MB_INVALID_SLAVE: return "MB_INVALID_SLAVE";
    case MB_INVALID_ADDRESS: return "MB_INVALID_ADDRESS";
    case MB_BUS_BUSY: return "MB_BUS_BUSY";
    case MB_CHANNEL_UNREACHABLE: return "MB_CHANNEL_UNREACHABLE";
    default: return "?";
  }
}

// Mapper mb_channel_parity_t/stop_bits til Arduino/ESP32's SERIAL_8xx-config.
// Altid 8 databits — hverken §4.2 eller Modbus RTU-praksis eksponerer andet.
uint32_t serial_config_for(mb_channel_parity_t parity, uint8_t stop_bits) {
  if (stop_bits == 2) {
    switch (parity) {
      case MB_CHANNEL_PARITY_EVEN: return SERIAL_8E2;
      case MB_CHANNEL_PARITY_ODD: return SERIAL_8O2;
      case MB_CHANNEL_PARITY_NONE:
      default: return SERIAL_8N2;
    }
  }
  switch (parity) {
    case MB_CHANNEL_PARITY_EVEN: return SERIAL_8E1;
    case MB_CHANNEL_PARITY_ODD: return SERIAL_8O1;
    case MB_CHANNEL_PARITY_NONE:
    default: return SERIAL_8N1;
  }
}

// Anvender en (ny eller initial) config på kanalens rigtige UART/GPIO'er.
// Kaldes UDELUKKENDE fra kanalens egen task (channel_task) — aldrig direkte
// fra REST-/TCP-lagets tasks, jf. modbus_channel_apply_config()'s
// kø-baserede design. `mode` i `new_config` IGNORERES bevidst — MODE_SEL er
// en input (fabriksvalgt, se kBoardModeSelPin), ikke noget en reconfigure
// kan ændre; `g_hardware_mode` (læst ved boot) bruges altid i stedet.
void apply_config_now(ChannelContext &ctx, const mb_channel_config_t &new_config) {
  ctx.config = new_config;
  ctx.config.mode = g_hardware_mode;

  digitalWrite(ctx.dir_pin, LOW);

  ctx.serial->end();
  ctx.serial->begin(ctx.config.baudrate, serial_config_for(ctx.config.parity, ctx.config.stop_bits), ctx.rx_pin,
                     ctx.tx_pin);
}

// Selve RTU-transaktionen — direkte portering af mønsteret i
// reference-plc-source/src/modbus_master.cpp:modbus_master_send_request()
// (to-fase timeout, DE/RE-toggling), men genbruger lib/modbus_pdu til
// framing/CRC/svar-komplethed i stedet for at gentage den logik.
mb_error_code_t execute_transaction(ChannelContext &ctx, uint8_t slave_id, const uint8_t *pdu, size_t pdu_len,
                                     uint8_t *out_pdu, size_t *out_pdu_len, size_t out_pdu_capacity) {
  size_t expected_len = 0;
  if (mb_pdu_expected_response_frame_len(pdu, pdu_len, &expected_len) != MB_PDU_VALID) {
    return MB_INVALID_ADDRESS;  // ukendt/ugyldig function code eller quantity — se lib/modbus_pdu
  }

  uint8_t frame[MB_RTU_FRAME_MAX_LEN];
  const size_t frame_len = mb_pdu_build_rtu_request(slave_id, pdu, pdu_len, frame, sizeof(frame));
  if (frame_len == 0) {
    return MB_INVALID_ADDRESS;
  }

  const bool is_rs485 = ctx.config.mode == MB_CHANNEL_MODE_RS485;

  // Tøm evt. støj fra bussen inden vi selv sender — TIDSBEGRÆNSET (§BUGS.md
  // v0.9.0.1): uden denne grænse kan en kontinuerligt støjende/floating
  // RX-linje (fx manglende terminering/bias-modstande på en RS485-bus)
  // holde denne løkke kørende for evigt, og dermed hænge HELE kanal-tasken
  // permanent efter blot ét kald.
  {
    const uint32_t drain_start = millis();
    while (ctx.serial->available() && millis() - drain_start < 50) {
      ctx.serial->read();
    }
  }

  // RS485 (§2.2.1/§2.0.1): DE/RE toggles omkring selve sendingen. RS232:
  // fuld-duplex, dir-pinden røres slet ikke (§4.2's afklaring — mode er en
  // deployment-tids-beslutning, ikke noget der skifter live).
  if (is_rs485) {
    digitalWrite(ctx.dir_pin, HIGH);
    delayMicroseconds(50);
  }

  ctx.serial->write(frame, frame_len);
  ctx.serial->flush();

  if (is_rs485) {
    const uint32_t byte_us = (11UL * 1000000UL) / ctx.config.baudrate;
    delayMicroseconds(byte_us + 100);
    digitalWrite(ctx.dir_pin, LOW);
  }

  // To-fase timeout: fuld timeout til FØRSTE byte, kort inter-character-
  // timeout herefter (samme filosofi som reference-implementeringen).
  uint8_t response[MB_RTU_FRAME_MAX_LEN];
  size_t received = 0;
  uint32_t interchar_ms = 38500UL / ctx.config.baudrate;
  if (interchar_ms < 2) interchar_ms = 2;
  if (interchar_ms > 20) interchar_ms = 20;

  // BUGS.md v0.9.0.1: `last_byte_time` opdateres PR. modtaget byte — måles
  // inter-character-timeouten mod den samlede transaktions starttidspunkt
  // (fast `start`) i stedet, udløber den næsten altid med det samme efter
  // FØRSTE byte, og en ægte fler-byte-respons ville aldrig kunne læses færdig.
  uint32_t last_byte_time = millis();
  bool timed_out = false;
  while (received < sizeof(response)) {
    const uint32_t active_timeout = (received == 0) ? ctx.config.timeout_ms : interchar_ms;
    if (millis() - last_byte_time > active_timeout) {
      timed_out = true;
      break;
    }
    if (ctx.serial->available()) {
      response[received++] = static_cast<uint8_t>(ctx.serial->read());
      last_byte_time = millis();
      if (mb_pdu_response_frame_complete(pdu, pdu_len, response, received)) {
        break;
      }
    } else {
      delay(1);
    }
  }

  if (ctx.config.inter_frame_delay_ms > 0) {
    delay(ctx.config.inter_frame_delay_ms);
  }

  if (timed_out || received == 0) {
    return MB_TIMEOUT;
  }

  const mb_pdu_parse_result_t parse_result =
      mb_pdu_parse_rtu_response(slave_id, response, received, out_pdu, out_pdu_len, out_pdu_capacity);
  switch (parse_result) {
    case MB_PDU_RESULT_OK:
    case MB_PDU_RESULT_EXCEPTION:
      // MB_PDU_RESULT_EXCEPTION er IKKE en kanal-fejl — det er stadig et
      // gyldigt, modtaget svar (en Modbus-exception er semantisk indhold,
      // ikke en transportfejl) — out_pdu er allerede udfyldt med
      // exception-PDU'en, og TCP-laget skal blot relaye den uændret.
      return MB_OK;
    case MB_PDU_RESULT_CRC_ERROR:
      return MB_CRC_ERROR;
    case MB_PDU_RESULT_SLAVE_MISMATCH:
      return MB_INVALID_SLAVE;
    case MB_PDU_RESULT_TOO_SHORT:
    case MB_PDU_RESULT_BUFFER_TOO_SMALL:
    default:
      return MB_CHANNEL_UNREACHABLE;
  }
}

void record_stats(ChannelContext &ctx, const ChannelRequest &req) {
  ctx.stats.total_requests++;
  if (req.result == MB_OK) {
    ctx.stats.successful_requests++;
    return;
  }

  switch (req.result) {
    case MB_TIMEOUT:
      ctx.stats.timeout_errors++;
      break;
    case MB_CRC_ERROR:
      ctx.stats.crc_errors++;
      break;
    case MB_EXCEPTION:
      ctx.stats.exception_errors++;
      break;
    default:
      break;
  }

  ctx.stats.has_last_error = true;
  ctx.stats.last_error_slave_id = req.slave_id;
  // FC01-06/16's adresse ligger altid i PDU-byte 1-2 (big-endian) — se
  // lib/modbus_pdu's understøttede function codes.
  ctx.stats.last_error_address = (req.pdu_len >= 3) ? static_cast<uint16_t>((req.pdu[1] << 8) | req.pdu[2]) : 0;
  ctx.stats.last_error_type = static_cast<uint8_t>(req.result);
  ctx.stats.last_error_at_uptime_s = millis() / 1000;
}

void channel_task(void *param) {
  ChannelContext *ctx = static_cast<ChannelContext *>(param);
  for (;;) {
    ChannelRequest *req = nullptr;
    if (xQueueReceive(ctx->queue, &req, portMAX_DELAY) != pdTRUE || req == nullptr) {
      continue;
    }

    if (req->type == ChannelRequestType::kReconfigure) {
      apply_config_now(*ctx, req->new_config);
      req->result = MB_OK;
      xSemaphoreGive(req->done);
      continue;
    }

    if (!ctx->config.enabled) {
      req->result = MB_NOT_ENABLED;
    } else {
      req->result = execute_transaction(*ctx, req->slave_id, req->pdu, req->pdu_len, req->out_pdu, req->out_pdu_len,
                                         req->out_pdu_capacity);
    }

    record_stats(*ctx, *req);

    if (req->result != MB_OK) {
      Serial.print("MODBUS-FEJL kanal ");
      Serial.print(ctx->name);
      Serial.print(": slave=");
      Serial.print(req->slave_id);
      Serial.print(" fc=");
      Serial.print(req->pdu_len > 0 ? req->pdu[0] : 0);
      Serial.print(" -> ");
      Serial.println(error_name(req->result));
    }
    xSemaphoreGive(req->done);
  }
}

void init_channel(ChannelContext &ctx, HardwareSerial &serial, size_t config_index, int tx_pin, int rx_pin,
                   int dir_pin, const char *task_name, const mb_channel_config_t &initial_config) {
  ctx.name = task_name;
  ctx.config_index = config_index;
  ctx.serial = &serial;
  ctx.tx_pin = tx_pin;
  ctx.rx_pin = rx_pin;
  ctx.dir_pin = dir_pin;
  ctx.stats = mb_channel_stats_t{};

  pinMode(ctx.dir_pin, OUTPUT);
  digitalWrite(ctx.dir_pin, LOW);

  apply_config_now(ctx, initial_config);

  ctx.queue = xQueueCreate(4, sizeof(ChannelRequest *));
  xTaskCreate(channel_task, task_name, 4096, &ctx, tskIDLE_PRIORITY + 1, nullptr);
}

}  // namespace

void modbus_channel_init_all() {
  // Hardware-revision 2026-09-14: MODE_SEL (GPIO4) er en INPUT, sat ved
  // fremstilling — læses HER, én gang, og gælder resten af boardets
  // levetid (indtil næste genstart). `mode` i den persisterede config
  // (fra en evt. ældre firmware-version, hvor det var software-sat)
  // IGNORERES bevidst og overskrives altid med den fabriksvalgte værdi.
  g_hardware_mode = read_board_mode_sel();
  Serial.print("MODE_SEL (GPIO4) laest ved boot: ");
  Serial.println(g_hardware_mode == MB_CHANNEL_MODE_RS485 ? "RS485" : "RS232");

  mb_channel_config_t config_a = config_get().channel[0];  // §4.2: persisteret config, ikke hardkodet
  mb_channel_config_t config_b = config_get().channel[1];
  config_a.mode = g_hardware_mode;
  config_b.mode = g_hardware_mode;

  init_channel(g_channelA, g_serialA, 0, kChannelATx, kChannelARx, kChannelADir, "mb_ch_a", config_a);
  init_channel(g_channelB, g_serialB, 1, kChannelBTx, kChannelBRx, kChannelBDir, "mb_ch_b", config_b);
}

mb_error_code_t modbus_channel_submit(ModbusChannelId channel, uint8_t slave_id, const uint8_t *pdu, size_t pdu_len,
                                       uint8_t *out_pdu, size_t *out_pdu_len, size_t out_pdu_capacity) {
  ChannelContext &ctx = context_for(channel);

  if (!ctx.config.enabled) {
    return MB_NOT_ENABLED;  // hurtig afvisning — ingen grund til at vaekke kanal-tasken
  }

  ChannelRequest req;
  req.type = ChannelRequestType::kTransaction;
  req.slave_id = slave_id;
  req.pdu = pdu;
  req.pdu_len = pdu_len;
  req.out_pdu = out_pdu;
  req.out_pdu_len = out_pdu_len;
  req.out_pdu_capacity = out_pdu_capacity;
  req.result = MB_CHANNEL_UNREACHABLE;
  req.done = xSemaphoreCreateBinary();
  if (req.done == nullptr) {
    return MB_CHANNEL_UNREACHABLE;
  }

  ChannelRequest *req_ptr = &req;
  if (xQueueSend(ctx.queue, &req_ptr, pdMS_TO_TICKS(1000)) != pdTRUE) {
    vSemaphoreDelete(req.done);
    return MB_BUS_BUSY;  // kanalens kø var fuld — en anden transaktion optager den
  }

  // Vent lidt laengere end kanalens egen timeout, saa vi altid faar
  // kanal-tasken resultat (MB_TIMEOUT) fremfor selv at time ud foerst.
  if (xSemaphoreTake(req.done, pdMS_TO_TICKS(ctx.config.timeout_ms + 1000)) != pdTRUE) {
    vSemaphoreDelete(req.done);
    return MB_TIMEOUT;
  }

  vSemaphoreDelete(req.done);
  return req.result;
}

namespace {

bool apply_config_to_channel(ChannelContext &ctx, const mb_channel_config_t &new_config) {
  ChannelRequest req;
  req.type = ChannelRequestType::kReconfigure;
  req.new_config = new_config;
  req.result = MB_CHANNEL_UNREACHABLE;
  req.done = xSemaphoreCreateBinary();
  if (req.done == nullptr) {
    return false;
  }

  ChannelRequest *req_ptr = &req;
  if (xQueueSend(ctx.queue, &req_ptr, pdMS_TO_TICKS(1000)) != pdTRUE) {
    vSemaphoreDelete(req.done);
    return false;
  }

  const bool ok = xSemaphoreTake(req.done, pdMS_TO_TICKS(2000)) == pdTRUE && req.result == MB_OK;
  vSemaphoreDelete(req.done);
  return ok;
}

}  // namespace

bool modbus_channel_apply_config(ModbusChannelId channel, const mb_channel_config_t &new_config) {
  ChannelContext &ctx = context_for(channel);
  if (!apply_config_to_channel(ctx, new_config)) {
    return false;
  }
  // Persistér ctx.config (EFTER apply_config_now() har tvunget mode til
  // g_hardware_mode), ikke den rå new_config-parameter — ellers ville en
  // forældet/vilkårlig mode-værdi fra kaldsleddet (fx en tom/default-
  // initialiseret struct, nu hvor mode ikke længere parses fra PUT-JSON,
  // se lib/channel_config) kunne blive skrevet til NVS i stedet for den
  // faktiske MODE_SEL-hardwareværdi.
  config_set_channel(ctx.config_index, ctx.config);
  return true;
}

mb_channel_config_t modbus_channel_get_config(ModbusChannelId channel) { return context_for(channel).config; }

mb_channel_stats_t modbus_channel_get_stats(ModbusChannelId channel) { return context_for(channel).stats; }
