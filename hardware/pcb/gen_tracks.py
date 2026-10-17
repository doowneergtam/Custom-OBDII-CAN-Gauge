# Routing for gen_pcb.py (exec'd with b, DIA, SIG, PWR in scope). Tries many net orders, keeps the best.
import random
from router import route_all
POWER = ["12V_IN", "12V_F", "12V_P", "5V", "GND"]
WIDTHS = {n: PWR for n in POWER}
WIDTHS["3V3"] = W3V3
NETS = ["CAN_TX", "CAN_RX", "CAN_RXM", "3V3", "CANH", "CANL", "12V_IN", "12V_F", "12V_P", "5V", "LIGHTS", "LED_K", "EX0", "AIN1", "AIN2", "NODE1", "NODE2", "PU1", "PU2", "GND"]
import os, pathlib as _pl
_seedfile = _pl.Path(__file__).resolve().parent / "best_seed.txt" if "__file__" in globals() else _pl.Path("best_seed.txt")
_seedfile = _pl.Path(here_dir) / "best_seed.txt"
FIRST = ["3V3", "CAN_TX", "CAN_RXM", "CAN_RX", "NODE1", "NODE2", "PU1", "PU2", "AIN1", "AIN2", "CANH", "CANL"]
def _order(seed):
    order = [n for n in NETS if n not in FIRST]
    random.Random(seed).shuffle(order)
    return FIRST + order
best = None
seeds = range(int(os.environ.get("NB1_TRIES", "40")))
if _seedfile.exists() and not os.environ.get("NB1_REROUTE"):
    seeds = [int(_seedfile.read_text().strip())]
# hand-placed stubs the router must keep: GND leaves header pin 7 and climbs the corridor between the
# module's 3V3 pin and the right mounting hole, leaving room for 5 V beside it
PREROUTES = [([(6.35, HEADER_Y), (6.35, -17.0), (9.0, -14.35), (9.0, -9.5)], PWR, "GND", 0),          # bottom: header ground up the right-hand column
             ([(9.0, -9.5), (9.0, -4.25), (3.81, -4.25), (3.81, -6.5)], PWR, "GND", 0),                 # bottom: module GND pin to that column
             ([(-2.0, 14.5), (-2.0, 12.0)], SIG, "LIGHTS", 0),                                           # bottom: J2 lights pin to its via
             ([(-4.0, 14.5), (-7.23, 13.31)], PWR, "GND", 0),                                            # bottom: J2 ground pin to the opto ground pin
             ([(-7.23, 13.31), (-7.23, 5.69)], PWR, "GND", 0),                                           # bottom: the two opto ground pins
             ([(-4.0, 14.5), (-3.5, 16.9), (14.0, 16.0), (17.0, 13.5), (19.5, 9.0), (20.3, 4.0), (19.08, 1.0)], PWR, "GND", 0),   # bottom: J2 ground over the top and down the right edge to D2
             ([(8.92, 1.0), (13.46, -3.5)], PWR, "12V_P", 0),                                           # bottom: TVS to the regulator input
             ([(8.89, HEADER_Y), (10.75, -16.5), (11.1, -11.0)], PWR, "5V", 0),                             # bottom: header 5 V to a via
             ([(11.1, -11.0), (8.0, -14.0), (-9.0, -14.0), (-11.0, -12.5), (-12.2, -11.5)], SIG, "5V", 1),    # top: 5 V west above the header to the pull-ups
             ([(-6.35, HEADER_Y), (-6.35, -13.0), (-5.5, -12.0)], SIG, "AIN1", 0),                            # bottom: J1.2 up to its via
             ([(-5.5, -12.0), (-7.0, -9.5), (-7.5, -8.0), (-10.75, -7.0), (-10.75, -5.5), (-12.2, -5.5)], SIG, "AIN1", 1),   # top: round the module pin to R11
             ([(-10.75, -5.5), (-10.75, -2.5), (-9.3, -2.5)], SIG, "AIN1", 1),                               # top: and on to R12
             ([(-17.5, -3.0), (-19.0, -3.5), (-20.0, -1.0), (-19.5, 3.5), (-9.0, 3.5), (-7.23, 3.5), (-7.23, 5.69)], PWR, "GND", 0)]    # bottom: sensor ground round the connector to the opto
def _via(ref, x, y, net, desc):
    return dict(ref=ref, value="via", lib="Via_Rivet", at=(x, y), rot=0, top_ok=True, silk=[],
                pads=[dict(name="1", x=0, y=0, shape="circle", size=(2.25, 2.25), drill=1.0, net=net)], desc=desc)
SEED_VIAS = [_via("VL", -2.0, 12.0, "LIGHTS", "rivet via, headlight signal up to the top side to reach R2"),
             _via("VP5", 11.1, -11.0, "5V", "rivet via, 5 V up to the top side for the sensor cluster"),
             _via("VA1", -5.5, -12.0, "AIN1", "rivet via, analog input 1 up to its resistors")]
for seed in seeds:
    order = _order(seed)
    for npass in range(4):   # nets that failed go to the front of the next pass
        b.tracks = [tuple(p) for p in PREROUTES]
        b.footprints = [f for f in b.footprints if not f["ref"].startswith("V")] + [dict(v) for v in SEED_VIAS]   # drop vias from the previous try, keep the seeded ones
        failed = route_all(b, DIA, order, WIDTHS, clr=0.42, seed=seed)
        if best is None or len(failed) < len(best[0]):
            best = (failed, list(b.tracks), seed, list(order), [f for f in b.footprints if f["ref"].startswith("V")])
        if not failed: break
        hard = list(dict.fromkeys(n for n, _ in failed))
        order = [n for n in FIRST if n in hard] + [n for n in FIRST if n not in hard] + [n for n in hard if n not in FIRST] + [n for n in order if n not in hard and n not in FIRST]
    if not failed: break
FAILED, b.tracks, SEED, ORDER, VIAS = best
b.footprints = [f for f in b.footprints if not f["ref"].startswith("V")] + VIAS
print("vias", len(VIAS), "top tracks", sum(1 for tr in b.tracks if tr[3] == 1))
_seedfile.write_text(str(SEED))   # best so far, so drc/map/export reuse it instead of searching again
print("seed", SEED, "unrouted", len(FAILED))
for net, pin in FAILED:
    print("UNROUTED", net, "to pad at", pin)
