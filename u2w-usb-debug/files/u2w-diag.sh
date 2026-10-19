#!/bin/sh
# u2w-diag: collect everything needed to investigate a CarPlay session that does not start.
# Read-only: it never restarts, kills or reconfigures anything. Passwords are redacted.
#
#   u2w-diag                  write /tmp/u2w-diag-<time>.tar.gz and print its path
#   u2w-diag -                write the tar.gz to stdout (ssh root@192.168.50.2 u2w-diag - > x.tgz)
#   u2w-diag -w 90 [-]        also record a 90 s live window (retry the phone meanwhile)
#
# From a laptop on the dongle's Wi-Fi: tools/u2w-diag.sh in the u2w-build repo.
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
R=${U2W_DIAG_ROOT:-}            # test hook: prefix for /etc, /tmp, /script, /var, /data
LOGREAD=${U2W_DIAG_LOGREAD:-logread}   # test hook: syslog source
watch=0
out=
while [ $# -gt 0 ]; do
    case $1 in
        -w) watch=$2; shift 2 ;;
        -) out=-; shift ;;
        -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
        *) echo "u2w-diag: unknown argument $1" >&2; exit 2 ;;
    esac
done
case $watch in ''|*[!0-9]*) echo 'u2w-diag: -w needs seconds' >&2; exit 2 ;; esac

stamp=$(date +%Y%m%d-%H%M%S)
name=u2w-diag-$stamp
work=$R/tmp/$name
mkdir -p "$work" || exit 1
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
mkdir kernel logs net wifi bt usb proc conf files

# run FILE CMD...: append "$ CMD" and its output (stderr too), never longer than 10 s.
run() {
    f=$1; shift
    { echo "\$ $*"; timeout 10 "$@" 2>&1; echo; } >> "$f"
}
# grab FILE SRC [LINES]: copy a file (or its last LINES lines) if it exists.
grab() {
    [ -e "$2" ] || return 0
    if [ -n "$3" ]; then tail -n "$3" "$2" > "$1" 2>&1; else cat "$2" > "$1" 2>&1; fi
}
# Passwords and keys never leave the box.
# Anywhere in a line, also after a log prefix ("Oct 19 12:00:00 box daemon.info: psk=...").
# A quoted value ("..." or '...', escapes included) is redacted inside its quotes; an unquoted
# value runs to the end of the line, because a WPA passphrase may contain spaces.  Text that
# is already "<redacted>" is left alone, so the final pass is idempotent.
keys='(wpa_passphrase|wpa_psk|sae_password|psk|[Ww]ifi[Pp]assword|[Pp]assword|PASSWORD|[Pp]assphrase|PASSPHRASE)'
redact() {
    sed -E -e "s/(^|[^A-Za-z0-9_])(\"?$keys\"?[[:space:]]*[=:][[:space:]]*)\"([^\"\\\\]|\\\\.)*\"/\\1\\2\"<redacted>\"/g" \
           -e "s/(^|[^A-Za-z0-9_])($keys[[:space:]]*[=:][[:space:]]*)'[^']*'/\\1\\2'<redacted>'/g" \
           -e "s/(^|[^A-Za-z0-9_])($keys[[:space:]]*[=:][[:space:]]*)[^\"'<[:space:]].*/\\1\\2<redacted>/"
}
grab_redacted() {
    [ -e "$2" ] && redact < "$2" > "$1" 2>&1
}
procs='fakeiOSDevice AppleCarPlay ARMiPhoneIAP2 ARMadb-driver bluetoothDaemon hcid hfpd dbus-daemon mdnsd hostapd udhcpd adbd boa colorLightDaemon riddle_top u2w-mfid mfid dropbear syslogd'

log_mark_dmesg=$(dmesg 2>/dev/null | wc -l)
log_mark_user=$(wc -l 2>/dev/null < "$R/tmp/ttyLog" || echo 0)

