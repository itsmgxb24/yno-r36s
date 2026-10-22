#!/bin/bash
# usb-tether.sh - internet on the R36S through a phone with USB tethering (or any USB Ethernet adapter)
#
#   sudo ./usb-tether.sh [command] [options]
#
# Commands
#   start    load the drivers, wait for the USB network interface, get an address, test  [default]
#   status   show the interface and run the connectivity test
#   stop     take the interface down again and undo the DNS change
#   toggle   stop if connected, otherwise start (used by the EmulationStation launcher)
#
# Options
#   -w, --wait SEC   how long to wait for the interface to appear (default 60)
#   -i, --iface DEV  use this interface instead of detecting one
#   -h, --help       this text
#
# Android: connect the phone to the R36S USB-C port (OTG adapter, data cable), then switch on
#   Settings -> Network -> Hotspot & tethering -> USB tethering.
# The kernel (4.4.189) has the drivers for it: rndis_host (the usual Android mode), cdc_ncm,
# cdc_ether, and for USB Ethernet dongles asix / ax88179 / r8152 / smsc95xx. It has no ipheth,
# so an iPhone does not work.
#
# Environment override: DNS_SERVERS ("1.1.1.1 8.8.8.8", only used when the DHCP server gave no working DNS)

set -u
export LC_ALL=C
export PATH="$PATH:/usr/sbin:/sbin:/usr/local/sbin:/usr/local/bin"

DNS_SERVERS="${DNS_SERVERS:-1.1.1.1 8.8.8.8}"
RESOLV_BAK=/tmp/usb-tether.resolv.bak
IFACE_FILE=/tmp/usb-tether.iface
TEST_URL=https://connect.ynoproject.net/
WAIT=60
IFACE=""
CMD=start

usage() { sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; }

case "${1:-}" in -h|--help|help) usage; exit 0 ;; esac

if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        exec sudo -E bash "$0" "$@"
    fi
    echo "This script must run as root (sudo not found)." >&2
    exit 1
fi

while [ $# -gt 0 ]; do
    case "$1" in
        start|stop|status|toggle) CMD=$1 ;;
        -w|--wait) WAIT=${2:-60}; shift ;;
        -i|--iface) IFACE=${2:-}; shift ;;
        -h|--help|help) usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage; exit 2 ;;
    esac
    shift
done

info() { printf '  [ .. ] %s\n' "$*"; }
ok()   { printf '  [ OK ] %s\n' "$*"; }
warn() { printf '  [WARN] %s\n' "$*"; }
bad()  { printf '  [FAIL] %s\n' "$*"; }
hint() { printf '         -> %s\n' "$*"; }
have() { command -v "$1" >/dev/null 2>&1; }

# USB network drivers the kernel ships as modules; unused ones cost nothing
load_drivers() {
    local m
    for m in usbnet cdc_ether rndis_host cdc_ncm asix ax88179_178a r8152 smsc95xx; do
        modprobe -q "$m" 2>/dev/null
    done
}

# first network interface that sits on the USB bus (usb0, eth1, enx...), not Wi-Fi / PPP / virtual
find_iface() {
    local n dev
    for n in /sys/class/net/*; do
        n=${n##*/}
        case $n in lo|wlan*|p2p*|ap[0-9]*|ppp*|tun*|tap*|veth*|docker*|br-*) continue ;; esac
        dev=$(readlink -f "/sys/class/net/$n/device" 2>/dev/null) || continue
        case $dev in */usb[0-9]*) echo "$n"; return 0 ;; esac
    done
    return 1
}

wait_iface() {
    local t=0
    [ -n "$IFACE" ] && [ -d "/sys/class/net/$IFACE" ] && return 0
    while :; do
        IFACE=$(find_iface) && return 0
        IFACE=
        [ "$t" -ge "$WAIT" ] && return 1
        sleep 1
        t=$((t + 1))
    done
}

has_ip() { ip -4 -o addr show dev "$1" scope global 2>/dev/null | grep -q inet; }

