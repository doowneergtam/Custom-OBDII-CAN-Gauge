"""NB1 pod carrier PCB generator (rev B: Traco TSR 1-2450 + WCMCU-230 module).

    python hardware/pcb/gen_pcb.py

Writes nb1-pod-carrier.kicad_pcb and nb1-pod-carrier-preview.svg.

Orientation: the view in this file (and the preview) is the PARTS side, which
is the rear of the pod, facing away from the Waveshare. Copper is on the other
face (B.Cu), toward the Waveshare. J1 is a long-pin header whose pins pass
through and plug into the Waveshare socket; every other part and both HY2.0
connectors sit on the rear face so the cables leave away from the screen.
Looking at the rear of the carrier matches looking at the back of the
Waveshare, so the header pin order below is the one printed on the Waveshare.

Single-sided, 0.8 mm signal / 1.2 mm power tracks, 0.6 mm clearance, 1.0 mm
drills. Wire links (JWx) are bare wires on the parts side where two nets must
cross.
"""
import pathlib, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from kicad_writer import Board, header_row, axial, radial, dip4, smd2

DIA = 46.0
HEADER_Y = -18.68
SIG, PWR, W3V3 = 0.6, 1.0, 0.8   # user's milling rules (rs-scanner pcblib): 0.6 signal, 1.0 power/GND, 0.8 3V3, 0.5 clearance, 2.25 pads, 1.0 holes

# WCMCU-230: 15.45 x 13.7 mm, one 1x6 header along one edge. Pin order as read
# on the module silkscreen, pin 1 first. EDIT THIS to match your module.
CAN_MODULE_PINS = ["CANL", "CANH", "CAN_RXM", "CAN_TX", "GND", "3V3"]   # module rotated 180 deg so CRX/CTX sit above J1 pins 4/5

b = Board("NB1 pod carrier rev B")

# ------------------------------------------------------------ parts
# J1 pin use (rev C): IO18 AIN2, IO17 AIN1, IO16 unused, RXD(44) CAN_TX, TXD(43) CAN_RX via R3, 3V3, GND, VBUS
b.footprint("J1", "to Waveshare 1x8", "PinHeader_1x08_P2.54mm_LongPins", (0, HEADER_Y),
            pads=header_row(8, names=["AIN2", "AIN1", "", "CAN_TX", "CAN_RX", "3V3", "GND", "5V"]),
            silk=[(-10.2, -1.3, 10.2, -1.3), (-10.2, 1.3, 10.2, 1.3), (-10.2, -1.3, -10.2, 1.3), (10.2, -1.3, 10.2, 1.3)],
            desc="Long-pin header (>=11 mm pins) soldered from the rear; pins protrude on the copper side into the Waveshare socket: IO18 IO17 IO16 RXD TXD 3V3 GND VBUS")

b.footprint("U2", "WCMCU-230 CAN module", "WCMCU-230_1x06", (0, -6.5),
            pads=header_row(6, names=CAN_MODULE_PINS),
            silk=[(-7.7, -1.5, 7.7, -1.5), (-7.7, 12.2, 7.7, 12.2), (-7.7, -1.5, -7.7, 12.2), (7.7, -1.5, 7.7, 12.2)],
            desc="SN65HVD230 module in a 1x6 female header; body 15.45 x 13.7 extends upward from the header. Has its own 120 ohm termination.")

b.footprint("J2", "HY2.0-5P car", "HY2.0_1x05_Vertical", (0, 14.5),
            pads=header_row(5, pitch=2.0, pad=1.6, drill=0.9, names=["GND", "LIGHTS", "CANL", "CANH", "12V_IN"]),
            silk=[(-6.3, -2.0, 6.3, -2.0), (-6.3, 3.7, 6.3, 3.7), (-6.3, -2.0, -6.3, 3.7), (6.3, -2.0, 6.3, 3.7)],
            desc="Car harness: GND, LIGHTS (12 V when headlights on), CAN L, CAN H, 12V IGN")

