include $(TOPDIR)/rules.mk

# Drop-in mdnsd + libdns_sd.so for the U2W: Apple mDNSResponder at the newest tag
# (mDNSResponder-2881.120.11) replacing the vendor 878.270.2 build, under the vendor contract:
# /usr/sbin/mdnsd, /usr/lib/libdns_sd.so (unversioned: the CarPlay daemons NEED "libdns_sd.so"),
# socket /var/run/mdnsd, /var/run/mdnsd.pid, /etc/mdnsd.conf (all upstream Posix defaults).
# The two vendor patches are ported: DirectLink for ncm*/usb*/wlan* and the per-interface
# _airplay/_carplay-ctrl filter (patches/1xx). No TLS (DNS-over-TLS would need mbedtls);
# unicast DNS and IPv6 stay enabled. mdns-utils (dns-sd) is a separate test package.
PKG_NAME:=mdnsresponder
PKG_VERSION:=2881.120.11
PKG_RELEASE:=1
PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/apple-oss-distributions/mDNSResponder
PKG_SOURCE_VERSION:=641cdf73ef46e1824895cbc44ad8ac7647da2479
PKG_MIRROR_HASH:=aff2d1362ec066c4f98847a30398876bb2ffe0a5717103078f16179e6348db1e
PKG_LICENSE:=Apache-2.0 BSD-3-Clause
PKG_LICENSE_FILES:=LICENSE
PKG_CPE_ID:=cpe:/a:apple:mdnsresponder
PKG_BUILD_FLAGS:=gc-sections lto

include $(INCLUDE_DIR)/package.mk

define Package/mdnsresponder/Default
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=IP Addresses and Names
  URL:=https://github.com/apple-oss-distributions/mDNSResponder
endef

define Package/mdnsd
$(call Package/mdnsresponder/Default)
  TITLE:=Apple mDNSResponder daemon and libdns_sd.so for the U2W
endef

define Package/mdnsd/description
 mDNSResponder-2881.120.11 mdnsd and its client library under the vendor names
 (/usr/sbin/mdnsd, /usr/lib/libdns_sd.so) with the Carlinkit DirectLink and
 per-interface AirPlay/CarPlay service filter patches.
endef

define Package/mdns-utils
$(call Package/mdnsresponder/Default)
  TITLE:=dns-sd test client for mdnsd (not in the image)
  DEPENDS:=+mdnsd
endef

# The upstream Makefile has no install target we can use (it installs libdns_sd.so.1 and
# restarts the daemon), so build the three targets and install by hand. Command-line
# variables also reach the Clients sub-make (dns-sd). Not parallel-safe upstream.
MDNS_MAKE_FLAGS:= \
	os=linux tls=no \
	CC="$(TARGET_CC)" LD="$(TARGET_CC)" STRIP=: \
	CFLAGS="$(TARGET_CPPFLAGS) $(TARGET_CFLAGS) -DmDNSResponderVersion=$(PKG_VERSION) -DMDNS_VERSIONSTR_NODTS=1" \
	CFLAGS_DEBUGGING="-DMDNS_DEBUGMSGS=0" \
	LINKOPTS="$(TARGET_LDFLAGS) -Wl,--as-needed" \
	SOOPTS="-shared -Wl,-soname,libdns_sd.so" \
	LIBS="$(TARGET_LDFLAGS) -Wl,--as-needed -static-libgcc -L../mDNSPosix/build/prod -ldns_sd"

define Build/Compile
	$(MAKE) -j1 -C $(PKG_BUILD_DIR)/mDNSPosix $(MDNS_MAKE_FLAGS) Daemon libdns_sd Clients
endef

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include $(1)/usr/lib
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/mDNSShared/dns_sd.h $(1)/usr/include/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/mDNSPosix/build/prod/libdns_sd.so $(1)/usr/lib/
endef

define Package/mdnsd/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/usr/lib
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/mDNSPosix/build/prod/mdnsd $(1)/usr/sbin/mdnsd
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/mDNSPosix/build/prod/libdns_sd.so $(1)/usr/lib/libdns_sd.so
endef

define Package/mdns-utils/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/Clients/build/dns-sd $(1)/usr/bin/dns-sd
endef

$(eval $(call BuildPackage,mdnsd))
$(eval $(call BuildPackage,mdns-utils))
