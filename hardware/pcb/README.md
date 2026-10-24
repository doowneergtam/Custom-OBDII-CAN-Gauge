# Pod carrier PCB, rev C (two analog inputs, no encoder)

Circular Ø46 mm two-layer board behind the Waveshare ESP32-S3-Touch-AMOLED-1.75.
It plugs into the Waveshare's 8-pin socket with a long-pin header and carries
the input protection, a Traco TSR 1-2450 5 V buck, a socket for a WCMCU-230
(SN65HVD230) CAN module, the headlight opto-sense, two conditioned analog
sensor inputs, and two HY2.0 connectors (car harness, sensors). Parts and
both connectors sit on the rear face, cables leave rearward. Bottom copper
faces the Waveshare; a sparse top copper layer (17 short tracks) carries the
crossings, joined by 9 rivet vias.

Rules: 2.25 mm pads, 1.0 mm holes (1.2 mm TVS, 0.9 mm HY2.0), 0.6 mm signal /
1.0 mm power and ground / 0.8 mm 3V3 tracks, 0.8 mm from the edge. Clearance
is 0.42 mm on this board (0603 chip parts in the sensor cluster); the checker
accepts 0.35 mm, so use a 0.2 mm isolation cut. This is finer than the rev B
rules, as agreed, because the sensor network would not fit at 0.5 mm.

## Files

| File | What |
|---|---|
| `gen_pcb.py` | Parts and placement (jumper positions overridable with `NB1_SJ1`, `NB1_SJ2`). |
| `gen_tracks.py` | Routing driver: net order, widths, hand-seeded stubs and vias, cached seed. |
| `router.py` | Two-layer grid router with rivet vias; top tracks only end on axial leads, chip pads or vias. |
| `drc.py`, `export_islands.py` | Crossing/clearance/edge/connectivity checks (geometric and shapely islands). |
| `sweep.py` | Tries jumper placements and reports which route cleanly. |
| `ascii_map.py`, `svg_preview.py` | Text map (`NB1_MAP_LAYER=1` for top) and the labelled preview. |
| `mill/nb1_carrier_copper_bottom_view.svg` | **Cut this first**: bottom copper, mirrored, with outline and drill layers by size. |
| `mill/nb1_carrier_copper_top_view.svg` | **Then flip and cut this**: top copper, parts-side view, not mirrored, same frame. |
| `mill/nb1_carrier_drill_only.svg`, `_copper_only.svg`, `_assembly_top_view.svg` | Drills alone, copper alone, placement with refs and pin names. |
| `assembly_sheet.py`, `mill/nb1_carrier_assembly_sheet.svg` | Blown-up assembly drawing with leader-line labels, pin numbers, via and top-side markings, and the BOM with part numbers. |
| `nb1-pod-carrier.kicad_pcb` | KiCad 8 file of the same board. |

```bash
python hardware/pcb/gen_pcb.py && python hardware/pcb/drc.py
```
Delete `best_seed.txt` after moving parts so the router searches again.

## Milling and assembly order

1. Mill the bottom (mirrored file), drill all holes, flip on two of the M2
   holes as your registration, mill the top (not mirrored).
2. Fit the 9 vias (1.0 mm holes): copper rivets swaged both sides, or trimmed resistor leads soldered
   both sides. The assembly sheet rings each one in its own colour (via key under the board). Positions, parts-side
   view, mm from the centre:

   | Via | Net | x | y |
   |---|---|---|---|
   | VL | LIGHTS | −2.0 | 12.0 |
   | VP5 | 5V | 11.1 | −11.0 |
   | VA1 | AIN1 | −5.5 | −12.0 |
   | V4 | NODE1 | −14.15 | −4.9 |
   | V5 | NODE2 | −11.65 | 1.1 |
   | V6 | AIN2 | −6.15 | −0.9 |
   | V7 | 5V | −12.15 | −9.65 |
   | V8 | GND | −7.65 | 1.85 |
   | V9 | GND | 2.1 | 8.6 |

3. Solder the 0603 parts and the two solder jumpers on the TOP side (they sit
   left of the CAN module, beside the sensor connector).
4. Fit the through-hole parts from the top and solder on the bottom. Three
   axial leads also carry a top-side track and must be soldered on the top as
   well: **D1 pin 1, D2 pin 1, R2 pin 2** (the top end). Everything else is
   bottom only.
5. J1: long-pin header, body on top, pins through into the Waveshare socket.

## Sensor inputs

Per input: sensor → 1 kΩ → GPIO, 1.5 kΩ GPIO → GND, so 0 to 5 V reads as 0 to
3 V. Solder jumper closed adds a 1 kΩ pull-up from 5 V at the sensor node for
resistive (temperature) senders; open for 0 to 5 V senders. SJ1 is input 1
(GPIO17, J3 pin 3), SJ2 is input 2 (GPIO18, J3 pin 4). J3: 5V, GND, SENS1,
SENS2. Firmware filtering replaces the input capacitor.

## Connectors and header

| J1 pin | Net | J2 (car) | J3 (sensors) |
|---|---|---|---|
| 1 IO18 | AIN2 | 1 GND | 1 5V out |
| 2 IO17 | AIN1 | 2 LIGHTS | 2 GND |
| 3 IO16 | unused | 3 CAN L | 3 SENS1 |
| 4 IO44 | CAN_TX | 4 CAN H | 4 SENS2 |
| 5 IO43 | CAN_RX via R3 | 5 12 V ign | |
| 6 3V3 | 3V3 | | |
| 7 GND | GND | | |
| 8 VBUS | 5V | | |

## Verify before milling

1. WCMCU-230 pin order (`CAN_MODULE_PINS` in `gen_pcb.py`): module reads
   `3V3 GND CTX CRX CANH CANL`, mounted rotated so it lands `CANL CANH CRX CTX GND 3V3`.
2. Traco pin 1 (+Vin) is the left pin of U1; D1 and D2 bands toward pin 1 (left).
3. C1 and C2 positive is pin 2 (right pad).
4. Mounting holes match the Waveshare at (±13.75, −14.70) and (0, +20.50),
   3.4 mm rings. The top ring sits 0.8 mm from the edge, which is the rule limit.

## Bill of materials (rev C)

| Ref | Part |
|---|---|
| J1 | 1x8 long-pin header 2.54 mm |
| J2 | HY2.0-5P vertical |
| J3 | HY2.0-4P vertical |
| J5 | 1x3 pads, wires to Waveshare EX0 / SDA / SCL |
| U2 + socket | WCMCU-230 CAN module, 1x6 female header |
| U1 | Traco TSR 1-2450 |
| F1 | Polyfuse 1.1 A 30 V radial |
| D1 | 1N5822 |
| D2 | P6KE33A or SMBJ26A on leads |
| C1 | 47 µF 50 V, 2.54 pitch |
| C2 | 100 µF 10 V, 2.54 pitch |
| R2 | 2.2 kΩ 1/4 W |
| R3 | 1 kΩ 1/4 W |
| U3 | PC817 |
| R11, R21, RP1, RP2 | 1 kΩ 0603 |
| R12, R22 | 1.5 kΩ 0603 |
| SJ1, SJ2 | solder-bridge pads |
| V* | 9 × 1.0 mm copper rivets |
