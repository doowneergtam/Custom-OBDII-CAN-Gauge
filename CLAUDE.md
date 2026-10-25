# NB1 Miata Gauge

Two products on one firmware core for a 1999-2000 Mazda Miata (NB1) with a
MegaSquirt MS2:

1. **Pod gauge** (`firmware/`, first): 52 mm pod on the Waveshare
   ESP32-S3-Touch-AMOLED-1.75 (466x466 round AMOLED, CO5300 QSPI, CST9217
   touch). Rotary encoder is the primary input. Reads MS2/Extra dash
   broadcast frames over CAN (TWAI, 500 kbps, 11-bit, base ID 1512).
2. **Cluster replacement** (later): ESP32-P4 plug-in for the factory cluster.
   Same channel/UI code, different display and I/O layer.

Full brief: `docs/brief.md`. Pin map: `docs/pod-pinout.md`.

## Toolchain

- ESP-IDF **v5.5.x** at `C:\Users\doown\esp\esp-idf`, tools in
  `C:\Users\doown\.espressif`. Not Arduino, not PlatformIO.
- Build from PowerShell: `. $HOME\esp\esp-idf\export.ps1` then
  `idf.py -C firmware set-target esp32s3`, `idf.py -C firmware build`,
  `idf.py -C firmware -p COMx flash monitor`.
- From Git Bash: `. ~/esp/esp-idf/export.sh`.
- Board support: managed component `waveshare/esp32_s3_touch_amoled_1_75`
  (^3.0.1) which pulls LVGL 9, `esp_lcd_co5300`, `esp_lcd_touch_cst9217`,
  `esp_lvgl_adapter`, `esp_io_expander_tca9554`.
- Console is USB-Serial-JTAG; UART0 pins carry CAN (TX GPIO44, RX GPIO43 via 1 k). Rev C carrier: no encoder, analog inputs on GPIO17 (AIN1) / GPIO18 (AIN2). See `docs/pod-pinout.md`. Firmware still has the rev B encoder map until the analog-input work lands.

## Firmware conventions

- C, ESP-IDF components under `firmware/components/`: `channels` (channel
  model, values, thresholds), `src_can` (TWAI listener + MS2 decoder),
  `src_pulse` (tach/VSS via PCNT, later), `ui` (LVGL faces, menu),
  `input_encoder` (knob + button components -> LVGL keypad indev: LEFT/RIGHT/ENTER), `config`
  (JSON on LittleFS), `hal_board` (board-specific init, the only place that
  knows which board we are on).
- Display and menu code only ever read `channel_t` records. Nothing in `ui`
  may include CAN or ADC headers.
- Every channel value path must survive the source going silent: show the
  last value greyed out after 500 ms, dashes after 3 s.
- No writes to flash on every change. Config saves are debounced (5 s idle)
  and odometer writes are batched.
- Keep `docs/decisions.md` updated when a brief assumption is overturned
  (first one: header GPIOs are 16/17/18/43/44, not 1/2/3).

## Status (2026-10-12, night)

Flashed and booting on the Waveshare board (COM9): display, touch, speaker,
CAN driver, Wi-Fi hotspot (on at boot) and the phone page all up; synthetic engine
on by default. Soak-tested via the serial console (see Bench tooling). Implemented:
- `channels`: 20 MS2 channels incl. derived BOOST and INJ DUTY, per-channel
  colour zones (cold/good/warn/bad) and range, unit kinds (F/C, psi/bar, mph/kmh).
- `theme`: 12 colour slots, 4 presets, style (ring/dial/numeric), bar fill
  (gradient/segments/solid), zone bands.
- `ui`: `face.c` draws every style with the LVGL draw API in one object; layouts
  1/3/4/6 (pentagon)/7 (hexagon); menu tree root > Channel | Layout | Theme
  (preset, fill, Style full-screen picker, bands) | Alarms (volume, warn/bad
  on, repeat, per-channel tones) | Brightness | Reset peaks; off-screen alarm banner.
- `alarms`: engine + `audio.c` tone synth through the ES8311 via the BSP codec.
- `config`: JSON on LittleFS (`/config/gauge.json`), debounced 5 s, atomic rename.
  Header is `gauge_config.h` (plain `config.h` collides with a component header).