# J3: sensor connector. Two analog inputs on the old encoder GPIOs (17, 18 = ADC2 ch 6, 7).
b.footprint("J3", "HY2.0-4P sensors", "HY2.0_1x04_Vertical", (-17.5, -2.0),
            pads=header_row(4, pitch=2.0, pad=1.6, drill=0.9, names=["5V", "GND", "NODE1", "NODE2"], vertical=True),
            silk=[(-3.7, -5.3, 2.0, -5.3), (-3.7, 5.3, 2.0, 5.3), (-3.7, -5.3, -3.7, 5.3), (2.0, -5.3, 2.0, 5.3)],
            desc="Sensors: 5 V out for 0-5 V senders, GND, AIN1, AIN2. Pin 1 (5V) at the bottom")

# Per channel: node -> R1 1k -> GPIO, R2 1.5k GPIO -> GND (always in: scales 0-5 V to 0-3 V, limits current).
# JP closed adds RP 1k from 5 V to the node for resistive (thermistor) senders; open for 0-5 V senders.
# 1206 chip resistors on the copper side; jumpers through-hole. Filtering is done in firmware (averaging).
# Sensor conditioning: six 0603 resistors and two solder-bridge jumpers on the TOP (parts) side, in a
# cluster between the sensor connector and the CAN module. Column A at x=-13, column B at x=-8.5, rows 3 mm apart.
P06 = dict(span=1.6, pad=(0.9, 1.0)); PSJ = dict(span=1.6, pad=(1.0, 1.2))
b.footprint("RP2", "1k", "R_0603", (-13.0, -11.5), pads=smd2(nets=("PU2", "5V"), **P06), desc="Pull-up 1k from 5 V, in circuit when SJ2 is bridged (top side)")
import os
_SJ1 = tuple(float(v) for v in os.environ.get("NB1_SJ1", "-17.0,-9.5").split(","))
_SJ2 = tuple(float(v) for v in os.environ.get("NB1_SJ2", "-13.0,0.5").split(","))
b.footprint("SJ2", "bridge=resistive", "SolderJumper_2", _SJ2, pads=smd2(nets=("PU2", "NODE2"), **PSJ), desc="Solder bridge (top side): closed = resistive sender on input 2, open = 0-5 V sender")
b.footprint("RP1", "1k", "R_0603", (-13.0, -8.5), pads=smd2(nets=("PU1", "5V"), **P06), desc="Pull-up 1k from 5 V, in circuit when SJ1 is bridged (top side)")
b.footprint("R11", "1k", "R_0603", (-13.0, -5.5), pads=smd2(nets=("NODE1", "AIN1"), **P06), desc="Series 1k, sensor node 1 to GPIO17 (top side)")
b.footprint("R21", "1k", "R_0603", (-13.0, -2.5), pads=smd2(nets=("NODE2", "AIN2"), **P06), desc="Series 1k, sensor node 2 to GPIO18 (top side)")
b.footprint("SJ1", "bridge=resistive", "SolderJumper_2", _SJ1, pads=smd2(nets=("NODE1", "PU1"), **PSJ), desc="Solder bridge (top side): closed = resistive sender on input 1, open = 0-5 V sender")
b.footprint("R12", "1k5", "R_0603", (-8.5, -2.5), pads=smd2(nets=("AIN1", "GND"), **P06), desc="1.5k GPIO17 to GND: with R11 scales 5 V to 3 V (top side)")
b.footprint("R22", "1k5", "R_0603", (-8.5, 0.5), pads=smd2(nets=("AIN2", "GND"), **P06), desc="1.5k GPIO18 to GND: with R21 scales 5 V to 3 V (top side)")

b.footprint("J5", "EX0 SDA SCL wires", "PinHeader_1x03_P2.54mm", (-16.5, 10.0),
            pads=header_row(3, names=["EX0", "SDA", "SCL"], vertical=True),
            desc="Short wires to the Waveshare test pads EX0 / SDA / SCL (SDA, SCL spare for a lux sensor)")

# R3: 1k in the CAN RX line. GPIO43 carries the ROM boot log for ~300 ms at reset while the
# transceiver also drives it; 1k limits that contention. Its body also carries CAN_RX over CAN_TX.
b.footprint("R3", "1k", "R_Axial_P7.62", (0.8, -10.0), pads=axial(7.62, nets=("CAN_RXM", "CAN_RX")),
            top_ok=True, desc="1k between module CRX (pin 1, left) and header RXD/GPIO43 (pin 2, right); CAN_TX passes underneath")

