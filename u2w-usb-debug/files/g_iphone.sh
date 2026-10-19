#!/bin/sh
# mdev hook for the g_iphone module uevent (SUBSYSTEM=module, see mdev.conf).
# The vendor ARMadb-driver unloads g_iphone when no CarPlay host answers; our
# NCM and FunctionFS ADB live in that gadget.  Reload it unless the vendor
# does so itself or another USB mode owns the port.  mdev waits for this
# script, so the work runs in the background.
PATH=${G_IPHONE_PATH:-/bin:/sbin:/usr/bin:/usr/sbin}
export PATH
TMP=${G_IPHONE_TMP:-/tmp}
PROC=${G_IPHONE_PROC:-/proc}
START_IPHONE=${G_IPHONE_START:-/script/start_iphone.sh}
KO=${G_IPHONE_KO:-/script/ko.tar.gz}
DELAY=${G_IPHONE_DELAY:-15}
LOCK=$TMP/g_iphone-heal.lock
LOG=$TMP/usb-debug.log
FFS=/dev/usb-ffs/adb

[ "$ACTION" = remove ] || exit 0

heal() {
    sleep "$DELAY"
    grep -Eq '^(g_iphone|g_android_accessory) ' "$PROC/modules" && return
    for f in UDiskPassThroughMode update_status; do
        [ -e "$TMP/$f" ] && return
    done
    echo 'usb-debug: g_iphone removed and not reloaded, restoring' >> "$LOG"
    # rmmod detached the old FunctionFS mount from its instance.  Unless both
    # go, start_iphone.sh keeps them and the new module never sees adbd.
    umount -l "$FFS" 2>/dev/null
    p=$(pidof adbd) && kill $p
    i=0
    while pidof adbd >/dev/null 2>&1 && [ "$i" -lt 5 ]; do
        i=$((i + 1)); sleep 1
    done
    [ -f "$TMP/g_iphone.ko" ] && [ -f "$TMP/f_ptp.ko" ] || tar -xzf "$KO" -C "$TMP"
    # Loads f_ptp*/g_iphone with the configured HNP interval and serial, then
    # mounts FunctionFS and starts adbd (install-firmware-profile.py).
    sh "$START_IPHONE"
}

mkdir "$LOCK" 2>/dev/null || exit 0
(
    trap 'rmdir "$LOCK"' EXIT
    trap 'exit 1' HUP TERM
    heal
) </dev/null >> "$LOG" 2>&1 &
