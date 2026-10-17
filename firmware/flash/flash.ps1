# Flash the prebuilt NB1 gauge image from any PC: needs only Python and esptool (pip install esptool).
# Usage: .\flash.ps1 COM9
param([string]$Port = "COM9")
Set-Location $PSScriptRoot
python -m esptool --chip esp32s3 -p $Port -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 nb1_gauge.bin
