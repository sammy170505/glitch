# Flash glitch_lab onto a Design-Phase board that has MITRE's INSECURE bootloader on it.
# No SWD needed: this goes over the XDS110 backchannel UART through the bootloader.
#
#   1. Put the board in update mode: HOLD S2 (PB21), tap S1 (NRST), release S2.
#      D1 (PB14) flashes red = bootloader is waiting.
#   2. .\flash.ps1 -Port COM7
#
# Uses `ectf hw reflash` = erase + flash + start.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [string]$Bin = ''
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Bin) { $Bin = Join-Path $here 'build\glitchlab_bootloader.bin' }
if (-not (Test-Path $Bin)) { throw "missing $Bin - run .\build.ps1 first" }

Write-Host "Ports present: $([System.IO.Ports.SerialPort]::GetPortNames() -join ', ')"
Write-Host "Flashing $Bin -> $Port (board must be in update mode: hold S2, tap S1, release S2)"
uvx ectf hw $Port reflash $Bin -n glitchlab
if ($LASTEXITCODE -ne 0) { throw "flash failed ($LASTEXITCODE)" }
Write-Host "Done. Next: python host\lab_client.py $Port selftest"
