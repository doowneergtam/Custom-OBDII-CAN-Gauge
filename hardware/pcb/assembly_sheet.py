"""Assembly sheet v2: parts drawn as shaded, colour-coded bodies at their real positions with the
reference printed on them (no leader lines), connector pin numbers, vias ringed and named in their
own colours with a key, a 2x inset of the small-parts cluster, and a BOM whose rows carry the same
colours. Output: mill/nb1_carrier_assembly_sheet.svg"""
import io, contextlib, math, pathlib
with contextlib.redirect_stdout(io.StringIO()):
    import gen_pcb as g
b, DIA = g.b, g.DIA

# colour per BOM group, body size (w, h mm) per ref (before rotation), label placement
GROUP = {"J1": "#2563eb", "J2": "#0891b2", "J3": "#0e7490", "J5": "#64748b", "U2": "#7c3aed", "U1": "#c9184a",
         "F1": "#d97706", "D1": "#b45309", "D2": "#92400e", "C1": "#15803d", "C2": "#166534", "R2": "#be185d",
         "R3": "#9d174d", "U3": "#ca8a04", "R11": "#ea580c", "R21": "#ea580c", "RP1": "#ea580c", "RP2": "#ea580c",
         "R12": "#f97316", "R22": "#f97316", "SJ1": "#111827", "SJ2": "#111827"}
BODY = {"J1": (20.4, 2.6), "J2": (12.6, 5.8), "J3": (5.8, 10.6), "J5": (7.6, 2.2), "U2": (15.45, 13.7), "U1": (11.7, 3.2),
        "F1": (7.3, 4.0), "D1": (5.5, 2.8), "D2": (5.3, 2.7), "C1": (6.3, 6.3), "C2": (6.3, 6.3), "R2": (2.4, 6.3),
        "R3": (6.3, 2.4), "U3": (4.6, 9.0), "R11": (1.6, 0.8), "R21": (1.6, 0.8), "RP1": (1.6, 0.8), "RP2": (1.6, 0.8),
        "R12": (1.6, 0.8), "R22": (1.6, 0.8), "SJ1": (2.6, 1.2), "SJ2": (2.6, 1.2)}
BOM = [
 ("J1", "Header 1x8, 2.54 mm, long pins", "Adafruit 400 (cut to 8) / Harwin M20-9990846", "body on top, pins through into the Waveshare socket"),
 ("J2", "HY2.0-5P vertical", "JST B5B-PH-K-S or HY2.0-5P", "car: 1 GND, 2 LIGHTS, 3 CAN L, 4 CAN H, 5 12 V"),
 ("J3", "HY2.0-4P vertical", "JST B4B-PH-K-S or HY2.0-4P", "sensors: 1 5V, 2 GND, 3 SENS1, 4 SENS2"),
 ("J5", "3 solder pads", "wire", "to Waveshare EX0 / SDA / SCL test pads"),
 ("U2", "CAN module 3.3 V in 1x6 socket", "WCMCU-230 + Sullins PPTC061LFBN-RC", "socket pin 1 = CANL at the left"),
 ("U1", "DC-DC 5 V 1 A, SIP-3", "Traco TSR 1-2450", "pin 1 (+Vin) left, 2 GND, 3 +Vout"),
 ("F1", "Polyfuse 1.1 A 30 V radial 5.08", "Bourns MF-R110", "no polarity"),
 ("D1", "Schottky 3 A, DO-201", "onsemi 1N5822G", "band left; solder pin 1 on BOTH sides"),
 ("D2", "TVS 33 V 600 W, DO-15", "Littelfuse P6KE33A / SMBJ26A on leads", "band left; solder pin 1 on BOTH sides"),
 ("C1", "47 uF 50 V, 2.5 mm pitch", "Nichicon UVR1H470MPD", "+ is the right pad"),
 ("C2", "100 uF 16 V, 2.5 mm pitch", "Panasonic ECA-1CM101", "+ is the right pad"),
 ("R2", "2.2 kOhm 1/4 W axial, vertical", "Yageo CFR-25JB-52-2K2", "solder the TOP end on both sides"),
 ("R3", "1 kOhm 1/4 W axial", "Yageo CFR-25JB-52-1K", "CAN RX series, body over CAN TX"),
 ("U3", "Optocoupler DIP-4", "Sharp PC817X2NSZ0F / Everlight EL817", "dot at bottom-left"),
 ("R11 R21 RP1 RP2", "1 kOhm 0603", "Yageo RC0603FR-071KL", "TOP side, see inset"),
 ("R12 R22", "1.5 kOhm 0603", "Yageo RC0603FR-071K5L", "TOP side, see inset"),
 ("SJ1 SJ2", "Solder-bridge pads", "none", "TOP side; bridged = resistive sender, open = 0-5 V"),
 ("V*", "Via, 1.0 mm hole, 9 off", "copper rivet or a trimmed resistor lead", "solder both sides first; colours in the via key"),
 ("H1-H3", "M2 standoff x3", "M2, ~8.5 mm", "Waveshare back to carrier copper face"),
]
# Vias: each wire via rings its pad in its own colour and prints its name above, in the main view and
# the inset, with a key under the board. The three component leads that also carry a top track are
# vias too (solder both sides); they get a double dark ring so they cannot be mistaken for wire vias.
VIA_COLOR = {"VL": "#e11d48", "VP5": "#f97316", "VA1": "#ca8a04", "V4": "#15803d", "V5": "#0d9488",
             "V6": "#0284c7", "V7": "#7c3aed", "V8": "#c026d3", "V9": "#78350f"}
