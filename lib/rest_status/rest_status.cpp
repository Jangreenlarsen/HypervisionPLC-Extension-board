#include "rest_status.h"

#include <cstdio>

namespace {

// Samme lille, lokale mode->streng-mapper som lib/channel_config/
// channel_config.cpp har (anonymous namespace, ikke delt på tværs af
// moduler — jf. CLAUDE.md's princip om at holde moduler begrebsmæssigt
// adskilte fremfor at dele triviel formateringslogik).
const char *mode_to_string(mb_channel_mode_t mode) { return mode == MB_CHANNEL_MODE_RS232 ? "rs232" : "rs485"; }

// Bygger "{"connected":true,"ip":"...","rssi_dbm":N}" eller "{"connected":false}"
// — genbrugt for BÅDE wifi- og (uden rssi_dbm) ethernet-feltet nedenfor.
int append_wifi_object(char *out, size_t out_capacity, const mb_status_data_t *data) {
  if (data->wifi_connected) {
    return snprintf(out, out_capacity, "\"wifi\":{\"connected\":true,\"ip\":\"%s\",\"rssi_dbm\":%d}", data->wifi_ip,
                     static_cast<int>(data->wifi_rssi_dbm));
  }
  return snprintf(out, out_capacity, "\"wifi\":{\"connected\":false}");
}

int append_ethernet_object(char *out, size_t out_capacity, const mb_status_data_t *data) {
  if (data->eth_connected) {
    return snprintf(out, out_capacity, "\"ethernet\":{\"connected\":true,\"ip\":\"%s\",\"status\":\"%s\"}", data->eth_ip,
                     data->eth_status);
  }
  return snprintf(out, out_capacity, "\"ethernet\":{\"connected\":false,\"status\":\"%s\"}", data->eth_status);
}

}  // namespace

size_t mb_status_build_json(const mb_status_data_t *data, char *out, size_t out_capacity) {
  const int header_written = snprintf(out, out_capacity,
                                       "{"
                                       "\"api_version\":%u,"
                                       "\"fw_version\":\"%s\","
                                       "\"fw_build\":\"%s\","
                                       "\"uptime_s\":%lu,"
                                       "\"heap_free_bytes\":%lu,"
                                       "\"active_channels\":%u,"
                                       "\"provisioned\":%s,"
                                       "\"board_mode\":\"%s\",",
                                       static_cast<unsigned>(MB_REST_API_VERSION), data->fw_version, data->fw_build,
                                       static_cast<unsigned long>(data->uptime_s),
                                       static_cast<unsigned long>(data->heap_free_bytes),
                                       static_cast<unsigned>(data->active_channels), data->provisioned ? "true" : "false",
                                       mode_to_string(data->board_mode));
  if (header_written <= 0 || static_cast<size_t>(header_written) >= out_capacity) {
    return 0;
  }
  size_t offset = static_cast<size_t>(header_written);

  const int wifi_written = append_wifi_object(out + offset, out_capacity - offset, data);
  if (wifi_written <= 0 || static_cast<size_t>(wifi_written) >= out_capacity - offset) {
    return 0;
  }
  offset += static_cast<size_t>(wifi_written);

  if (offset + 1 >= out_capacity) return 0;
  out[offset++] = ',';

  const int eth_written = append_ethernet_object(out + offset, out_capacity - offset, data);
  if (eth_written <= 0 || static_cast<size_t>(eth_written) >= out_capacity - offset) {
    return 0;
  }
  offset += static_cast<size_t>(eth_written);

  if (offset + 2 >= out_capacity) return 0;
  out[offset++] = '}';
  out[offset] = '\0';
  return offset;
}

namespace {

// Skriver "[1,2,3]" for de givne function codes — delt af begge
// FC-lister i mb_status_build_capabilities_json() nedenfor.
int append_fc_array(char *out, size_t out_capacity, const uint8_t *fcs, size_t count) {
  size_t pos = 0;
  if (out_capacity == 0) return -1;
  out[pos++] = '[';
  for (size_t i = 0; i < count; i++) {
    const int written = snprintf(out + pos, out_capacity - pos, "%s%u", i > 0 ? "," : "", static_cast<unsigned>(fcs[i]));
    if (written <= 0 || static_cast<size_t>(written) >= out_capacity - pos) return -1;
    pos += static_cast<size_t>(written);
  }
  if (pos + 1 >= out_capacity) return -1;
  out[pos++] = ']';
  return static_cast<int>(pos);
}

}  // namespace

size_t mb_status_build_capabilities_json(const mb_capabilities_data_t *data, char *out, size_t out_capacity) {
  const int header_written =
      snprintf(out, out_capacity, "{\"api_version\":%u,\"fw_version\":\"%s\",\"modbus_tcp\":{\"supported_function_codes\":",
               static_cast<unsigned>(MB_REST_API_VERSION), data->fw_version);
  if (header_written <= 0 || static_cast<size_t>(header_written) >= out_capacity) return 0;
  size_t offset = static_cast<size_t>(header_written);

  int written = append_fc_array(out + offset, out_capacity - offset, data->modbus_tcp_fcs, data->modbus_tcp_fc_count);
  if (written < 0) return 0;
  offset += static_cast<size_t>(written);

  written = snprintf(out + offset, out_capacity - offset,
                      ",\"max_read_quantity\":%u,\"max_write_quantity\":%u},"
                      "\"rest_diagnostic\":{\"supported_function_codes\":",
                      static_cast<unsigned>(data->modbus_tcp_max_read_quantity),
                      static_cast<unsigned>(data->modbus_tcp_max_write_quantity));
  if (written <= 0 || static_cast<size_t>(written) >= out_capacity - offset) return 0;
  offset += static_cast<size_t>(written);

  written = append_fc_array(out + offset, out_capacity - offset, data->rest_diagnostic_fcs, data->rest_diagnostic_fc_count);
  if (written < 0) return 0;
  offset += static_cast<size_t>(written);

  written = snprintf(out + offset, out_capacity - offset, ",\"max_read_quantity\":%u,\"max_write_quantity\":%u}}",
                      static_cast<unsigned>(data->rest_diagnostic_max_read_quantity),
                      static_cast<unsigned>(data->rest_diagnostic_max_write_quantity));
  if (written <= 0 || static_cast<size_t>(written) >= out_capacity - offset) return 0;
  offset += static_cast<size_t>(written);

  return offset;
}

size_t mb_status_build_error_json(int error_code, const char *error, const char *message, char *out,
                                   size_t out_capacity) {
  int written;
  if (error_code >= 0) {
    written = snprintf(out, out_capacity, "{\"ok\":false,\"error_code\":%d,\"error\":\"%s\",\"message\":\"%s\"}",
                        error_code, error, message);
  } else {
    written = snprintf(out, out_capacity, "{\"ok\":false,\"error\":\"%s\",\"message\":\"%s\"}", error, message);
  }

  if (written <= 0 || static_cast<size_t>(written) >= out_capacity) {
    return 0;
  }
  return static_cast<size_t>(written);
}
