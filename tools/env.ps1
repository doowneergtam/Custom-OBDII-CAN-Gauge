# Dot-source this in PowerShell to get idf.py on PATH for this repo:
#   . .\tools\env.ps1
# Puts the real CPython ahead of the Microsoft Store "python" stub, then runs
# ESP-IDF's export.ps1.
$env:PATH = "C:\Users\doown\AppData\Local\Python\bin;" + $env:PATH
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
. "$HOME\esp\esp-idf\export.ps1"
