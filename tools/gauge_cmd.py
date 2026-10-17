"""Send one or more commands to the gauge's serial console and print replies.

    python tools/gauge_cmd.py COM9 "stats" "level" "tap 233 233"

Does not reset the board. Each command waits briefly for its reply.
"""
import sys, time
import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM9"
cmds = sys.argv[2:] or ["stats"]
s = serial.Serial(); s.port = port; s.baudrate = 115200; s.timeout = 0.05
s.dtr = False; s.rts = False; s.open()
with s:
    time.sleep(0.2); s.read(65536)
    for c in cmds:
        s.write((c + "\n").encode()); s.flush()
        t0 = time.time(); buf = b""
        while time.time() - t0 < 1.5:
            chunk = s.read(4096)
            if chunk: buf += chunk
            if b"\nok" in buf or b"\nerr" in buf: time.sleep(0.1); buf += s.read(4096); break
        print(f"> {c}")
        for line in buf.decode("utf-8", "replace").splitlines():
            if line.strip(): print("  " + line.rstrip())
