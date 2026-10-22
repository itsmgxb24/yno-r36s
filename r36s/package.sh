#!/bin/bash
# Packs the client into a zip whose contents go straight into the ports folder of the SD card.
#
#   r36s/package.sh <version> [path to yno-r36s] [output dir]
#
# yno.conf is shipped as yno.conf.example (run.sh copies it on the first start), so unpacking a
# newer zip over an installed one keeps the user's settings, games and saves.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="${1:?usage: package.sh <version> [yno-r36s binary] [output dir]}"
BIN="${2:-$HOME/yno-build-out/yno-r36s}"
OUT="${3:-$(pwd)}"
[ -f "$BIN" ] || { echo "binary not found: $BIN (build it first)"; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/yno-native" "$STAGE/usb-tether"
cp "$HERE/device/Yume Nikki Online (native).sh" "$HERE/device/USB Tethering.sh" "$STAGE/"
cp "$HERE"/device/yno-native/{run.sh,quit.gptk} "$STAGE/yno-native/"
cp "$HERE/device/yno-native/yno.conf" "$STAGE/yno-native/yno.conf.example"
cp "$BIN" "$STAGE/yno-native/yno-r36s"
cp "$HERE/device/usb-tether/usb-tether.sh" "$STAGE/usb-tether/"
chmod +x "$STAGE"/*.sh "$STAGE/yno-native/run.sh" "$STAGE/yno-native/yno-r36s" "$STAGE/usb-tether/usb-tether.sh"

mkdir -p "$OUT"
ZIP="$OUT/yno-r36s-$VERSION.zip"
rm -f "$ZIP"
(cd "$STAGE" && zip -qr -X "$ZIP" .)
echo "packed: $ZIP"
