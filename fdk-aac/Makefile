include $(TOPDIR)/rules.mk

# Drop-in for the vendor libfdk-aac 0.1.4 (soname libfdk-aac.so.1) that AppleCarPlay links:
# fdk-aac 0.1.6, the last 0.1.x release (same public ABI as 0.1.4; 2.x is soname .so.2).
# Patches: 010 keeps the 0.1.5+ error-path output clear inside callers that pass timeDataSize
# in bytes (AppleCarPlay), 020 restores AACENC_BITRATEMODE 8 for ELD (AppleCarPlay's ELD
# encoder fails to initialise on stock 0.1.5+), 030 backports an LD-SBR bounds check from 2.0.
PKG_NAME:=fdk-aac
PKG_VERSION:=0.1.6
PKG_RELEASE:=2
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=https://codeload.github.com/mstorsjo/fdk-aac/tar.gz/v$(PKG_VERSION)?
PKG_HASH:=adbcd793e406e1b88b3c1c41382d49f8c27371485b823c0fdab69c9124fd2ce3
PKG_LICENSE:=Fraunhofer-FDK-AAC-for-Android
PKG_LICENSE_FILES:=NOTICE
# The GitHub archive has no configure script.
PKG_FIXUP:=autoreconf
PKG_INSTALL:=1
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/libfdk-aac
  SECTION:=sound
  CATEGORY:=Sound
  TITLE:=Fraunhofer FDK AAC codec library (vendor 0.1.4 drop-in)
  URL:=https://github.com/mstorsjo/fdk-aac
endef

define Package/libfdk-aac/description
  fdk-aac 0.1.6 under the vendor file names (/usr/lib/libfdk-aac.so.1.0.0, soname
  libfdk-aac.so.1), patched for the AppleCarPlay call pattern.
endef

CONFIGURE_ARGS += --disable-static --disable-example

# The library is C++ built with -fno-exceptions -fno-rtti and linked by the C driver (upstream
# Makefile.am), so it needs no libstdc++; --as-needed also drops the unused libgcc_s, leaving
# NEEDED libm.so.6 + libc.so.6 like the vendor file.
TARGET_LDFLAGS += -Wl,--as-needed

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include $(1)/usr/lib/pkgconfig
	$(CP) $(PKG_INSTALL_DIR)/usr/include/fdk-aac $(1)/usr/include/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libfdk-aac.so* $(1)/usr/lib/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/pkgconfig/fdk-aac.pc $(1)/usr/lib/pkgconfig/
endef

# Upstream 0.1.6 builds libfdk-aac.so.1.0.1 (version-info 1:1:0); install it under the vendor
# name with the vendor's two symlinks.
define Package/libfdk-aac/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/lib/libfdk-aac.so.1.0.1 $(1)/usr/lib/libfdk-aac.so.1.0.0
	$(LN) libfdk-aac.so.1.0.0 $(1)/usr/lib/libfdk-aac.so.1
	$(LN) libfdk-aac.so.1.0.0 $(1)/usr/lib/libfdk-aac.so
endef

$(eval $(call BuildPackage,libfdk-aac))
