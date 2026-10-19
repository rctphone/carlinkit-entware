include $(TOPDIR)/rules.mk

# From OpenWrt packages utils/dbus (1.16.2), as a drop-in for the vendor D-Bus 1.0.2 under the
# vendor names: /usr/sbin/dbus-daemon (bluetoothDaemon copies it to /tmp/bin and runs
# "dbus-daemon --system --print-pid --print-address"), /usr/lib/libdbus-1.so.3 for hcid,
# hfpd, bluetoothDaemon and ARMadb-driver (vendor-imports.txt). The vendor /etc/dbus-1
# system.conf and system.d policies stay and are read unchanged (patch 100). Like the
# vendor build: runs as root, no inotify reload, no service activation, no stats/verbose.
# Checks are compiled out: 1.16 adds client-side checks 1.0.2 lacked (UTF-8 on append), and
# a failed check aborts the calling vendor client by default; the bus still validates.
PKG_NAME:=dbus
PKG_VERSION:=1.16.2
PKG_RELEASE:=1
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.xz
PKG_SOURCE_URL:=https://dbus.freedesktop.org/releases/dbus
PKG_HASH:=0ba2a1a4b16afe7bceb2c07e9ce99a8c2c3508e5dec290dbb643384bd6beb7e2
PKG_LICENSE:=AFL-2.1 GPL-2.0-or-later
PKG_LICENSE_FILES:=COPYING
PKG_CPE_ID:=cpe:/a:freedesktop:dbus
PKG_BUILD_DEPENDS:=expat
PKG_BUILD_PARALLEL:=1
PKG_INSTALL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/meson.mk

define Package/dbus/Default
  URL:=https://dbus.freedesktop.org/
endef

define Package/libdbus
$(call Package/dbus/Default)
  SECTION:=libs
  CATEGORY:=Libraries
  TITLE:=D-Bus library (vendor libdbus-1.so.3 replacement)
endef

define Package/dbus
$(call Package/dbus/Default)
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=D-Bus message bus daemon (vendor dbus-daemon replacement)
  DEPENDS:=+libdbus +libexpat
endef

define Package/dbus-utils
$(call Package/dbus/Default)
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=D-Bus dbus-send and dbus-monitor (off by default)
  DEPENDS:=+libdbus
endef

MESON_ARGS += \
	-Druntime_dir=/var/run \
	-Dsystem_socket=/var/run/dbus/system_bus_socket \
	-Dsystem_pid_file=/tmp/dbus/pid \
	-Dsession_socket_dir=/tmp \
	-Ddbus_user=root \
	-Dasserts=false \
	-Dchecks=false \
	-Dverbose_mode=false \
	-Dstats=false \
	-Dmessage_bus=true \
	-Dtools=true \
	-Dtraditional_activation=false \
	-Duser_session=false \
	-Dinotify=disabled \
	-Depoll=enabled \
	-Dkqueue=disabled \
	-Dlaunchd=disabled \
	-Dsystemd=disabled \
	-Dselinux=disabled \
	-Dapparmor=disabled \
	-Dlibaudit=disabled \
	-Dx11_autolaunch=disabled \
	-Drelocation=disabled \
	-Ddoxygen_docs=disabled \
	-Dxml_docs=disabled \
	-Dducktype_docs=disabled \
	-Dqt_help=disabled \
	-Dmodular_tests=disabled \
	-Dintrusive_tests=false \
	-Dinstalled_tests=false

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include $(1)/usr/lib/pkgconfig $(1)/usr/lib/dbus-1.0/include/dbus
	$(CP) $(PKG_INSTALL_DIR)/usr/include/dbus-1.0 $(1)/usr/include/
	$(INSTALL_DATA) $(PKG_INSTALL_DIR)/usr/lib/dbus-1.0/include/dbus/*.h $(1)/usr/lib/dbus-1.0/include/dbus/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libdbus-1.so* $(1)/usr/lib/
	$(INSTALL_DATA) $(PKG_INSTALL_DIR)/usr/lib/pkgconfig/dbus-1.pc $(1)/usr/lib/pkgconfig/
endef

# The real file keeps its upstream name (libdbus-1.so.3.38.x); the vendor libdbus-1.so.3.2.0
# and its libdbus-1.so dev link are pruned by the image installer.
define Package/libdbus/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libdbus-1.so.* $(1)/usr/lib/
endef

# The daemon links the shared libdbus (its private _dbus_* symbols are exported under
# LIBDBUS_PRIVATE_1.16.2, so daemon and library must come from the same build).
# Vendor path /usr/sbin; vendor configs stay.
define Package/dbus/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/dbus-daemon $(1)/usr/sbin/
endef

define Package/dbus-utils/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/dbus-send $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/dbus-monitor $(1)/usr/bin/
endef

$(eval $(call BuildPackage,libdbus))
$(eval $(call BuildPackage,dbus))
$(eval $(call BuildPackage,dbus-utils))
