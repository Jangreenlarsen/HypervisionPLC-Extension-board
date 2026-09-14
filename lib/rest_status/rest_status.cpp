#include "rest_status.h"

#include <cstdio>

namespace {

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
    return snprintf(out, out_capacity, "\"ethernet\":{\"connected\":true,\"ip\":\"%s\"}", data->eth_ip);
  }
  return snprintf(out, out_capacity, "\"ethernet\":{\"connected\":false}");
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
                                       "\"provisioned\":%s,",
                                       static_cast<unsigned>(MB_REST_API_VERSION), data->fw_version, data->fw_build,
                                       static_cast<unsigned long>(data->uptime_s),
                                       static_cast<unsigned long>(data->heap_free_bytes),
                                       static_cast<unsigned>(data->active_channels), data->provisioned ? "true" : "false");
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
