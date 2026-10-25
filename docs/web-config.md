# Phone web interface

Design preview: `design/web-config/index.html` (published artifact). This is
the exact page the firmware will serve; it detects whether it is talking to a
gauge or running standalone with a simulated engine.

## How it reaches the phone

- Gauge menu gets a **Wi-Fi** entry. Selecting it starts a WPA2 hotspot
  (default name `NB1-Gauge-<last 4 of MAC>`, password from config, shown on
  the gauge screen while the hotspot is up) and a captive-portal DNS so any
  address resolves to the gauge. mDNS also answers `gauge.local`.
- The hotspot turns itself off after N idle minutes (default 10, user
  setting) so the radio is not running while driving. The gauge draws a small
  Wi-Fi glyph in the arc gap while it is on.
- One HTML file, gzip-compressed and embedded in flash (`EMBED_FILES`),
  about 25 KB. No external resources on the device build: the Google Fonts
  link is dropped and the system fallback stacks are used.

## JSON interface (ESP-IDF `esp_http_server`)

| Method | Path | Body / reply |
|---|---|---|
| GET | `/` | the page |
| GET | `/api/config` | the same JSON as `/config/gauge.json`, plus `wifi` |
| PUT | `/api/config` | full config; validated, applied live, saved (debounced) |
| GET | `/api/state` | `{ "values": { "RPM": 2517, ... }, "screen": ["BOOST", ...], "aux_raw": [{"volts": 2.431, "ohms": 812}, {}] }` polled at 4 Hz |
| POST | `/api/reset-peaks` | |
| POST | `/api/test-alarm` | `{ "tone": "chirp" }` plays on the gauge speaker |
| POST | `/api/identify` | flashes the display twice |
| POST | `/api/reboot` | |
| POST | `/api/update` | OTA firmware upload (later) |

Config writes from the page go through the same `config` component as the
menu, so the two never disagree. Changes are applied live before they are
saved: the page is the way to adjust zones while watching the Live tab.

## Pages

Live (all channels, on-screen ones marked), Screen (layout picker, cell
channels, brightness), Channels (range, zones, warn/bad tone per channel),
Theme (preview, presets, style, bar fill, bands, 12 colours), Alarms (volume,
mute, warn/bad enable, repeat, audition), System (units, hotspot name,
password and timeout, config backup/restore, reboot, factory reset).

## Security

WPA2 with a user-set password (minimum 8 characters), no open network. The
API is reachable only on the hotspot's own subnet. Nothing here is on the
internet, and the gauge never joins another network.

## Linked preview

`design/pod-preview/pod-preview.html` embeds `design/pod-preview/phone.html` (a doctype-wrapped copy of
`design/web-config/index.html`, rebuilt by hand when the phone page changes) in its Phone tab. The two talk
over postMessage with the same calls the real page makes to the gauge API, so the phone drives the gauge
preview live. Publish the gauge preview with `files: {"phone.html": ...}`.

## Firmware status (2026-10-12)

Implemented in `firmware/components/webcfg`. The page is embedded gzipped (`page.html.gz`, built by
`tools/build_web.py`). Hotspot defaults: `NB1-Gauge-<mac>` / `miata2000`, channel 6, WPA2, 3 clients,
idle timeout from config (default 10 min, only when no client is associated). Captive DNS answers every
name with 192.168.4.1; Android/iOS/Windows probe URLs redirect to the page. PUT /api/config applies live
via `ui_reload()` and saves. Actions: reset-peaks, test-alarm {tone}, identify, reboot.

## Analog inputs (carrier rev C, 2026-10-16)

Config block `aux` with keys `"1"` and `"2"` (GPIO17 and GPIO18):

```json
"aux": { "1": { "enabled": true, "resistive": false, "label": "FUEL P", "units": "psi", "decimals": 1,
                "pts": [[0.5, 0], [4.5, 100]] },
         "2": { "enabled": true, "resistive": true,  "label": "TRANS T", "units": "F", "decimals": 0,
                "pts": [[2800, 100], [450, 200], [160, 260]] } }
```

`pts` are `[input, value]` pairs, straight lines between them, flat beyond
the ends. `input` is the sensor voltage at the sensor node for 0-5 V senders,
or the sender's resistance in ohms for resistive senders (the carrier's
jumper must be bridged for those). The firmware undoes the 1k / 1.5k divider
and, for resistive senders, the 1k pull-up and the 2.5k load, so the numbers
you enter are the sensor's own. Enabled inputs appear in `channels`, the
cell pickers and the alarm tones under ids `AUX1` and `AUX2`; disabled ones
are hidden. The page's Channels tab carries the two input cards and shows the
live raw reading from `aux_raw`. Console: `aux`.

## Theme controls (2026-10-24)

`zoom` (0.7-1.3) scales the whole screen. `theme.el` holds one object per face element
(`digits`, `label`, `units`, `minmax`, `bar`, `ticks`, `needle`, `window`, `scale`) with `on` and `dy`
(vertical offset as a fraction of the cell radius, plus is down); the sizes stay in the
existing `*_scale` keys. `scale` is the scale numbers (tick numerals on Dial and Stock 1,
the range ends on Ring and Stock 2): its size is `theme.num_scale` and its `dy` is a radial
offset instead, plus is outward (-0.4..0.4). Turning `ticks` off no longer hides the numbers. `theme.ticks` = `{maj_w, maj_len, min_w, min_len, maj_n, min_n}`
(multipliers, counts; 0 = style default), `theme.needle` = `{w, len, hub}` multipliers,
`theme.sweep_rot` degrees (-90..90). `channels[id].label` renames a channel on the gauge
(blank restores the preset name).
