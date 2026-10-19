#!/usr/bin/env bash
# Verify the usb-dev kernel profile without building or flashing a kernel.
# Runs inside u2w-builder with u2w-build mounted read-only at /work (this repository checked out at
# work/carlinkit-entware) and the kernel cache at /kernel-cache. KERNEL_BUNDLE is the ludwig-v Kernel/ dir.
#   docker run --rm -v "$PWD:/work:ro" -v u2w-kernel-cache:/kernel-cache:ro u2w-builder \
#     bash /work/work/carlinkit-entware/kernel/profiles/check-usb-dev.sh
set -euo pipefail
profiles=$(cd "$(dirname "$0")" && pwd)
bundle=${KERNEL_BUNDLE:-/work/work/kernel-upstream/Kernel}
version=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["kernel_version"])' "$profiles/../sources.lock.json")
src=/kernel-cache/linux-$version-carlinkit
dtc=/kernel-cache/build/kernel-$version/scripts/dtc/dtc
board=arch/arm/boot/dts/nxp/imx/imx6ull-carlinkit
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"   # merge_config.sh creates its temp files in the current directory

cp "$bundle/config/imx6ull-carlinkit-6.18.config" .config
"$src/scripts/kconfig/merge_config.sh" -m -O "$work" .config "$profiles/usb-dev.config" >/dev/null
make -s -C "$src" O="$work" ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- olddefconfig
while IFS= read -r option; do
  [ -z "$option" ] || grep -qx "$option" .config || { echo "Missing after olddefconfig: $option" >&2; exit 1; }
done < "$profiles/usb-dev.config"

includes=(-I "$src/arch/arm/boot/dts/nxp/imx" -I "$src/arch/arm/boot/dts" -I "$src/scripts/dtc/include-prefixes" -I "$src/include")
compile() { arm-linux-gnueabi-cpp -nostdinc -undef -D__DTS__ -x assembler-with-cpp "${includes[@]}" "$1" | "$dtc" -q -I dts -O dtb -o "$2" -; }
cat "$src/$board.dts" "$profiles/usb-dev.dts.inc" > board.dts
compile "$src/$board.dts" base.dtb
compile board.dts board.dtb
cmp -s base.dtb "/kernel-cache/build/kernel-$version/$board.dtb" || { echo 'Base DTB differs from the built kernel DTB' >&2; exit 1; }

# Phandle numbers shift when a reference disappears; mask them so only real changes remain.
normalize() { "$dtc" -q -I dtb -O dts "$1" | sed -E 's/<0x[0-9a-f]+( |>)/<PH\1/g; s/phandle = <[^>]*>/phandle = PH/'; }
changes=$(diff <(normalize base.dtb) <(normalize board.dtb) | grep -E '^[<>]' | grep -vE 'phandle = PH;|pinctrl-0 = <PH 0x[0-9a-f]+>;' || true)
expected=$'< \t\t\t\tfsl,ext-reset-output;\n< \t\t\t\tpinctrl-names = "default";\n< \t\t\t\tpinctrl-0 = <PH>;'
[ "$changes" = "$expected" ] || { printf 'Unexpected DT changes:\n%s\n' "$changes" >&2; exit 1; }
python3 -I - board.dtb "$dtc" <<'PY'
import re, subprocess, sys
s = subprocess.run([sys.argv[2], '-q', '-I', 'dtb', '-O', 'dts', sys.argv[1]], capture_output=True, text=True, check=True).stdout
def node(name):
    m = re.search(rf'\b{re.escape(name)} {{(.*?)\n\t+}};', s, re.S)
    if not m: raise SystemExit(f'Node not found: {name}')
    return m.group(1)
assert 'fsl,ext-reset-output' not in node('watchdog@20bc000'), 'wdog1 still resets via WDOG_B'
assert 'pinctrl-0' not in node('usb@2184000'), 'usbotg1 still muxes the ID pin'
PY
echo "usb-dev profile verified: 4 configfs options survive olddefconfig; DT changes are exactly wdog1 ext-reset + usbotg1 pinctrl. No kernel built or flashed."
