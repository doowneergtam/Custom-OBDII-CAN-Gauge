# Flash the prebuilt NB1 gauge image from any PC. Uses esptool from Python
# (pip install esptool) or, when Python is missing, the standalone esptool.exe
# that the Arduino ESP32 core installs under %LOCALAPPDATA%\Arduino15.
# Usage: .\flash.ps1 COM9     (port from Device Manager: Espressif USB, VID 303A PID 1001)
param([string]$Port = "COM9")
Set-Location $PSScriptRoot
$flashArgs = @("--chip", "esp32s3", "-p", $Port, "-b", "460800", "--before", "default_reset", "--after", "hard_reset",
               "write_flash", "--flash_mode", "dio", "--flash_freq", "80m", "--flash_size", "16MB",
               "0x0", "bootloader.bin", "0x8000", "partition-table.bin", "0x10000", "nb1_gauge.bin")
$havePy = $false
if (Get-Command python -ErrorAction SilentlyContinue) { & python -c "import esptool" 2>$null; $havePy = ($LASTEXITCODE -eq 0) }
if ($havePy) { & python -m esptool @flashArgs; exit $LASTEXITCODE }
$exe = Get-ChildItem "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py\*\esptool.exe" -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
if ($exe) { Write-Host "No Python esptool; using $($exe.FullName)"; & $exe.FullName @flashArgs; exit $LASTEXITCODE }
Write-Error "No esptool found. Either 'pip install esptool' or install the ESP32 board package in the Arduino IDE (it ships esptool.exe)."
exit 1