- Synthetic data is a runtime toggle (`g_ui_cfg.sim`, System menu / web page), compiled in
  behind `CONFIG_NB1_SIM_DATA`; feeds raw MS frames so the decoder path runs.
- Data source toggle MegaSquirt / generic OBD-II (`g_ui_cfg.source`): `src_can` polls Mode 01
  PIDs on 0x7DF in OBD mode and fills RPM, MAP/boost, CLT, speed, TPS, timing, IAT, load,
  MAF, trims, O2. Five OBD-only channels added (LOAD, MAF, STFT, LTFT, O2).
- Touch: tap cell = select, tap again = reset min/max, hold = menu, swipe = move; menu
  tap middle row = choose, above/below or swipe = scroll. Selection ring fades after 3 s.
- Scalers (theme): digit, label, units, minmax, bar (ring thickness), cell (outer cells), num (scale numbers).
  Face elements (`theme.el`, see `docs/web-config.md`) each have on/off and an offset; `scale` (tick numerals,
  range ends) is separate from `ticks` and its offset is radial.
  Per-channel show_units/show_min/show_max. User colour presets and saved screens in
  config (`theme_presets`, `screens`); Screens menu on the gauge, Saved screens card on the page.
- Numeric style has no bar: digits as big as fit, coloured by zone. Min/max shown as lo/hi.
- Design references: `design/pod-preview/pod-preview.html` (gauge screens) and
  `design/web-config/index.html` (phone setup page, see `docs/web-config.md`), both published artifacts.

- `webcfg`: Wi-Fi hotspot + captive DNS + mDNS (gauge.local) + HTTP server serving the phone
  page (`tools/build_web.py` packs `design/web-config/index.html` into `page.html.gz`; rerun it
  after page changes) and the JSON API in `docs/web-config.md`. Config file now uses the page
  schema. Switched on from System > Wi-Fi setup; credentials shown on screen; idle timeout.

- Digits above 48 px: `fonts` component renders `Montserrat-Light.ttf` (OFL, embedded, 444 KB)
  with LVGL TinyTTF at size steps 56..400 px, created on first use. Rerun `idf.py reconfigure`
  if the TTF is added or removed.

- Analog inputs (carrier rev C, 2026-10-16): `src_adc` reads GPIO17/18 (ADC2 ch 6/7) with
  calibration, 16-sample averaging, timeout retries (radio shares ADC2), undoes the carrier's
  1k/1.5k divider and the 1k pull-up for resistive senders, and scales through user points into
  channels AUX1/AUX2 (`g_aux`, config block `aux`, phone page Channels tab "Input 1/2" cards,
  console `aux`). Disabled inputs are hidden from pickers (`ch_def_t.enabled`). The encoder is
  behind `CONFIG_NB1_ENCODER` (default off) because its pull-ups would load the inputs.
  Flashed 2026-10-17 from the prebuilt bundle (boots clean, inputs off by default); not yet tested with a sender.

- Stock styles and preset (2026-10-23): `STYLE_STOCK` ("Stock 1", id `stock`) in `face_styles.inc` draws the factory NB1 tach/speedo
  (chrome bezel, rim ticks at "nice" major/minor steps with the numerals inside, thin needle in the neutral
  colour on a big hub, zones as blocks in the tick ring like the orange redline, value in an odometer window).
  Preset "Stock" is the cream face with black type. Pick both (Theme > preset Stock, Style > Stock) for the
  factory look. Mirrored in `pod-preview.html` (`styleStock`) and the phone page (`STYLES`, mini preview).
  Built and flashed on the main PC 2026-10-23. Choosing Stock in the style picker applies the Stock preset (the picker
  previews it in those colours; stepping off or backing out restores). Numerals and labels go through
  `fonts_numerals()`: LiberationSans-BoldItalic.ttf (OFL, in `components/fonts` with its licence) via TinyTTF, bitmap
  Montserrat otherwise. The bezel is a soft sheen only, no dark half. `STYLE_STOCK2` ("Stock 2", id `stock2`) is the small
  fuel/oil/temp gauge: pivot 0.30R below centre, 90-degree sweep across the top from left to right, seven ticks, range ends lettered past the
  sweep, hub 0.22R. Both are proportioned off the factory faces (Stock 1: majors 0.115R long and 0.03R wide, minors 0.012R, numerals 44 px (0.14R) at 0.69R,
  hub 0.145R, needle 0.87R). Choosing either applies the Stock preset and turns the zone bands off: the faces are black
  and white like the factory, and the zone blocks and min/max pointers only appear with the bands switch. Menu, title,
  style pill and alarm banner colours come from `ui_restyle()` (ui.c): background = face colour, text and selection
  bubble keep the theme colours while they contrast (brightness gap > 90/255) and flip to near-black/near-white otherwise;
  it runs from `apply_theme()`, so preset changes restyle an open menu.

