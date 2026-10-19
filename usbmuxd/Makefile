include $(TOPDIR)/rules.mk

# From OpenWrt packages utils/usbmuxd (same pinned git commit), built without preflight, so it
# needs no libimobiledevice/libssl. Separate package, not in the image and never started at
# boot; the vendor 1.1.0 binary could not start (its libraries were never shipped).
PKG_NAME:=usbmuxd
PKG_RELEASE:=1
PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/libimobiledevice/usbmuxd.git
PKG_SOURCE_DATE:=2025-02-01
PKG_SOURCE_VERSION:=523f7004dce885fe38b4f80e34a8f76dc8ea98b5
PKG_MIRROR_HASH:=f340f0bbbe65135a9a9d52a4e28cd64b1285930560f999f156124dfde7b42850
PKG_LICENSE:=GPL-2.0-or-later
PKG_FIXUP:=autoreconf
PKG_INSTALL:=1
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/usbmuxd
  SECTION:=utils
  CATEGORY:=Utilities
  SUBMENU:=libimobiledevice
  TITLE:=USB multiplexing daemon for iPhone/iPod (off by default)
  URL:=https://www.libimobiledevice.org/
  DEPENDS:=+libusb-1.0 +libplist +libimobiledevice-glue
endef

CONFIGURE_ARGS += --without-preflight --without-systemd PACKAGE_VERSION=1.1.1+$(PKG_SOURCE_DATE)

define Package/usbmuxd/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/sbin/usbmuxd $(1)/usr/sbin/
endef

$(eval $(call BuildPackage,usbmuxd))
