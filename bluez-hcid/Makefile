include $(TOPDIR)/rules.mk

# Drop-in /usr/sbin/hcid for the U2W: BlueZ 3.36 (bluez-utils-3.36, the tree the vendor
# built: "bluez-arm/bluez-utils-3.36" RPATH, version string 3.36), with the Carlinkit
# delta re-implemented from the vendor binary and the SDP server security fixes of
# BlueZ 5.x backported. bluetoothDaemon copies it to /tmp/bin and runs "hcid -x -n -m 8196".
# Vendor contract kept: /etc/bluetooth/hcid.conf, storage in /tmp/bluetooth,
# /tmp/bluetooth_status pairing hooks, GetDeiceEIR on the experimental org.bluez.Adapter,
# every SSP confirmation forwarded to the agent, plugin dir /lib/bluetooth/plugins.
# Links the vendor /usr/lib/libbluetooth.so.2 (bluez-libs 3.36, stays in the image; built
# here from the same release only to link, not shipped) and libdbus-1.so.3 (feed dbus).
# kernel.org no longer carries the 3.x archives; the Debian originals are byte-identical.
PKG_NAME:=bluez-hcid
PKG_VERSION:=3.36
PKG_RELEASE:=1
PKG_SOURCE:=bluez-utils_$(PKG_VERSION).orig.tar.gz
PKG_SOURCE_URL:=https://snapshot.debian.org/archive/debian/20080801T000000Z/pool/main/b/bluez-utils/ \
	http://archive.debian.org/debian/pool/main/b/bluez-utils/
PKG_HASH:=d5781c9b6604c63720020169df4081c364f3f8e79df951db0421e89667fb891d
PKG_BUILD_DIR:=$(BUILD_DIR)/bluez-utils-$(PKG_VERSION)
PKG_LICENSE:=GPL-2.0-or-later
PKG_LICENSE_FILES:=COPYING
PKG_CPE_ID:=cpe:/a:bluez:bluez
PKG_BUILD_FLAGS:=gc-sections lto

BLUEZ_LIBS_SOURCE:=bluez-libs_$(PKG_VERSION).orig.tar.gz
BLUEZ_LIBS_DIR:=$(PKG_BUILD_DIR)/bluez-libs-$(PKG_VERSION)
BLUEZ_LIBS_ROOT:=$(PKG_BUILD_DIR)/bluez-libs-root

include $(INCLUDE_DIR)/package.mk

define Download/bluez-libs
  FILE:=$(BLUEZ_LIBS_SOURCE)
  URL:=https://snapshot.debian.org/archive/debian/20080801T000000Z/pool/main/b/bluez-libs/ \
	http://archive.debian.org/debian/pool/main/b/bluez-libs/
  HASH:=692f994d8479143c18e781f9afa6c8265770a5affbf2e6147edc70b78aa3bfae
endef
$(eval $(call Download,bluez-libs))

define Package/bluez-hcid/Default
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=Bluetooth
  URL:=http://www.bluez.org/
endef

define Package/hcid
$(call Package/bluez-hcid/Default)
  TITLE:=BlueZ 3.36 hcid (vendor drop-in, SDP server fixes)
  DEPENDS:=+libdbus
endef

define Package/hciconfig
$(call Package/bluez-hcid/Default)
  TITLE:=BlueZ 3.36 hciconfig (separate package, off by default)
endef

define Package/hcitool
$(call Package/bluez-hcid/Default)
  TITLE:=BlueZ 3.36 hcitool (separate package, off by default)
endef

define Package/hcid/description
 Replaces the vendor /usr/sbin/hcid (BlueZ 3.36 with the Carlinkit changes). Same
 D-Bus API, config and storage paths as the vendor build; the SDP server gets the
 continuation-state and request bounds fixes from BlueZ 5.x.
endef

# libbluetooth.so.2 is the vendor library (bluez-libs 3.36, same soname/ABI as the one
# built here to link); no package of this build provides it.
define Package/hcid/extra_provides
	echo 'libbluetooth.so.2'
endef
Package/hciconfig/extra_provides=$(Package/hcid/extra_provides)
Package/hcitool/extra_provides=$(Package/hcid/extra_provides)

