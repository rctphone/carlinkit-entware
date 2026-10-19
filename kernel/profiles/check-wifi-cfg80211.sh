#!/usr/bin/env bash
# Verify the wifi-cfg80211 profile's 88x2bs.ko without loading it. Runs inside u2w-builder:
#   docker run --rm -v "$PWD:/work:ro" -v u2w-kernel-cache:/kernel-cache:ro u2w-builder \
#     bash /work/kernel/profiles/check-wifi-cfg80211.sh
set -euo pipefail
version=$(python3 -c 'import json;print(json.load(open("/work/sources.lock.json"))["kernel_version"])')
ko=drivers/net/wireless/rtl8822bs/88x2bs.ko
new=/kernel-cache/build/kernel-$version-wifi-cfg80211/$ko
base=/kernel-cache/build/kernel-$version/$ko
nm=arm-linux-gnueabi-nm
fail() { echo "FAIL: $*" >&2; exit 1; }
[ -f "$new" ] || fail "missing $new (run make kernel-wifi)"
vermagic=$(strings "$new" | sed -n 's/^vermagic=//p')   # no "grep -q" early exit under pipefail
case "$vermagic" in "$version "*) ;; *) fail "vermagic '$vermagic' is not $version";; esac
defined=$($nm "$new" | awk '$2 ~ /^[tTdDbBrR]$/ {print $3}')
imports=$($nm -u "$new" | awk '{print $2}')
for s in cfg80211_rtw_start_ap cfg80211_rtw_change_beacon rtw_cfg80211_rx_mframe rtw_wdev_alloc cfg80211_rtw_external_auth rtw_BIP_verify rtw_hostapd_ioctl rtw_ioctl; do
  grep -qx "$s" <<<"$defined" || fail "symbol not built: $s"
done
# cfg80211_rx_mgmt() is an inline wrapper of cfg80211_rx_mgmt_ext() on 6.x.
for s in wiphy_register cfg80211_rx_mgmt_ext cfg80211_new_sta; do
  grep -qx "$s" <<<"$imports" || fail "cfg80211 import missing: $s"
done
# The baseline module must stay WEXT-only, i.e. the profile is what adds cfg80211.
if [ -f "$base" ] && $nm "$base" | grep -q ' cfg80211_rtw_start_ap$'; then fail 'baseline 88x2bs.ko already has cfg80211'; fi
printf 'wifi-cfg80211 88x2bs.ko ok: %s bytes (baseline %s); cfg80211 AP + 11w BIP + RTL_IOCTL_HOSTAPD present\n' \
  "$(stat -c %s "$new")" "$( [ -f "$base" ] && stat -c %s "$base" || echo n/a)"