# input protection chain, right side, top to bottom
b.footprint("F1", "Polyfuse 1.1A 30V", "Fuse_Radial_P5.08", (13.0, 11.5), pads=radial(5.08, nets=("12V_IN", "12V_F")), desc="Resettable fuse, Bourns MF-R110")
b.footprint("D1", "1N5822", "D_DO-201_P10.16", (12.5, 6.5), pads=axial(10.16, nets=("12V_P", "12V_F")), top_ok=True, desc="Reverse polarity Schottky; band (cathode) toward pin 1, the left end")
b.footprint("D2", "P6KE33A", "D_DO-15_P10.16", (14.0, 1.0), pads=axial(10.16, drill=1.2, nets=("12V_P", "GND")), top_ok=True, desc="Load dump TVS; band toward pin 1 (left)")
b.footprint("C1", "47uF 50V", "CP_Radial_P2.54", (4.0, 9.5), pads=radial(2.54, nets=("GND", "12V_P")), desc="Input bulk, + is pin 2 (right), - pin 1 (left)")
b.footprint("U1", "TSR 1-2450", "SIP-3_P2.54mm", (16.0, -3.5),
            pads=header_row(3, names=["12V_P", "GND", "5V"]),
            silk=[(-5.85, -1.5, 5.85, -1.5), (-5.85, 1.5, 5.85, 1.5), (-5.85, -1.5, -5.85, 1.5), (5.85, -1.5, 5.85, 1.5)],
            desc="Traco TSR 1-2450: pin 1 +Vin (left), 2 GND, 3 +Vout (right). 6.5-36 V in, 5 V 1 A out")
b.footprint("C2", "100uF 10V", "CP_Radial_P2.54", (14.0, -7.5), pads=radial(2.54, nets=("GND", "5V")), desc="5 V output bulk, + is pin 2 (right), - pin 1 (left)")
# headlight sense, left side: 12 V -> R2 -> opto LED; transistor pulls EX0 low
b.footprint("R2", "2k2", "R_Axial_P7.62", (-12.5, 9.5), rot=90, pads=axial(7.62, nets=("LED_K", "LIGHTS")), top_ok=True, desc="Opto LED limit, ~5 mA at 12 V")
b.footprint("U3", "PC817", "DIP-4_W7.62mm", (-8.5, 9.5), pads=dip4(["LED_K", "GND", "GND", "EX0"]),
            desc="1 anode (from R2), 2 cathode (GND), 3 emitter (GND), 4 collector (EX0). Lights on = EX0 low")

# ------------------------------------------------------------ mounting holes (match the Waveshare)
# M2 holes with a 3.4 mm copper ring so they show in the mill files and the standoffs seat on copper
for i, (x, y) in enumerate([(-13.75, -14.7), (13.75, -14.7), (0, 20.5)]):
    b.footprint(f"H{i + 1}", "M2 mount", "MountingHole_2.2mm_Ring3.4", (x, y),
                pads=[dict(name="1", x=0, y=0, shape="circle", size=(3.4, 3.4), drill=2.2, net="")],
                desc="M2 mounting hole matching the Waveshare, 3.4 mm copper ring, no net")

here_dir = str(pathlib.Path(__file__).resolve().parent)
exec(open(pathlib.Path(here_dir) / "gen_tracks.py", encoding="utf-8").read())

b.text("NB1 POD CARRIER B", 0, -22.0, size=1.0)
b.text("CAR", 0, 18.3, size=0.9)

here = pathlib.Path(__file__).resolve().parent
b.write(here / "nb1-pod-carrier.kicad_pcb", DIA)
print("wrote board: nets", len(b.nets) - 1, "parts", len(b.footprints), "segments", sum(len(tr[0]) - 1 for tr in b.tracks))
from svg_preview import write_svg
write_svg(b, DIA, here / "nb1-pod-carrier-preview.svg")
print("wrote preview")
from export_islands import export_all, drc as island_drc
print("wrote VCarve SVGs in", export_all(b, DIA, here / "mill"))
for e in island_drc(b, DIA): print("ISLAND-DRC", e)
