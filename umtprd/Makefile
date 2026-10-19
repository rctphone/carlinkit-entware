include $(TOPDIR)/rules.mk

# uMTP-Responder: current, small C MTP server over FunctionFS, replacing the vendor mtp-server
# (which needs the vendor g_android_accessory gadget, absent on 6.18). Separate package, not in
# the image and never started; using it needs an ffs.mtp function in the USB gadget.
PKG_NAME:=umtprd
PKG_VERSION:=1.8.1
PKG_RELEASE:=1
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=https://github.com/viveris/uMTP-Responder/archive/refs/tags/
PKG_SOURCE_URL_FILE:=umtprd-$(PKG_VERSION).tar.gz
PKG_HASH:=6f57f61a1993059bcdd5f598d8c2a3888991497ef42ba3594947f00bd7746b1f
PKG_BUILD_DIR:=$(BUILD_DIR)/uMTP-Responder-umtprd-$(PKG_VERSION)
PKG_LICENSE:=GPL-3.0-or-later

include $(INCLUDE_DIR)/package.mk

define Package/umtprd
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=uMTP-Responder MTP server over FunctionFS (off by default)
  URL:=https://github.com/viveris/uMTP-Responder
endef

# Command-line CFLAGS/LDFLAGS replace the upstream ones (-O3, -s), so restate its needs. The
# toolchain has 3.10 kernel headers (no FunctionFS v2): use the v1 descriptors 6.18 still accepts.
define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) $(TARGET_CONFIGURE_OPTS) \
		CFLAGS="$(TARGET_CPPFLAGS) $(TARGET_CFLAGS) -I./inc -DUSE_SYSLOG -DOLD_FUNCTIONFS_DESCRIPTORS" \
		LDFLAGS="$(TARGET_LDFLAGS) -lpthread -lrt"
endef

define Package/umtprd/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/etc/umtprd
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/umtprd $(1)/usr/sbin/umtprd
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/conf/umtprd.conf $(1)/etc/umtprd/umtprd.conf
endef

$(eval $(call BuildPackage,umtprd))
