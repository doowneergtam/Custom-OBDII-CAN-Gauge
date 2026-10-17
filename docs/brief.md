# NB1 Miata Gauge Project Brief

Copied 2026-10-10 from the project brief doc (Mathew Greenwood). Corrections
discovered since are tracked in `decisions.md`, not edited here.

Two products share one firmware core: a standalone 52 mm gauge pod on an
ESP32-S3 round AMOLED, and a plug-in factory cluster replacement on an
ESP32-P4. Target car is a 2000 Mazda Miata (NB1) getting a MegaSquirt MS2.
Goal is a sellable product, scoped first to 1999-2000 NB1.

## Vehicle facts that drive the design

The NB1 has no CAN bus. The stock ECU speaks K-line (ISO 9141-2) on the OBD-II
port at 10.4 kbaud, roughly 4 to 6 PID reads per second total. Mazda did not
put CAN on a Miata until the 2006 NC.

The stock cluster receives everything as discrete harness signals:

- Ignition 12V, constant 12V for the odometer, ground, dimmer-controlled illumination
- Tach pulse from the ECU, VSS pulse from the transmission
- Fuel and coolant senders as variable resistance to ground. The gauge coolant
  sender is separate from the ECU coolant sensor.
- High beam, both turn signals
- Warning lamps: charge, oil pressure switch, brake, seatbelt, SRS, ABS, check engine

Verify from the NB1 workshop wiring diagram whether the stock cluster buffers
the VSS and sends a speed signal out to the ECU and cruise control. If so, the
replacement must regenerate that output. Also check whether the seatbelt and
key-in chime lives inside the cluster.

## MegaSquirt MS2

MS2/Extra firmware 3.4 and later has Dash Broadcasting (CAN-bus/Testmodes
menu in TunerStudio): standard 11-bit CAN frames at 500 kbps from base ID 1512
(0x5E8) in automatic mode. Frame 1512 carries MAP, RPM, CLT and TPS. Following
frames carry pulse width, MAT, advance, AFR target and actual, battery
voltage, the two generic sensor inputs, knock, and VSS in frame 1516. The
byte table is in `ms2-can.md`.

Install requirements:

- The MS2 must physically have its CAN transceiver fitted and wired out. The
  MS2 daughtercard has the CAN hardware; on a V3.0 mainboard CANH and CANL
  must be jumpered to spare DB37 pins. On an MSPNP2, check whether CAN reaches
  a connector at all.
- Firmware 3.4 or newer. Older firmware forces the harder 29-bit MegaSquirt
  request protocol.
- Speed is on CAN only if the VSS is wired to an MS2 spare digital input and configured.
- MS2 has two spare analog inputs exposed as generic sensors. Use them for oil
  pressure and oil temp; they appear in the broadcast, so the gauge needs no
  sensors of its own.
- Private two-node bus between gauge and ECU: 120 ohm termination at both ends.

With an MS2 installed, K-line goes silent (code must time out cleanly and
stop retrying), the check engine lamp is usually unused, and the tach wire may
carry 5V pulses instead of 12V.

## Shared firmware architecture

One codebase serves both products. Only the display driver and the I/O layer differ.

- Toolchain: ESP-IDF, not Arduino. P4 support in Arduino trails and the MIPI
  and LVGL drivers are IDF-first.
- UI: LVGL 9. Round-screen support, arc and scale widgets for bar and needle
  styles, native encoder and keypad input modes (rotate moves focus, push
  selects). Touch is a pointer device.
- Channel model: every displayed value is a channel record with name (8
  characters max), source, CAN ID, start bit, bit length, endianness, scale,
  offset, units, min, max, and warning thresholds. Sources: CAN, K-line PID,
  analog ADC, GPIO lamp, tach or VSS pulse. Display and menu layers never care
  where a value came from.
- Ship a MegaSquirt dash broadcast preset that pre-fills every channel. The
  raw channel editor comes later.
- One ECU type setting: stock (tach wire plus K-line) or MegaSquirt (CAN for
  RPM and all engine data).
- Config: one JSON file on LittleFS holding channels, layout, styles, names,
  brightness. Wear-leveled writes, never on every change. Odometer counters
  persist the same way.
- Layouts: 1 point is a full face with selectable styles (circular bar around
  digits, analog needle, big digital). 2 splits halves, 3 a triangle, 4
  quadrants, 5 to 6 a ring of cells around a center value.
- Features: per-channel warning thresholds with color change and flash, peak
  hold and min/max, startup needle sweep, auto-dim from the headlight input,
  periodic pixel shift and screen-off timeout against AMOLED burn-in, Wi-Fi
  config page for names and channel definitions in phase 2.

