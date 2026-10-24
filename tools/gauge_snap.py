"""Grab a picture of the gauge's first face over the serial console.

    python tools/gauge_snap.py COM11 out.png [step]

Sends `snap <step>` (default 2: every other pixel) and turns the hex rows the
firmware prints into a PNG. The image is rendered on the gauge through the same
drawing code as the screen: cached artwork plus the moving parts.
"""
import sys, time
import serial
from PIL import Image

port, out = sys.argv[1], sys.argv[2]
step = int(sys.argv[3]) if len(sys.argv) > 3 else 2
s = serial.Serial(); s.port = port; s.baudrate = 115200; s.timeout = 0.2
s.dtr = False; s.rts = False; s.open()
with s:
    time.sleep(0.2); s.read(65536)
    s.write(f"snap {step}\n".encode()); s.flush()
    buf = b""; t0 = time.time()
    while time.time() - t0 < 60 and b"endsnap" not in buf:
        buf += s.read(65536)
text = buf.decode("ascii", "replace")
lines = [l.strip() for l in text.splitlines()]
i = next(k for k, l in enumerate(lines) if l.startswith("snap ") and len(l.split()) == 4)   # skip the echoed command
_, w, h, st = lines[i].split()[:4]
rows = []
for l in lines[i + 1:]:
    if l.startswith("endsnap"): break
    if l and all(c in "0123456789abcdef" for c in l): rows.append(l)
cols = len(rows[0]) // 4
img = Image.new("RGB", (cols, len(rows)))
for y, l in enumerate(rows):
    for x in range(min(cols, len(l) // 4)):
        v = int(l[4 * x:4 * x + 4], 16)
        img.putpixel((x, y), (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
img.save(out)
print("wrote", out, img.size)
