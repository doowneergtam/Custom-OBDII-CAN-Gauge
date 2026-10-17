#include "channels.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static ch_def_t   s_def[CH_MAX];
static ch_state_t s_state[CH_MAX];
static int        s_count;
static ch_units_t s_units;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

void channels_init(void)
{
    memset(s_def, 0, sizeof(s_def));
    memset(s_state, 0, sizeof(s_state));
    for (int i = 0; i < CH_MAX; i++) {
        s_state[i].peak  = -INFINITY;
        s_state[i].floor =  INFINITY;
        s_def[i].scale   = 1.0f;
        s_def[i].big_endian = true;
    }
    s_count = 0;
}

static ch_def_t *slot(ch_id_t id, const char *name, const char *units, ch_kind_t kind,
                      float min, float max, uint8_t decimals)
{
    ch_def_t *d = &s_def[id];
    memset(d, 0, sizeof *d);
    snprintf(d->name, CH_NAME_LEN, "%s", name);
    snprintf(d->units, CH_UNITS_LEN, "%s", units);
    d->kind = kind; d->min = min; d->max = max; d->decimals = decimals;
    d->scale = 1.0f; d->big_endian = true;
    d->show_units = d->show_min = d->show_max = true;
    d->enabled = true;
    if (id + 1 > s_count) s_count = id + 1;
    return d;
}
static void can(ch_def_t *d, uint32_t id, uint8_t start_bit, uint8_t bit_len, bool is_signed, float scale)
{
    d->source = CH_SRC_CAN; d->can_id = id; d->start_bit = start_bit; d->bit_len = bit_len;
    d->is_signed = is_signed; d->scale = scale;
}
static void zone(ch_def_t *d, float from, float to, ch_zone_level_t level)
{
    if (d->zone_count >= CH_ZONES_MAX) return;
    d->zones[d->zone_count++] = (ch_zone_t){ from, to, level };
}