Bench rig, which also becomes the production test fixture: one ESP32-S3
sending MS2 dash-broadcast frames on CAN, a second ESP32 generating tach and
VSS pulses, a potentiometer per sender, a toggle switch per lamp. The whole UI
can be finished before the car has an MS2.

## Version 1: pod gauge

A standalone gauge for a standard 52 mm (2-1/16 inch) pod, built on the
Waveshare ESP32-S3-Touch-AMOLED-1.75
(https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75).

| Item | Detail |
|---|---|
| Display | 466x466 round AMOLED, CO5300 driver over QSPI, 44.5 mm active area |
| Touch | CST9217 capacitive, I2C |
| MCU | ESP32-S3R8, 8 MB PSRAM, 16 MB flash |
| Onboard | AXP2101 PMIC, PCF85063 RTC, QMI8658 IMU, ES8311 and ES7210 audio, TCA9554 IO expander, TF slot |
| Exposed I/O | 8-pin header with 3 GPIO and 1 UART, plus I2C pads and 3 expander IOs |

Pin budget as originally guessed in the brief (superseded by `pod-pinout.md`):
GPIO 1/2/3 for encoder A/B/push, UART TX/RX for the CAN transceiver
(TJA1051T/3 or SN65HVD230) via the native TWAI controller, I2C pads unused
(future ADS1115). No MCP2515 module needed. USB-C serial covers debugging.

Power: automotive 12V to 5V buck with load-dump and reverse-polarity
protection on ignition-switched power. Leave the battery connector empty.
Cheap USB car bucks brown out during cranking.

Risks: AMOLED burn-in (mitigated in firmware), heat since the AMOLED and PMIC
are rated to about 60 C (vent the pod), and touch while driving is awkward so
the encoder is the primary input.

Stock-ECU fallback if the MS2 is delayed: oil pressure and oil temp through an
ADS1115 on I2C, optional K-line on the UART pins through an L9637D. The same
two pins swap to a CAN transceiver later.

Build order:

1. Display and LVGL with one arc gauge on fake data
2. Encoder and menu system
3. CAN listener with one hardcoded MS2 channel
4. Channel editor and layouts
5. Persistence
6. Wi-Fi config page and polish
7. Satellite mode, where the pod takes values and config from a cluster master over CAN

## Version 2: factory cluster replacement

A plug-in replacement that reuses the factory cluster connector, the stock
lens, and a 3D-printed housing made from a scan of the stock cluster.

Stock NB1 layout, left to right: small round coolant gauge low in the left
corner, large tach, small round oil pressure gauge high in the center, large
speedo, small round fuel gauge low in the right corner, and a small landscape
odometer LCD bottom-center. The whole cluster is a D shape with a flat bottom
and arched top. Estimates until the scan replaces them: 320 to 340 mm wide,
140 to 150 mm tall at center, big dials 95 to 100 mm, small dials about 50 mm,
center column 45 to 50 mm wide. Measure tunnel inner diameters, center column
width at its narrowest, and depth behind the lens.

Connector: the stock cluster PCB carries board-mount headers the harness plugs
into. Photograph them and the harness plugs, read molded part numbers, and
match them in the connector manufacturer catalog for PCB footprints. Do not
cut donor harnesses.

Controller: ESP32-P4. Prototype on a dev kit with a MIPI-DSI display attached
(Espressif ESP32-P4 Function EV board with 7 inch 1024x600, or Waveshare P4
kits with 7 inch or 10.1 inch panels). Design the carrier PCB around the
Waveshare ESP32-P4-NANO footprint: Pi Zero size, 40-pin header, MIPI-DSI
connector. Later revisions can place the P4 module directly on the carrier
without firmware changes. Instant-on decided this over a Raspberry Pi.
Alternatives considered: NXP i.MX RT1170 (720p class, heavier toolchain),
Infineon Traveo II (real cluster silicon, impractical for a small team), and a
Pi CM5 as renderer with an ESP32-S3 doing real-time I/O and power sequencing,
which is the only route for a 1920x1080 eDP panel or on-board TunerStudio and
datalogging.

Display: one large screen is preferred. No off-the-shelf D-shaped panels exist
at this size. Free-form panels are OEM custom with minimum orders in the
thousands; Chinese vendors can cut a 12.3 or 13.3 inch LCD to a D for
low-thousands tooling and hundreds minimum, a possible version two. The
practical approach is a rectangle masked by the D-shaped housing with
firmware painting black outside the visible area.

