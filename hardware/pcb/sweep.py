"""Try placement variants for the sensor cluster and report unrouted counts.
Each variant edits gen_pcb.py / gen_tracks.py through environment-driven overrides, so the
files themselves are not changed: gen_pcb.py reads NB1_SJ1, NB1_SJ2 (x,y) and NB1_J3 (pin order).
Usage: python sweep.py  -> prints a table, best first."""
import os, subprocess, sys, itertools, pathlib, re
PY = sys.executable
HERE = pathlib.Path(__file__).resolve().parent
variants = []
sj1 = [(-13.0, 0.5), (-17.0, -9.5), (-9.0, -5.5), (-17.0, -12.0), (-8.5, 3.5)]
sj2 = [(-8.5, -11.5), (-17.0, -12.0), (-8.5, 3.5), (-17.0, -9.5), (-13.0, 0.5)]
j3 = ["5V,GND,NODE1,NODE2"]
for a, b_, j in itertools.product(sj1, sj2, j3):
    if a == b_: continue
    variants.append((a, b_, j))
results = []
for (a, b_, j) in variants:
    env = dict(os.environ, NB1_SJ1=f"{a[0]},{a[1]}", NB1_SJ2=f"{b_[0]},{b_[1]}", NB1_J3=j, NB1_TRIES="12", NB1_REROUTE="1")
    out = subprocess.run([PY, str(HERE / "gen_pcb.py")], capture_output=True, text=True, env=env, cwd=str(HERE)).stdout
    m = re.search(r"seed (\d+) unrouted (\d+)", out)
    un = int(m.group(2)) if m else 99
    clear = len(re.findall(r"ISLAND-DRC CLEAR", out))
    results.append((un, clear, a, b_, j))
    print(un, clear, a, b_, j, flush=True)
results.sort()
print("BEST", results[:5])