VIA_NET = {"VL": "LIGHTS", "VP5": "5V", "VA1": "AIN1", "V4": "NODE1", "V5": "NODE2", "V6": "AIN2", "V7": "5V", "V8": "GND", "V9": "GND"}
LEAD_VIAS = {("D1", "1"), ("D2", "1"), ("R2", "2")}
LEAD_COLOR = "#111827"
def esc(s): return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
def pads_of(f):
    fx, fy = f["at"]; r = math.radians(f["rot"]); c, s = math.cos(r), math.sin(r)
    for p in f["pads"]:
        yield p, fx + p["x"] * c - p["y"] * s, fy + p["x"] * s + p["y"] * c

def draw_board(o, S, cx, cy, clip=None, show_tracks=True, label_scale=1.0):
    R = DIA / 2
    def X(x): return cx + x * S
    def Y(y): return cy - y * S
    if show_tracks:
        for tr in b.tracks:
            pts, w, net = tr[0], tr[1], tr[2]; top = len(tr) > 3 and tr[3] == 1
            d = "M " + " L ".join(f"{X(x):.1f} {Y(y):.1f}" for x, y in pts)
            if top: o.append(f'<path d="{d}" fill="none" stroke="#c0392b" stroke-width="{w * S * 0.7:.1f}" stroke-dasharray="6 4" opacity="0.8" stroke-linecap="round" stroke-linejoin="round"/>')
            else: o.append(f'<path d="{d}" fill="none" stroke="#bdb08a" stroke-width="{w * S:.1f}" opacity="0.45" stroke-linecap="round" stroke-linejoin="round"/>')
    for f in b.footprints:
        for p, px, py in pads_of(f):
            sw, sh = p["size"]; via = f["ref"].startswith("V")
            fill = VIA_COLOR.get(f["ref"], "#c0392b") if via else ("#e67e22" if p.get("layer") == "F.Cu" else "#d4a017")
            if p.get("shape") == "rect":
                o.append(f'<rect x="{X(px) - sw * S / 2:.1f}" y="{Y(py) - sh * S / 2:.1f}" width="{sw * S:.1f}" height="{sh * S:.1f}" fill="{fill}" stroke="#444" stroke-width="0.8" transform="rotate({-f["rot"]} {X(px):.1f} {Y(py):.1f})"/>')
            else:
                o.append(f'<circle cx="{X(px):.1f}" cy="{Y(py):.1f}" r="{sw * S / 2:.1f}" fill="{fill}" stroke="#444" stroke-width="0.8"/>')
            if p.get("drill"): o.append(f'<circle cx="{X(px):.1f}" cy="{Y(py):.1f}" r="{p["drill"] * S / 2:.1f}" fill="#fbfaf7"/>')
    # bodies on top of pads, translucent, with the ref inside
    for f in b.footprints:
        ref = f["ref"]
        if ref not in BODY: continue
        w, h = BODY[ref]; fx, fy = f["at"]; col = GROUP.get(ref, "#888")
        if ref == "U2": fy = fy + 13.7 / 2 - 1.5     # module body extends upward from the socket row
        if ref == "U3": fy = fy + 0.0
        o.append(f'<rect x="{X(fx) - w * S / 2:.1f}" y="{Y(fy) - h * S / 2:.1f}" width="{w * S:.1f}" height="{h * S:.1f}" rx="{min(w, h) * S * 0.15:.1f}" fill="{col}" fill-opacity="0.28" stroke="{col}" stroke-width="2" transform="rotate({-f["rot"]} {X(fx):.1f} {Y(fy):.1f})"/>')
        fs = 13 * label_scale if min(w, h) * S > 14 * label_scale else 9 * label_scale
        o.append(f'<text x="{X(fx):.1f}" y="{Y(fy) + fs * 0.35:.1f}" font-size="{fs:.0f}" font-weight="700" fill="{col}" text-anchor="middle" stroke="#fbfaf7" stroke-width="3" paint-order="stroke">{ref}</text>')
    for f in b.footprints:
        if f["ref"] in ("J1", "J2", "J3", "U2", "U1", "J5"):
            for p, px, py in pads_of(f):
                if p.get("net") or f["ref"] == "J5":
                    o.append(f'<text x="{X(px):.1f}" y="{Y(py) + 3 * label_scale:.1f}" font-size="{8 * label_scale:.0f}" fill="#000" text-anchor="middle" font-weight="700">{p["name"]}</text>')
    # via marks on top of everything: a ring in the via's colour with its name above; the component
    # leads that double as vias get a double dark ring and a "both sides" label
    halo = 'stroke="#fbfaf7" stroke-width="3" paint-order="stroke"'
    for f in b.footprints:
        if f["ref"].startswith("V"):
            fx, fy = f["at"]; col = VIA_COLOR.get(f["ref"], "#c0392b")
            o.append(f'<circle cx="{X(fx):.1f}" cy="{Y(fy):.1f}" r="{1.4 * S:.1f}" fill="none" stroke="{col}" stroke-width="{0.25 * S:.1f}"/>')
            o.append(f'<text x="{X(fx):.1f}" y="{Y(fy) - 1.9 * S:.1f}" font-size="{11 * label_scale:.0f}" fill="{col}" text-anchor="middle" font-weight="700" {halo}>{f["ref"]}</text>')
        for p, px, py in pads_of(f):
            if (f["ref"], p["name"]) in LEAD_VIAS:
                o.append(f'<circle cx="{X(px):.1f}" cy="{Y(py):.1f}" r="{1.5 * S:.1f}" fill="none" stroke="{LEAD_COLOR}" stroke-width="{0.25 * S:.1f}"/>')
                o.append(f'<circle cx="{X(px):.1f}" cy="{Y(py):.1f}" r="{1.0 * S:.1f}" fill="none" stroke="{LEAD_COLOR}" stroke-width="{0.12 * S:.1f}"/>')
                o.append(f'<text x="{X(px):.1f}" y="{Y(py) - 1.9 * S:.1f}" font-size="{10 * label_scale:.0f}" fill="{LEAD_COLOR}" text-anchor="middle" font-weight="700" {halo}>{f["ref"]}-{p["name"]} both sides</text>')