| Panel | Active area | Coverage and notes |
|---|---|---|
| 12.3 inch 1920x720 bar LCD | 292 x 110 mm | Covers both big dials and center, misses 15 to 20 mm each side and the corner gauges. Automotive versions reach 1000 nits. Usually LVDS, so the carrier needs a MIPI-DSI-to-LVDS bridge, and 1920x720 at 60 Hz is near the top of the P4's two DSI lanes. |
| 10.1 inch 1280x800 kit panel | 217 x 136 mm | Covers big dials and center only. MIPI native, no bridge, comfortable for the P4. |
| 15.6 inch 1080p laptop panel | 344 x 194 mm | Covers the full D. eDP, beyond the P4, Pi only. About 400 nits. Must fit untrimmed behind the dash. |
| Hybrid | 12.3 or 10.1 center plus two pod AMOLED modules | Pod modules sit in the corner tunnels as CAN satellites. |

The multi-screen alternative was fully considered: a 3.5 inch 320x480
portrait center, two 4 inch 720x720 round IPS for tach and speedo, two 1.75
inch AMOLED pod modules for the corners, all satellites on a private CAN bus
with the center as master, about $170 to $220 in displays. Resolve the display
interface first; it is the riskiest item. Prove the 12.3 inch panel with a
bridge on a breakout before committing it to the PCB.

Carrier PCB blocks, each breadboarded on the dev kit header first:

- Automotive buck with 36V-rated input, TVS, reverse-polarity protection, bulk capacitance for cranking
- Ground-switched lamp inputs: pull-up to 12V, divider to 3.3V. Bulb-equivalent
  resistive loads on the charge lamp (the alternator L terminal needs lamp
  current to excite on many Japanese alternators) and the SRS lamp (the module
  checks for bulb presence)
- 12V-switched inputs for turn signals, high beam, and dimmer with clamp and RC filtering
- Tach and VSS pulse inputs through a comparator so both 5V and 12V pulses count
- Pull-ups and filtered ADC inputs for fuel and temp senders, heavy software damping on fuel
- Buffered open-collector VSS output back to the ECU, if the stock cluster does this
- TJA1051T/3 CAN and L9637D K-line with ESD protection, and a 2-pin CAN pigtail
  connector since the stock harness has no CAN pair
- Keypad connector for a 5-button pad (up, down, left, right, select), driven as an LVGL keypad device
- Ignition-off sense line plus a few hundred milliseconds of hold-up
  capacitance so the P4 can flush odometer and config before power drops
- Speaker driver if the chime lives in the cluster
- 2x20 socket for the P4-NANO, and a thermal path from the P4 and display to the housing

Odometer: the installer sets the starting mileage once at install to match
the stock cluster, then it locks. Log the stock mileage at swap time. A
non-working or reset odometer is an inspection or title problem in some states.

With an MS2 installed nothing on the connector changes except the tach pulse
level and the unused check engine lamp. Speed, fuel, coolant gauge, and every
lamp come from the harness regardless of ECU.

## Open questions and next steps

Open questions:

- [x] Exact header GPIO numbers on the Waveshare 1.75 inch board (resolved 2026-10-10, see `pod-pinout.md`)
- [ ] NB1 cluster connector pinout and board-mount header part numbers, from the workshop manual and the physical cluster
- [ ] Does the NB1 cluster output a speed signal to the ECU? Is the chime inside the cluster?
- [ ] Which MS2 variant (MSPNP2 or DIY V3.0), and whether CAN is brought out
- [ ] Scan dimensions: tunnel diameters, center column width, depth, and whether a 292 x 110 mm or larger rectangle fits behind the opening
- [ ] P4 frame rate at 1920x720 with a needle sweep, measured on the dev kit

Immediate next steps:

1. Order the Waveshare ESP32-S3-Touch-AMOLED-1.75, a rotary encoder with push,
   a TJA1051T/3 breakout, an automotive 12V to 5V buck, and a second ESP32-S3
   for the bench CAN simulator
2. Start the pod firmware in ESP-IDF with LVGL 9, following the build order above
3. Order an ESP32-P4 dev kit with display and measure LVGL frame rate at full resolution
4. Pull the NB1 workshop wiring diagram and produce the cluster connector pin table
5. Scan the cluster and overlay candidate panel rectangles on the D outline

Sources: Waveshare ESP32-S3-Touch-AMOLED-1.75 wiki, MS2/Extra 3.4 setup manual
(CAN broadcasting), DIYAutoTune MS2 daughterboard
(https://www.diyautotune.com/product/cpu-megasquirt-ii-daughterboard/).
