include $(TOPDIR)/rules.mk
PKG_NAME:=u2w-low-latency
PKG_VERSION:=1.0.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-only
PKGARCH:=all
include $(INCLUDE_DIR)/package.mk

# Runtime half of the low-latency set. install-firmware-profile.py applies the static half when
# this package is installed: 88x2bs.ko rtw_vht_enable=2 rtw_bw_mode=0x11 (VHT40), Wi-Fi channel
# 44, dtim_period=1, MediaLatency 500. Kernel half: kernel/profiles/low-latency.patch.
define Package/u2w-low-latency
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=U2W low-latency CarPlay settings (CPU idle, daemon priorities, NCM timer)
endef

define Build/Compile
endef

define Package/u2w-low-latency/install
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/u2w-low-latency.init $(1)/etc/init.d/u2w-low-latency
endef

$(eval $(call BuildPackage,u2w-low-latency))
