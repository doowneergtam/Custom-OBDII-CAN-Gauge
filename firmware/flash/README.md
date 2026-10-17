# Prebuilt image

Built 2026-10-16 from this commit: analog inputs on GPIO17/18, no encoder
(carrier rev C). Flash it from any PC with Python:

```
pip install esptool
.\flash.ps1 COM9        # Windows, port from Device Manager (Espressif USB, VID 303A PID 1001)
./flash.sh /dev/ttyACM0 # Linux / macOS
```

The Waveshare board enumerates as a USB serial port on its USB-C socket; no
drivers needed on Windows 10/11. If the port does not appear, hold BOOT while
plugging in. The config file on the gauge's LittleFS survives flashing.
Rebuild from source with ESP-IDF 5.5: `idf.py -C firmware build` (see the
project CLAUDE.md for the toolchain).
