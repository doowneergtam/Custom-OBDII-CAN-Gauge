"""Readable preview of the generated board: parts-side view on the left with
reference designators and connector pin names next to the pads, a parts table
on the right, and a net colour legend. Nothing overlaps: values and
descriptions live in the table, not on the board."""
import math

COLORS = {"GND": "#3a3a3a", "12V_IN": "#d62828", "12V_F": "#e85d04", "12V_P": "#f48c06", "5V": "#c9184a",
          "3V3": "#e07a5f", "CANH": "#1d4ed8", "CANL": "#0891b2", "CAN_TX": "#7c3aed", "CAN_RX": "#9333ea", "CAN_RXM": "#a855f7",
          "ENC_A": "#15803d", "ENC_B": "#16a34a", "ENC_SW": "#65a30d", "LIGHTS": "#eab308", "LED_K": "#ca8a04",
          "EX0": "#a16207", "SDA": "#6b7280", "SCL": "#6b7280"}
# connectors whose pin names are printed next to the pads: ref -> (dx, dy, rotate)
PIN_LABELS = {"J1": (0, -2.3, -90), "J2": (0, -2.4, -90), "U2": (0, 2.0, 90), "J3": (2.2, 0, 0), "J5": (2.0, 0, 0), "U1": (0, 2.0, 90)}

def esc(s): return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")

def wrap(s, n=58):
    words, lines, cur = s.split(), [], ""
    for w in words:
        if len(cur) + len(w) + 1 > n and cur: lines.append(cur); cur = w
        else: cur = (cur + " " + w).strip()
    if cur: lines.append(cur)
    return lines[:3]

