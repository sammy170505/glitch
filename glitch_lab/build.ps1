# Build glitch_lab firmware inside the existing eCTF `build-hsm` Docker image.
#   .\build.ps1                    -> build\glitchlab_bootloader.{elf,bin,lst}   (default, flash over UART)
#   .\build.ps1 -Layout bare       -> build\glitchlab_bare.{elf,bin,lst}         (flash over SWD/Uniflash)
#   .\build.ps1 -Clean
param(
    [ValidateSet('bootloader', 'bare')][string]$Layout = 'bootloader',
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$fw   = Join-Path $here 'firmware'
$out  = Join-Path $here 'build'
New-Item -ItemType Directory -Force $out | Out-Null

$target = 'all'
if ($Clean) { $target = 'clean' }

docker run --rm `
    -v "${fw}:/src" -v "${out}:/out" -w /src `
    --entrypoint make build-hsm `
    $target LAYOUT=$Layout OUTDIR=/out
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
