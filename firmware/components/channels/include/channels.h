// Channel model: every displayed value is a channel. Display and menu code
// read channel records and never know where a value came from.
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CH_NAME_LEN   9   // 8 chars + NUL
#define CH_UNITS_LEN  8
#define CH_ZONES_MAX  4

typedef enum {
    CH_SRC_NONE = 0,
    CH_SRC_CAN,        // decoded from a CAN frame (id/start_bit/bit_len)
    CH_SRC_KLINE,      // ISO 9141-2 PID (stock ECU path)
    CH_SRC_ADC,        // analog sender
    CH_SRC_GPIO,       // lamp input, 0/1
    CH_SRC_PULSE,      // tach or VSS frequency
    CH_SRC_DERIVED,    // computed from other channels (boost, duty)
    CH_SRC_SIM,        // bench / fake data
} ch_source_t;

// Unit family. Base units are imperial; the display converts.
typedef enum { CH_KIND_NONE = 0, CH_KIND_TEMP, CH_KIND_PRESS, CH_KIND_SPEED } ch_kind_t;

// Colour zones. Anything not covered by a zone is GOOD. Channels with zero
// zones are informational and draw in the neutral colour.
typedef enum { CH_ZONE_COLD = 0, CH_ZONE_GOOD, CH_ZONE_WARN, CH_ZONE_BAD } ch_zone_level_t;
typedef struct { float from, to; ch_zone_level_t level; } ch_zone_t;

// Fixed slots for the preset channels (MS2/Extra dash broadcast + derived).
typedef enum {
    CH_RPM = 0, CH_BOOST, CH_MAP, CH_CLT, CH_OILP, CH_OILT, CH_AFR, CH_AFRTGT,
    CH_EGOCOR, CH_MAT, CH_EGT, CH_TPS, CH_ADV, CH_PW1, CH_DUTY, CH_BATT,
    CH_KNOCK, CH_VSS, CH_TCRET, CH_LAUNCH,
    // generic OBD-II only (dead on a MegaSquirt)
    CH_LOAD, CH_MAF, CH_STFT, CH_LTFT, CH_O2,
    // analog inputs on the carrier (rev C): GPIO17 / GPIO18 through the divider
    CH_AUX1, CH_AUX2,
    CH_PRESET_COUNT,
    CH_MAX = 32,
} ch_id_t;

typedef struct {
    char        name[CH_NAME_LEN];     // shown on the gauge; the user may rename it
    char        dname[CH_NAME_LEN];    // the preset's name, restored when the label is cleared
    char        units[CH_UNITS_LEN];   // base units label (used when kind == NONE)
    ch_source_t source;
    ch_kind_t   kind;

    // CAN decode (CH_SRC_CAN)
    uint32_t    can_id;
    uint8_t     start_bit;     // MSB-first bit index within the 8-byte frame
    uint8_t     bit_len;       // 1..32
    bool        is_signed;
    bool        big_endian;

    float       scale;         // value = raw * scale + offset
    float       offset;

    float       min;           // gauge range, base units
    float       max;
    uint8_t     decimals;

    ch_zone_t   zones[CH_ZONES_MAX];
    uint8_t     zone_count;

    // per-channel display switches
    bool        show_units;
    bool        show_min;
    bool        show_max;
    bool        enabled;       // false = hidden from pickers and the page (unused analog input)
} ch_def_t;

typedef struct {
    float    value;
    float    peak;             // max since boot / reset
    float    floor;            // min since boot / reset
    int64_t  last_update_us;   // esp_timer_get_time() of last set
    bool     valid;            // false until first value arrives
} ch_state_t;

// Staleness thresholds used by the UI (CLAUDE.md: grey after 500 ms,
// dashes after 3 s).
#define CH_STALE_US     (500 * 1000)
#define CH_DEAD_US      (3000 * 1000)

typedef enum { CH_FRESH = 0, CH_STALE, CH_DEAD } ch_freshness_t;

typedef struct { bool temp_c; bool press_bar; bool speed_kmh; } ch_units_t;

void              channels_init(void);
void              channels_load_preset_megasquirt(void);

int               channels_count(void);
const ch_def_t   *channels_def(ch_id_t id);
ch_def_t         *channels_def_mut(ch_id_t id);
const ch_state_t *channels_state(ch_id_t id);

// Called by data sources. Thread-safe (spinlock around the record).
void   channels_set_value(ch_id_t id, float value);
// Decode a raw CAN payload for every channel bound to can_id, then refresh
// derived channels. Returns the number of channels updated.
int    channels_feed_can(uint32_t can_id, const uint8_t *data, uint8_t dlc);

float            channels_value(ch_id_t id);
ch_freshness_t   channels_freshness(ch_id_t id, int64_t now_us);
ch_zone_level_t  channels_zone_of(const ch_def_t *d, float value);
ch_zone_level_t  channels_zone(ch_id_t id);         // zone of the current value
float            channels_frac(const ch_def_t *d, float value);  // 0..1 across min..max
void             channels_reset_peaks(void);
void             channels_reset_peak(ch_id_t id);

// Display units
void             channels_set_units(ch_units_t u);
ch_units_t       channels_get_units(void);
float            channels_to_display(const ch_def_t *d, float base);
float            channels_from_display(const ch_def_t *d, float shown);
const char      *channels_units_label(const ch_def_t *d);
uint8_t          channels_display_decimals(const ch_def_t *d);

#ifdef __cplusplus
}
#endif