# ---------- summary ----------
{
    echo "u2w-diag $stamp"
    echo "uptime:   $(cat /proc/uptime)  load: $(cat /proc/loadavg)"
    echo "firmware: $(cat "$R/etc/software_version" 2>/dev/null)  product: $(cat "$R/etc/box_product_type" 2>/dev/null)"
    echo "kernel:   $(uname -a)"
    echo "image:    $(sed -n -e 's/.*"profile": *"\([^"]*\)".*/\1/p' -e 's/.*"kernel_commit": *"\([0-9a-f]\{12\}\).*/\1/p' "$R/etc/u2w-entware-profile.json" 2>/dev/null | head -2 | tr '\n' ' ')"
    echo
    echo "== processes (pid, or MISSING)"
    for p in $procs; do printf '  %-18s %s\n' "$p" "$(pidof "$p" 2>/dev/null || echo MISSING)"; done
    echo
    echo "== USB device controllers (ci_hdrc.1 = the car/host port)"
    for u in /sys/class/udc/*; do
        [ -e "$u" ] || continue
        printf '  %s state=%s speed=%s function=%s\n' "${u##*/}" "$(cat "$u/state" 2>/dev/null)" \
            "$(cat "$u/current_speed" 2>/dev/null)" "$(cat "$u/function" 2>/dev/null)"
    done
    echo "  gadget modules: $(grep -E '^(g_iphone|g_android|f_ptp|cdc_ncm|usb_f_)' /proc/modules | awk '{print $1"("$3")"}' | tr '\n' ' ')"
    echo "  /dev/ipod_iap2: $([ -e /dev/ipod_iap2 ] && echo present || echo absent)"
    echo "  FunctionFS adb: $(ls /dev/usb-ffs/adb 2>/dev/null | tr '\n' ' ')"
    echo
    echo "== network"
    ifconfig 2>/dev/null | awk '/^[a-z]/ {i=$1} /inet addr/ {print "  " i " " $2}'
    echo
    echo "== Wi-Fi AP"
    for f in rf_info; do sed 's/^/  /' /proc/net/rtl88x2bs/wlan0/$f 2>/dev/null | head -2; done
    grep -E 'wireless_mode|SSID' /proc/net/rtl88x2bs/wlan0/ap_info 2>/dev/null | sed 's/^/  /'
    echo "  stations: $(grep -c 'sta.s macaddr' /proc/net/rtl88x2bs/wlan0/all_sta_info 2>/dev/null)"
    echo "  hostapd.conf: $(grep -E '^(channel|hw_mode|wpa|wpa_key_mgmt|ieee80211w|ieee80211ac)=' "$R/etc/hostapd.conf" 2>/dev/null | tr '\n' ' ')"
    echo
    echo "== Bluetooth"
    timeout 5 hciconfig hci0 2>&1 | sed 's/^/  /' | head -4
    echo
    echo "== CarPlay link lock / vendor flag files"
    for f in "$R"/tmp/.*_daemon_started "$R"/tmp/.FisrtConnectHU "$R"/tmp/update_status \
             "$R"/tmp/UDiskPassThroughMode "$R"/tmp/bluetoothdaemon_running "$R"/tmp/wifi_status \
             "$R"/tmp/.hciattach_done "$R"/tmp/.bluetoothDaemon_init_done "$R"/tmp/.mfi_auth_lock; do
        [ -e "$f" ] && printf '  %s: %s\n' "${f#"$R"}" "$(head -c 80 "$f" 2>/dev/null | tr '\n' ' ')"
    done
    echo
    echo "== last CarPlay-related log lines"
    # "Still not connect to AppServer" repeats every 2 s while no head unit talks to the box.
    echo "  'Still not connect to AppServer' lines: $(grep -ac 'Still not connect to AppServer' "$R/tmp/ttyLog" 2>/dev/null)"
    grep -aiE 'fakeiOS|carplay|iap2|AppServer|HU |head ?unit|connect|error|fail|timeout' "$R/tmp/ttyLog" 2>/dev/null |
        grep -av -e 'check_log_size' -e 'Still not connect to AppServer' | tail -n 40 | redact | sed 's/^/  /'
    echo
    echo "== video (vendor log: fakeiOSDevice, AppleCarPlay, ARMadb-driver)"
    # Glitch counters: decoder back-pressure from the car, late phone frames, keyframe
    # requests the box ignored vs. served, frame cache overflow, SPS/PPS change restarts.
    for m in 'HU video decoder busy' 'Late frame' 'ForceKeyFrame ignore it!' '###ForceKeyFrame VideoConfig!' \
             'Need Keyframe!' 'Frame Cache too much' 'spspps is different'; do
        printf '  %-32s %s\n' "$m:" "$(grep -acF "$m" "$R/tmp/ttyLog" 2>/dev/null)"
    done
    # Negotiated size and fps (car Open, what the iPhone was told, the custom fps override),
    # then the periodic rate lines (fakeiOSDevice encrypt bitrate/cpu, ARMadb-driver 10 s rates).
    for m in 'Open!!! size=' 'screenFps(' 'Registered screen' 'use custom FPS' 'Video Latency'; do
        grep -aF "$m" "$R/tmp/ttyLog" 2>/dev/null | tail -n 3
    done | sed 's/^/  /'
    grep -aE 'video framerate: |box video frame rate: ' "$R/tmp/ttyLog" 2>/dev/null | tail -n 12 | sed 's/^/  /'
} > summary.txt 2>&1

# ---------- kernel ----------
dmesg > kernel/dmesg.txt 2>&1
cp /proc/modules kernel/modules.txt 2>/dev/null
for p in /sys/module/g_iphone/parameters/*; do
    [ -e "$p" ] && echo "${p##*/}=$(cat "$p" 2>/dev/null)"
done > kernel/g_iphone-parameters.txt
run kernel/cmdline.txt cat /proc/cmdline
run kernel/interrupts.txt cat /proc/interrupts

# ---------- logs ----------
# The vendor start_bluetooth_wifi.sh prints "wpa_passphrase=..." into its log: redact logs too.
grab logs/userspace.log "$R/tmp/ttyLog" 5000
[ -s logs/userspace.log ] || grab logs/userspace.log "$R/tmp/userspace.log" 5000
[ -e logs/userspace.log ] && { redact < logs/userspace.log > logs/u.tmp; mv logs/u.tmp logs/userspace.log; }
$LOGREAD 2>&1 | redact > logs/syslog.txt
for f in usb-debug.log adbd-native.log u2w-mfid.log AutoTest.log gadget-repair.log; do
    grab "logs/$f" "$R/tmp/$f" 2000
