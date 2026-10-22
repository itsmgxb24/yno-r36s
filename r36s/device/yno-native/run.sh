#!/bin/bash
# Native YNOproject client for the R36S (dArkOS): EasyRPG Player with YNO multiplayer, libcurl networking.
#
#   run.sh                  shows the list of games of the YNOproject: pick one with A
#   run.sh <game id>        starts that game right away (yume, 2kki, flow, ... = ynoproject.net/<id>/)
#
# Game files are downloaded on demand into yno-native/games/<id>/ (saves go there too, so they are
# visible from a PC). Back to the list: Start (settings) -> Exit. Quit: Select + Start.
# Settings: yno-native/yno.conf.
# If there is no internet it first looks for a phone with USB tethering (../usb-tether/), then the ESP32
# (../esp32-internet/).

export PATH="$PATH:/usr/sbin:/sbin:/usr/local/sbin:/usr/local/bin"

if [ "$(id -u)" -ne 0 ]; then
    exec sudo -E bash "$0" "$@"
fi

GAME="${1:-}"          # empty = show the game list
APP="$(cd "$(dirname "$0")" && pwd)"
DIR="$(dirname "$APP")"
LOG="$APP/last-run.log"
CONF="$APP/yno.conf"
ESP="$DIR/esp32-internet/r36s-ppp.sh"
TETHER="$DIR/usb-tether/usb-tether.sh"
BIN="$APP/yno-r36s"
GAMES="$APP/games"
TTY=/dev/tty1

AUTH=""                    # JWT of a logged-in YNO account ("auth" cookie), empty = guest
SAVESYNC=1                 # 1 = slot 1 follows the save on the server (needs AUTH)
ROOM_KEY_FILE="$APP/room.key"
LOG_LEVEL=debug            # debug | info | warning: how much last-run.log says
VIDEO_DRIVER=""            # empty = automatic (kmsdrm on the handheld); e.g. "offscreen" for tests
CHAT_PINNED=0              # 1 = keep the chat on screen all the time
# a release zip ships yno.conf.example, so that unpacking an update never replaces the user's settings
[ -f "$CONF" ] || cp "$APP/yno.conf.example" "$CONF" 2>/dev/null
[ -f "$CONF" ] && . "$CONF"

mkdir -p "$GAMES"
exec > "$LOG" 2>&1
echo "YNO native - $(date '+%F %H:%M:%S') - game: ${GAME:-(list)}"

show()  { fold -s -w 78 > "$TTY" 2>/dev/null; }
clear_screen() { printf '\033c' > "$TTY" 2>/dev/null; }
say()   { echo "$*"; printf '%s\n' "$*" | show; }

fail() {
    echo "ERROR: $*"
    clear_screen
    { echo "Yume Nikki Online failed:"; echo "$*"; echo; echo "Log: $LOG"; } | show
    sleep 10
    exit 1
}

QPID=
cleanup() {
    trap - EXIT
    [ -n "$QPID" ] && kill "$QPID" 2>/dev/null
    pkill -f "gptokeyb.*yno-r36s" 2>/dev/null
    sync
    clear_screen
    echo "done"
}
trap cleanup EXIT
trap 'exit 1' INT TERM HUP

clear_screen
say "Yume Nikki Online${GAME:+ ($GAME)}"
[ -x "$BIN" ] || chmod +x "$BIN" 2>/dev/null
[ -x "$BIN" ] || fail "$BIN not found - copy the whole yno-native folder"

# ---------------------------------------------------------------- internet + clock
net_ok() {
    local code
    code=$(curl -sk -m 10 -o /dev/null -w '%{http_code}' "https://connect.ynoproject.net/${GAME:-yume}/api/players" 2>/dev/null)
    [ -n "$code" ] && [ "$code" != 000 ]
}

sync_clock() {      # no RTC: a wrong date breaks HTTPS
    local hdr now here
    hdr=$(curl -skI -m 10 https://connect.ynoproject.net/ 2>/dev/null | sed -n 's/^[Dd]ate: *//p' | tr -d '\r')
    [ -n "$hdr" ] || return 0
    now=$(date -u -d "$hdr" +%s 2>/dev/null) || return 0
    here=$(date +%s)
    if [ $((now - here)) -gt 120 ] || [ $((here - now)) -gt 120 ]; then
        echo "clock is off by $((now - here)) s - setting it from the server"
        date -u -s "@$now" >/dev/null
    fi
}

say "Checking the internet connection..."
if ! net_ok && [ -f "$TETHER" ]; then
    say "No internet - looking for a phone with USB tethering..."
    bash "$TETHER" start --wait 4
fi
if ! net_ok; then
    if [ -f "$ESP" ]; then
        say "No internet - connecting through the ESP32 (up to a minute)..."
        bash "$ESP" start
    fi
    net_ok || fail "No internet connection. Connect Wi-Fi, switch on USB tethering on a phone, or plug in the ESP32 and start again."
fi
sync_clock

# ---------------------------------------------------------------- run
export SDL_GAMECONTROLLERCONFIG_FILE=/opt/inttools/gamecontrollerdb.txt
export SDL_VIDEO_EGL_DRIVER=libEGL.so
export LD_LIBRARY_PATH="$APP/lib:$LD_LIBRARY_PATH"
export HOME=/home/ark
export YNO_LOG="$LOG_LEVEL"
[ -n "$VIDEO_DRIVER" ] && export SDL_VIDEODRIVER="$VIDEO_DRIVER"
[ "$CHAT_PINNED" = 1 ] && export YNO_CHAT_PINNED=1

ARGS=(--yno-games-dir "$GAMES" --fullscreen --scaling nearest)
[ -n "$GAME" ] && ARGS+=(--yno-game "$GAME" --project-path "$GAMES/$GAME")
[ -n "$AUTH" ] && ARGS+=(--yno-auth "$AUTH")
[ "$SAVESYNC" = 1 ] || ARGS+=(--yno-no-save-sync)
[ -f "$ROOM_KEY_FILE" ] && ARGS+=(--yno-room-key-file "$ROOM_KEY_FILE")

say "Starting..."
if [ -x /opt/inttools/gptokeyb ]; then
    /opt/inttools/gptokeyb -1 "yno-r36s" -c "$APP/quit.gptk" > /dev/null 2>&1 &
    QPID=$!
fi
cd "$GAMES" && "$BIN" "${ARGS[@]}"
echo "client exited with $?"
exit 0
