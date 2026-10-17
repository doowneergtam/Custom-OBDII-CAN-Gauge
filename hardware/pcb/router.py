"""Two-layer grid router (Dijkstra, 8-neighbour) for the pod carrier.

Layer 0 = bottom copper (toward the Waveshare), layer 1 = top copper (parts
side). A path may change layer only by placing a via (rivet: 2.25 pad, 1.0
hole), which costs VIA_COST grid steps. Through-hole pads exist on both layers,
but a top-layer track may only enter a pad whose footprint allows it
(``top_ok``: axial parts and vias), because connector and socket bodies cover
their pads on the parts side. SMD pads are bottom-only. Hand-placed tracks in
b.tracks before routing are kept and used as seeds.

Tracks are stored as (points, width, net, layer). Vias are appended to
b.footprints as refs V1, V2 ... with net set, so later checks see them."""
import heapq, math, random

VIA_COST = 120
VIA_PAD, VIA_DRILL = 2.25, 1.0

def route_all(b, dia, order, widths, clr=0.5, pitch=0.25, keep=None, seed=0, layers=2):
    rnd = random.Random(seed)
    R = dia / 2 - 0.6
    def cell(x, y): return (round((x + R) / pitch), round((y + R) / pitch))
    def xy(c): return (round(c[0] * pitch - R, 3), round(c[1] * pitch - R, 3))
    pads = []   # (x, y, r, net, layers_set, top_ok)
    for f in b.footprints:
        fx, fy = f["at"]; r = math.radians(f["rot"]); cs, sn = math.cos(r), math.sin(r)
        for p in f["pads"]:
            x, y = fx + p["x"] * cs - p["y"] * sn, fy + p["x"] * sn + p["y"] * cs
            lay = ({1} if p.get("layer") == "F.Cu" else {0}) if (p.get("drill", 1.0) == 0) else {0, 1}
            pads.append((x, y, max(p["size"]) / 2, p.get("net", ""), lay, f.get("top_ok", False) or lay == {1}))   # top-side SMD pads are always reachable from the top
    for (x, y, d) in b.holes:
        pads.append((x, y, d / 2 + 0.3, "HOLE", {0, 1}, False))
    ws = sorted(set(widths.values()) | {0.8, VIA_PAD})   # VIA_PAD entry = keepout map for placing a via
    blocked = {L: {w: {} for w in ws} for L in range(layers)}
    def block(x, y, rad, owner, L):
        for w in ws:
            rw = rad + w / 2
            c0 = cell(x, y); k = int(rw / pitch) + 1
            for i in range(c0[0] - k, c0[0] + k + 1):
                for j in range(c0[1] - k, c0[1] + k + 1):
                    if math.hypot(i - c0[0], j - c0[1]) * pitch <= rw:
                        blocked[L][w].setdefault((i, j), set()).add(owner)
    owner = {L: {} for L in range(layers)}   # cells inside a pad on that layer -> (net, top_ok)
    def add_pad(x, y, rad, net, lay, top_ok):
        for L in lay:
            block(x, y, rad + clr, net, L)
            c0 = cell(x, y); k = int(rad / pitch) + 1
            for i in range(c0[0] - k, c0[0] + k + 1):
                for j in range(c0[1] - k, c0[1] + k + 1):
                    if math.hypot(i - c0[0], j - c0[1]) * pitch <= rad: owner[L][(i, j)] = (net, top_ok)
        # SMD pads occupy one layer only; the other layer is free under them
    for (x, y, rad, net, lay, top_ok) in pads:
        add_pad(x, y, rad, net, lay, top_ok)
    if keep:
        for (x, y, rad) in keep:
            for L in range(layers): block(x, y, rad, "KEEP", L)
    preset = {}
    for tr in list(b.tracks):
        pts, w0, net0 = tr[0], tr[1], tr[2]; L0 = tr[3] if len(tr) > 3 else 0
        for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
            k = max(1, int(math.hypot(x2 - x1, y2 - y1) / (pitch / 2)))
            for t in range(k + 1):
                x, y = x1 + (x2 - x1) * t / k, y1 + (y2 - y1) * t / k
                block(x, y, w0 / 2 + clr, net0, L0); preset.setdefault(net0, set()).add((L0,) + cell(x, y))
    def free(n, net, w):
        L, c = n[0], (n[1], n[2])
        if math.hypot(*xy(c)) > R: return False
        o = owner[L].get(c)
        s = blocked[L][w].get(c)
        if o is not None:
            return o[0] == net and (L == 0 or o[1]) and (s is None or s == {net})
        return s is None or s == {net}
    def via_ok(c, net, w):
        # a via needs a pad-sized free spot on both layers
        for L in range(layers):
            if owner[L].get(c) is not None: return False
            s = blocked[L][VIA_PAD].get(c)
            if s is not None and s != {net}: return False
        if math.hypot(*xy(c)) > R - VIA_PAD: return False
        return True
    failed = []
    nvia = sum(1 for f in b.footprints if f["ref"].startswith("V"))
    for net in order:
        w = widths.get(net, 0.8)
        pins = [(x, y, lay, tok) for (x, y, rad, nn, lay, tok) in pads if nn == net]
        if len(pins) < 2: continue
        rnd.shuffle(pins)
        def pin_nodes(p): return {(L,) + cell(p[0], p[1]) for L in p[2] if L == 0 or p[3]}   # top entry only where the lead can be soldered on top
        groups = []
        if net in preset:
            # split the seeded cells into connected groups; the first is the start, the others must be joined too
            left = set(preset[net])
            while left:
                start = next(iter(left)); comp = {start}; stack = [start]; left.discard(start)
                while stack:
                    L0, i0, j0 = stack.pop()
                    nbrs = [(L0, i0 + di, j0 + dj) for di in (-1, 0, 1) for dj in (-1, 0, 1)] + [(1 - L0, i0, j0)]
                    for nb in nbrs:
                        if nb in left:
                            left.discard(nb); comp.add(nb); stack.append(nb)
                groups.append(comp)
            done = set(groups[0]); groups = groups[1:]; pins_left = pins[:]
        else:
            done = pin_nodes(pins[0]); pins_left = pins[1:]
        while pins_left or groups:
            targets = {}
            for gi, grp in enumerate(groups):
                for n in grp: targets[n] = ("group", gi)
            for p in pins_left:
                for n in pin_nodes(p): targets[n] = p
            dist = {n: 0 for n in done}; prev = {}
            pq = [(0, n) for n in done]; heapq.heapify(pq); hit = None
            while pq:
                d, n = heapq.heappop(pq)
                if d > dist.get(n, 1e9): continue
                if n in targets: hit = n; break
                L, ci, cj = n
                for di in (-1, 0, 1):
                    for dj in (-1, 0, 1):
                        if di == dj == 0: continue
                        nn = (L, ci + di, cj + dj)
                        if not free(nn, net, w):
                            if nn not in targets: continue
                            sb = blocked[nn[0]][w].get((nn[1], nn[2]))
                            if sb is not None and sb != {net}: continue      # entering a target pad still respects other nets' clearance
                        if di and dj and (not free((L, ci + di, cj), net, w) or not free((L, ci, cj + dj), net, w)): continue
                        nd = d + (1.4 if di and dj else 1.0) + (0.4 if (di, dj) != prev.get(n, (None, (0, 0)))[1] else 0) + (0.3 if L == 1 else 0)
                        if nd < dist.get(nn, 1e9):
                            dist[nn] = nd; prev[nn] = (n, (di, dj)); heapq.heappush(pq, (nd, nn))
                if layers > 1:
                    nn = (1 - L, ci, cj)
                    c = (ci, cj)
                    thru = owner[0].get(c) == (net, True) and owner[1].get(c) == (net, True)   # inside a top-ok through-hole pad of this net: free layer change, no via
                    if thru or via_ok(c, net, w):
                        nd = d + (0 if thru else VIA_COST)
                        if nd < dist.get(nn, 1e9):
                            dist[nn] = nd; prev[nn] = (n, (0, 0)); heapq.heappush(pq, (nd, nn))
            if hit is None:
                # nothing reachable from the current island: note the break, start a new island from the next pin
                if pins_left:
                    p = pins_left.pop(0); failed.append((net, p[:2])); done = pin_nodes(p)
                else:
                    failed.append((net, "seed group")); done = set(groups.pop(0))
                continue
            path = [hit]
            while path[-1] not in done: path.append(prev[path[-1]][0])
            path.reverse()
            # split the path into same-layer runs; a layer change is a via
            run = [path[0]]
            def flush(run):
                if len(run) < 2: return
                pts = [xy(run[0][1:])]
                for a, c2, d2 in zip(run, run[1:], run[2:] + [None]):
                    if d2 is None or (c2[1] - a[1], c2[2] - a[2]) != (d2[1] - c2[1], d2[2] - c2[2]): pts.append(xy(c2[1:]))
                b.track(pts, net, w, run[0][0])
            for n in path[1:]:
                if n[0] != run[-1][0]:
                    flush(run)
                    x, y = xy(n[1:])
                    if owner[0].get(n[1:]) == (net, True) and owner[1].get(n[1:]) == (net, True):
                        run = [n]; continue      # layer change inside a through-hole pad: the pad is the via
                    nvia += 1
                    b.footprints.append(dict(ref=f"V{nvia}", value="via", lib="Via_Rivet", at=(x, y), rot=0, top_ok=True, silk=[],
                                             pads=[dict(name="1", x=0, y=0, shape="circle", size=(VIA_PAD, VIA_PAD), drill=VIA_DRILL, net=net)],
                                             desc="rivet via"))
                    add_pad(x, y, VIA_PAD / 2, net, {0, 1}, True)
                    run = [n]
                else:
                    run.append(n)
            flush(run)
            for n in path:
                done.add(n)
                x, y = xy(n[1:]); block(x, y, w / 2 + clr, net, n[0])
            tgt = targets[hit]
            if isinstance(tgt, tuple) and tgt and tgt[0] == "group":
                done |= groups.pop(tgt[1])
            else:
                pins_left.remove(tgt)
    return failed
