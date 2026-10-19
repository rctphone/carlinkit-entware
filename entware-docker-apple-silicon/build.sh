#!/usr/bin/env bash
# Build the carlinkit feed packages with the EE71 Entware fork (armv7-3.10, glibc 2.17).
# Standalone version of the u2w-build entware/build.sh: the fork is cloned at a pinned commit
# into a docker volume, and this repository is mounted as the "carlinkit" feed.
#   entware-docker-apple-silicon/build.sh setup|toolchain|build [package]|collect|shell
set -euo pipefail
TOOLS=$(cd "$(dirname "$0")" && pwd)
ROOT=$(dirname "$TOOLS")
IMAGE=${IMAGE:-carlinkit-entware}
VOLUME=${VOLUME:-carlinkit-entware-build}
ENTWARE_REPOSITORY=${ENTWARE_REPOSITORY:-https://github.com/rctphone/Entware-EE71-glibc2.17.git}
ENTWARE_COMMIT=${ENTWARE_COMMIT:-8457da6cb663229ef9c0221c0d892e5afb64fbba}
OUT=${OUT:-$ROOT/packages}
# Exported packages. libxml2, wpa-supplicant, wpa-cli, iw, dbus-utils, mdns-utils, usbmuxd
# (+libplist, glue) and umtprd are separate packages (off by default in the U2W image).
PACKAGES=(dropbear adbd hostapd u2w-usb-debug u2w-low-latency u2w-mfid libnl-core libnl-genl libopenssl
          zlib libusb-1.0 nand-utils libexpat libdbus dbus mdnsd libfdk-aac hcid
          libxml2 wpa-supplicant wpa-cli iw dbus-utils mdns-utils
          usbmuxd libplist libimobiledevice-glue umtprd)
run() {
  docker run --rm -v "$VOLUME:/entware" -v "$ROOT:/feeds/carlinkit:ro" -v "$TOOLS:/u2w:ro" "$IMAGE" bash -ec "$1"
}
case "${1:-}" in
  setup)
    docker build -t "$IMAGE" "$TOOLS"
    docker volume create "$VOLUME" >/dev/null
    # The fork at the pinned commit. Tracked files are reset on every setup (the edits below are
    # re-applied); build output (bin, build_dir, staging_dir, dl) stays in the volume.
    docker run --rm -v "$VOLUME:/entware" "$IMAGE" bash -ec "
      git config --global --add safe.directory /entware
      [ -d .git ] || { git init -q; git remote add origin '$ENTWARE_REPOSITORY'; }
      git cat-file -e '$ENTWARE_COMMIT^{commit}' 2>/dev/null || git fetch -q --depth=1 origin '$ENTWARE_COMMIT'
      [ ! -L feeds/rctphone ] || rm feeds/rctphone
      git -c advice.detachedHead=false checkout -q -f --detach '$ENTWARE_COMMIT'
      # scripts/feeds replaces feeds/<name> with the src-link symlink, so link a copy.
      rm -rf .rctphone-feed && cp -a feeds/rctphone .rctphone-feed
    "
    run '
      # Selected packages need neither Python 2 (node_legacy) nor the Go host tools.
      sed -i "/SetupHostCommand,python2,/,/python2.7 -V/d" include/prereq-build.mk
      sed -i "/^tools-y += go-\(bootstrap\|src\)$/d" tools/Makefile
      ! grep -q "python2\.7 -V" include/prereq-build.mk
      # Dropbear: key-only init, no blank-root patch, no install-time keys (release 6).
      d=package/network/services/dropbear
      rm $d/patches/600-allow-blank-root-password.patch
      cp /u2w/runtime/dropbear.init /u2w/runtime/dropbear.default $d/files/
      sed -i -e "/^define Package\/dropbear\/postinst$/,/^endef$/d" -e "s/^PKG_RELEASE:=5$/PKG_RELEASE:=6/" $d/Makefile
      grep -q "^PKG_RELEASE:=6$" $d/Makefile
      # libxml2 (separate package): reader-only, no zlib/iconv, HTML, XPath, schemas, validation, writer.
      x=package/libs/libxml2/Makefile
      sed -i -e "s/^\(  DEPENDS:=+libpthread\) +zlib/\1/" \
        -e "s/-DLIBXML2_WITH_\(C14N\|DEBUG\|HTML\|ICONV\|OUTPUT\|PATTERN\|SCHEMAS\|VALID\|WRITER\|XINCLUDE\|XPATH\|XPTR\|XPTR_LOCS\|ZLIB\)=ON/-DLIBXML2_WITH_\1=OFF/" $x
      ! grep -q -- "-DLIBXML2_WITH_ZLIB=ON" $x
      # mtd-utils: flash_erase only (no libuuid, no ubihealthd: getrandom needs glibc 2.25).
      m=package/utils/mtd-utils/Makefile
      sed -i -e "/^PKG_BUILD_DEPENDS:=util-linux$/d" -e "s/--without-zlib$/--without-zlib --without-ubifs --without-jffs --disable-ubihealthd/" $m
      grep -q -- "--without-ubifs --without-jffs" $m
      # iw (separate package) on libnl-3 instead of libnl-tiny.
      w=package/network/utils/iw/Makefile
      sed -i -e "s/DEPENDS:= +libnl-tiny/DEPENDS:= +libnl-genl/" -e "s|usr/include/libnl-tiny|usr/include/libnl3|" \
        -e "s/NLLIBNAME=\"libnl-tiny\"/NLLIBNAME=\"libnl-3.0\"/" \
        -e "s|LIBS=\"-lm -lnl-tiny\"|LIBS=\"-L\$(STAGING_DIR)/usr/lib -lm -lnl-genl-3 -lnl-3\"|" $w
      grep -q "lnl-genl-3 -lnl-3" $w
      rm -rf tmp
      # rctphone: the fork in-tree feed. carlinkit: this repository.
      printf "src-link rctphone /entware/.rctphone-feed\nsrc-link carlinkit /feeds/carlinkit\n" > feeds.conf
      ./scripts/feeds update -a
      ./scripts/feeds install -a
      # The feed hostapd keeps the vendor package name and overrides the fork core package.
      ./scripts/feeds install -f -p carlinkit hostapd
      cp /u2w/u2w.config .config
      make defconfig
      for c in TARGET_armv7_3_10 PACKAGE_dropbear PACKAGE_adbd PACKAGE_u2w-usb-debug PACKAGE_u2w-low-latency PACKAGE_u2w-mfid PACKAGE_hostapd \
               PACKAGE_libopenssl PACKAGE_zlib PACKAGE_libxml2 PACKAGE_libusb-1.0 PACKAGE_nand-utils \
               PACKAGE_libexpat PACKAGE_libdbus PACKAGE_dbus PACKAGE_mdnsd PACKAGE_libfdk-aac PACKAGE_hcid \
               PACKAGE_wpa-supplicant PACKAGE_wpa-cli PACKAGE_iw PACKAGE_usbmuxd PACKAGE_umtprd USE_GC_SECTIONS USE_LTO USE_STRIP; do
        grep -q "^CONFIG_$c=y$" .config || { echo "defconfig dropped CONFIG_$c" >&2; exit 1; }
      done
      grep -q "^CONFIG_TARGET_OPTIMIZATION=\"-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=softfp -Os -mthumb" .config
      grep -q "CONFIGURE_PREFIX:=/usr" include/package-defaults.mk
      if grep -R -E "^\+.*/opt/(etc|var|bin|sbin|lib|usr)" feeds/carlinkit/ --include="*.patch"; then
        echo "Unexpected /opt patch in the carlinkit feed" >&2; exit 1
      fi
    '
    ;;
  toolchain) run "make -j${JOBS:-4} tools/install toolchain/install V=s";;
  # Top-level packages (libnl/openssl follow as dependencies), or one named package.
  build)
    targets=$(printf 'package/%s/compile ' ${2:-dropbear adbd hostapd u2w-usb-debug u2w-low-latency u2w-mfid zlib libxml2 libusb mtd-utils iw usbmuxd umtprd expat dbus mdnsresponder fdk-aac bluez-hcid})
    run "make -j${JOBS:-4} $targets V=s";;
  collect)
    mkdir -p "$OUT"
    docker run --rm -v "$VOLUME:/entware:ro" -v "$OUT:/packages" "$IMAGE" bash -ec "
      arch=\$(sed -n 's/^CONFIG_TARGET_ARCH_PACKAGES=\"\\(.*\\)\"$/\\1/p' .config)
      for p in ${PACKAGES[*]}; do
        f=\$(find bin -type f -name \"\${p}_*_\$arch.ipk\")
        [ \$(echo \"\$f\" | grep -c .) = 1 ] || { echo \"Expected one \$p package: \$f\" >&2; exit 1; }
        cp \"\$f\" /packages/
      done
    "
    python3 "$TOOLS/audit.py" --report "$OUT/installed-sizes.json" "$OUT"/*.ipk
    ;;
  shell) run 'bash';;
  *) echo 'Usage: entware-docker-apple-silicon/build.sh setup|toolchain|build [package]|collect|shell' >&2; exit 2;;
esac
