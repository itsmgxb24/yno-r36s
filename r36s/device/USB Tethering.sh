#!/bin/bash
# USB Tethering - one-button launcher for EmulationStation (no keyboard needed)
#
# Plug the phone into the R36S, switch USB tethering ON on the phone, then start this entry:
#   not connected -> brings up the USB network interface and tests the internet
#   connected     -> disconnects again
# The result is shown on the screen and saved to usb-tether/last-run.log.
#
# Layout (in the ports folder, e.g. /roms/ports/):
#   USB Tethering.sh             <- this file, appears in EmulationStation
#   usb-tether/usb-tether.sh     <- the main script

export PATH="$PATH:/usr/sbin:/sbin:/usr/local/sbin:/usr/local/bin"

if [ "$(id -u)" -ne 0 ]; then
    exec sudo -E bash "$0" "$@"
fi

DIR="$(cd "$(dirname "$0")" && pwd)"
APP="$DIR/usb-tether"
MAIN="$APP/usb-tether.sh"
LOG="$APP/last-run.log"
TTY=/dev/tty1

chmod 666 "$TTY" 2>/dev/null
show()  { fold -s -w 78 > "$TTY" 2>/dev/null; }
clear_screen() { printf '\033c' > "$TTY" 2>/dev/null; }

clear_screen
printf '%s\n' "USB Tethering" "" "Switch USB tethering ON on the phone." "Working... this can take up to a minute." | show

mkdir -p "$APP" 2>/dev/null
{
    if [ ! -f "$MAIN" ]; then
        echo "ERROR: $MAIN not found."
        echo "Copy the 'usb-tether' folder next to this file."
        exit 3
    fi
    bash "$MAIN" toggle
} > "$LOG" 2>&1
rc=$?

clear_screen
show < "$LOG"
{
    echo
    if [ "$rc" -eq 0 ]; then echo "=== DONE ==="; else echo "=== FAILED (see messages above) ==="; fi
    echo "Log: $LOG"
} | show

# no keyboard: keep the result on screen for a while, longer on failure
if [ "$rc" -eq 0 ]; then sleep 8; else sleep 25; fi
clear_screen
exit "$rc"
