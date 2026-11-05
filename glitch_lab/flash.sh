#!/usr/bin/env bash
# Flash glitch_lab onto a Design-Phase board via the MITRE insecure bootloader (macOS / Linux).
#
#   1. Put the board in update mode: hold S2, tap S1, release S2 (D1 blinks red).
#   2. ./flash.sh --port /dev/tty.usbmodemXXXX
#      (find the port with:  ls /dev/tty.usbmodem*   on macOS,  or  /dev/ttyACM*  on Linux)
#
# Run with:  bash flash.sh --port <dev>   (or chmod +x flash.sh first)
set -euo pipefail

port=""
bin=""
while [ $# -gt 0 ]; do
  case "$1" in
    -Port|--port|-p) port="$2"; shift 2 ;;
    -Bin|--bin|-b)   bin="$2";  shift 2 ;;
    *) if [ -z "$port" ]; then port="$1"; shift; else echo "unknown arg: $1" >&2; exit 2; fi ;;
  esac
done

here="$(cd "$(dirname "$0")" && pwd)"
[ -z "$bin" ] && bin="$here/build/glitchlab_bootloader.bin"

if [ -z "$port" ]; then
  echo "usage: ./flash.sh --port /dev/tty.usbmodemXXXX" >&2
  echo "serial devices present:" >&2
  ls /dev/tty.usbmodem* /dev/ttyACM* 2>/dev/null || echo "  (none found - is the XDS110 plugged in?)" >&2
  exit 1
fi
[ -f "$bin" ] || { echo "missing $bin - run ./build.sh first" >&2; exit 1; }

echo "Flashing $bin -> $port"
echo "(board must be in update mode: hold S2, tap S1, release S2)"
uvx ectf hw "$port" reflash "$bin" -n glitchlab
echo "Done. Next: python host/lab_client.py $port selftest"
