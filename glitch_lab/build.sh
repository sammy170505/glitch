#!/usr/bin/env bash
# Build glitch_lab firmware inside the eCTF `build-hsm` Docker image (macOS / Linux).
#   ./build.sh                 -> build/glitchlab_bootloader.{elf,bin,lst}   (default, flash over UART)
#   ./build.sh --layout bare   -> build/glitchlab_bare.{elf,bin,lst}         (flash over SWD/Uniflash)
#   ./build.sh --clean
#
# Run with:  bash build.sh   (or chmod +x build.sh first, then ./build.sh)
set -euo pipefail

layout="bootloader"
target="all"
while [ $# -gt 0 ]; do
  case "$1" in
    -Layout|--layout|-l) layout="$2"; shift 2 ;;
    bootloader|bare)     layout="$1"; shift ;;
    -Clean|--clean|-c)   target="clean"; shift ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

here="$(cd "$(dirname "$0")" && pwd)"
fw="$here/firmware"
out="$here/build"
mkdir -p "$out"

docker run --rm \
  -v "$fw:/src" -v "$out:/out" -w /src \
  --entrypoint make build-hsm \
  "$target" LAYOUT="$layout" OUTDIR=/out