define Build/Prepare
	$(call Build/Prepare/Default)
	$(HOST_TAR) -C $(PKG_BUILD_DIR) -xzf $(DL_DIR)/$(BLUEZ_LIBS_SOURCE)
endef

# bluez-libs: link-time only, installed into a private root (never staged or shipped).
# Its sdp.c trips -Werror=format-security; old libtool does not pass LTO to shared links.
BLUEZ_LIBS_CONFIGURE_VARS:= \
	CFLAGS="$(TARGET_CFLAGS) -fno-lto -Wno-error=format-security" \
	LDFLAGS="$(TARGET_LDFLAGS) -fno-lto"

# hcid only: eglib (no GLib, like the vendor build, which exports its g_* symbols with
# -rdynamic), no plugins, audio, input, network, serial, cups or extra daemons.
# dbus_watch_get_fd is used instead of dbus_watch_get_unix_fd, so hcid imports only
# libdbus symbols the vendor D-Bus 1.0.2 library also has. PIE like the vendor binary
# (the configure probe fails under the package build and would drop it silently).
CONFIGURE_ARGS += \
	--libdir=/lib \
	--disable-fortify \
	--enable-pie \
	--disable-glib \
	--disable-network \
	--disable-serial \
	--disable-input \
	--disable-audio \
	--disable-gstreamer \
	--disable-alsa \
	--disable-usb \
	--disable-netlink \
	--enable-tools \
	--disable-bccmd \
	--disable-hid2hci \
	--disable-dfutool \
	--disable-hidd \
	--disable-pand \
	--disable-dund \
	--disable-cups \
	--disable-test \
	--disable-manpages \
	--disable-configfiles \
	--disable-initscripts \
	--disable-pcmciarules

CONFIGURE_VARS += \
	BLUEZ_CFLAGS="-I$(BLUEZ_LIBS_ROOT)/usr/include" \
	BLUEZ_LIBS="-L$(BLUEZ_LIBS_ROOT)/usr/lib -lbluetooth" \
	ac_cv_lib_dbus_1_dbus_watch_get_unix_fd=no \
	ac_cv_prog_cc_pie=yes

# common/ includes <bluetooth/*.h> without BLUEZ_CFLAGS. --as-needed drops the libgcc_s
# the toolchain adds otherwise: NEEDED stays libbluetooth, libdbus-1, libdl, libc.
TARGET_CPPFLAGS += -I$(BLUEZ_LIBS_ROOT)/usr/include
TARGET_LDFLAGS += -L$(BLUEZ_LIBS_ROOT)/usr/lib -Wl,-rpath-link=$(BLUEZ_LIBS_ROOT)/usr/lib \
	-Wl,--as-needed

define Build/Configure
	$(call Build/Configure/Default,--libdir=/usr/lib --disable-static --enable-shared --disable-fortify,$(BLUEZ_LIBS_CONFIGURE_VARS),bluez-libs-$(PKG_VERSION))
	$(MAKE) -C $(BLUEZ_LIBS_DIR)
	$(MAKE) -C $(BLUEZ_LIBS_DIR) install DESTDIR=$(BLUEZ_LIBS_ROOT)
	rm -f $(BLUEZ_LIBS_ROOT)/usr/lib/libbluetooth.la
	$(call Build/Configure/Default)
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR)/eglib
	$(MAKE) -C $(PKG_BUILD_DIR)/gdbus
	$(MAKE) -C $(PKG_BUILD_DIR)/common libhelper.a
	$(MAKE) -C $(PKG_BUILD_DIR)/sdpd
	$(MAKE) -C $(PKG_BUILD_DIR)/hcid hcid
	$(MAKE) -C $(PKG_BUILD_DIR)/tools hciconfig hcitool
endef

# Vendor paths and names (the donor ships all three in /usr/sbin).
define Package/hcid/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/hcid/hcid $(1)/usr/sbin/hcid
endef

define Package/hciconfig/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/tools/hciconfig $(1)/usr/sbin/hciconfig
endef

define Package/hcitool/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/tools/hcitool $(1)/usr/sbin/hcitool
endef

$(eval $(call BuildPackage,hcid))
$(eval $(call BuildPackage,hciconfig))
$(eval $(call BuildPackage,hcitool))