// MS2/Extra dash broadcast, base ID 1512 (0x5E8). See docs/ms2-can.md.
// Zones are the shipped defaults; the user edits them per channel.
void channels_load_preset_megasquirt(void)
{
    ch_def_t *d;
    d = slot(CH_RPM,    "RPM",      "rpm", CH_KIND_NONE,  0,    8000, 0); can(d, 0x5E8, 16, 16, false, 1.0f);
        zone(d, 6500, 7200, CH_ZONE_WARN); zone(d, 7200, 1e9f, CH_ZONE_BAD);
    d = slot(CH_BOOST,  "BOOST",    "psi", CH_KIND_PRESS, -15,  20,   1); d->source = CH_SRC_DERIVED;
        zone(d, -1e9f, 0, CH_ZONE_COLD); zone(d, 12, 16, CH_ZONE_WARN); zone(d, 16, 1e9f, CH_ZONE_BAD);
    d = slot(CH_MAP,    "MAP",      "kPa", CH_KIND_NONE,  0,    250,  0); can(d, 0x5E8, 0, 16, true, 0.1f);
        zone(d, 185, 210, CH_ZONE_WARN); zone(d, 210, 1e9f, CH_ZONE_BAD);
    d = slot(CH_CLT,    "COOLANT",  "F",   CH_KIND_TEMP,  100,  260,  0); can(d, 0x5E8, 32, 16, true, 0.1f);
        zone(d, -1e9f, 160, CH_ZONE_COLD); zone(d, 215, 230, CH_ZONE_WARN); zone(d, 230, 1e9f, CH_ZONE_BAD);
    d = slot(CH_OILP,   "OIL PRES", "psi", CH_KIND_PRESS, 0,    100,  0); can(d, 0x5EB, 16, 16, true, 0.1f);
        zone(d, -1e9f, 10, CH_ZONE_BAD); zone(d, 10, 20, CH_ZONE_WARN); zone(d, 80, 1e9f, CH_ZONE_WARN);
    d = slot(CH_OILT,   "OIL TEMP", "F",   CH_KIND_TEMP,  100,  300,  0); can(d, 0x5EB, 32, 16, true, 0.1f);
        zone(d, -1e9f, 140, CH_ZONE_COLD); zone(d, 240, 260, CH_ZONE_WARN); zone(d, 260, 1e9f, CH_ZONE_BAD);
    d = slot(CH_AFR,    "AFR",      "afr", CH_KIND_NONE,  10,   20,   1); can(d, 0x5EA, 8, 8, false, 0.1f);
        zone(d, -1e9f, 11, CH_ZONE_BAD); zone(d, 11, 12, CH_ZONE_WARN); zone(d, 15.5f, 17, CH_ZONE_WARN); zone(d, 17, 1e9f, CH_ZONE_BAD);
    d = slot(CH_AFRTGT, "AFR TGT",  "afr", CH_KIND_NONE,  10,   20,   1); can(d, 0x5EA, 0, 8, false, 0.1f);
    d = slot(CH_EGOCOR, "EGO COR",  "%",   CH_KIND_NONE,  50,   150,  0); can(d, 0x5EA, 16, 16, true, 0.1f);
        zone(d, -1e9f, 85, CH_ZONE_WARN); zone(d, 115, 1e9f, CH_ZONE_WARN);
    d = slot(CH_MAT,    "INTAKE",   "F",   CH_KIND_TEMP,  0,    200,  0); can(d, 0x5E9, 32, 16, true, 0.1f);
        zone(d, 120, 150, CH_ZONE_WARN); zone(d, 150, 1e9f, CH_ZONE_BAD);
    d = slot(CH_EGT,    "EGT",      "F",   CH_KIND_TEMP,  400,  1800, 0); can(d, 0x5EA, 32, 16, true, 0.1f);
        zone(d, 1500, 1650, CH_ZONE_WARN); zone(d, 1650, 1e9f, CH_ZONE_BAD);
    d = slot(CH_TPS,    "THROTTLE", "%",   CH_KIND_NONE,  0,    100,  0); can(d, 0x5E8, 48, 16, true, 0.1f);
    d = slot(CH_ADV,    "TIMING",   "deg", CH_KIND_NONE,  -10,  50,   1); can(d, 0x5E9, 48, 16, true, 0.1f);
    d = slot(CH_PW1,    "PULSE W",  "ms",  CH_KIND_NONE,  0,    25,   2); can(d, 0x5E9, 0, 16, false, 0.001f);
    d = slot(CH_DUTY,   "INJ DUTY", "%",   CH_KIND_NONE,  0,    100,  0); d->source = CH_SRC_DERIVED;
        zone(d, 80, 90, CH_ZONE_WARN); zone(d, 90, 1e9f, CH_ZONE_BAD);
    d = slot(CH_BATT,   "BATTERY",  "V",   CH_KIND_NONE,  8,    16,   1); can(d, 0x5EB, 0, 16, true, 0.1f);
        zone(d, -1e9f, 11.5f, CH_ZONE_BAD); zone(d, 11.5f, 12.5f, CH_ZONE_WARN); zone(d, 15, 15.5f, CH_ZONE_WARN); zone(d, 15.5f, 1e9f, CH_ZONE_BAD);
    d = slot(CH_KNOCK,  "KNOCK",    "deg", CH_KIND_NONE,  0,    10,   1); can(d, 0x5EB, 48, 8, false, 0.1f);
        zone(d, 0.1f, 2, CH_ZONE_WARN); zone(d, 2, 1e9f, CH_ZONE_BAD);
    d = slot(CH_VSS,    "SPEED",    "mph", CH_KIND_SPEED, 0,    160,  0); can(d, 0x5EC, 0, 16, false, 0.1f * 2.23694f); // m/s -> mph
    d = slot(CH_TCRET,  "TC RTD",   "deg", CH_KIND_NONE,  0,    20,   1); can(d, 0x5EC, 16, 16, true, 0.1f);
        zone(d, 0.1f, 1e9f, CH_ZONE_WARN);
    d = slot(CH_LAUNCH, "LAUNCH",   "deg", CH_KIND_NONE,  -20,  20,   1); can(d, 0x5EC, 32, 16, true, 0.1f);
    // Generic OBD-II channels: filled by the OBD poller, no CAN decode of their own.
    d = slot(CH_LOAD, "LOAD",     "%",   CH_KIND_NONE,  0,    100,  0); d->source = CH_SRC_NONE;
    d = slot(CH_MAF,  "MAF",      "g/s", CH_KIND_NONE,  0,    300,  1); d->source = CH_SRC_NONE;
    d = slot(CH_STFT, "STFT",     "%",   CH_KIND_NONE,  -25,  25,   1); d->source = CH_SRC_NONE;
        zone(d, -1e9f, -15, CH_ZONE_WARN); zone(d, 15, 1e9f, CH_ZONE_WARN);
    d = slot(CH_LTFT, "LTFT",     "%",   CH_KIND_NONE,  -25,  25,   1); d->source = CH_SRC_NONE;
        zone(d, -1e9f, -15, CH_ZONE_WARN); zone(d, 15, 1e9f, CH_ZONE_WARN);
    d = slot(CH_O2,   "O2 B1S1",  "V",   CH_KIND_NONE,  0,    1,    2); d->source = CH_SRC_NONE;
    // Analog inputs: name, units, range and scaling come from the "aux" config; off until enabled there.
    d = slot(CH_AUX1, "AUX 1", "V", CH_KIND_NONE, 0, 5, 2); d->source = CH_SRC_ADC; d->enabled = false;
    d = slot(CH_AUX2, "AUX 2", "V", CH_KIND_NONE, 0, 5, 2); d->source = CH_SRC_ADC; d->enabled = false;
}

