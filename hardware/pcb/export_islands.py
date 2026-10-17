"""VCarve-ready export in the same form as rs-scanner/pcb/pcblib.py: one closed
copper island per net (pads + traces unioned with shapely), exported MIRRORED
for the bottom copper so it mills as drawn, drill circles grouped by size,
board outline, plus an un-mirrored assembly view. Also a shapely DRC: island to
island clearance, edge distance, connectivity.

Files (in mill/):
  nb1_carrier_copper_bottom_view.svg   outline + copper islands + drills, mirrored (the one to cut)
  nb1_carrier_copper_only.svg          copper islands only, mirrored
  nb1_carrier_drill_only.svg           drills only, mirrored, one layer per size
  nb1_carrier_assembly_top_view.svg    parts side, not mirrored, with refs and pin names
"""
import math, pathlib
from shapely.geometry import Point, LineString, Polygon
from shapely.ops import unary_union

CLEAR, EDGE = 0.35, 0.8

def _pads(b):
    out = []
    for f in b.footprints:
        fx, fy = f["at"]; r = math.radians(f["rot"]); cs, sn = math.cos(r), math.sin(r)
        for p in f["pads"]:
            x, y = fx + p["x"] * cs - p["y"] * sn, fy + p["x"] * sn + p["y"] * cs
            out.append(dict(net=p.get("net", ""), x=x, y=y, od=max(p["size"]), hole=p.get("drill", 0), ref=f["ref"], name=p["name"],
                            layers=({1} if p.get("layer") == "F.Cu" else {0}) if not p.get("drill", 0) else {0, 1}))
    return out

def islands(b, layer=0):
    nets = {}
    for i, p in enumerate(_pads(b)):
        if layer not in p["layers"]: continue           # SMD pads live on one layer only
        key = p["net"] or f"NC_{p['ref']}_{p['name']}"
        nets.setdefault(key, []).append(Point(p["x"], p["y"]).buffer(p["od"] / 2, 24))
    for tr in b.tracks:
        pts, w, net = tr[0], tr[1], tr[2]
        if (tr[3] if len(tr) > 3 else 0) != layer: continue
        if len(pts) > 1:
            nets.setdefault(net, []).append(LineString(pts).buffer(w / 2, 12, join_style=1, cap_style=1))
    return {n: unary_union(g).simplify(0.004) for n, g in nets.items()}

def has_top(b): return any(len(tr) > 3 and tr[3] == 1 for tr in b.tracks)

def drc(b, dia):
    errs = []
    both = {}
    for L in ([0, 1] if has_top(b) else [0]):
        for n, g in islands(b, L).items(): both.setdefault(n, []).append(g)
    for n, gs in both.items():   # connectivity with both layers overlaid (through-hole pads and vias join them)
        u = unary_union(gs)
        if u.geom_type != "Polygon" and not n.startswith("NC_"):
            errs.append(f"OPEN   {n}: {len(u.geoms)} pieces")
    for L in ([0, 1] if has_top(b) else [0]):
        errs += _drc_layer(b, dia, L)
    return errs

def _drc_layer(b, dia, L):
    errs = []
    isl = islands(b, L)
    tag = "bot" if L == 0 else "top"
    names = list(isl)
    pads = _pads(b)
    for i, a in enumerate(names):
        for c in names[i + 1:]:
            d = isl[a].distance(isl[c])
            if d < CLEAR - 1e-6:
                # same-part pin pitch (0.1 in headers, 2 mm HY2.0) is the part's business: report as info
                pp = min((math.hypot(p["x"] - q["x"], p["y"] - q["y"]) - p["od"] / 2 - q["od"] / 2
                          for p in pads for q in pads if p["ref"] == q["ref"] and {p["net"] or f"NC_{p['ref']}_{p['name']}", q["net"] or f"NC_{q['ref']}_{q['name']}"} == {a, c}), default=9)
                kind = "info  " if abs(pp - d) < 0.03 else "CLEAR "
                errs.append(f"{kind} [{tag}] {a} <-> {c}: {d:.3f} mm" + (" (pin pitch of one part)" if kind == "info  " else ""))
    edge = Point(0, 0).buffer(dia / 2, 64).exterior
    for n in names:
        d = isl[n].distance(edge)
        if d < EDGE:
            errs.append(f"EDGE   [{tag}] {n}: {d:.2f} mm from the board edge")
    return errs

def _pt(x, y, r, mirror):
    if mirror: x = -x
    return x + r, r - y

