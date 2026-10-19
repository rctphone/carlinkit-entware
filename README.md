# carlinkit-entware

Source-only [Entware](https://github.com/Entware/Entware) feed, build scripts and kernel
patches for the **Carlinkit U2W** wireless CarPlay dongle (NXP i.MX6ULL, Cortex-A7,
RTL8822BS Wi-Fi/BT, vendor glibc 2.20 rootfs).

The packages replace or add userspace pieces of the vendor firmware with current
open-source builds: same binary names, paths and sonames as the vendor files they replace,
so the vendor CarPlay daemons keep working unchanged.

**No vendor binaries here.** This repository has only open-source code, patches against it
and our own scripts. There are no firmware images, `.ipk`/`.ko` files, donor rootfs files or
flash dumps. Everything upstream is downloaded at build time from pinned URLs/commits with
hashes (`PKG_HASH`/`PKG_MIRROR_HASH` in each package Makefile, `kernel/sources.lock.json`
for the kernel bundle).

## Packages

Each top-level directory is an OpenWrt/Entware package (`Makefile`, plus `files/`, `src/`,
`patches/` where needed).

| Package | What it builds |
|---|---|
| `adbd` | AOSP 5.0.2 adbd, FunctionFS USB only: no TCP listener, no fallback |
| `hostapd` | hostap 2.12 with the CarPlay vendor-contract patch (vendor `/etc/hostapd.conf` keys, WPA2/WPA3 SAE, PMF); also `wpa-supplicant`/`wpa-cli` |
| `dbus` | D-Bus 1.16.2 daemon and libdbus-1.so.3; `--system` reads the vendor config path |
| `expat` | libexpat 2.9.0 for dbus |
| `mdnsresponder` | Apple mDNSResponder 2881.120.11 (`mdnsd`, `libdns_sd.so`) with DirectLink interfaces and a per-interface `_airplay`/`_carplay-ctrl` filter |
| `fdk-aac` | fdk-aac 0.1.6 with fixes for the call patterns of the CarPlay audio path |
| `bluez-hcid` | BlueZ 3.36 `hcid` with the vendor-compatible D-Bus/status extensions and SDP-server bounds fixes |
| `libplist` | libplist 2.8.0 (for usbmuxd) |
| `libimobiledevice-glue` | libimobiledevice-glue 1.3.3 (for usbmuxd) |
| `usbmuxd` | usbmuxd (git 2025-02), no preflight |
| `umtprd` | uMTP-Responder 1.8.1, MTP over FunctionFS |
| `u2w-usb-debug` | USB supervisor (ADB/NCM gadget), USB-stick hotplug, `g_iphone` reload hook, `u2w-diag` |
| `u2w-low-latency` | init script: no deep CPU idle, background daemons at nice 19, cdc_ncm TX timer 0 |
| `u2w-mfid` | `mfid`: the MFi coprocessor (I2C) served over TCP on USB NCM |

## Building

The target is the EE71 Entware fork,
[rctphone/Entware-EE71-glibc2.17](https://github.com/rctphone/Entware-EE71-glibc2.17)
(`armv7-3.10`, Cortex-A7 softfp, glibc 2.17 so binaries run on the vendor glibc 2.20).
`entware-docker-apple-silicon/` has the Debian build image and a wrapper that clones the fork
at a pinned commit into a docker volume and mounts this repository as the `carlinkit` feed:

```sh
entware-docker-apple-silicon/build.sh setup        # build image, pinned fork, feeds, defconfig (u2w.config)
entware-docker-apple-silicon/build.sh toolchain
entware-docker-apple-silicon/build.sh build        # all packages, or: build u2w-usb-debug
entware-docker-apple-silicon/build.sh collect      # export .ipk to packages/, run audit.py
```

`IMAGE`, `VOLUME`, `ENTWARE_COMMIT`, `JOBS` and `OUT` can be overridden from the
environment. `audit.py` checks the exported packages without running them: no `/opt` paths,
no shipped keys, soft-float ELF, and every symbol listed in `vendor-imports.txt` is still
exported by the replacement libraries (the file lists symbol names only).

## Using the feed

In any Entware/OpenWrt tree, add to `feeds.conf` (pin a commit, not a branch):

```
src-git carlinkit https://github.com/rctphone/carlinkit-entware.git^<commit>
```

or `src-link carlinkit /path/to/carlinkit-entware` for a local checkout, then:

```sh
./scripts/feeds update carlinkit
./scripts/feeds install -a -p carlinkit
./scripts/feeds install -f -p carlinkit hostapd   # override the core hostapd package
```

## Kernel patches

`kernel/profiles/` holds patches, config fragments and DTS includes on top of ludwig-v's
Linux 6.18 port for the dongle. The bundle itself is **not** copied here; it is pinned in
[`kernel/sources.lock.json`](kernel/sources.lock.json) (`carlinkit_repository`,
`carlinkit_commit`, the sha256 of the bundle `Kernel/SHA256SUMS`, and the NXP kernel commit).
Bundle files at that commit:

- [Kernel/README.md](https://raw.githubusercontent.com/ludwig-v/wireless-carplay-dongle-reverse-engineering/b437135316275a5d3c375bdd1ceac2a6bcf1315b/Kernel/README.md)
- [Kernel/SHA256SUMS](https://raw.githubusercontent.com/ludwig-v/wireless-carplay-dongle-reverse-engineering/b437135316275a5d3c375bdd1ceac2a6bcf1315b/Kernel/SHA256SUMS)
  (sha256 `aaec4f944ad7ef9e12879b42dd6de4ad2edeee1a570439a2a5302781a5a3c940`)
- [Kernel/patches/series](https://raw.githubusercontent.com/ludwig-v/wireless-carplay-dongle-reverse-engineering/b437135316275a5d3c375bdd1ceac2a6bcf1315b/Kernel/patches/series)
- [Kernel/config/imx6ull-carlinkit-6.18.config](https://raw.githubusercontent.com/ludwig-v/wireless-carplay-dongle-reverse-engineering/b437135316275a5d3c375bdd1ceac2a6bcf1315b/Kernel/config/imx6ull-carlinkit-6.18.config)
- [Kernel/new-files/arch/arm/boot/dts/nxp/imx/imx6ull-carlinkit.dts](https://raw.githubusercontent.com/ludwig-v/wireless-carplay-dongle-reverse-engineering/b437135316275a5d3c375bdd1ceac2a6bcf1315b/Kernel/new-files/arch/arm/boot/dts/nxp/imx/imx6ull-carlinkit.dts)
- [Kernel/new-files/drivers/usb/gadget/legacy/iphone.c](https://raw.githubusercontent.com/ludwig-v/wireless-carplay-dongle-reverse-engineering/b437135316275a5d3c375bdd1ceac2a6bcf1315b/Kernel/new-files/drivers/usb/gadget/legacy/iphone.c)
- [Kernel/new-files/drivers/net/wireless/rtl8822bs/](https://github.com/ludwig-v/wireless-carplay-dongle-reverse-engineering/tree/b437135316275a5d3c375bdd1ceac2a6bcf1315b/Kernel/new-files/drivers/net/wireless/rtl8822bs)

Build order: NXP `linux-imx` at `kernel_commit`, copy the bundle `new-files/`, apply the bundle
`patches/series` with [`kernel/apply-bundle.py`](kernel/apply-bundle.py) (recounts stale hunk
headers, keeps NXP's newer role-switch guard in 0010), then for each profile apply
`<profile>.patch`, append `<profile>.dts.inc` to `imx6ull-carlinkit.dts`, and merge
`<profile>.config` into the bundle config.

| Profile | Purpose |
|---|---|
| `usb-adb` | g_iphone with NCM and FunctionFS ADB, vendor-equivalent DT fixes (includes the Wi-Fi changes) |
| `wifi-cfg80211` | RTL8822BS driver in cfg80211 mode with PMF; the vendor WEXT AP path is unchanged |
| `adb-coexist` | FunctionFS ADB inside g_iphone: CarPlay and ADB on one USB port (generated by `kernel/gen-adb-coexist.py`) |
| `hnp-interval` | stock `hnp_interval_ms` parameter for the role-switch delay |
| `ncm-addr` | fixed NCM `dev_addr`/`host_addr` module parameters |
| `low-latency` | SDIO Wi-Fi RX in the SCHED_FIFO irq thread, 88x2bs at -O2 without debug logging |
| `iap2-wait` | `/dev/ipod_iap2` on a wait queue instead of busy polling |
| `usb-dev` | USB device/OTG config and DT pin fixes |
| `usb-adb-qemu` | usb-adb variant for QEMU testing |

The image profile `usb-adb-wifi` is `usb-adb` + `adb-coexist` + `hnp-interval` + `ncm-addr` +
`low-latency` + `iap2-wait`. `check-usb-dev.sh` and `check-wifi-cfg80211.sh` verify a profile's
config and DTB delta against a prepared tree; they expect the u2w-build layout (`/work`,
`/kernel-cache`).

## License

Our own code (the `u2w-*` packages, build scripts, `audit.py`, `apply-bundle.py`) is under the
GNU General Public License v2.0 only (GPL-2.0-only), see [LICENSE](LICENSE). Exception:
`adbd/src` is compiled into AOSP adbd and is therefore under adbd's Apache-2.0 license
(Apache-2.0 and GPL-2.0 cannot be combined in one binary). Each package Makefile states the upstream
license in `PKG_LICENSE`. Patches to upstream software are under that software's license:
hostapd (BSD-3-Clause), BlueZ and the Linux kernel patches (GPL-2.0), D-Bus (AFL-2.1 or
GPL-2.0-or-later), mDNSResponder (Apache-2.0), fdk-aac (Fraunhofer FDK AAC license), adbd
(Apache-2.0).
