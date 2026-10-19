include $(TOPDIR)/rules.mk

# adbd from the same AOSP system/core commit as Entware's host "adb" package. The tarball is
# our own: Entware's pinned hash for adb-5.0.2~6fe92d1a no longer matches a fresh git archive
# of that commit (no mirror copy exists), so this package pins the archive it actually built.
PKG_NAME:=adbd
PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://android.googlesource.com/platform/system/core
PKG_SOURCE_VERSION:=6fe92d1a3fb17545d82d020a3c995f32e6b71f9d
PKG_VERSION:=5.0.2~$(call version_abbrev,$(PKG_SOURCE_VERSION))
PKG_RELEASE:=5
PKG_FILE_DEPENDS:=$(CURDIR)/src $(CURDIR)/patches
PKG_MIRROR_HASH:=0fadd8b8fd597646f0f1b89fa24efe60042f3a8200ee026646f093fcdb9466fb
PKG_UNPACK=$(HOST_TAR) -C $(PKG_BUILD_DIR) --strip-components=1 -xf $(DL_DIR)/$(PKG_SOURCE)
PKG_LICENSE:=Apache-2.0
PKG_LICENSE_FILES:=adb/NOTICE

include $(INCLUDE_DIR)/package.mk

define Package/adbd
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=ADB daemon for the U2W (FunctionFS USB only)
  URL:=https://developer.android.com/tools/adb
  DEPENDS:=
endef

define Package/adbd/description
 adbd from AOSP system/core android-5.0.2 with Linux shims. USB over FunctionFS
 (/dev/usb-ffs/adb) is the only transport: no TCP listener, no fallback, no switch;
 without ep0 the daemon exits. This private development image disables RSA
 authentication (ro.adb.secure=0). The shell is /bin/sh.
endef

define Build/Prepare
	# Build/Prepare/Default already applies ./patches (both the vendor policy
	# patch and 001-usb-only); applying PatchDir again would duplicate them.
	$(call Build/Prepare/Default)
	$(INSTALL_DIR) $(PKG_BUILD_DIR)/adb/u2w
	$(CP) ./src/. $(PKG_BUILD_DIR)/adb/u2w/
	$(CP) ./src/Makefile $(PKG_BUILD_DIR)/adb/Makefile.u2w
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR)/adb -f Makefile.u2w \
		$(TARGET_CONFIGURE_OPTS) \
		CFLAGS="$(TARGET_CFLAGS)" \
		CPPFLAGS="$(TARGET_CPPFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)"
endef

define Package/adbd/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/adb/adbd $(1)/usr/sbin/adbd
	$(INSTALL_BIN) ./files/logcat $(1)/usr/bin/logcat
endef

$(eval $(call BuildPackage,adbd))
