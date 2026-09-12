#include "modbus_channel.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace {

// GPIO-allokering — EXPANSION_BOARD_DESIGN.md §2.0.1, ESP32-WROOM-32 DevKit.
constexpr int kChannelATx = 17;
constexpr int kChannelARx = 16;
constexpr int kChannelAModeSel = 4;
constexpr int kChannelADir = 27;

constexpr int kChannelBTx = 18;
constexpr int kChannelBRx = 19;
constexpr int kChannelBModeSel = 23;
constexpr int kChannelBDir = 25;

// v1-placeholder: kanal-config-REST-endpointet (Fase 5, resten) findes ikke
// endnu, så baudrate/parity/mode er fast hardkodet her — samme defaults som
// PLC-repoets egen Master #1 (`reference-plc-source`). Erstattes af rigtig
// pr.-kanal-config når `PUT /api/channels/{n}/config` bygges.
constexpr uint32_t kDefaultBaud = 9600;
constexpr uint32_t kDefaultTimeoutMs = 500;
constexpr bool kDefaultIsRs485 = true;  // begge kanaler RS485 som default (§2.2.1's mode-valg er endnu ikke REST-styret)

struct ChannelRequest {
  uint8_t slave_id;
  const uint8_t *pdu;
  size_t pdu_len;
  uint8_t *out_pdu;
  size_t *out_pdu_len;
  size_t out_pdu_capacity;
  mb_error_code_t result;
  SemaphoreHandle_t done;
};

struct ChannelContext {
  HardwareSerial *serial;
  int dir_pin;
  int mode_sel_pin;
  bool is_rs485;
  uint32_t baud;
  uint32_t timeout_ms;
  QueueHandle_t queue;
};

HardwareSerial g_serialA(1);  // UART-periferi #1 (§2.0: udelukkende brugt her, ikke af CLI'en som ejer UART0)
HardwareSerial g_serialB(2);  // UART-periferi #2

ChannelContext g_channelA;
ChannelContext g_channelB;

ChannelContext &context_for(ModbusChannelId channel) { return (channel == ModbusChannelId::kA) ? g_channelA : g_channelB; }

// Mapper mb_pdu_parse_rtu_response()'s resultat til mb_error_code_t (§4).
// MB_PDU_RESULT_EXCEPTION regnes IKKE som en kanal-fejl — det er stadig et
// gyldigt, modtaget svar (en Modbus-exception er semantisk indhold, ikke en
// transportfejl) — out_pdu er allerede udfyldt med exception-PDU'en af
// parse-funktionen, og TCP-laget skal blot relaye den uændret.
mb_error_code_t map_parse_result(mb_pdu_parse_result_t result) {
  switch (result) {
    case MB_PDU_RESULT_OK:
    case MB_PDU_RESULT_EXCEPTION:
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

  while (ctx.serial->available()) {
    ctx.serial->read();  // tøm evt. støj fra bussen inden vi selv sender
  }

  // RS485 (§2.2.1/§2.0.1): DE/RE toggles omkring selve sendingen. RS232:
  // fuld-duplex, dir-pinden røres slet ikke (§4.2's afklaring — mode er en
  // deployment-tids-beslutning, ikke noget der skifter live).
  if (ctx.is_rs485) {
    digitalWrite(ctx.dir_pin, HIGH);
    delayMicroseconds(50);
  }

  ctx.serial->write(frame, frame_len);
  ctx.serial->flush();

  if (ctx.is_rs485) {
    const uint32_t byte_us = (11UL * 1000000UL) / ctx.baud;
    delayMicroseconds(byte_us + 100);
    digitalWrite(ctx.dir_pin, LOW);
  }

  // To-fase timeout: fuld timeout til FØRSTE byte, kort inter-character-
  // timeout herefter (samme filosofi som reference-implementeringen).
  uint8_t response[MB_RTU_FRAME_MAX_LEN];
  size_t received = 0;
  uint32_t interchar_ms = 38500UL / ctx.baud;
  if (interchar_ms < 2) interchar_ms = 2;
  if (interchar_ms > 20) interchar_ms = 20;

  const uint32_t start = millis();
  bool timed_out = false;
  while (received < sizeof(response)) {
    const uint32_t active_timeout = (received == 0) ? ctx.timeout_ms : interchar_ms;
    if (millis() - start > active_timeout) {
      timed_out = true;
      break;
    }
    if (ctx.serial->available()) {
      response[received++] = static_cast<uint8_t>(ctx.serial->read());
      if (mb_pdu_response_frame_complete(pdu, pdu_len, response, received)) {
        break;
      }
    } else {
      delay(1);
    }
  }

  if (timed_out || received == 0) {
    return MB_TIMEOUT;
  }

  const mb_pdu_parse_result_t parse_result =
      mb_pdu_parse_rtu_response(slave_id, response, received, out_pdu, out_pdu_len, out_pdu_capacity);
  return map_parse_result(parse_result);
}

void channel_task(void *param) {
  ChannelContext *ctx = static_cast<ChannelContext *>(param);
  for (;;) {
    ChannelRequest *req = nullptr;
    if (xQueueReceive(ctx->queue, &req, portMAX_DELAY) == pdTRUE && req != nullptr) {
      req->result = execute_transaction(*ctx, req->slave_id, req->pdu, req->pdu_len, req->out_pdu, req->out_pdu_len,
                                         req->out_pdu_capacity);
      xSemaphoreGive(req->done);
    }
  }
}

void init_channel(ChannelContext &ctx, HardwareSerial &serial, int tx_pin, int rx_pin, int mode_sel_pin, int dir_pin,
                   const char *task_name) {
  ctx.serial = &serial;
  ctx.dir_pin = dir_pin;
  ctx.mode_sel_pin = mode_sel_pin;
  ctx.is_rs485 = kDefaultIsRs485;
  ctx.baud = kDefaultBaud;
  ctx.timeout_ms = kDefaultTimeoutMs;

  pinMode(ctx.dir_pin, OUTPUT);
  digitalWrite(ctx.dir_pin, LOW);
  pinMode(ctx.mode_sel_pin, OUTPUT);
  digitalWrite(ctx.mode_sel_pin, ctx.is_rs485 ? HIGH : LOW);  // §2.0.1: modevalg-GPIO, HIGH=RS485 (vilkårlig men dokumenteret polaritet)

  serial.begin(ctx.baud, SERIAL_8N1, rx_pin, tx_pin);

  ctx.queue = xQueueCreate(4, sizeof(ChannelRequest *));
  xTaskCreate(channel_task, task_name, 4096, &ctx, tskIDLE_PRIORITY + 1, nullptr);
}

}  // namespace

void modbus_channel_init_all() {
  init_channel(g_channelA, g_serialA, kChannelATx, kChannelARx, kChannelAModeSel, kChannelADir, "mb_ch_a");
  init_channel(g_channelB, g_serialB, kChannelBTx, kChannelBRx, kChannelBModeSel, kChannelBDir, "mb_ch_b");
}

mb_error_code_t modbus_channel_submit(ModbusChannelId channel, uint8_t slave_id, const uint8_t *pdu, size_t pdu_len,
                                       uint8_t *out_pdu, size_t *out_pdu_len, size_t out_pdu_capacity) {
  ChannelContext &ctx = context_for(channel);

  ChannelRequest req;
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
  if (xSemaphoreTake(req.done, pdMS_TO_TICKS(ctx.timeout_ms + 1000)) != pdTRUE) {
    vSemaphoreDelete(req.done);
    return MB_TIMEOUT;
  }

  vSemaphoreDelete(req.done);
  return req.result;
}
