include $(TOPDIR)/rules.mk

# From OpenWrt packages libs/libplist, at the 2.8.0 release (no libxml2: libplist 2.x has its
# own parser). Dependency of the separate usbmuxd package; not in the image.
PKG_NAME:=libplist
PKG_VERSION:=2.8.0
PKG_RELEASE:=1
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.bz2
PKG_SOURCE_URL:=https://github.com/libimobiledevice/libplist/releases/download/$(PKG_VERSION)
PKG_HASH:=b1f59f7634c58b2481325a23ff4e3bf51574a42d868cbe466d2b39b04550752a
PKG_LICENSE:=LGPL-2.1-or-later
PKG_INSTALL:=1
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/libplist
  SECTION:=libs
  CATEGORY:=Libraries
  SUBMENU:=libimobiledevice
  TITLE:=Apple property list library
  URL:=https://www.libimobiledevice.org/
endef

CONFIGURE_ARGS += --disable-static --without-cython --without-tools

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include $(1)/usr/lib/pkgconfig
	$(CP) $(PKG_INSTALL_DIR)/usr/include/plist $(1)/usr/include/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libplist-2.0*.so* $(1)/usr/lib/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/pkgconfig/libplist-2.0.pc $(1)/usr/lib/pkgconfig/
endef

define Package/libplist/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libplist-2.0.so.* $(1)/usr/lib/
endef

$(eval $(call BuildPackage,libplist))