S = 20; R = DIA / 2
BX, BY = 60 + R * S, 100 + R * S
W, H = 1750, 1200
o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Segoe UI, Arial, sans-serif">',
     f'<rect width="{W}" height="{H}" fill="#fbfaf7"/>',
     f'<text x="{BX:.0f}" y="40" text-anchor="middle" font-size="22" font-weight="700" fill="#222">NB1 pod carrier rev C, assembly (parts side, rear of the pod)</text>',
     f'<text x="{BX:.0f}" y="62" text-anchor="middle" font-size="12" fill="#555">Shaded bodies are the parts at true size, colours match the BOM. Gold pads: bottom solder. Orange pads: top-side chip parts. Coloured rings: vias, see the key below. Double black rings: component leads soldered on both sides. Red dashes: top copper.</text>',
     f'<circle cx="{BX:.1f}" cy="{BY:.1f}" r="{R * S:.1f}" fill="#efe9d8" stroke="#333" stroke-width="2"/>']
draw_board(o, S, BX, BY)
o.append(f'<text x="{BX:.0f}" y="{BY + R * S + 28:.0f}" font-size="12" fill="#333" text-anchor="middle">J1 pins 1-8: AIN2, AIN1, unused, CAN_TX, CAN_RX, 3V3, GND, 5V (pin 1 = IO18 end, left).  Solder D1 pin 1, D2 pin 1 and the top end of R2 on BOTH sides.</text>')
# inset: the sensor cluster at 2x
IS = 40; ix0, iy0 = 1080, 760; iw, ih = 640, 420
cxm, cym = -11.5, -5.0     # mm centre of the inset window
o.append(f'<rect x="{ix0}" y="{iy0}" width="{iw}" height="{ih}" fill="#efe9d8" stroke="#333" stroke-width="2"/>')
o.append(f'<clipPath id="ins"><rect x="{ix0}" y="{iy0}" width="{iw}" height="{ih}"/></clipPath>')
o.append('<g clip-path="url(#ins)">')
draw_board(o, IS, ix0 + iw / 2 - cxm * IS, iy0 + ih / 2 + cym * IS, label_scale=1.3)
o.append('</g>')
o.append(f'<text x="{ix0 + 10}" y="{iy0 - 8}" font-size="14" font-weight="700" fill="#222">Inset, 2x: sensor cluster, top side (x {cxm - iw / IS / 2:.0f} to {cxm + iw / IS / 2:.0f} mm, y {cym - ih / IS / 2:.0f} to {cym + ih / IS / 2:.0f} mm)</text>')
# the inset's footprint on the main view
o.append(f'<rect x="{BX + (cxm - iw / IS / 2) * S:.1f}" y="{BY - (cym + ih / IS / 2) * S:.1f}" width="{iw / IS * S:.1f}" height="{ih / IS * S:.1f}" fill="none" stroke="#111" stroke-dasharray="5 4" stroke-width="1.5"/>')
# BOM
tx = 1080; ty = 90
o.append(f'<text x="{tx}" y="{ty - 18}" font-size="16" font-weight="700" fill="#222">Bill of materials</text>')
for i, (ref, desc, pn, note) in enumerate(BOM):
    yy = ty + i * 34; col = GROUP.get(ref.split()[0], "#c0392b" if ref.startswith("V") else "#6b7280")
    o.append(f'<rect x="{tx - 8}" y="{yy - 13}" width="{W - tx - 20}" height="32" fill="{col}" fill-opacity="0.12" stroke="{col}" stroke-width="1"/>')
    o.append(f'<rect x="{tx - 8}" y="{yy - 13}" width="8" height="32" fill="{col}"/>')
    o.append(f'<text x="{tx + 8}" y="{yy}" font-size="12" font-weight="700" fill="{col}">{esc(ref)}</text>')
    o.append(f'<text x="{tx + 130}" y="{yy}" font-size="11" fill="#222">{esc(desc)}</text>')
    o.append(f'<text x="{tx + 130}" y="{yy + 14}" font-size="10" fill="#1d4ed8">{esc(pn)}</text>')
    o.append(f'<text x="{tx + 400}" y="{yy + 7}" font-size="10" fill="#555">{esc(note)}</text>')
