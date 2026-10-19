include $(TOPDIR)/rules.mk

# From OpenWrt packages libs/libimobiledevice-glue, at the 1.3.3 release.
# Dependency of the separate usbmuxd package; not in the image.
PKG_NAME:=libimobiledevice-glue
PKG_VERSION:=1.3.3
PKG_RELEASE:=1
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.bz2
PKG_SOURCE_URL:=https://github.com/libimobiledevice/libimobiledevice-glue/releases/download/$(PKG_VERSION)
PKG_HASH:=920ce01382a32695f49b23292b4979a03f0afd16c58e8755d8b7f41804acc1a9
PKG_LICENSE:=LGPL-2.1-or-later
PKG_INSTALL:=1
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/libimobiledevice-glue
  SECTION:=libs
  CATEGORY:=Libraries
  SUBMENU:=libimobiledevice
  TITLE:=Common code used by libimobiledevice and usbmuxd
  URL:=https://www.libimobiledevice.org/
  DEPENDS:=+libplist
endef

CONFIGURE_ARGS += --disable-static

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include $(1)/usr/lib/pkgconfig
	$(CP) $(PKG_INSTALL_DIR)/usr/include/libimobiledevice-glue $(1)/usr/include/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libimobiledevice-glue-1.0.so* $(1)/usr/lib/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/pkgconfig/libimobiledevice-glue-1.0.pc $(1)/usr/lib/pkgconfig/
endef

define Package/libimobiledevice-glue/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libimobiledevice-glue-1.0.so.* $(1)/usr/lib/
endef

$(eval $(call BuildPackage,libimobiledevice-glue))
