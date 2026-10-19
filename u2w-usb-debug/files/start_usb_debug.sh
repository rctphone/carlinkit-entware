#!/bin/sh
# USB management supervisor: native FunctionFS ADB is owned by g_iphone
# (adb_coexist).  This script keeps NCM/DHCP alive and restarts adbd (which is
# USB-only by build) if it dies.
PATH=${USB_DEBUG_PATH:-/bin:/sbin:/usr/bin:/usr/sbin}
export PATH
STATE=${USB_DEBUG_STATE:-/tmp}
SYS=${USB_DEBUG_SYS:-/sys}
PIDFILE=$STATE/usb-debug.pid
LOCK=$STATE/usb-debug.lock
LOG=$STATE/usb-debug.log
ADBPID=$STATE/usb-adbd.pid
DHCPPID=$STATE/udhcpd-usb.pid
ACMPID=$STATE/usb-acm.pid
CONF=$STATE/udhcpd-usb.conf
ADBD=${USB_DEBUG_ADBD:-/usr/sbin/adbd}

# "PID:starttime|what" of processes already verified by alive(): the steady-state loop then
# needs no fork (read and set are builtins); a PID reused by another process has a different
# start time (/proc/PID/stat field 22) and is verified again.
verified=' '
alive() {
    [ -s "$1" ] || return 1
    read -r p 2>/dev/null < "$1" || return 1
    case "$p" in ''|*[!0-9]*) return 1;; esac
    read -r stat 2>/dev/null < /proc/"$p"/stat || return 1
    set -- "$1" "$2" $stat
    st=${24}
    token="$p:$st|$2"
    case "$verified" in *" $token "*) return 0;; esac
    if [ "$2" = '@child' ]; then
        # getty execs the login shell; its command line changes but parentage does not.
        [ "$6" = "$$" ] || return 1
    else
        # Do not act on a PID reused by another process.
        case "$(tr '\000' ' ' < /proc/"$p"/cmdline 2>/dev/null)" in
            *"$2"*) ;;
            *) return 1;;
        esac
    fi
    verified="$verified$token "
    return 0
}

if ! mkdir "$LOCK" 2>/dev/null; then
    [ -s "$PIDFILE" ] || exit 0
    alive "$PIDFILE" start_usb_debug.sh && exit 0
    rmdir "$LOCK" 2>/dev/null || exit 0
    mkdir "$LOCK" 2>/dev/null || exit 0
fi
echo $$ > "$PIDFILE"

stop_owned() {
    if alive "$1" "$2"; then kill "$(cat "$1")" 2>/dev/null; fi
    rm -f "$1"
}
cleanup() {
    stop_owned "$ADBPID" "$ADBD"
    stop_owned "$DHCPPID" "$CONF"
    stop_owned "$ACMPID" '@child'
    rm -f "$PIDFILE"
    rmdir "$LOCK" 2>/dev/null
}
trap cleanup EXIT
trap 'exit 0' INT TERM
echo 'usb-debug: starting NCM/ADB supervisor' >> "$LOG"

find_iface() {
    if [ -n "${USB_DEBUG_IFACE:-}" ]; then
        [ -d "$SYS/class/net/$USB_DEBUG_IFACE" ] && echo "$USB_DEBUG_IFACE"
        return
    fi
    for d in "$SYS"/class/net/ncm*; do
        [ -e "$d" ] || continue
        case "$(readlink -f "$d/device" 2>/dev/null)" in
            *ci_hdrc.1*|*dummy_udc*) echo "${d##*/}"; return;;
        esac
    done
    # Some gadget netdevs do not expose a UDC parent. Keep vendor ncm0 untouched.
    last=
    for d in "$SYS"/class/net/ncm*; do
        [ -e "$d" ] || continue
        [ "${d##*/}" = ncm0 ] || last=${d##*/}
    done
    [ -z "$last" ] || echo "$last"
}

provision_key() {
    [ -s /adb_keys ] || [ -s /data/misc/adb/adb_keys ] || {
        if [ -s /mnt/UPAN/adbkey.pub ]; then
            mkdir -p /data/misc/adb
            (umask 077; { cat /mnt/UPAN/adbkey.pub; printf '\n'; } > /data/misc/adb/adb_keys)
            echo 'usb-debug: public key provisioned from USB stick' >> "$LOG"
        fi
    }
}

start_adb() {
    alive "$ADBPID" "$ADBD" && return
    # A vendor service may already own adbd; do not start a competing listener.
    pidof adbd >/dev/null 2>&1 && return
    [ -x "$ADBD" ] || return
    # g_iphone owns the FFS mount; restart the daemon in place.
    "$ADBD" >> "$LOG" 2>&1 &
    echo 'usb-debug: adbd started' >> "$LOG"
    echo $! > "$ADBPID"
}

start_acm() {
    [ -c /dev/ttyGS0 ] && [ -x /sbin/getty ] || return
    alive "$ACMPID" '@child' && return
    ps | grep '[g]etty.*ttyGS0' >/dev/null 2>&1 && return
    /sbin/getty -n -l /bin/sh -L ttyGS0 115200 vt100 >> "$LOG" 2>&1 &
    echo $! > "$ACMPID"
}

previous=
pass=0
while :; do
    provision_key
    start_acm
    # Fast path: the configured interface still exists with the same ifindex.
    iface=
    if [ -n "$previous" ]; then
        read -r idx 2>/dev/null < "$SYS/class/net/${previous%%:*}/ifindex" &&
            [ "${previous%%:*}:$idx" = "$previous" ] && iface=${previous%%:*}
    fi
    [ -n "$iface" ] || iface=$(find_iface)
    if [ -n "$iface" ]; then
        idx=
        read -r idx 2>/dev/null < "$SYS/class/net/$iface/ifindex"
        generation=$iface:$idx
        # The address is re-checked on a new interface and every 6th pass (~30 s).
        pass=$(( (pass + 1) % 6 ))
        if [ "$generation" != "$previous" ] || { [ "$pass" -eq 0 ] &&
           ! ifconfig "$iface" 2>/dev/null | grep -Eq 'inet (addr:)?192\.168\.7\.1([[:space:]]|$)'; }; then
            if ifconfig "$iface" 192.168.7.1 netmask 255.255.255.0 up >> "$LOG" 2>&1; then
                stop_owned "$DHCPPID" "$CONF"
                cat > "$CONF" <<EOF
start 192.168.7.100
end 192.168.7.150
interface $iface
pidfile $DHCPPID
lease_file $STATE/udhcpd-usb.leases
option subnet 255.255.255.0
EOF
                previous=$generation
                echo "usb-debug: configured $generation at 192.168.7.1" >> "$LOG"
            fi
        fi
        if [ "$generation" = "$previous" ]; then
            if ! alive "$DHCPPID" "$CONF"; then
                udhcpd -f "$CONF" >> "$LOG" 2>&1 &
                echo $! > "$DHCPPID"
            fi
        fi
    else
        previous=
        stop_owned "$DHCPPID" "$CONF"
    fi
    # Native adbd is supervised regardless of NCM (config 2 has no NCM).
    start_adb
    # wait makes SIGTERM from the init script take effect immediately.
    sleep 5 & wait $!
done