# via key under the board: the nine wire vias in their colours, then the three component leads that
# double as vias, with net and parts-side position (mm from the centre)
KX, KY = 60, 1076
o.append(f'<text x="{KX}" y="{KY}" font-size="14" font-weight="700" fill="#222">Via key (parts-side view, mm from centre): nine wire vias, each its own colour, and the three component leads that double as vias</text>')
vias = {f["ref"]: f["at"] for f in b.footprints if f["ref"].startswith("V")}
rows = [(ref, VIA_COLOR[ref], f"{VIA_NET[ref]}  ({vias[ref][0]:g}, {vias[ref][1]:g})", False) for ref in VIA_COLOR if ref in vias]
rows += [(f"{f['ref']} pin {p['name']}", LEAD_COLOR, f"{p.get('net', '')}  ({px:g}, {py:g})  solder both sides", True)
         for f in b.footprints for p, px, py in pads_of(f) if (f["ref"], p["name"]) in LEAD_VIAS]
for i, (name, col, detail, lead) in enumerate(rows):
    x = KX + (i // 4) * 340; y = KY + 24 + (i % 4) * 24
    if lead:
        o.append(f'<circle cx="{x + 8}" cy="{y - 4}" r="7" fill="none" stroke="{col}" stroke-width="2.5"/>')
        o.append(f'<circle cx="{x + 8}" cy="{y - 4}" r="3.5" fill="none" stroke="{col}" stroke-width="1.5"/>')
    else:
        o.append(f'<circle cx="{x + 8}" cy="{y - 4}" r="7" fill="{col}" stroke="#444" stroke-width="0.8"/>')
    o.append(f'<text x="{x + 22}" y="{y}" font-size="12"><tspan font-weight="700" fill="{col}">{esc(name)}</tspan><tspan fill="#333">  {esc(detail)}</tspan></text>')
o.append("</svg>")
out = pathlib.Path(__file__).resolve().parent / "mill" / "nb1_carrier_assembly_sheet.svg"
out.write_text("\n".join(o), encoding="utf-8")
print("wrote", out)
