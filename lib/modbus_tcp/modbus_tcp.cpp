#include "modbus_tcp.h"

#include <cstring>

#include "modbus_pdu.h"

mb_mbap_result_t mb_mbap_parse_header(const uint8_t *buf, size_t len, mb_mbap_header_t *out_header) {
  if (buf == nullptr || out_header == nullptr || len < MB_MBAP_HEADER_LEN) {
    return MB_MBAP_TOO_SHORT;
  }

  out_header->transaction_id = static_cast<uint16_t>((buf[0] << 8) | buf[1]);
  out_header->protocol_id = static_cast<uint16_t>((buf[2] << 8) | buf[3]);
  out_header->length = static_cast<uint16_t>((buf[4] << 8) | buf[5]);
  out_header->unit_id = buf[6];

  if (out_header->protocol_id != MB_MBAP_PROTOCOL_ID) {
    return MB_MBAP_INVALID_PROTOCOL_ID;
  }
  return MB_MBAP_OK;
}

mb_mbap_result_t mb_mbap_extract_pdu(const uint8_t *adu, size_t total_len, mb_mbap_header_t *out_header,
                                      const uint8_t **out_pdu, size_t *out_pdu_len) {
  const mb_mbap_result_t header_result = mb_mbap_parse_header(adu, total_len, out_header);
  if (header_result != MB_MBAP_OK) {
    return header_result;
  }

  // header.length taeller Unit ID (1 byte) + PDU — saa PDU-laengden er
  // length - 1, og den samlede ADU skal vaere praecis MB_MBAP_HEADER_LEN +
  // (length - 1) bytes (Unit ID ligger i selve headeren, IKKE gentaget i
  // PDU-delen).
  if (out_header->length == 0) {
    return MB_MBAP_LENGTH_MISMATCH;  // length skal mindst daekke Unit ID'et selv
  }
  const size_t expected_pdu_len = out_header->length - 1;
  if (expected_pdu_len > MB_PDU_MAX_LEN) {
    return MB_MBAP_PDU_TOO_LARGE;
  }
  if (total_len != MB_MBAP_HEADER_LEN + expected_pdu_len) {
    return MB_MBAP_LENGTH_MISMATCH;
  }

  *out_pdu = adu + MB_MBAP_HEADER_LEN;
  *out_pdu_len = expected_pdu_len;
  return MB_MBAP_OK;
}

size_t mb_mbap_build_response(uint16_t transaction_id, uint8_t unit_id, const uint8_t *pdu, size_t pdu_len,
                               uint8_t *out_buf, size_t out_capacity) {
  if (out_buf == nullptr || out_capacity < MB_MBAP_HEADER_LEN + pdu_len) {
    return 0;
  }

  const uint16_t length = static_cast<uint16_t>(pdu_len + 1);  // + Unit ID
  out_buf[0] = static_cast<uint8_t>((transaction_id >> 8) & 0xFF);
  out_buf[1] = static_cast<uint8_t>(transaction_id & 0xFF);
  out_buf[2] = static_cast<uint8_t>((MB_MBAP_PROTOCOL_ID >> 8) & 0xFF);
  out_buf[3] = static_cast<uint8_t>(MB_MBAP_PROTOCOL_ID & 0xFF);
  out_buf[4] = static_cast<uint8_t>((length >> 8) & 0xFF);
  out_buf[5] = static_cast<uint8_t>(length & 0xFF);
  out_buf[6] = unit_id;

  if (pdu_len > 0) {
    memcpy(out_buf + MB_MBAP_HEADER_LEN, pdu, pdu_len);
  }
  return MB_MBAP_HEADER_LEN + pdu_len;
}