- Face renderer (2026-10-24): each needle/ring face caches its static artwork in a PSRAM image
  (`face_object.inc`, P_BG pass via an LVGL canvas, rebuilt on `face_restyle_all()`/style/channel/stale
  change); each frame blits it and draws only the moving parts (P_FG). Moving parts are marked
  `DYN_BEGIN()/DYN_END()` in the styles; a P_MEASURE pass records their rectangles and only the
  old+new rectangles are invalidated (needles as a chain of 80 px boxes, `LV_INV_BUF_SIZE=96`).
  Needles and hubs are drawn by `fast_raster.inc` straight into the RGB565 strip buffer (convex
  polygons only: the Stock needle is two pieces). The needle/bar follows the data with a critically
  damped spring at the frame rate; the reading updates at 8 Hz (`DIGIT_US`). UI tick 16 ms,
  `LV_DEF_REFR_PERIOD=16`, draw buffers 2 x 48 lines internal. Measured 32-39 fps on Stock 1
  (was ~24). Console `stats` prints `perf`/`perf2`/`perf3` frame timings; `snap [step]` and
  `tools/gauge_snap.py COM11 out.png [step]` capture the first face as a PNG.
- Stock numerals font (2026-10-24): `NB1StockNumerals-BoldItalic.ttf`, built by
  `tools/make_stock_font.py` from TeX Gyre Heros Bold Italic (free Helvetica clone, GUST Font
  License, `components/fonts/GUST-FONT-LICENSE.txt`): basic Latin, TrueType outlines, slant 16 deg.
  Replaces Liberation Sans (whose "1" did not match the cluster).

Not yet in firmware: stock-ECU path,
headlight/ambient dimming input (EXIO0 or I2C lux sensor via the custom PCB).
Wi-Fi: on at boot by default; `tools/serial_peek.py COM9 N [--noreset]` reads the log.

## Build order (from the brief)

1. Display + LVGL, one arc gauge on fake data
2. Encoder + menu
3. CAN listener, one hardcoded MS2 channel
4. Channel editor + layouts
5. Persistence
6. Wi-Fi config page, polish
7. Satellite mode (pod takes values/config from a cluster master over CAN)

## Working agreement (updated 2026-10-12)

- Hardware stage reached 2026-10-12: the Waveshare board is on COM9 (Espressif native USB, VID 303A PID 1001); on the second PC (no ESP-IDF, no Python) it is COM22 and `firmware/flash/flash.ps1` flashes the prebuilt bundle with the Arduino-bundled esptool. Build with `idf.py build`, flash with `idf.py -p COM9 flash`, peek at the log with `python tools/serial_peek.py COM9 12`. Still confirm the port shows that VID/PID before flashing.
- The GUI spec lives in `design/pod-preview/pod-preview.html` (with the phone page embedded); keep it and the firmware in step.

## Bench tooling (2026-10-12)

- `tools/serial_peek.py COM9 N [--noreset]` dumps N seconds of the log; without
  `--noreset` it resets the board first so the boot log is included.
- `tools/gauge_cmd.py COM9 "cmd" ...` sends console commands and prints replies.
- Console commands (`components/gauge_console`): `stats`, `vals`, `level`, `tap x y`,
  `hold`, `click`, `rot n`, `swipe dir`, `cfg <json>`, `getcfg`, `sim on|off`,
  `wifi on|off`, `tone n`, `screen save <name>|load <i>`, `save`, `reboot`, `help`.
  A `soak` heartbeat logs heap once a minute.
- `tools/soak.py COM9 <minutes> [seed]` runs random interaction cycles via the
  console and writes `firmware/soak.log`; flags errors, draw failures, reboots.
