#include "modbus_pdu.h"

#include <cstring>

uint16_t mb_pdu_calc_crc16(const uint8_t *buffer, size_t len) {
  uint16_t crc = 0xFFFF;

  for (size_t i = 0; i < len; i++) {
    crc ^= buffer[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }

  return crc;
}

size_t mb_pdu_build_rtu_request(uint8_t slave_id, const uint8_t *pdu, size_t pdu_len, uint8_t *out_frame,
                                 size_t out_capacity) {
  if (pdu == nullptr || out_frame == nullptr || pdu_len == 0 || pdu_len > MB_PDU_MAX_LEN) {
    return 0;
  }

  const size_t frame_len = 1 + pdu_len + 2;
  if (out_capacity < frame_len) {
    return 0;
  }

  out_frame[0] = slave_id;
  memcpy(out_frame + 1, pdu, pdu_len);

  const uint16_t crc = mb_pdu_calc_crc16(out_frame, 1 + pdu_len);
  out_frame[1 + pdu_len] = crc & 0xFF;
  out_frame[1 + pdu_len + 1] = (crc >> 8) & 0xFF;

  return frame_len;
}

// v0.28.0 (DESIGN_GUIDE_MODBUS_EXPANSION_FC_CAPABILITIES.md §1) — SKAL
// holdes i sync med switch-casene lige nedenfor, se modbus_pdu.h's
// kommentar ved MB_PDU_SUPPORTED_FUNCTIONS.
const uint8_t MB_PDU_SUPPORTED_FUNCTIONS[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x0F, 0x10};

mb_pdu_validation_t mb_pdu_expected_response_frame_len(const uint8_t *request_pdu, size_t request_pdu_len,
                                                        size_t *out_expected_len) {
  if (request_pdu == nullptr || out_expected_len == nullptr || request_pdu_len == 0) {
    return MB_PDU_MALFORMED_REQUEST;
  }

  const uint8_t fc = request_pdu[0];

  switch (fc) {
    case 0x01:  // Read Coils
    case 0x02: {  // Read Discrete Inputs
      if (request_pdu_len != 5) return MB_PDU_MALFORMED_REQUEST;
      const uint16_t qty = (static_cast<uint16_t>(request_pdu[3]) << 8) | request_pdu[4];
      if (qty == 0 || qty > MB_PDU_MAX_READ_BIT_QUANTITY) return MB_PDU_MALFORMED_REQUEST;
      const size_t byte_count = (static_cast<size_t>(qty) + 7) / 8;
      *out_expected_len = 1 + 1 + 1 + byte_count + 2;
      return MB_PDU_VALID;
    }
    case 0x03:  // Read Holding Registers
    case 0x04: {  // Read Input Registers
      if (request_pdu_len != 5) return MB_PDU_MALFORMED_REQUEST;
      const uint16_t qty = (static_cast<uint16_t>(request_pdu[3]) << 8) | request_pdu[4];
      if (qty == 0 || qty > MB_PDU_MAX_READ_REGISTER_QUANTITY) return MB_PDU_MALFORMED_REQUEST;
      *out_expected_len = 1 + 1 + 1 + static_cast<size_t>(qty) * 2 + 2;
      return MB_PDU_VALID;
    }
    case 0x05:  // Write Single Coil
    case 0x06: {  // Write Single Register
      if (request_pdu_len != 5) return MB_PDU_MALFORMED_REQUEST;
      *out_expected_len = 1 + 5 + 2;  // svaret ekkoer requestets PDU 1:1
      return MB_PDU_VALID;
    }
    case 0x0F: {  // Write Multiple Coils (FC15)
      if (request_pdu_len < 6) return MB_PDU_MALFORMED_REQUEST;
      const uint16_t qty = (static_cast<uint16_t>(request_pdu[3]) << 8) | request_pdu[4];
      const uint8_t byte_count = request_pdu[5];
      const uint8_t expected_byte_count = static_cast<uint8_t>((qty + 7) / 8);
      if (qty == 0 || qty > MB_PDU_MAX_WRITE_COIL_QUANTITY || byte_count != expected_byte_count ||
          request_pdu_len != static_cast<size_t>(6 + byte_count)) {
        return MB_PDU_MALFORMED_REQUEST;
      }
      *out_expected_len = 1 + 1 + 2 + 2 + 2;  // svar = adresse+fc+startadresse(2)+quantity(2)+CRC(2)
      return MB_PDU_VALID;
    }
    case 0x10: {  // Write Multiple Registers (FC16)
      if (request_pdu_len < 6) return MB_PDU_MALFORMED_REQUEST;
      const uint16_t qty = (static_cast<uint16_t>(request_pdu[3]) << 8) | request_pdu[4];
      const uint8_t byte_count = request_pdu[5];
      if (qty == 0 || qty > MB_PDU_MAX_WRITE_REGISTER_QUANTITY || byte_count != qty * 2 ||
          request_pdu_len != static_cast<size_t>(6 + byte_count)) {
        return MB_PDU_MALFORMED_REQUEST;
      }
      *out_expected_len = 1 + 1 + 2 + 2 + 2;  // svar = adresse+fc+startadresse(2)+quantity(2)+CRC(2)
      return MB_PDU_VALID;
    }
    default:
      return MB_PDU_UNSUPPORTED_FUNCTION;
  }
}

bool mb_pdu_response_frame_complete(const uint8_t *request_pdu, size_t request_pdu_len, const uint8_t *frame_so_far,
                                     size_t received_len) {
  if (frame_so_far == nullptr || received_len < 2) return false;

  const uint8_t function_code = frame_so_far[1];
  if (function_code & 0x80) {
    return received_len >= MB_RTU_EXCEPTION_FRAME_LEN;
  }

  size_t expected_len = 0;
  if (mb_pdu_expected_response_frame_len(request_pdu, request_pdu_len, &expected_len) != MB_PDU_VALID) {
    return false;
  }

  // Bruger den SELV-udledte forventede længde (fra vores egen forespørgsel),
  // ikke slavens ekkoede byte_count-felt (frame_so_far[2]) — en korrupt eller
  // ondsindet byte_count kan derfor ikke få funktionen til at afslutte for
  // tidligt eller vente for evigt.
  return received_len >= expected_len;
}

mb_pdu_parse_result_t mb_pdu_parse_rtu_response(uint8_t expected_slave_id, const uint8_t *frame, size_t frame_len,
                                                 uint8_t *out_pdu, size_t *out_pdu_len, size_t out_pdu_capacity) {
  if (frame == nullptr || out_pdu == nullptr || out_pdu_len == nullptr || frame_len < 4) {
    return MB_PDU_RESULT_TOO_SHORT;
  }

  const uint16_t received_crc = (static_cast<uint16_t>(frame[frame_len - 1]) << 8) | frame[frame_len - 2];
  const uint16_t calculated_crc = mb_pdu_calc_crc16(frame, frame_len - 2);
  if (received_crc != calculated_crc) {
    return MB_PDU_RESULT_CRC_ERROR;
  }

  if (frame[0] != expected_slave_id) {
    return MB_PDU_RESULT_SLAVE_MISMATCH;
  }

  const size_t pdu_len = frame_len - 1 - 2;
  if (pdu_len > out_pdu_capacity) {
    return MB_PDU_RESULT_BUFFER_TOO_SMALL;
  }
  memcpy(out_pdu, frame + 1, pdu_len);
  *out_pdu_len = pdu_len;

  if (pdu_len >= 1 && (out_pdu[0] & 0x80)) {
    return MB_PDU_RESULT_EXCEPTION;
  }

  return MB_PDU_RESULT_OK;
}