# what is plugged in, for the log: vendor:product and name, hubs left out
list_usb() {
    local d
    for d in /sys/bus/usb/devices/*; do
        [ -f "$d/idVendor" ] || continue
        [ "$(cat "$d/bDeviceClass" 2>/dev/null)" = 09 ] && continue
        printf '         %s:%s %s\n' "$(cat "$d/idVendor")" "$(cat "$d/idProduct")" \
            "$(cat "$d/manufacturer" 2>/dev/null) $(cat "$d/product" 2>/dev/null)"
    done
}

get_address() {
    local dev=$1 i
    ip link set "$dev" up 2>/dev/null
    # NetworkManager (dArkOS) normally configures a new Ethernet device on its own
    for i in 1 2 3 4 5 6 7 8; do
        has_ip "$dev" && return 0
        sleep 1
    done
    if have nmcli && nmcli -t general status >/dev/null 2>&1; then
        info "asking NetworkManager to connect $dev"
        timeout 40 nmcli device connect "$dev" >/dev/null 2>&1
        has_ip "$dev" && return 0
    fi
    if have dhcpcd; then
        info "running dhcpcd on $dev"
        timeout 40 dhcpcd -4 -1 -t 25 "$dev" >/dev/null 2>&1
        has_ip "$dev" && return 0
    fi
    return 1
}

route_dev() {
    ip route get 1.1.1.1 2>/dev/null | awk '{ for (i = 1; i <= NF; i++) if ($i == "dev") { print $(i+1); exit } }'
}

dns_works() {
    if have getent; then getent hosts example.com >/dev/null 2>&1; else ping -c 1 -W 3 example.com >/dev/null 2>&1; fi
}

fix_dns() {
    local s
    dns_works && return 0
    warn "the name lookup does not work - using $DNS_SERVERS"
    [ -e "$RESOLV_BAK" ] || cp -a --remove-destination /etc/resolv.conf "$RESOLV_BAK" 2>/dev/null
    { for s in $DNS_SERVERS; do echo "nameserver $s"; done; } > /etc/resolv.conf
    dns_works
}

connectivity() {
    local code
    code=$(curl -sk -m 10 -o /dev/null -w '%{http_code}' "$TEST_URL" 2>/dev/null)
    if [ -n "$code" ] && [ "$code" != 000 ]; then
        ok "reached $TEST_URL (HTTP $code)"
        return 0
    fi
    if ping -c 1 -W 4 1.1.1.1 >/dev/null 2>&1; then
        bad "1.1.1.1 answers but $TEST_URL does not (DNS or clock?)"
    else
        bad "no answer from the internet through $IFACE"
        hint "does the phone have mobile data / Wi-Fi? tethering shares the phone's connection"
    fi
    return 1
}

do_start() {
    echo "USB tethering - $(date '+%F %H:%M:%S')"
    load_drivers
    info "waiting for a USB network interface (up to ${WAIT}s)"
    if ! wait_iface; then
        bad "no USB network interface appeared"
        hint "plug the phone into the R36S USB-C port (data cable, OTG adapter) and switch USB tethering ON"
        hint "the console always powers the port, but a phone only shows up on USB after it is told to: pull the"
        hint "notification 'Charging this device via USB' and pick USB tethering, or set Developer options ->"
        hint "Default USB configuration -> USB tethering. If the phone never shows that notification it does not"
        hint "see a USB host at all (charge-only cable, or the port does not talk data to it)"
        hint "USB devices the kernel sees:"
        list_usb
        hint "kernel messages:"
        dmesg 2>/dev/null | grep -iE 'usb|rndis|cdc|ncm' | tail -8 | sed 's/^/         /'
        return 1
    fi
    ok "interface $IFACE ($(cat "/sys/class/net/$IFACE/address" 2>/dev/null), driver $(basename "$(readlink -f "/sys/class/net/$IFACE/device/driver" 2>/dev/null)"))"
    echo "$IFACE" > "$IFACE_FILE"

    if ! get_address "$IFACE"; then
        bad "no IP address from the phone (DHCP failed)"
        return 1
    fi
    ok "address: $(ip -4 -o addr show dev "$IFACE" scope global | awk '{print $4}' | head -1)"

    if [ "$(route_dev)" = "$IFACE" ]; then
        ok "default route goes through $IFACE"
    else
        warn "traffic to the internet goes through '$(route_dev)', not $IFACE"
        ip route show default 2>/dev/null | sed 's/^/         /'
    fi
    fix_dns
    connectivity
}

do_stop() {
    local dev=${IFACE:-$(cat "$IFACE_FILE" 2>/dev/null)}
    [ -n "$dev" ] || dev=$(find_iface) || { info "nothing to disconnect"; return 0; }
    info "disconnecting $dev"
    have nmcli && timeout 20 nmcli device disconnect "$dev" >/dev/null 2>&1
    have dhcpcd && dhcpcd -k "$dev" >/dev/null 2>&1
    ip link set "$dev" down 2>/dev/null
    if [ -e "$RESOLV_BAK" ]; then
        cat "$RESOLV_BAK" > /etc/resolv.conf 2>/dev/null
        rm -f "$RESOLV_BAK"
    fi
    rm -f "$IFACE_FILE"
    ok "disconnected"
}

do_status() {
    local dev=${IFACE:-$(find_iface)}
    if [ -z "$dev" ]; then
        bad "no USB network interface"
        list_usb
        return 1
    fi
    IFACE=$dev
    info "$dev: $(ip -4 -o addr show dev "$dev" scope global | awk '{print $4}' | head -1)"
    has_ip "$dev" || { bad "$dev has no IPv4 address"; return 1; }
    connectivity
}

case "$CMD" in
    start)  do_start ;;
    stop)   do_stop ;;
    status) do_status ;;
    toggle)
        dev=$(find_iface) || dev=
        if [ -n "$dev" ] && has_ip "$dev"; then IFACE=$dev; do_stop; else do_start; fi ;;
esac