done
grab logs/u2w-gadget-trace.log "$R/data/u2w-gadget-trace.log" 2000

# ---------- processes / CPU ----------
run proc/ps.txt ps w
run proc/top.txt top -b -n 1
for d in /proc/[0-9]*; do
    read -r s 2>/dev/null < "$d/stat" || continue
    set -- $s
    echo "$1 ${2} utime=${14} stime=${15} threads=${20}"
done > proc/cpu-ticks.txt
run proc/meminfo.txt cat /proc/meminfo
run proc/df.txt df
run proc/stat.txt cat /proc/stat

# ---------- network ----------
run net/ifconfig.txt ifconfig -a
run net/ip-addr.txt ip addr
run net/route.txt route -n
run net/netstat.txt netstat -anp
run net/arp.txt cat /proc/net/arp
for f in "$R"/tmp/udhcpd*.leases "$R"/var/lib/udhcpd.leases; do
    [ -e "$f" ] && run net/dhcp-leases.txt dumpleases -f "$f"
done
grab net/udhcpd-usb.conf "$R/tmp/udhcpd-usb.conf"

# ---------- Wi-Fi ----------
for f in rf_info ap_info sec_info all_sta_info survey_info chan_plan country_code linked_info_dump; do
    grab "wifi/$f.txt" "/proc/net/rtl88x2bs/wlan0/$f"
done
grab_redacted conf/hostapd.conf "$R/etc/hostapd.conf"
run wifi/init_bluetooth_wifi-insmod.txt grep -n 'insmod' "$R/script/init_bluetooth_wifi.sh"

# ---------- Bluetooth ----------
run bt/hciconfig.txt hciconfig -a
run bt/hciconfig.txt hciconfig hci0 pageparms
run bt/hciconfig.txt hciconfig hci0 inqparms
run bt/hcitool-con.txt hcitool con
for f in "$R"/tmp/bluetooth*; do [ -f "$f" ] && grab "bt/${f##*/}" "$f"; done

# ---------- USB ----------
for u in /sys/class/udc/*; do
    [ -e "$u" ] || continue
    for a in state function current_speed maximum_speed is_a_peripheral b_hnp_enable soft_connect; do
        echo "${u##*/}/$a=$(cat "$u/$a" 2>/dev/null)"
    done
done > usb/udc.txt
run usb/lsusb.txt lsusb
run usb/devices.txt ls -l /sys/bus/usb/devices/
run usb/otg.txt cat /sys/bus/platform/devices/ci_hdrc.1/inputs/b_bus_req /sys/bus/platform/devices/ci_hdrc.1/power/runtime_status

# ---------- configuration (redacted) ----------
for f in riddle.conf riddle_default.conf airplay.conf udhcpd.conf hfpd.conf mdev.conf box_product_type \
         software_version bluetooth_name wifi_name u2w-mfid.conf; do
    grab_redacted "conf/$f" "$R/etc/$f"
done
grab_redacted conf/tmp-riddle.conf "$R/tmp/.riddle.conf"
run files/tmp-listing.txt ls -la "$R/tmp"
run files/etc-listing.txt ls -la "$R/etc"

# ---------- optional live window ----------
if [ "$watch" -gt 0 ]; then
    echo "u2w-diag: recording ${watch}s; retry CarPlay on the phone now" >&2
    end=$(( $(cut -d. -f1 /proc/uptime) + watch ))
    while [ "$(cut -d. -f1 /proc/uptime)" -lt "$end" ]; do
        {
            echo "--- up=$(cut -d' ' -f1 /proc/uptime) load=$(cut -d' ' -f1-3 /proc/loadavg)"
            for p in $procs; do printf '%s=%s ' "$p" "$(pidof "$p" 2>/dev/null || echo -)"; done; echo
            echo "udc=$(cat /sys/class/udc/ci_hdrc.1/state 2>/dev/null) stations=$(grep -c 'sta.s macaddr' /proc/net/rtl88x2bs/wlan0/all_sta_info 2>/dev/null)"
        } >> logs/watch.txt
        sleep 2
    done
    dmesg | tail -n +"$((log_mark_dmesg + 1))" > logs/watch-dmesg.txt
    tail -n +"$((log_mark_user + 1))" "$R/tmp/ttyLog" 2>/dev/null | redact > logs/watch-userspace.log
    $LOGREAD 2>&1 | redact > logs/syslog.txt
fi

# Final pass: every collected file, whatever its source, goes through the redactor.
find . -type f | while read -r f; do
    redact < "$f" > "$f.redact" && mv "$f.redact" "$f"
done

cd .. || exit 1
if [ "$out" = - ]; then
    tar -czf - "$name"
else
    tar -czf "$R/tmp/$name.tar.gz" "$name" && echo "$R/tmp/$name.tar.gz"
fi
