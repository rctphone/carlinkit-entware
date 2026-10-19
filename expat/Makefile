include $(TOPDIR)/rules.mk

# From OpenWrt packages libs/expat, at the 2.9.0 release (fixes CVE-2026-102633, a 32-bit
# integer overflow, and CVE-2026-77214). Only the D-Bus 1.16 daemon uses it (config reader);
# the vendor 1.0.2 daemon used libxml2. No DTD/namespace support: D-Bus needs neither.
PKG_NAME:=expat
PKG_VERSION:=2.9.0
PKG_RELEASE:=1
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=https://github.com/libexpat/libexpat/releases/download/R_$(subst .,_,$(PKG_VERSION))
PKG_HASH:=16afbb9cefead2aa278105cf27d9f597bde7fbf3dbb85015857ca7ca6a4e89ba
PKG_LICENSE:=MIT
PKG_LICENSE_FILES:=COPYING
PKG_CPE_ID:=cpe:/a:libexpat_project:libexpat
CMAKE_INSTALL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/libexpat
  SECTION:=libs
  CATEGORY:=Libraries
  TITLE:=An XML parsing library
  URL:=https://libexpat.github.io/
endef

define Package/libexpat/description
 A fast, non-validating, stream-oriented XML parsing library.
endef

# glibc 2.17 has no getrandom(): default hash salts come from /dev/urandom (as Entware does on
# its 3.x kernels), not the time-based fallback. (dbus-daemon sets its own salt.)
CMAKE_OPTIONS += \
	-DEXPAT_BUILD_TOOLS=OFF \
	-DEXPAT_BUILD_EXAMPLES=OFF \
	-DEXPAT_BUILD_TESTS=OFF \
	-DEXPAT_BUILD_DOCS=OFF \
	-DEXPAT_BUILD_FUZZERS=OFF \
	-DEXPAT_DTD=OFF \
	-DEXPAT_NS=OFF \
	-DEXPAT_DEV_URANDOM=ON

define Package/libexpat/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libexpat.so.* $(1)/usr/lib/
endef

$(eval $(call BuildPackage,libexpat))
