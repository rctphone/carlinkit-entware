include $(TOPDIR)/rules.mk
PKG_NAME:=u2w-usb-debug
PKG_VERSION:=1.0.0
PKG_RELEASE:=9
PKG_LICENSE:=GPL-2.0-only
PKGARCH:=all
include $(INCLUDE_DIR)/package.mk

define Package/u2w-usb-debug
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=U2W USB NCM management and USB ADB supervisor
  DEPENDS:=+adbd
endef

define Build/Compile
endef

define Package/u2w-usb-debug/install
	$(INSTALL_DIR) $(1)/script $(1)/etc/init.d $(1)/etc/mdev $(1)/usr/sbin
	$(INSTALL_BIN) ./files/start_usb_debug.sh $(1)/script/start_usb_debug.sh
	$(INSTALL_BIN) ./files/u2w-usb-debug.init $(1)/etc/init.d/u2w-usb-debug
	# Read-only CarPlay troubleshooting bundle (tools/u2w-diag.sh fetches it over Wi-Fi/USB).
	$(INSTALL_BIN) ./files/u2w-diag.sh $(1)/usr/sbin/u2w-diag
	# Replace both vendor USB-stick handlers (one script): detect the filesystem
	# type instead of assuming FAT.
	$(INSTALL_BIN) ./files/udisk_hotplug.sh $(1)/etc/mdev/udisk_hotplug.sh
	$(INSTALL_BIN) ./files/udisk_hotplug.sh $(1)/etc/mdev/udisk_insert.sh
	# mdev.conf: SUBSYSTEM=module;g_iphone ... $$/etc/mdev/g_iphone.sh
	$(INSTALL_BIN) ./files/g_iphone.sh $(1)/etc/mdev/g_iphone.sh
endef

$(eval $(call BuildPackage,u2w-usb-debug))
