"""Print whatever the gauge says on its USB serial port for a few seconds.

    python tools/serial_peek.py COM9 12

Opens the port, pulses reset through DTR/RTS so the boot log is included,
then dumps output for N seconds. Uses pyserial from the ESP-IDF environment.
"""
import sys, time
import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM9"
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 10
noreset = "--noreset" in sys.argv

s = serial.Serial()
s.port = port; s.baudrate = 115200; s.timeout = 0.2
s.dtr = False; s.rts = False      # do not touch the reset lines on open
s.open()
with s:
    if not noreset:
        # esptool-style reset: EN low via RTS, release
        s.rts = True
        time.sleep(0.1)
        s.rts = False
    t0 = time.time()
    buf = b""
    while time.time() - t0 < secs:
        buf += s.read(4096)
    text = buf.decode("utf-8", "replace")
    lines = [l for l in text.splitlines() if l.strip()]
    print(f"--- {len(lines)} lines in {secs:.0f}s ---")
    for l in lines[-120:]:
        print(l)