int channels_count(void) { return s_count; }
const ch_def_t   *channels_def(ch_id_t id)     { return (id < CH_MAX) ? &s_def[id] : NULL; }
ch_def_t         *channels_def_mut(ch_id_t id) { return (id < CH_MAX) ? &s_def[id] : NULL; }
const ch_state_t *channels_state(ch_id_t id)   { return (id < CH_MAX) ? &s_state[id] : NULL; }

void channels_set_value(ch_id_t id, float value)
{
    if (id >= CH_MAX) return;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    ch_state_t *s = &s_state[id];
    s->value = value;
    if (value > s->peak)  s->peak  = value;
    if (value < s->floor) s->floor = value;
    s->last_update_us = now;
    s->valid = true;
    portEXIT_CRITICAL(&s_lock);
}

// Derived channels: boost from MAP (gauge pressure, 101.3 kPa baro), injector
// duty from pulse width and rpm (4-stroke, one injection per cycle).
static void update_derived(void)
{
    if (s_def[CH_BOOST].source == CH_SRC_DERIVED && s_state[CH_MAP].valid) {
        channels_set_value(CH_BOOST, (s_state[CH_MAP].value - 101.3f) / 6.895f);
    }
    if (s_def[CH_DUTY].source == CH_SRC_DERIVED && s_state[CH_PW1].valid && s_state[CH_RPM].valid) {
        channels_set_value(CH_DUTY, s_state[CH_PW1].value * s_state[CH_RPM].value / 1200.0f);
    }
}

// Extract bit_len bits starting at start_bit (MSB-first numbering, bit 0 =
// MSB of byte 0) from an 8-byte big-endian frame.
static uint32_t extract_be(const uint8_t *data, uint8_t start_bit, uint8_t bit_len)
{
    uint32_t v = 0;
    for (uint8_t i = 0; i < bit_len; i++) {
        uint8_t bit = start_bit + i;
        v = (v << 1) | ((data[bit >> 3] & (0x80u >> (bit & 7))) ? 1u : 0u);
    }
    return v;
}

