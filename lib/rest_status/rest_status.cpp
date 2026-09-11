#include "rest_status.h"

#include <cstdio>

size_t mb_status_build_json(const mb_status_data_t *data, char *out, size_t out_capacity) {
  int written;
  if (data->wifi_connected) {
    written = snprintf(out, out_capacity,
                        "{"
                        "\"api_version\":%u,"
                        "\"fw_version\":\"%s\","
                        "\"fw_build\":\"%s\","
                        "\"uptime_s\":%lu,"
                        "\"heap_free_bytes\":%lu,"
                        "\"active_channels\":%u,"
                        "\"provisioned\":%s,"
                        "\"wifi\":{\"connected\":true,\"ip\":\"%s\",\"rssi_dbm\":%d}"
                        "}",
                        static_cast<unsigned>(MB_REST_API_VERSION), data->fw_version, data->fw_build,
                        static_cast<unsigned long>(data->uptime_s), static_cast<unsigned long>(data->heap_free_bytes),
                        static_cast<unsigned>(data->active_channels), data->provisioned ? "true" : "false",
                        data->wifi_ip, static_cast<int>(data->wifi_rssi_dbm));
  } else {
    written = snprintf(out, out_capacity,
                        "{"
                        "\"api_version\":%u,"
                        "\"fw_version\":\"%s\","
                        "\"fw_build\":\"%s\","
                        "\"uptime_s\":%lu,"
                        "\"heap_free_bytes\":%lu,"
                        "\"active_channels\":%u,"
                        "\"provisioned\":%s,"
                        "\"wifi\":{\"connected\":false}"
                        "}",
                        static_cast<unsigned>(MB_REST_API_VERSION), data->fw_version, data->fw_build,
                        static_cast<unsigned long>(data->uptime_s), static_cast<unsigned long>(data->heap_free_bytes),
                        static_cast<unsigned>(data->active_channels), data->provisioned ? "true" : "false");
  }

  if (written <= 0 || static_cast<size_t>(written) >= out_capacity) {
    return 0;
  }
  return static_cast<size_t>(written);
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
