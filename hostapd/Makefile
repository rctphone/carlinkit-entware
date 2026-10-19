include $(TOPDIR)/rules.mk

# Drop-in /usr/sbin/hostapd for the U2W on the 6.18 kernel (88x2bs.ko in cfg80211 mode):
# WPA2/WPA3-Personal transition (SAE + PMF), AP only. It keeps the vendor contract: reads
# /etc/hostapd.conf, builds the Apple CarPlay IE itself and writes /tmp/wifi_status and
# /tmp/wifi_connection_list like the vendor 2021 RTL and 2024 BCM v2.7 hostapds.
# Source is the hostap_2_12 release tag with only the U2W patch: none of the fork's
# package/network/services/hostapd patches or its src/ overlay (ubus/ucode/multicall/mesh/
# RADIUS/TLS-variant infrastructure, or code these configs do not compile) are needed.
# Vendor sonames throughout: libnl-3.so.200 / libnl-genl-3.so.200 (as in 2024)
# and libcrypto.so.1.1 from the fork's libopenssl, shared with the packed CarPlay daemons.
# The same source also builds wpa_supplicant/wpa_cli: separate packages, not in the image.
PKG_NAME:=hostapd
PKG_RELEASE:=1
PKG_SOURCE_URL:=https://w1.fi/hostap.git
PKG_SOURCE_PROTO:=git
PKG_SOURCE_DATE:=2026-08-07
# hostap_2_12
PKG_SOURCE_VERSION:=831364bf02710ad09c2f27d3efa92abeeb5634c0
PKG_MIRROR_HASH:=d1b9ca0e6aaaba928fc5d5b02c0084083e745a38ae1214d0e2dc5d1ea6d50393
PKG_LICENSE:=BSD-3-Clause
PKG_CPE_ID:=cpe:/a:w1.fi:hostapd
PKG_BUILD_PARALLEL:=1
PKG_BUILD_FLAGS:=gc-sections lto

include $(INCLUDE_DIR)/package.mk

define Package/hostapd
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=WirelessAPD
  TITLE:=hostapd (vendor drop-in) with WPA2/WPA3 transition, PMF and CarPlay IE
  URL:=https://w1.fi/hostapd/
  DEPENDS:=+libnl-genl +libopenssl
  VARIANT:=hostapd
endef

define Package/wpa-supplicant
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=WirelessAPD
  TITLE:=wpa_supplicant (separate package): WPA2/WPA3-Personal station
  URL:=https://w1.fi/wpa_supplicant/
  DEPENDS:=+libnl-genl +libopenssl
  VARIANT:=wpa_supplicant
endef

define Package/wpa-cli
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=WirelessAPD
  TITLE:=wpa_supplicant command-line client
  DEPENDS:=+wpa-supplicant
  VARIANT:=wpa_supplicant
endef

define Package/hostapd/description
 Replaces the vendor /usr/sbin/hostapd. Needs the 6.18 kernel with 88x2bs.ko built
 for cfg80211. install-firmware-profile.py checks the module and edits the security
 and driver lines of /etc/hostapd.conf; every other vendor line is kept.
endef

define Build/Configure
	$(CP) ./files/$(BUILD_VARIANT)-u2w.config $(PKG_BUILD_DIR)/$(BUILD_VARIANT)/.config
endef

TARGET_CPPFLAGS := \
	-I$(STAGING_DIR)/usr/include/libnl3 \
	-I$(PKG_BUILD_DIR)/src/crypto \
	$(TARGET_CPPFLAGS) \
	-I$(STAGING_DIR)/usr/include \
	-DCONFIG_LIBNL20 \
	-D_GNU_SOURCE

# LIBS on the make command line replaces hostapd's own "LIBS +=" lines, so every library is
# named here. Without EAP, SAE uses crypto_openssl.c alone, so libcrypto without libssl.
define Build/Compile
	CFLAGS="$(TARGET_CPPFLAGS) $(TARGET_CFLAGS)" \
	$(MAKE) $(PKG_JOBS) -C $(PKG_BUILD_DIR)/$(BUILD_VARIANT) \
		$(TARGET_CONFIGURE_OPTS) \
		CONFIG_DRIVER_NL80211=y CONFIG_TLS=openssl CONFIG_SAE=y \
		LIBS="$(TARGET_LDFLAGS) -L$(STAGING_DIR)/usr/lib -lnl-3 -lnl-genl-3 -lcrypto -lm" \
		LIBS_c="$(TARGET_LDFLAGS)" \
		AR="$(TARGET_CROSS)gcc-ar" \
		BCHECK= \
		$(if $(findstring s,$(OPENWRT_VERBOSE)),V=1) \
		$(if $(filter hostapd,$(BUILD_VARIANT)),hostapd hostapd_cli,wpa_supplicant wpa_cli)
endef

define Package/hostapd/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/hostapd/hostapd $(1)/usr/sbin/hostapd
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/hostapd/hostapd_cli $(1)/usr/sbin/hostapd_cli
endef

define Package/wpa-supplicant/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/wpa_supplicant/wpa_supplicant $(1)/usr/sbin/wpa_supplicant
endef

define Package/wpa-cli/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/wpa_supplicant/wpa_cli $(1)/usr/sbin/wpa_cli
endef

$(eval $(call BuildPackage,hostapd))
$(eval $(call BuildPackage,wpa-supplicant))
$(eval $(call BuildPackage,wpa-cli))
