#!/bin/bash
# Copies the native client to the ports folder of the R36S SD card.
#
#   r36s/deploy.sh /run/media/$USER/EASYROMS/ports [path to yno-r36s]
#
# The program itself is not kept in git: build it first (r36s/build/build.sh).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
PORTS="${1:?usage: deploy.sh <ports folder on the card> [yno-r36s binary]}"
BIN="${2:-$HOME/yno-build-out/yno-r36s}"
[ -f "$BIN" ] || { echo "binary not found: $BIN (build it first)"; exit 1; }

mkdir -p "$PORTS/yno-native"
cp "$HERE/device/Yume Nikki Online (native).sh" "$PORTS/"
# keep the user's settings, replace everything else
if [ -f "$PORTS/yno-native/yno.conf" ]; then
    cp "$HERE"/device/yno-native/{run.sh,quit.gptk} "$PORTS/yno-native/"
else
    cp "$HERE"/device/yno-native/{run.sh,quit.gptk,yno.conf} "$PORTS/yno-native/"
fi
cp "$BIN" "$PORTS/yno-native/yno-r36s"
mkdir -p "$PORTS/usb-tether"
cp "$HERE/device/USB Tethering.sh" "$PORTS/"
cp "$HERE/device/usb-tether/usb-tether.sh" "$PORTS/usb-tether/"
chmod +x "$PORTS/USB Tethering.sh" "$PORTS/usb-tether/usb-tether.sh" "$PORTS/Yume Nikki Online (native).sh" "$PORTS/yno-native/run.sh" "$PORTS/yno-native/yno-r36s"
sync
echo "deployed to $PORTS"
