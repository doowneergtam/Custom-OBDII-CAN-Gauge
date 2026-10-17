#!/bin/sh
# Flash the prebuilt NB1 gauge image: needs only Python and esptool (pip install esptool).
# Usage: ./flash.sh /dev/ttyACM0   (or COM9 on Windows)
cd "$(dirname "$0")"
python -m esptool --chip esp32s3 -p "${1:-/dev/ttyACM0}" -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 nb1_gauge.bin