int channels_feed_can(uint32_t can_id, const uint8_t *data, uint8_t dlc)
{
    uint8_t frame[8] = {0};
    memcpy(frame, data, dlc > 8 ? 8 : dlc);
    int updated = 0;
    for (int i = 0; i < s_count; i++) {
        const ch_def_t *d = &s_def[i];
        if (d->source != CH_SRC_CAN || d->can_id != can_id) continue;
        if (d->bit_len == 0 || d->start_bit + d->bit_len > 64) continue;
        if ((d->start_bit + d->bit_len + 7) / 8 > dlc) continue;

        uint32_t raw = extract_be(frame, d->start_bit, d->bit_len);
        float val;
        if (d->is_signed) {
            int32_t sraw = (int32_t)raw;
            if (d->bit_len < 32 && (raw & (1u << (d->bit_len - 1)))) sraw -= (int32_t)(1u << d->bit_len);
            val = (float)sraw;
        } else {
            val = (float)raw;
        }
        channels_set_value((ch_id_t)i, val * d->scale + d->offset);
        updated++;
    }
    if (updated) update_derived();
    return updated;
}

float channels_value(ch_id_t id) { return (id < CH_MAX) ? s_state[id].value : NAN; }

ch_freshness_t channels_freshness(ch_id_t id, int64_t now_us)
{
    if (id >= CH_MAX || !s_state[id].valid) return CH_DEAD;
    int64_t age = now_us - s_state[id].last_update_us;
    if (age > CH_DEAD_US)  return CH_DEAD;
    if (age > CH_STALE_US) return CH_STALE;
    return CH_FRESH;
}

ch_zone_level_t channels_zone_of(const ch_def_t *d, float value)
{
    for (int i = 0; i < d->zone_count; i++) {
        if (value >= d->zones[i].from && value < d->zones[i].to) return d->zones[i].level;
    }
    return CH_ZONE_GOOD;
}

ch_zone_level_t channels_zone(ch_id_t id)
{
    if (id >= CH_MAX || !s_state[id].valid) return CH_ZONE_GOOD;
    return channels_zone_of(&s_def[id], s_state[id].value);
}

float channels_frac(const ch_def_t *d, float value)
{
    float span = d->max - d->min;
    float f = span > 0 ? (value - d->min) / span : 0;
    if (isnan(f)) f = 0;
    return f < 0 ? 0 : f > 1 ? 1 : f;
}

void channels_reset_peaks(void)
{
    for (int i = 0; i < CH_MAX; i++) channels_reset_peak((ch_id_t)i);
}

void channels_reset_peak(ch_id_t id)
{
    if (id >= CH_MAX) return;
    portENTER_CRITICAL(&s_lock);
    s_state[id].peak  = s_state[id].valid ? s_state[id].value : -INFINITY;
    s_state[id].floor = s_state[id].valid ? s_state[id].value :  INFINITY;
    portEXIT_CRITICAL(&s_lock);
}

// ---------------------------------------------------------------- units
void       channels_set_units(ch_units_t u) { s_units = u; }
ch_units_t channels_get_units(void)         { return s_units; }

float channels_to_display(const ch_def_t *d, float v)
{
    switch (d->kind) {
    case CH_KIND_TEMP:  return s_units.temp_c    ? (v - 32.0f) * 5.0f / 9.0f : v;
    case CH_KIND_PRESS: return s_units.press_bar ? v * 0.0689476f : v;
    case CH_KIND_SPEED: return s_units.speed_kmh ? v * 1.609344f : v;
    default:            return v;
    }
}

float channels_from_display(const ch_def_t *d, float v)
{
    switch (d->kind) {
    case CH_KIND_TEMP:  return s_units.temp_c    ? v * 9.0f / 5.0f + 32.0f : v;
    case CH_KIND_PRESS: return s_units.press_bar ? v / 0.0689476f : v;
    case CH_KIND_SPEED: return s_units.speed_kmh ? v / 1.609344f : v;
    default:            return v;
    }
}

const char *channels_units_label(const ch_def_t *d)
{
    switch (d->kind) {
    case CH_KIND_TEMP:  return s_units.temp_c    ? "C"    : "F";
    case CH_KIND_PRESS: return s_units.press_bar ? "bar"  : "psi";
    case CH_KIND_SPEED: return s_units.speed_kmh ? "km/h" : "mph";
    default:            return d->units;
    }
}

uint8_t channels_display_decimals(const ch_def_t *d)
{
    if (d->kind == CH_KIND_PRESS && s_units.press_bar) return 2;
    return d->decimals;
}
