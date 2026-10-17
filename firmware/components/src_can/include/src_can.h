// CAN data source. Two modes on the same TWAI controller:
//   MegaSquirt: passive, decodes the dash-broadcast frames through the
//               channel definitions.
//   OBD-II:     polls Mode 01 PIDs on 0x7DF and parses 0x7E8 replies.
// Nothing here knows what the MegaSquirt frames mean; the channel
// definitions do that decoding.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { SRC_CAN_MODE_MS = 0, SRC_CAN_MODE_OBD = 1 } src_can_mode_t;

typedef struct {
    int      gpio_tx;
    int      gpio_rx;
    uint32_t bitrate;        // 500000 for MegaSquirt and most OBD cars
    bool     listen_only;    // true: never ACK (sniffing only; breaks OBD polling)
    src_can_mode_t mode;
} src_can_config_t;

typedef struct {
    uint32_t frames_rx;
    uint32_t frames_matched;   // frames that updated at least one channel
    uint32_t obd_requests;
    uint32_t bus_errors;
    uint32_t bus_off_count;
    int64_t  last_frame_us;
} src_can_stats_t;

esp_err_t src_can_start(const src_can_config_t *cfg);
void      src_can_set_mode(src_can_mode_t mode);
// While the synthetic engine is on, bus data is received but not applied.
void      src_can_set_feed(bool enabled);
void      src_can_stats(src_can_stats_t *out);

#ifdef __cplusplus
}
#endif
