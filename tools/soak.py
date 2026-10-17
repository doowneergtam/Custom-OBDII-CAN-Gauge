"""Overnight soak driver for the gauge's serial console.

    python tools/soak.py COM9 <minutes> [seed]

Talks to the firmware's command console (components/console) over the USB
serial port, runs randomised interaction cycles, and writes everything it
sends and receives to firmware/soak.log. Flags lines that look like trouble
(LVGL errors, ESP errors, draw failures, panics, unexpected reboots) and
prints a summary at the end. Never resets the board on open.
"""
import random, re, sys, time, pathlib
import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM9"
MINUTES = float(sys.argv[2]) if len(sys.argv) > 2 else 10
SEED = int(sys.argv[3]) if len(sys.argv) > 3 else int(time.time())
LOG = pathlib.Path(__file__).resolve().parent.parent / "firmware" / "soak.log"

BAD = re.compile(r"\[Error\]|Guru Meditation|abort\(\)|Backtrace|Draw bitmap failed|assert failed|E \(\d+\)|ESP-ROM:esp32s3|rst:0x|Stack smashing|CORRUPT HEAP")
IGNORE = re.compile(r"Deprecated: use the dedicated|No free interrupt inputs for TWAI")  # known, harmless

rng = random.Random(SEED)
log = LOG.open("a", encoding="utf-8")
def L(s):
    line = f"{time.strftime('%H:%M:%S')} {s}"
    log.write(line + "\n"); log.flush()

issues, reboots, cmds = [], 0, 0
s = serial.Serial(); s.port = PORT; s.baudrate = 115200; s.timeout = 0.05
s.dtr = False; s.rts = False; s.open()

def drain(wait=0.25):
    """Read whatever the gauge printed, log it, note anything bad."""
    global reboots
    t0 = time.time(); buf = b""
    while time.time() - t0 < wait:
        chunk = s.read(4096)
        if chunk: buf += chunk; t0 = time.time() if wait < 1 else t0
    text = buf.decode("utf-8", "replace")
    for line in text.splitlines():
        line = line.rstrip()
        if not line: continue
        L("  < " + line)
        if "ESP-ROM:esp32s3" in line or "rst:0x" in line: reboots += 1
        if BAD.search(line) and not IGNORE.search(line): issues.append(line)
    return text

def cmd(c, wait=0.4):
    global cmds
    cmds += 1
    L("> " + c)
    s.write((c + "\n").encode()); s.flush()
    return drain(wait)

CH = ["RPM", "BOOST", "MAP", "CLT", "OILP", "OILT", "AFR", "BATT", "KNOCK", "VSS", "EGT", "MAT", "DUTY", "TPS", "ADV"]
STYLES = ["ring", "dial", "numeric"]; FILLS = ["gradient", "segments", "solid"]
LAYOUTS = [1, 3, 4, 6, 7]

def step_random():
    k = rng.randint(0, 11)
    if k == 0:
        cmd(f'cfg {{"layout": {rng.choice(LAYOUTS)}}}', 0.8)
    elif k == 1:
        cells = [rng.choice(CH) for _ in range(7)]
        cmd('cfg {"cells": ' + str(cells).replace("'", '"') + '}', 0.8)
    elif k == 2:
        t = {"style": rng.choice(STYLES), "fill": rng.choice(FILLS), "bands": rng.random() < 0.7,
             "digit_scale": round(rng.uniform(0.5, 5.0), 2), "label_scale": round(rng.uniform(0.5, 4.0), 2),
             "units_scale": round(rng.uniform(0.5, 4.0), 2), "minmax_scale": round(rng.uniform(0.5, 4.0), 2),
             "bar_scale": round(rng.uniform(0.5, 3.0), 2), "cell_scale": round(rng.uniform(1.0, 2.5), 2)}
        cmd('cfg {"theme": ' + str(t).replace("'", '"').replace("True", "true").replace("False", "false") + '}', 1.0)
    elif k == 3:
        cmd("hold", 0.5); cmd(f"rot {rng.randint(0, 8)}", 0.3); cmd("click", 0.5); cmd("level", 0.3)
        for _ in range(rng.randint(0, 3)): cmd(f"rot {rng.choice([-1, 1])}", 0.3)
        cmd("hold", 0.5); cmd("hold", 0.5); cmd("hold", 0.5)
    elif k == 4:
        cmd(f"tap {rng.randint(20, 446)} {rng.randint(20, 446)}", 0.4)
        cmd(f"tap {rng.randint(20, 446)} {rng.randint(20, 446)}", 0.4)
    elif k == 5:
        cmd(f"swipe {rng.choice(['left', 'right', 'up', 'down'])}", 0.4)
    elif k == 6:
        cmd(f"tone {rng.randint(1, 5)}", 0.8)
    elif k == 7:
        cmd(f"screen save Soak{rng.randint(1, 4)}", 0.8); cmd("screen load 0", 1.0)
    elif k == 8:
        cmd("wifi off", 1.5); cmd("stats", 0.5); cmd("wifi on", 3.0)
    elif k == 9:
        cmd('cfg {"brightness": %d}' % rng.choice([20, 40, 60, 80, 100]), 0.5)
    elif k == 10:
        cmd(f"sim {rng.choice(['off', 'on'])}", 0.5)
    else:
        cmd("vals", 0.6)
    cmd("stats", 0.5)

L(f"=== soak start port={PORT} minutes={MINUTES} seed={SEED}")
drain(1.0)
cmd("stats", 0.6)
t_end = time.time() + MINUTES * 60
cycle = 0
while time.time() < t_end:
    cycle += 1
    L(f"--- cycle {cycle}")
    step_random()
    drain(0.3)
cmd("sim on", 0.5); cmd('cfg {"layout": 1, "theme": {"style": "ring", "digit_scale": 1, "label_scale": 1, "units_scale": 1, "minmax_scale": 1, "bar_scale": 1, "cell_scale": 1}}', 1.0)
final = cmd("stats", 0.8)
m = re.search(r"stats (.*)", final)
L(f"=== soak end cycles={cycle} cmds={cmds} reboots={reboots} issues={len(issues)}")
print(f"cycles={cycle} cmds={cmds} reboots={reboots} issues={len(issues)}")
print("final:", m.group(1) if m else "n/a")
for i in issues[:20]: print("ISSUE:", i[:160])
