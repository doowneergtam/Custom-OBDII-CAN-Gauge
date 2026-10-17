# NB1 Miata Gauge

Firmware and hardware notes for a MegaSquirt-fed digital gauge for the
1999-2000 Mazda Miata. Two products, one firmware core:

- `firmware/` — pod gauge on the Waveshare ESP32-S3-Touch-AMOLED-1.75 (first)
- cluster replacement on ESP32-P4 (later, same `components/`)

Docs: [brief](docs/brief.md) · [pod pin map](docs/pod-pinout.md) ·
[MegaSquirt CAN](docs/ms2-can.md) · [decisions](docs/decisions.md)

## Build (Windows, PowerShell)

```powershell
. $HOME\esp\esp-idf\export.ps1
idf.py -C firmware set-target esp32s3
idf.py -C firmware build
idf.py -C firmware -p COM5 flash monitor
```

## Flashing without the toolchain

`firmware/flash/` holds the latest built image and a one-line script for
esptool, so any PC with Python can flash the gauge. See
`firmware/flash/README.md`.
