"""Poor man's DRC for the generated board: run after gen_pcb.py.
Reports (1) track segments of different nets that cross or come closer than
CLR, (2) tracks passing closer than CLR to a pad of another net, (3) pads of
different nets closer than CLR, (4) copper outside the board, (5) nets with
pads that no track reaches (unrouted)."""
import math, pathlib, sys, io, contextlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
with contextlib.redirect_stdout(io.StringIO()):
    import gen_pcb
b, DIA = gen_pcb.b, gen_pcb.DIA
CLR = 0.3

def seg_dist(p, a, b_):
    ax, ay = a; bx, by = b_; px, py = p
    dx, dy = bx - ax, by - ay
    L = dx * dx + dy * dy
    t = 0 if L == 0 else max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / L))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))

def segs_dist(a1, a2, b1, b2):
    def cross(o, a, b_): return (a[0] - o[0]) * (b_[1] - o[1]) - (a[1] - o[1]) * (b_[0] - o[0])
    d1, d2 = cross(b1, b2, a1), cross(b1, b2, a2)
    d3, d4 = cross(a1, a2, b1), cross(a1, a2, b2)
    if ((d1 > 0) != (d2 > 0)) and ((d3 > 0) != (d4 > 0)) and d1 * d2 < 0 and d3 * d4 < 0:
        return 0.0
    return min(seg_dist(a1, b1, b2), seg_dist(a2, b1, b2), seg_dist(b1, a1, a2), seg_dist(b2, a1, a2))

segs = []
for tr in b.tracks:
    pts, w, net = tr[0], tr[1], tr[2]; lay = tr[3] if len(tr) > 3 else 0
    for a, c in zip(pts, pts[1:]):
        segs.append((a, c, w, net, lay))
pads = []
smd_only = {}
for f in b.footprints:
    fx, fy = f["at"]; r = math.radians(f["rot"]); cs, sn = math.cos(r), math.sin(r)
    for p in f["pads"]:
        x, y = fx + p["x"] * cs - p["y"] * sn, fy + p["x"] * sn + p["y"] * cs
        pads.append(((x, y), max(p["size"]) / 2, p.get("net", ""), f"{f['ref']}.{p['name']}"))
        if p.get("drill", 1.0) == 0: smd_only[f"{f['ref']}.{p['name']}"] = 1 if p.get("layer") == "F.Cu" else 0
for (x, y, d) in b.holes:
    pads.append(((x, y), d / 2 + 0.3, "HOLE", "H"))

issues = []
for i in range(len(segs)):
    a1, a2, w1, n1, l1 = segs[i]
    for j in range(i + 1, len(segs)):
        b1, b2, w2, n2, l2 = segs[j]
        if n1 == n2 or l1 != l2: continue
        d = segs_dist(a1, a2, b1, b2) - w1 / 2 - w2 / 2
        if d < CLR: issues.append(f"track {n1} {a1}-{a2} vs track {n2} {b1}-{b2}: gap {d:.2f}")
    for (pc, pr, pn, pname) in pads:
        if pn == n1: continue
        if pname in smd_only and smd_only[pname] != l1: continue   # SMD pad on the other layer
        d = seg_dist(pc, a1, a2) - pr - w1 / 2
        if d < CLR: issues.append(f"track {n1} {a1}-{a2} vs pad {pname} ({pn}): gap {d:.2f}")
    for pt in (a1, a2):
        if math.hypot(*pt) + w1 / 2 > DIA / 2 - 0.5: issues.append(f"track {n1} point {pt} too near the edge")
for i in range(len(pads)):
    for j in range(i + 1, len(pads)):
        (c1, r1, n1, p1), (c2, r2, n2, p2) = pads[i], pads[j]
        if n1 == n2 and n1 != "": continue
        if p1.split(".")[0] == p2.split(".")[0]: continue   # pins of one part: pitch is the part's, not ours
        d = math.hypot(c1[0] - c2[0], c1[1] - c2[1]) - r1 - r2
        if d < CLR: issues.append(f"pad {p1} ({n1}) vs pad {p2} ({n2}): gap {d:.2f}")
for (pc, pr, pn, pname) in pads:
    if math.hypot(*pc) + pr > DIA / 2 - 0.5: issues.append(f"pad {pname} too near the edge")
# connectivity: every pad with a net must touch a track end or another pad of that net
for (pc, pr, pn, pname) in pads:
    if pn in ("", "HOLE"): continue
    ok = any(n == pn and (math.hypot(a[0] - pc[0], a[1] - pc[1]) <= pr + 0.1 or math.hypot(c[0] - pc[0], c[1] - pc[1]) <= pr + 0.1) for a, c, w, n, l in segs)
    if not ok: issues.append(f"pad {pname} ({pn}) has no track")
# per-net: tracks must form one connected set (union-find on segment ends + pads)
for net in sorted(set(n for *_, n in segs)):
    nodes = []
    for a, c, w, n, l in segs:
        if n == net: nodes.append([a, c])
    for (pc, pr, pn, pname) in pads:
        if pn == net: nodes.append([pc, pc])
    parent = list(range(len(nodes)))
    def find(i):
        while parent[i] != i: parent[i] = parent[parent[i]]; i = parent[i]
        return i
    for i in range(len(nodes)):
        for j in range(i + 1, len(nodes)):
            if segs_dist(nodes[i][0], nodes[i][1], nodes[j][0], nodes[j][1]) < 0.9:
                parent[find(i)] = find(j)
    groups = len(set(find(i) for i in range(len(nodes))))
    if groups > 1: issues.append(f"net {net} is in {groups} pieces")
print(f"{len(issues)} issues")
for s in issues: print(" ", s)