def write_svg(b, dia, path, scale=14):
    r = dia / 2
    cx, cy = 400, 400
    W, H = 1500, 820
    def X(x): return cx + x * scale
    def Y(y): return cy - y * scale
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Segoe UI, Arial, sans-serif">',
         f'<rect width="{W}" height="{H}" fill="#f6f3ec"/>',
         f'<text x="{cx}" y="36" text-anchor="middle" font-size="20" font-weight="700" fill="#222">{esc(b.title)}: parts side (rear of the pod), copper on the far side</text>',
         f'<circle cx="{cx}" cy="{cy}" r="{r * scale}" fill="#1f6f3f" stroke="#111" stroke-width="2"/>']
    # tracks (copper is on the far side; drawn translucent so pads stay readable)
    for tr in b.tracks:
        pts, width, net = tr[0], tr[1], tr[2]
        if len(tr) > 3 and tr[3] == 1: continue
        d = "M " + " L ".join(f"{X(x):.1f} {Y(y):.1f}" for x, y in pts)
        o.append(f'<path d="{d}" fill="none" stroke="{COLORS.get(net, "#888")}" stroke-width="{width * scale:.1f}" stroke-linecap="round" stroke-linejoin="round" opacity="0.8"/>')
    for (x, y, d) in b.holes:
        o.append(f'<circle cx="{X(x):.1f}" cy="{Y(y):.1f}" r="{d * scale / 2:.1f}" fill="#f6f3ec" stroke="#222"/>')
    for f in b.footprints:
        fx, fy = f["at"]; rot = f["rot"]
        c, s = math.cos(math.radians(rot)), math.sin(math.radians(rot))
        for (x1, y1, x2, y2) in f["silk"]:
            ax, ay = fx + x1 * c - y1 * s, fy + x1 * s + y1 * c
            bx, by = fx + x2 * c - y2 * s, fy + x2 * s + y2 * c
            o.append(f'<line x1="{X(ax):.1f}" y1="{Y(ay):.1f}" x2="{X(bx):.1f}" y2="{Y(by):.1f}" stroke="#eee" stroke-width="1.5" stroke-dasharray="4 3"/>')
        for p in f["pads"]:
            px, py = fx + p["x"] * c - p["y"] * s, fy + p["x"] * s + p["y"] * c
            sw, sh = p["size"]
            net = p.get("net", "")
            col = COLORS.get(net, "#d4a017") if net else "#d4a017"
            if p.get("shape") == "rect":
                o.append(f'<rect x="{X(px) - sw * scale / 2:.1f}" y="{Y(py) - sh * scale / 2:.1f}" width="{sw * scale:.1f}" height="{sh * scale:.1f}" fill="{col}" stroke="#fff" stroke-width="1.2"/>')
            else:
                o.append(f'<circle cx="{X(px):.1f}" cy="{Y(py):.1f}" r="{sw * scale / 2:.1f}" fill="{col}" stroke="#fff" stroke-width="1.2"/>')
            if p.get("drill"):
                o.append(f'<circle cx="{X(px):.1f}" cy="{Y(py):.1f}" r="{p["drill"] * scale / 2:.1f}" fill="#f6f3ec"/>')
            if f["ref"] in PIN_LABELS and net:
                dx, dy, trot = PIN_LABELS[f["ref"]]
                lx, ly = X(px + dx), Y(py + dy)
                anchor = "start" if trot == 0 else ("end" if trot == -90 else "start")
                o.append(f'<text x="{lx:.1f}" y="{ly:.1f}" font-size="10" fill="#fff" text-anchor="{anchor}" dominant-baseline="middle" transform="rotate({trot} {lx:.1f} {ly:.1f})">{esc(net)}</text>')
        # reference designator in a pill at the footprint centre (offset for connectors so it misses the pins)
        rx, ry = X(fx), Y(fy)
        if f["ref"] in ("J1",): ry += 0
        if f["ref"] == "U2": ry -= 5.5 * scale
        if f["ref"] == "J2": ry -= 1.6 * scale
        o.append(f'<rect x="{rx - 14:.1f}" y="{ry - 9:.1f}" width="28" height="18" rx="4" fill="#111" opacity="0.75"/>')
        o.append(f'<text x="{rx:.1f}" y="{ry + 4:.1f}" font-size="12" font-weight="700" fill="#fff" text-anchor="middle">{f["ref"]}</text>')
    for tr in b.tracks:   # top-side tracks (parts side) drawn last, dashed
        if len(tr) > 3 and tr[3] == 1:
            pts, width, net = tr[0], tr[1], tr[2]
            d = "M " + " L ".join(f"{X(x):.1f} {Y(y):.1f}" for x, y in pts)
            o.append(f'<path d="{d}" fill="none" stroke="{COLORS.get(net, "#888")}" stroke-width="{width * scale:.1f}" stroke-dasharray="6 4" stroke-linecap="round" stroke-linejoin="round"/>')
    o.append(f'<text x="{cx}" y="{cy + r * scale + 28}" font-size="12" fill="#333" text-anchor="middle">Diameter {dia} mm. Header J1 at the bottom plugs into the Waveshare; both HY2.0 connectors face away from the screen.</text>')
    # legend
    lx, ly = 30, 60
    o.append(f'<text x="{lx}" y="{ly - 10}" font-size="12" font-weight="700" fill="#222">Nets</text>')
    for net, col in COLORS.items():
        o.append(f'<rect x="{lx}" y="{ly}" width="14" height="10" fill="{col}"/><text x="{lx + 20}" y="{ly + 9}" font-size="11" fill="#222">{net}</text>')
        ly += 15
    # parts table
    tx, ty = 790, 70
    o.append(f'<text x="{tx}" y="{ty - 14}" font-size="14" font-weight="700" fill="#222">Parts</text>')
    o.append(f'<text x="{tx}" y="{ty + 4}" font-size="11" fill="#555">Ref</text><text x="{tx + 40}" y="{ty + 4}" font-size="11" fill="#555">Value</text><text x="{tx + 230}" y="{ty + 4}" font-size="11" fill="#555">What it does, pin order</text>')
    ty += 12
    for f in b.footprints:
        lines = wrap(f["desc"]) or [""]
        hgt = 14 * max(2, len(lines)) + 6
        o.append(f'<rect x="{tx - 6}" y="{ty}" width="{W - tx - 14}" height="{hgt}" fill="#fff" stroke="#ddd"/>')
        o.append(f'<text x="{tx}" y="{ty + 15}" font-size="12" font-weight="700" fill="#222">{f["ref"]}</text>')
        o.append(f'<text x="{tx + 40}" y="{ty + 15}" font-size="12" fill="#222">{esc(f["value"])}</text>')
        for i, ln in enumerate(lines):
            o.append(f'<text x="{tx + 230}" y="{ty + 15 + 14 * i}" font-size="11" fill="#333">{esc(ln)}</text>')
        ty += hgt + 3
    o.append("</svg>")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(o))
