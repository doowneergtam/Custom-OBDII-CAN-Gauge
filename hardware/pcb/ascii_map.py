"""ASCII picture of the generated board at 0.5 mm per character (parts-side view, +y up).
Pads print as the net's letter in upper case, tracks in lower case, holes as #."""
import io, contextlib, math, sys
with contextlib.redirect_stdout(io.StringIO()):
    import gen_pcb as g
b, R = g.b, g.DIA / 2
P = 0.5
N = int(2 * R / P) + 1
grid = [["." if math.hypot((i - N // 2) * P, (j - N // 2) * P) <= R else " " for i in range(N)] for j in range(N)]
LET = {"GND": "g", "5V": "5", "3V3": "3", "12V_IN": "i", "12V_F": "f", "12V_P": "p", "CANH": "h", "CANL": "l",
       "CAN_TX": "t", "CAN_RX": "r", "CAN_RXM": "m", "ENC_A": "a", "ENC_B": "b", "ENC_SW": "w", "LIGHTS": "L", "LED_K": "k", "EX0": "x", "SDA": "d", "SCL": "c", "AIN1": "1", "AIN2": "2", "NODE1": "n", "NODE2": "o", "PU1": "u", "PU2": "v", "5V_S": "s"}
def put(x, y, ch):
    i, j = round((x + R) / P), round((R - y) / P)
    if 0 <= j < N and 0 <= i < N and grid[j][i] != " ": grid[j][i] = ch
import os
LAYER = int(os.environ.get("NB1_MAP_LAYER", "0"))
for tr in b.tracks:
    pts, w, net = tr[0], tr[1], tr[2]
    if (tr[3] if len(tr) > 3 else 0) != LAYER: continue
    for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
        n = max(1, int(math.hypot(x2 - x1, y2 - y1) / (P / 2)))
        for k in range(n + 1):
            put(x1 + (x2 - x1) * k / n, y1 + (y2 - y1) * k / n, LET.get(net, "?"))
for f in b.footprints:
    fx, fy = f["at"]; r = math.radians(f["rot"]); cs, sn = math.cos(r), math.sin(r)
    for p in f["pads"]:
        put(fx + p["x"] * cs - p["y"] * sn, fy + p["x"] * sn + p["y"] * cs, LET.get(p.get("net", ""), "o").upper() if p.get("net") else "O")
for (x, y, d) in b.holes:
    put(x, y, "#")
for j, row in enumerate(grid):
    y = R - j * P
    print(f"{y:6.1f} " + "".join(c or "" for c in row))
print("       x from %.1f to %.1f, 0.5 mm per column" % (-R, R))
