# tools/build_check.ps1 - set up the ESP-IDF env by hand, then run idf.py.
# Skips idf-env/Initialize-Idf: on this machine they cannot resolve the python path.
# Usage: powershell -ExecutionPolicy Bypass -File tools\build_check.ps1 [idf.py args...]
# NOTE: keep this file ASCII-only. PowerShell 5.1 reads BOM-less UTF-8 as ANSI,
# and non-ASCII comments break parsing of the whole script.
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$IdfArgs = @("build")
)

$ErrorActionPreference = "Stop"

$env:IDF_TOOLS_PATH = "D:\Software\Espressif"
$env:IDF_PATH = "D:\Software\Espressif\frameworks\esp-idf-v5.5.5"
$Python = "D:\Software\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"

if (-not (Test-Path $Python)) { throw "python env not found: $Python" }

# idf_tools.py export prints KEY=VAL pairs (toolchain, cmake, ninja, ...).
$exports = & $Python "$env:IDF_PATH\tools\idf_tools.py" export --format key-value
if ($LASTEXITCODE -ne 0) { throw "idf_tools.py export failed" }
foreach ($line in $exports) {
    $parts = $line -split "=", 2
    if ($parts.Count -lt 2) { continue }
    $name = $parts[0].Trim()
    $val = $parts[1]
    if (-not $name) { continue }
    Set-Item -Path "Env:$name" -Value $val
}

$env:PYTHONNOUSERSITE = "True"
$env:PYTHONPATH = $null

& $Python "$env:IDF_PATH\tools\check_python_dependencies.py"
if ($LASTEXITCODE -ne 0) { throw "python dependency check failed" }

Set-Location "D:\Project\ai-passport-pinball\ai-passport-pinball"
& $Python "$env:IDF_PATH\tools\idf.py" @IdfArgs
exit $LASTEXITCODE