def _path(poly, r, mirror):
    def ring(c):
        return " ".join(("M" if i == 0 else "L") + "%.3f,%.3f" % _pt(x, y, r, mirror) for i, (x, y) in enumerate(c)) + " Z"
    return ring(poly.exterior.coords) + "".join(" " + ring(q.coords) for q in poly.interiors)

def export(b, dia, path, copper=True, drill=True, outline=True, assembly=False, title="", layer=0):
    r = dia / 2
    mirror = (layer == 0) and not assembly
    out = ""
    def layer_(i, label, content, style):
        return f'<g id="{i}" inkscape:groupmode="layer" inkscape:label="{label}" {style}>\n{content}\n</g>\n'
    if outline:
        out += layer_("outline", "Outline", f'<circle cx="{r}" cy="{r}" r="{r}"/>', 'fill="none" stroke="#000" stroke-width="0.1"')
    if copper:
        for L in ([0, 1] if assembly and has_top(b) else [layer]):
            tag = "bot" if L == 0 else "top"
            paths = "".join(f'<path id="{tag}_{n}" d="{_path(g, r, mirror)}"/>\n' for n, g in islands(b, L).items()
                            for g in (g.geoms if g.geom_type != "Polygon" else [g]))
            colr = "#c8892a" if L == 0 else "#c0392b"
            out += layer_(f"copper_{tag}", f"Copper ({'bottom' if L == 0 else 'top'})", paths, f'fill="{colr}" fill-opacity="{0.55 if assembly else 1}" fill-rule="evenodd" stroke="none"')
    if drill:
        pads = _pads(b)
        for hs in sorted({p["hole"] for p in pads if p["hole"]}):
            holes = "".join('<circle cx="%.3f" cy="%.3f" r="%.3f"/>' % (*_pt(p["x"], p["y"], r, mirror), hs / 2) for p in pads if p["hole"] == hs)
            out += layer_(f"drill_{hs}", f"Drill {hs}mm", holes, 'fill="none" stroke="#000" stroke-width="0.1"')
    if assembly:
        g = ""
        for f in b.footprints:
            fx, fy = f["at"]; rot = math.radians(f["rot"]); cs, sn = math.cos(rot), math.sin(rot)
            for (x1, y1, x2, y2) in f["silk"]:
                ax, ay = _pt(fx + x1 * cs - y1 * sn, fy + x1 * sn + y1 * cs, r, False)
                bx, by = _pt(fx + x2 * cs - y2 * sn, fy + x2 * sn + y2 * cs, r, False)
                g += f'<line x1="{ax:.3f}" y1="{ay:.3f}" x2="{bx:.3f}" y2="{by:.3f}"/>\n'
            xx, yy = _pt(fx, fy, r, False)
            g += f'<text x="{xx:.3f}" y="{yy + 0.5:.3f}" font-size="1.3" text-anchor="middle" font-family="sans-serif" fill="#003" stroke="none">{f["ref"]}</text>\n'
        for p in _pads(b):
            if p["net"] and p["ref"] in ("J1", "J2", "J3", "J5", "U2", "U1"):
                xx, yy = _pt(p["x"], p["y"], r, False)
                g += f'<text x="{xx:.3f}" y="{yy + 1.9:.3f}" font-size="0.6" text-anchor="middle" font-family="sans-serif" fill="#000" stroke="none">{p["net"]}</text>\n'
        out += layer_("assembly", "Assembly (parts side)", g, 'fill="none" stroke="#0044cc" stroke-width="0.15"')
    pathlib.Path(path).write_text(
        f'<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
        f'width="{dia}mm" height="{dia}mm" viewBox="0 0 {dia} {dia}">\n<title>{title}</title>\n{out}</svg>\n', encoding="utf-8")

def export_all(b, dia, outdir, name="nb1_carrier"):
    outdir = pathlib.Path(outdir); outdir.mkdir(exist_ok=True)
    n = outdir / name
    export(b, dia, f"{n}_copper_bottom_view.svg", title=f"{name} - bottom copper, as milled (mirrored)")
    export(b, dia, f"{n}_copper_only.svg", drill=False, outline=False, title=f"{name} - bottom copper islands only (mirrored)")
    export(b, dia, f"{n}_drill_only.svg", copper=False, outline=False, title=f"{name} - drills, bottom view (mirrored)")
    if has_top(b):
        export(b, dia, f"{n}_copper_top_view.svg", layer=1, title=f"{name} - TOP copper (parts side), as milled, not mirrored")
    export(b, dia, f"{n}_assembly_top_view.svg", assembly=True, title=f"{name} - placement, parts side")
    return outdir
